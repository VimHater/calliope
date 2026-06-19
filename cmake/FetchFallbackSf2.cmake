# Fetch a small General-MIDI SoundFont used as the audio backend's *fallback* voice:
# when an instrument's SSO .sfz patch is missing, the backend plays it through this
# SF2 (via tsf) by GM program number. Best-effort — if the download fails the build
# still succeeds, the fallback is just unavailable (missing-.sfz instruments go
# silent). Sets CALLIOPE_FALLBACK_SF2 to the file path when available.
#
# Skip with -DCALLIOPE_FETCH_FALLBACK_SF2=OFF, or point CALLIOPE_FALLBACK_SF2_URL at
# any GM .sf2 (you can also drop a file at the destination path yourself).

option(CALLIOPE_FETCH_FALLBACK_SF2 "Fetch a small GM SF2 fallback for the audio backend" ON)
set(CALLIOPE_FALLBACK_SF2_URL
    "https://github.com/FluidSynth/fluidsynth/raw/master/sf2/VintageDreamsWaves-v2.sf2"
    CACHE STRING "URL of the fallback General-MIDI SF2")

set(_sf2_dir "${CMAKE_SOURCE_DIR}/third_party/soundfonts")
set(_sf2 "${_sf2_dir}/fallback-gm.sf2")
set(CALLIOPE_FALLBACK_SF2 "" CACHE INTERNAL "Path to the fallback GM SF2 (empty if none)")

if(EXISTS "${_sf2}")
    set(CALLIOPE_FALLBACK_SF2 "${_sf2}" CACHE INTERNAL "" FORCE)
    message(STATUS "Fallback SF2: present (${_sf2})")
elseif(CALLIOPE_FETCH_FALLBACK_SF2)
    message(STATUS "Fallback SF2: downloading ${CALLIOPE_FALLBACK_SF2_URL}")
    file(MAKE_DIRECTORY "${_sf2_dir}")
    file(DOWNLOAD "${CALLIOPE_FALLBACK_SF2_URL}" "${_sf2}" STATUS _st TIMEOUT 60)
    list(GET _st 0 _code)
    if(_code EQUAL 0)
        set(CALLIOPE_FALLBACK_SF2 "${_sf2}" CACHE INTERNAL "" FORCE)
        message(STATUS "Fallback SF2: ready (${_sf2})")
    else()
        file(REMOVE "${_sf2}")
        message(WARNING "Fallback SF2: download failed (${_st}); "
                        "audio fallback disabled. Set CALLIOPE_FALLBACK_SF2_URL or "
                        "drop a GM .sf2 at ${_sf2}.")
    endif()
else()
    message(STATUS "Fallback SF2: skipped (CALLIOPE_FETCH_FALLBACK_SF2=OFF)")
endif()
