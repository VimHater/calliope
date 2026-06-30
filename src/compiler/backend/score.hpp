#pragma once

#include "core/music.hpp"
#include "core/rational.hpp"

#include <string>
#include <vector>

// Score flattening — the shared timing seam between backends.
//
// The Music IR (Note|Rest|Seq|Par|Control) is a tree; every backend needs a flat,
// absolute-timed list of sounding notes. This walk produces that list once, baking
// each Control's tempo into absolute **seconds** (so per-region / polytempo just
// works), and stamping each note with its instrument and velocity. Backends convert
// seconds to their own base (MIDI ticks, audio samples). A minimal stand-in for the
// eventual score IR (spec §13 / O9).

namespace calliope::backend {

// One sounding note: when it starts, which MIDI key, how long — all absolute, with
// `start`/`dur` measured in **seconds** (tempo already resolved). `key` is collapsed
// to a MIDI key number (spelling dropped at the playback boundary), clamped 0..127.
struct TimedNote {
    Rational start;         // seconds
    int key = 0;
    Rational dur;           // seconds
    int instrument = -1;    // named-instrument id from the enclosing Control (-1 = none/custom)
    std::string sfz_path;   // custom .sfz path from the enclosing Control ("" = named/none)
    int gm = -1;            // raw GM program number from the enclosing Control (-1 = none)
    int velocity = 80;      // note-on velocity 0..127 from the enclosing Control
};

// Flatten the subtree rooted at `root` into absolute-timed notes (time-ordered by
// construction: Seq lays its right child after the left, Par overlays both, Rest
// advances time without emitting). Rests contribute no TimedNote.
//
// An active `meter` Control accents strong-beat onsets (so meter is audible), and
// — when `errors` is non-null — each `|` barline is checked against that meter, a
// message per measure that does not fill exactly one bar ("bar 3: 5/4 in a 4/4
// meter"). Backends pass nullptr (accent only); the driver passes a vector to
// surface bar errors at compile time.
std::vector<TimedNote> flatten(const music::Music& m, music::MusicId root,
                               std::vector<std::string>* errors = nullptr);

// Like flatten, but split into one note list per *voice* — the branches of the
// top-level `:=:` (Par) chain, each on the same clock (enclosing tempo/meter applied).
// One voice if the piece isn't a top-level parallel. For the visualizer's per-voice
// rows. (Chords — Pars inside a voice — are not split.)
std::vector<std::vector<TimedNote>> flatten_voices(const music::Music& m, music::MusicId root);

} // namespace calliope::backend
