#include "backend/score.hpp"

#include "core/pitch.hpp"

#include <string>

namespace calliope::backend {

namespace {

// The control context in effect for a subtree (set by enclosing Control nodes).
struct Ctx {
    int inst = -1;            // named-instrument id (-1 = none/custom)
    std::string path;         // custom .sfz path ("" = none)
    int gm = -1;              // raw GM program number (-1 = none)
    int bpm = 120;            // tempo
    int velocity = 80;        // note-on velocity
    int mnum = -1;            // active meter numerator (-1 = no meter)
    int mden = -1;            // active meter denominator
    Rational mstart;          // musical offset (whole-notes) where the meter began
    Rational gate{1, 1};      // articulation: sounding-length factor (1/1 = full)
    int accent = 0;           // articulation: velocity delta added to the base
};

// Length of a subtree along two clocks: real time (seconds, tempo baked) and
// musical time (whole-notes, tempo-independent — what meters measure).
struct Len { Rational secs; Rational beats; };

// A whole-note duration -> seconds at `bpm`: a whole note is 4 beats = 240/bpm s.
Rational to_seconds(const Rational& whole, int bpm) {
    return rat_mul(whole, rational(240, bpm));
}

int clamp127(long long v) { return v < 0 ? 0 : (v > 127 ? 127 : static_cast<int>(v)); }

// a mod b for non-negative a and positive b (bar positions are non-negative).
Rational rat_mod(Rational a, Rational b) {
    if (b.num == 0) return a;
    Rational q = rat_div(a, b);
    long long f = q.num / q.den;        // q >= 0, so truncation == floor
    return rat_sub(a, rat_mul(rational_from_int(f), b));
}

// Metric accent: a note onset on a strong beat is louder, so the same notes sound
// different under different meters. `pos` is the onset's offset within the bar
// (whole-notes, in [0, barlen)). Downbeat is strongest; a secondary accent falls on
// the metric midpoint of simple meters and every third beat of compound ones.
int accent_velocity(int base, Rational pos, int num, int den) {
    if (pos.num == 0) return clamp127(base + 18);          // downbeat
    Rational beats = rat_mul(pos, rational_from_int(den));  // pos / (1/den) = beat index
    if (beats.den != 1) return base;                        // off-beat: no accent
    long long bi = beats.num;
    bool secondary = false;
    if (num % 3 == 0 && den >= 8) secondary = (bi % 3 == 0);   // compound: groups of 3
    else if (num % 2 == 0)        secondary = (bi == num / 2);  // simple even: mid-bar
    return secondary ? clamp127(base + 10) : base;
}

// Walk the subtree, appending notes at absolute offset `osec` (seconds) / `obeat`
// (whole-notes) under control context `cx`. `last_bar` is the musical offset of the
// most recent barline (or the meter's start), threaded sequentially so a `|` can
// check the measure before it. Returns the subtree's two lengths.
Len collect(const music::Music& m, music::MusicId id, Rational osec, Rational obeat,
            const Ctx& cx, Rational& last_bar, std::vector<TimedNote>& out,
            std::vector<std::string>* errors) {
    Len zero{rational_from_int(0), rational_from_int(0)};
    if (id == music::NoMusic) return zero;
    const music::MusicNode& n = m.nodes[id];
    switch (n.kind) {
        case music::MusicKind::Note: {
            int key = semitones(n.pitch) + 12; // our C0 = 0; MIDI C0 = 12
            if (key < 0) key = 0;
            if (key > 127) key = 127;
            // the slot advances by the full notated duration; the note *sounds* for
            // slot * gate (a staccato note is short, leaving a gap before the next).
            Rational slot = to_seconds(n.dur, cx.bpm);
            Rational sounding = to_seconds(rat_mul(n.dur, cx.gate), cx.bpm);
            int base = cx.velocity + cx.accent;     // articulation accent
            int vel = base;
            if (cx.mnum > 0) {                       // an active meter accents strong beats
                Rational barlen = rational(cx.mnum, cx.mden);
                Rational pos = rat_mod(rat_sub(obeat, cx.mstart), barlen);
                vel = accent_velocity(base, pos, cx.mnum, cx.mden);
            }
            out.push_back({osec, key, sounding, cx.inst, cx.path, cx.gm, clamp127(vel)});
            return {slot, n.dur};
        }
        case music::MusicKind::Rest:
            return {to_seconds(n.dur, cx.bpm), n.dur};
        case music::MusicKind::Seq: {
            Len a = collect(m, n.left, osec, obeat, cx, last_bar, out, errors);
            Len b = collect(m, n.right, rat_add(osec, a.secs), rat_add(obeat, a.beats),
                            cx, last_bar, out, errors);
            return {rat_add(a.secs, b.secs), rat_add(a.beats, b.beats)};
        }
        case music::MusicKind::Par: {
            // parallel voices each carry their own barline accounting (independent
            // copies of last_bar); the longer branch sets the length.
            Rational lb1 = last_bar;
            Len a = collect(m, n.left, osec, obeat, cx, lb1, out, errors);
            Rational lb2 = last_bar;
            Len b = collect(m, n.right, osec, obeat, cx, lb2, out, errors);
            return {rat_lt(a.secs, b.secs) ? b.secs : a.secs,
                    rat_lt(a.beats, b.beats) ? b.beats : a.beats};
        }
        case music::MusicKind::Control: {
            Ctx inner = cx;
            if (n.instrument >= 0 || !n.sfz_path.empty() || n.gm >= 0) {
                inner.inst = n.instrument;
                inner.path = n.sfz_path;
                inner.gm = n.gm;
            }
            if (n.tempo >= 0) inner.bpm = n.tempo;
            if (n.velocity >= 0) inner.velocity = n.velocity;
            if (n.gate.num > 0) { inner.gate = n.gate; inner.accent = n.accent; }
            bool new_meter = n.meter_num > 0;
            if (new_meter) {
                inner.mnum = n.meter_num;
                inner.mden = n.meter_den;
                inner.mstart = obeat;            // bars are counted from here
            }
            // a fresh meter scope restarts bar accounting; other Controls keep it
            Rational child_lb = new_meter ? obeat : last_bar;
            std::size_t first_note = out.size();
            Len r = collect(m, n.left, osec, obeat, inner, child_lb, out, errors);
            if (!new_meter) last_bar = child_lb; // continue the outer measure
            // damper pedal: each note rings until the pedal lifts (subtree end) — but
            // a re-struck *same* key cuts the earlier one off, so a repeated bass note
            // under the pedal doesn't pile up overlapping copies into a loud blur.
            if (n.sustain) {
                Rational end = rat_add(osec, r.secs);
                for (std::size_t k = first_note; k < out.size(); k++) {
                    Rational stop = end;
                    for (std::size_t j = first_note; j < out.size(); j++)
                        if (out[j].key == out[k].key && rat_lt(out[k].start, out[j].start) &&
                            rat_lt(out[j].start, stop))
                            stop = out[j].start;
                    out[k].dur = rat_sub(stop, out[k].start);
                }
            }
            return r;
        }
        case music::MusicKind::Barline: {
            // a barline checks the measure just completed against the active meter
            if (cx.mnum > 0 && errors) {
                Rational barlen = rational(cx.mnum, cx.mden);
                Rational measured = rat_sub(obeat, last_bar);
                if (!rat_eq(measured, barlen)) {
                    Rational rel = rat_sub(last_bar, cx.mstart);
                    Rational q = rat_div(rel, barlen);
                    long long idx = q.num / q.den + 1;
                    errors->push_back(
                        "bar " + std::to_string(idx) + ": " + std::to_string(measured.num) +
                        "/" + std::to_string(measured.den) + " in a " +
                        std::to_string(cx.mnum) + "/" + std::to_string(cx.mden) + " meter");
                }
            }
            last_bar = obeat;
            return zero;
        }
    }
    return zero;
}

} // namespace

std::vector<TimedNote> flatten(const music::Music& m, music::MusicId root,
                               std::vector<std::string>* errors) {
    std::vector<TimedNote> out;
    Rational last_bar = rational_from_int(0);
    collect(m, root, rational_from_int(0), rational_from_int(0), Ctx{}, last_bar, out, errors);
    return out;
}

} // namespace calliope::backend
