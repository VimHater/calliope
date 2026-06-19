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
    int velocity = 80;      // note-on velocity 0..127 from the enclosing Control
};

// Flatten the subtree rooted at `root` into absolute-timed notes (time-ordered by
// construction: Seq lays its right child after the left, Par overlays both, Rest
// advances time without emitting). Rests contribute no TimedNote.
std::vector<TimedNote> flatten(const music::Music& m, music::MusicId root);

} // namespace calliope::backend
