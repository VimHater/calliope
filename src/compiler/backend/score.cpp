#include "backend/score.hpp"

#include "core/pitch.hpp"

namespace calliope::backend {

namespace {

// The control context in effect for a subtree (set by enclosing Control nodes).
struct Ctx {
    int inst = -1;            // named-instrument id (-1 = none/custom)
    std::string path;         // custom .sfz path ("" = none)
    int bpm = 120;            // tempo
    int velocity = 80;        // note-on velocity
};

// A whole-note duration -> seconds at `bpm`: a whole note is 4 beats = 240/bpm s.
Rational to_seconds(const Rational& whole, int bpm) {
    return rat_mul(whole, rational(240, bpm));
}

// Walk the subtree, appending notes at absolute offset `off` (seconds), under the
// control context `cx`. Returns the subtree's length in seconds, so Seq places its
// right child after the left.
Rational collect(const music::Music& m, music::MusicId id, Rational off,
                 const Ctx& cx, std::vector<TimedNote>& out) {
    if (id == music::NoMusic) return rational_from_int(0);
    const music::MusicNode& n = m.nodes[id];
    switch (n.kind) {
        case music::MusicKind::Note: {
            int key = semitones(n.pitch) + 12; // our C0 = 0; MIDI C0 = 12
            if (key < 0) key = 0;
            if (key > 127) key = 127;
            Rational dur = to_seconds(n.dur, cx.bpm);
            out.push_back({off, key, dur, cx.inst, cx.path, cx.velocity});
            return dur;
        }
        case music::MusicKind::Rest:
            return to_seconds(n.dur, cx.bpm);
        case music::MusicKind::Seq: {
            Rational a = collect(m, n.left, off, cx, out);
            Rational b = collect(m, n.right, rat_add(off, a), cx, out);
            return rat_add(a, b);
        }
        case music::MusicKind::Par: {
            Rational a = collect(m, n.left, off, cx, out);
            Rational b = collect(m, n.right, off, cx, out);
            return rat_lt(a, b) ? b : a; // the longer branch sets the length
        }
        case music::MusicKind::Control: {
            // each Control overrides one axis of the context for its subtree
            Ctx inner = cx;
            if (n.instrument >= 0 || !n.sfz_path.empty()) { inner.inst = n.instrument; inner.path = n.sfz_path; }
            if (n.tempo >= 0) inner.bpm = n.tempo;
            if (n.velocity >= 0) inner.velocity = n.velocity;
            return collect(m, n.left, off, inner, out);
        }
    }
    return rational_from_int(0);
}

} // namespace

std::vector<TimedNote> flatten(const music::Music& m, music::MusicId root) {
    std::vector<TimedNote> out;
    collect(m, root, rational_from_int(0), Ctx{}, out);
    return out;
}

} // namespace calliope::backend
