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
//
// `Modify` (tempo / dynamics / key controls) is not built yet — those combinators
// don't exist as builtins. Durations are exact `Rational` whole-note fractions
// (quarter = 1/4), per spec O11.

namespace calliope::music {

using MusicId = std::int32_t;
constexpr MusicId NoMusic = -1;

enum class MusicKind : std::uint8_t { Note, Rest, Seq, Par };

struct MusicNode {
    MusicKind kind = MusicKind::Rest;
    Pitch pitch;                 // Note
    Rational dur;                // Note, Rest
    MusicId left = NoMusic;      // Seq / Par
    MusicId right = NoMusic;     // Seq / Par
};

struct Music {
    std::vector<MusicNode> nodes;
};

MusicId add(Music& m, MusicNode n);
MusicId note(Music& m, Pitch p, Rational dur);
MusicId rest(Music& m, Rational dur);
MusicId seq(Music& m, MusicId a, MusicId b);
MusicId par(Music& m, MusicId a, MusicId b);

// Transpose every Note in the subtree by (diatonic steps, semitones); Rests and
// structure are preserved. Returns a fresh subtree (input is left untouched).
MusicId transpose(Music& m, MusicId id, int dstep, int dsemi);

// Debug rendering, e.g. "(C4:1/4 :+: D4:1/4)".
std::string show(const Music& m, MusicId id);

} // namespace calliope::music
