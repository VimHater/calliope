// Single translation unit that compiles the miniaudio implementation. The header
// is ~4MB, so it is built exactly once here; everywhere else includes "miniaudio.h"
// for declarations only. We only need the WAV encoder, so disable the device and
// resource-manager subsystems — but leave the codec paths alone (disabling them
// strips dr_wav and the WAV encoder reports MA_NO_BACKEND at runtime).
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#define MA_NO_RESOURCE_MANAGER
#include "miniaudio.h"
