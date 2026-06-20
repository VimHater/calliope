#pragma once

// Live visualization backend — TODO (not implemented yet).
// (The implementation will include core/music.hpp + backend/score.hpp; this stub
// only declares the mode enum so call sites and docs can reference the API.)
//
// Calliope plays a piece through miniaudio (backend/audio.hpp `play`). This will
// add a synchronized **raylib** window that draws the score as it sounds, sharing
// the same flattened, absolute-timed note stream (backend/score.hpp `flatten`) so
// the picture and the audio stay locked to one clock.
//
// Two planned modes:
//   * VizMode::Daw   — a DAW-like piano-roll / multi-track view: time on X, pitch
//                      on Y, one lane (color) per instrument, a moving playhead.
//   * VizMode::Piano — a piano-keyboard view: keys light up as notes sound.
//
// Intended shape (subject to change):
//   bool visualize(const music::Music& m, music::MusicId root,
//                  VizMode mode, const VisualizeOptions& opt, std::string& err);
// Likely run on the main thread (raylib requires it) with audio on its own
// device thread; `play` would hand its render buffer + the timed notes here.
//
// Build wiring: raylib prebuilts live in third_party/libs/raylib (libraylib.a +
// include/). A future CALLIOPE_VISUAL option will compile backend/visualize.cpp
// and link raylib into `calliope` / `calliopei`, mirroring calliope_add_audio().

namespace calliope::backend {

enum class VizMode {
    Daw,    // piano-roll / track view
    Piano,  // piano-keyboard view
};

// TODO: implement visualize() over raylib. Declared here so call sites and docs
// can reference the intended API before the implementation lands.

} // namespace calliope::backend
