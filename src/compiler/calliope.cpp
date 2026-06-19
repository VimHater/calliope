#include "core/driver.hpp"
#include "helper.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// Calliope compiler CLI (`calliope`) — a thin I/O shell over calliope::driver.
//
//   calliope [options] <file.cal>
//
// Reads a program, hands it to the driver, and writes the result through the
// chosen backend. All the real work lives in core/driver and cli (helper.{hpp,cpp}).

namespace {

using calliope::cli::Emit;

void usage(const char* prog) {
    std::fprintf(stderr,
        "usage: %s [options] <file.cal>\n"
        "\n"
        "  -o <file>       write output to <file>; the backend is chosen by its\n"
        "                  extension (.mid/.midi = MIDI, .wav = audio, .ir = Music IR)\n"
        "  --emit <fmt>    force the backend: ir | midi | wav (overrides the extension)\n"
        "  --soundfont <f> .sfz instrument for the audio (wav) backend\n"
        "                  (default: the bundled SSO Grand Piano)\n"
        "  --dump <what>   debug dump to stdout: tokens, ast, types (repeatable)\n"
        "  -h, --help      show this help\n"
        "\n"
        "With no -o and no --emit, the Music IR is printed to stdout.\n"
        "Backends implemented so far: ir, midi, wav.\n",
        prog);
}

} // namespace

int main(int argc, char** argv) {
    Emit emit = Emit::Ir;
    bool emit_given = false;
    bool want_tokens = false, want_ast = false, want_types = false;
    const char* input = nullptr;
    std::string out_path;
    std::string soundfont; // reserved for audio backends

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) { usage(argv[0]); return 0; }
        if (!std::strcmp(a, "-o")) {
            if (++i >= argc) { std::fprintf(stderr, "error: -o needs a file path\n"); return 2; }
            out_path = argv[i];
            continue;
        }
        if (!std::strcmp(a, "--emit")) {
            if (++i >= argc || !calliope::cli::parse_emit(argv[i], emit)) {
                std::fprintf(stderr, "error: --emit needs one of ir|midi|wav|mp3|mp4|musicxml\n");
                return 2;
            }
            emit_given = true;
            continue;
        }
        if (!std::strcmp(a, "--soundfont")) {
            if (++i >= argc) { std::fprintf(stderr, "error: --soundfont needs a file path\n"); return 2; }
            soundfont = argv[i];
            continue;
        }
        if (!std::strcmp(a, "--dump")) {
            if (++i >= argc) { std::fprintf(stderr, "error: --dump needs tokens|ast|types\n"); return 2; }
            const char* w = argv[i];
            if      (!std::strcmp(w, "tokens")) want_tokens = true;
            else if (!std::strcmp(w, "ast"))    want_ast = true;
            else if (!std::strcmp(w, "types"))  want_types = true;
            else { std::fprintf(stderr, "error: unknown --dump '%s'\n", w); return 2; }
            continue;
        }
        if (a[0] == '-') { std::fprintf(stderr, "error: unknown option '%s'\n", a); return 2; }
        input = a;
    }

    if (!input) { usage(argv[0]); return 2; }

    std::string src = calliope::cli::read_file(input);
    if (src.empty()) {
        std::fprintf(stderr, "error: cannot read '%s'\n", input);
        return 2;
    }

    if (want_tokens) calliope::cli::dump_tokens(src);
    if (want_ast)    calliope::cli::dump_ast(src);

    // Files do not get the prelude automatically — they must `#load "prelude"`.
    // Relative `#load` paths resolve against the file's own directory.
    std::string base = calliope::driver::directory_of(input);
    calliope::driver::LoadOptions opts{calliope::cli::prelude_path(), base, false};
    calliope::driver::Compilation c;
    calliope::driver::compile(src, opts, c);

    if (want_types && !c.main_type.empty())
        std::printf("== types ==\nmain :: %s\n", c.main_type.c_str());

    calliope::cli::print_errors(c);

    if (!c.has_main) {
        std::fprintf(stderr, "error: no 'main' to compile\n");
        return 1;
    }

    // Choose the backend: an explicit --emit wins; otherwise infer from the -o
    // extension; with neither, print the Music IR to stdout.
    if (!emit_given) {
        if (!out_path.empty()) {
            std::string e = calliope::cli::ext_of(out_path);
            if (!calliope::cli::emit_from_ext(e, emit)) {
                std::fprintf(stderr, "error: unknown output extension '.%s' (use .mid or .ir)\n", e.c_str());
                return 2;
            }
        } else {
            emit = Emit::Ir;
        }
    }
    // Binary backends need a destination; default it from the input name.
    if (out_path.empty() && emit == Emit::Midi) out_path = calliope::cli::replace_ext(input, ".mid");
    if (out_path.empty() && emit == Emit::Wav)  out_path = calliope::cli::replace_ext(input, ".wav");
    if (out_path.empty() && emit == Emit::Mp3)  out_path = calliope::cli::replace_ext(input, ".mp3");
    if (out_path.empty() && emit == Emit::Mp4)  out_path = calliope::cli::replace_ext(input, ".mp4");

    // Audio backends fall back to the bundled Grand Piano when no --soundfont given.
    const bool is_audio = emit == Emit::Wav || emit == Emit::Mp3 || emit == Emit::Mp4;
    if (soundfont.empty() && is_audio) soundfont = calliope::cli::default_soundfont();

    return calliope::cli::emit_output(c, emit, out_path, soundfont);
}
