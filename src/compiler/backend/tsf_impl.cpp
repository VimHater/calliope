// Single translation unit that compiles the TinySoundFont implementation. Every
// other TU includes "tsf.h" for declarations only (TSFDEF defaults to extern).
// tsf is the SF2 fallback synth: when an instrument's .sfz is missing, the audio
// backend plays it through a placeholder General-MIDI SF2 by program number.
#define TSF_IMPLEMENTATION
#include "tsf.h"
