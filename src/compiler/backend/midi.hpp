#pragma once

#include "core/music.hpp"

#include <string>

// MIDI backend: lower the Music IR to a Standard MIDI File (format 0).
//
// This is the first real backend (spec §13 / O9). It walks the Music tree
// (Note|Rest|Seq|Par) into a flat, time-sorted note-on/off event stream — a
// minimal stand-in for the score IR — and writes a .mid file. Pitch spelling is
// collapsed to a MIDI key number here, at the playback boundary (spec O2/O8);
// durations (exact rationals) become ticks at a fixed resolution.

namespace calliope::backend {

// Write the Music subtree rooted at `root` as a .mid file at `path`.
// Returns false and fills `err` on failure.
bool write_midi(const music::Music& m, music::MusicId root,
                const std::string& path, std::string& err);

} // namespace calliope::backend
