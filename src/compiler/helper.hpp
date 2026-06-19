#pragma once

#include "core/driver.hpp"

#include <string>

// Host-side helpers shared by the `calliope` and `calliopei` entry points, so
// those stay thin I/O shells over `calliope::driver`. Nothing here is language
// logic — just file I/O, error reporting, and output-backend plumbing.

namespace calliope::cli {

// Read a whole file; returns "" if it can't be opened.
std::string read_file(const std::string& path);

// Path to the bundled standard library (CALLIOPE_PRELUDE_PATH), "" if undefined.
// Returns a static string literal — safe to bind to a string_view.
const char* prelude_path();

// Default audio soundfont: the SSO Grand Piano (.sfz) under CALLIOPE_SOUNDFONT_DIR.
// "" if that dir is undefined (e.g. binaries built without the audio backend).
// The file may still be absent if the SSO library was never fetched.
std::string default_soundfont();

// Print a compilation's parse / type / runtime errors to stderr.
void print_errors(const driver::Compilation& c);

// Debug dumps of the user program (used by `--dump`).
void dump_tokens(const std::string& src);
void dump_ast(const std::string& src);

// ---- output backends -----------------------------------------------------
enum class Emit { Ir, Midi, Wav, Mp3, Mp4, MusicXml };

bool parse_emit(const std::string& s, Emit& out);   // by name (ir|midi|…)
const char* emit_name(Emit e);
bool emit_from_ext(const std::string& ext, Emit& out); // by file extension

std::string ext_of(const std::string& path);            // "song.mid" -> "mid"
std::string replace_ext(const std::string& path, const std::string& newext);

// Write `main`'s value through the chosen backend (out_path "" = stdout, IR only).
// `soundfont` is the default .sfz voice for audio ("" if none); `base_dir` is the
// source file's directory, against which a custom `sfz "rel/path"` resolves.
// Returns the process exit code.
int emit_output(const driver::Compilation& c, Emit emit, const std::string& out_path,
                const std::string& soundfont, const std::string& base_dir);

} // namespace calliope::cli
