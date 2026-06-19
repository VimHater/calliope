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
// Requires the sfizz soundfont (`opt.sfz_path`, an .sfz instrument) — there is no
// default. This header is backend-agnostic; the implementation links sfizz +
// miniaudio and is compiled only into binaries built with CALLIOPE_WITH_AUDIO.

namespace calliope::backend {

struct AudioOptions {
    int sample_rate = 44100;
    int bpm = 120;          // fixed tempo (no Modify/Control nodes yet)
    int velocity = 80;      // note-on velocity, matching the MIDI backend
    std::string sfz_path;   // required: path to an .sfz instrument
};

// Render the Music subtree rooted at `root` to a .wav file at `path`.
// Returns false and fills `err` on failure (bad soundfont, write error, …).
bool write_wav(const music::Music& m, music::MusicId root,
               const AudioOptions& opt, const std::string& path, std::string& err);

} // namespace calliope::backend
