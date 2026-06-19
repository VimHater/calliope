// Single translation unit that compiles the miniaudio implementation. The header
// is ~4MB, so it is built exactly once here; everywhere else includes "miniaudio.h"
// for declarations only. We use the WAV encoder (offline render) and the device
// I/O layer (live playback via `backend::play`), so keep both; only the resource
// manager is unused. Leave the codec paths alone — disabling them strips dr_wav
// and the WAV encoder reports MA_NO_BACKEND at runtime.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_RESOURCE_MANAGER
#include "miniaudio.h"
