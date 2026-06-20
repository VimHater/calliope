#pragma once

#include "pitch.hpp"
#include "rational.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Music IR (language_spec.md §3) — the value a Calliope program evaluates to.
// Index-pooled tree (same style as the AST): a `MusicKind` tag + fields, nodes
// in `Music.nodes`, referenced by `MusicId`. No inheritance, all members public,
// free-function API. Notation literals and the `:+:` / `:=:` combinators desugar
// into this; the score IR (later) is lowered from it.
//
//   Note pitch dur      a single sounding pitch of a given duration
//   Rest dur            silence
//   Seq  a b   (:+:)    a then b
//   Par  a b   (:=:)    a together with b
//   Control inst child  a `Modify` node: assign an instrument to a sub-phrase
//   Barline             a zero-duration measure boundary (the `|` operator)
//
// `Control` is the `Modify` family (spec §8). It wraps a single child (in `left`)
// and carries exactly one control payload along one axis (the others stay unset, so
// an enclosing Control still governs them):
//   • instrument — a named-instrument id, a user `sfz_path`, or a raw `gm` program
//                  number (`gm "<n>"`); for the latter two instrument stays -1
//   • tempo      — beats per minute (`tempo` >= 0 means set)
//   • velocity   — note-on velocity 0..127 (`velocity` >= 0 means set)
//   • meter      — time signature (`meter_num`/`meter_den` >= 0 means set); the
//                  timing pass reads it for strong-beat accent + bar validation
// The payload stays abstract here; the flatten seam / backends resolve it. Note
// durations are exact `Rational` whole-note fractions (quarter = 1/4), per spec O11.
//
// `Barline` is a marker, not a wrapper: it sits in a Seq between two measures (`a |
// b` = Seq(a, Seq(Barline, b))) and carries no time. The timing pass uses it to
// check each measure fills the active meter; it is otherwise inert.

namespace calliope::music {

using MusicId = std::int32_t;
constexpr MusicId NoMusic = -1;

enum class MusicKind : std::uint8_t { Note, Rest, Seq, Par, Control, Barline };

struct MusicNode {
    MusicKind kind = MusicKind::Rest;
    Pitch pitch;                 // Note
    Rational dur;                // Note, Rest
    MusicId left = NoMusic;      // Seq / Par / Control (child)
    MusicId right = NoMusic;     // Seq / Par
    int instrument = -1;         // Control: named-instrument id (-1 = none / custom)
    std::string sfz_path;        // Control: user-supplied .sfz path ("" = named/none)
    int gm = -1;                 // Control: raw GM program number (-1 = unset)
    int tempo = -1;              // Control: beats per minute (-1 = unset)
    int velocity = -1;           // Control: note-on velocity 0..127 (-1 = unset)
    int meter_num = -1;          // Control: time-signature numerator (-1 = unset)
    int meter_den = -1;          // Control: time-signature denominator (-1 = unset)
};

struct Music {
    std::vector<MusicNode> nodes;
};

MusicId add(Music& m, MusicNode n);
MusicId note(Music& m, Pitch p, Rational dur);
MusicId rest(Music& m, Rational dur);
MusicId seq(Music& m, MusicId a, MusicId b);
MusicId par(Music& m, MusicId a, MusicId b);
MusicId control(Music& m, int instrument, std::string sfz_path, MusicId child);
MusicId control(Music& m, int instrument, MusicId child); // sfz_path = "" (named)
MusicId control_gm(Music& m, int gm, MusicId child);      // raw GM program number
MusicId control_tempo(Music& m, int bpm, MusicId child);
MusicId control_velocity(Music& m, int velocity, MusicId child);
MusicId control_meter(Music& m, int num, int den, MusicId child); // time signature
MusicId barline(Music& m);                                // a measure boundary marker

// Transpose every Note in the subtree by (diatonic steps, semitones); Rests and
// structure are preserved. Returns a fresh subtree (input is left untouched).
MusicId transpose(Music& m, MusicId id, int dstep, int dsemi);

// Scale every Note/Rest duration in the subtree by `factor` (structure and pitch
// preserved). Used by `tuplet` — e.g. a triplet scales durations by 2/3. Returns
// a fresh subtree.
MusicId scale_dur(Music& m, MusicId id, Rational factor);

// Deep structural equality of two subtrees: same shape, same spelled pitches
// (C# != Db), same durations. Used by `==` / `/=` on Music values.
bool equal(const Music& m, MusicId a, MusicId b);

// Set every Note/Rest duration in the subtree to `dur` (structure and pitch
// preserved). Used by chord-level durations (`<c e g>2`). Returns a fresh subtree.
MusicId set_dur(Music& m, MusicId id, Rational dur);

// Tie two subtrees: they must have identical shape and pitch (a note tied to the
// same note, a chord to the same chord, …); the result keeps that shape with each
// Note/Rest duration summed. Sets ok=false (and returns NoMusic) on any mismatch.
MusicId tie(Music& m, MusicId a, MusicId b, bool& ok);

// Debug rendering, e.g. "(C4:1/4 :+: D4:1/4)".
std::string show(const Music& m, MusicId id);

} // namespace calliope::music
