# FetchSoundfonts.cmake
#
# The Sonatina Symphonic Orchestra (SSO) — a free SFZ orchestral sample library —
# is the default soundfont for playback. It is large (~2.6 GB) and is NOT
# committed (see third_party/soundfonts/.gitignore), so we fetch it on demand by
# shallow-cloning the upstream repository.
#
# Controls:
#   -DCALLIOPE_FETCH_SOUNDFONTS=OFF   skip the download (the audio backend will
#                                     simply be unavailable until it is present)
#
# The library lands in third_party/soundfonts/sso and its path is exposed to the
# C++ code as the CALLIOPE_SOUNDFONT_DIR macro.

# Honor a pre-set -DCALLIOPE_FETCH_SOUNDFONTS=OFF (option() respects existing vars).
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
option(CALLIOPE_FETCH_SOUNDFONTS "Download the SSO soundfont library if missing" ON)

set(CALLIOPE_SOUNDFONT_URL "https://github.com/peastman/sso")
set(CALLIOPE_SOUNDFONT_DIR "${CMAKE_SOURCE_DIR}/third_party/soundfonts/sso")

# This subdirectory only exists once the library is fully present.
set(_sso_sentinel "${CALLIOPE_SOUNDFONT_DIR}/Sonatina Symphonic Orchestra")

if(EXISTS "${_sso_sentinel}")
    message(STATUS "Soundfonts: SSO present (${CALLIOPE_SOUNDFONT_DIR})")
elseif(NOT CALLIOPE_FETCH_SOUNDFONTS)
    message(STATUS "Soundfonts: SSO missing; fetch disabled "
                   "(-DCALLIOPE_FETCH_SOUNDFONTS=ON to enable)")
else()
    find_program(GIT_EXECUTABLE git)
    if(NOT GIT_EXECUTABLE)
        message(WARNING "Soundfonts: git not found; cannot fetch SSO. "
                        "Clone ${CALLIOPE_SOUNDFONT_URL} into ${CALLIOPE_SOUNDFONT_DIR} manually.")
    else()
        message(STATUS "Soundfonts: cloning SSO (~2.6 GB, shallow) — this may take a while...")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --depth 1 "${CALLIOPE_SOUNDFONT_URL}"
                    "${CALLIOPE_SOUNDFONT_DIR}"
            RESULT_VARIABLE _sso_rc)
        if(NOT _sso_rc EQUAL 0)
            message(WARNING "Soundfonts: clone failed (exit ${_sso_rc}); "
                            "audio playback will be unavailable until SSO is fetched.")
        else()
            message(STATUS "Soundfonts: SSO ready (${CALLIOPE_SOUNDFONT_DIR})")
        endif()
    endif()
endif()
