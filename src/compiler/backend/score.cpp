#include "backend/score.hpp"

#include "core/pitch.hpp"

namespace calliope::backend {

namespace {

// Walk the subtree, appending notes at absolute offset `off` (whole notes).
// Returns the subtree's length, so Seq can place its right child after the left.
Rational collect(const music::Music& m, music::MusicId id, Rational off,
                 std::vector<TimedNote>& out) {
    if (id == music::NoMusic) return rational_from_int(0);
    const music::MusicNode& n = m.nodes[id];
    switch (n.kind) {
        case music::MusicKind::Note: {
            int key = semitones(n.pitch) + 12; // our C0 = 0; MIDI C0 = 12
            if (key < 0) key = 0;
            if (key > 127) key = 127;
            out.push_back({off, key, n.dur});
            return n.dur;
        }
        case music::MusicKind::Rest:
            return n.dur;
        case music::MusicKind::Seq: {
            Rational a = collect(m, n.left, off, out);
            Rational b = collect(m, n.right, rat_add(off, a), out);
            return rat_add(a, b);
        }
        case music::MusicKind::Par: {
            Rational a = collect(m, n.left, off, out);
            Rational b = collect(m, n.right, off, out);
            return rat_lt(a, b) ? b : a; // the longer branch sets the length
        }
    }
    return rational_from_int(0);
}

} // namespace

std::vector<TimedNote> flatten(const music::Music& m, music::MusicId root) {
    std::vector<TimedNote> out;
    collect(m, root, rational_from_int(0), out);
    return out;
}

} // namespace calliope::backend
