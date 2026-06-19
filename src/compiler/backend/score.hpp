#pragma once

#include "core/music.hpp"
#include "core/rational.hpp"

#include <string>
#include <vector>

// Score flattening — the shared timing seam between backends.
//
// The Music IR (Note|Rest|Seq|Par) is a tree; every backend ultimately needs a
// flat, absolute-timed list of sounding notes. This walk produces that list once,
// in exact Rational whole-note units (quarter = 1/4), so each backend converts to
// its own time base (MIDI ticks, audio samples) without re-deriving the layout.
// It is a minimal stand-in for the eventual score IR (spec §13 / O9).

namespace calliope::backend {

// One sounding note: when it starts, which MIDI key, how long — all absolute,
// `start`/`dur` measured in whole notes. `key` is already collapsed to a MIDI key
// number (spelling dropped at the playback boundary) and clamped to 0..127.
struct TimedNote {
    Rational start;
    int key = 0;
    Rational dur;
    int instrument = -1;    // named-instrument id from the enclosing Control (-1 = none/custom)
    std::string sfz_path;   // custom .sfz path from the enclosing Control ("" = named/none)
};

// Flatten the subtree rooted at `root` into absolute-timed notes (time-ordered by
// construction: Seq lays its right child after the left, Par overlays both, Rest
// advances time without emitting). Rests contribute no TimedNote.
std::vector<TimedNote> flatten(const music::Music& m, music::MusicId root);

} // namespace calliope::backend
