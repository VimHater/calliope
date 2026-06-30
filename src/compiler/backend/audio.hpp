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

// Render the same subtree and play it on the default audio output device. For a
// single instrument (the common case) the render runs on a background thread while
// playback streams behind it, so sound starts almost at once instead of waiting for
// the whole piece to render; several instruments fall back to render-then-stream.
// Blocks until the piece finishes. Returns false and fills `err` on failure.
//
// TODO(visualize): while this plays, drive a raylib window rendering the score —
// a DAW-like piano-roll/track view and a piano-keyboard view (see
// backend/visualize.hpp). Playback and the visual share this timed-note stream.
bool play(const music::Music& m, music::MusicId root,
          const AudioOptions& opt, std::string& err);

// ---- non-blocking playback (for a synced visualizer) ---------------------
// `play` above blocks; these split it so another loop (a raylib window) can run
// while audio streams. `play_start` opens the device and begins playing on a
// background thread, returning an opaque handle (nullptr + `err` on failure).
// Poll `playback_seconds` for the playhead, `playback_finished` to know when the
// piece has drained, then `play_stop` to close the device and free the handle.
struct Playback;  // opaque; defined in audio.cpp

Playback* play_start(const music::Music& m, music::MusicId root,
                     const AudioOptions& opt, std::string& err);
double playback_seconds(const Playback* p);        // current playhead, seconds
double playback_total_seconds(const Playback* p);  // total length, seconds
bool   playback_finished(const Playback* p);       // audio fully drained?
void   play_stop(Playback* p);                     // stop device + free handle

// transport controls (for the visualizer): pause holds the cursor + emits silence;
// seek jumps the playhead (clamped to [0, total]).
void   playback_set_paused(Playback* p, bool paused);
bool   playback_paused(const Playback* p);
void   playback_seek(Playback* p, double seconds);

} // namespace calliope::backend
