#pragma once

#include "core/music.hpp"

#include <string>

// Audio backend: render the Music IR to a WAV file (offline, deterministic).
//
// The first audio backend (spec §13 / O9). It flattens the Music tree to timed
// notes (backend/score.hpp), drives the sfizz SFZ sampler block by block to
// synthesize PCM, and writes a stereo WAV via miniaudio's encoder. Pitch spelling
// is collapsed to MIDI keys at this playback boundary; exact-rational durations
// become sample positions at a fixed tempo.
//
// Notes are grouped by instrument (named id or a custom `sfz "..."` path); each
// group renders on its own synth — the matching SSO .sfz via sfizz, else a
// placeholder GM SF2 via tsf — and the groups are summed. This header is
// backend-agnostic; the implementation links sfizz + tsf + miniaudio and is
// compiled only into binaries built with CALLIOPE_WITH_AUDIO.

namespace calliope::backend {

struct AudioOptions {
    int sample_rate = 44100;
    std::string sfz_path;   // default voice for un-instrumented notes (an .sfz path)
    std::string base_dir;   // a custom `sfz "rel/path"` resolves against this dir
    // tempo and velocity are resolved per-note in `flatten` (TimedNote), not here.
};

// Render the Music subtree rooted at `root` to a .wav file at `path`.
// Returns false and fills `err` on failure (bad soundfont, write error, …).
bool write_wav(const music::Music& m, music::MusicId root,
               const AudioOptions& opt, const std::string& path, std::string& err);

} // namespace calliope::backend
