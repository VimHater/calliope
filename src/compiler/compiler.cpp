#include "core/ast.hpp"
#include "core/driver.hpp"
#include "core/eval.hpp"
#include "core/lexer.hpp"
#include "core/parser.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// Calliope compiler driver — an I/O shell around calliope::driver.
//
//   calliope [options] <file.cal>
//
// Reads a program (and the standard-library prelude), hands them to the driver
// API, and writes the result. Today only the Music IR backend is wired up (it
// prints the evaluated IR to stdout); the audio / MIDI / engraving backends are
// stubs until the score IR + synth layers land.

namespace {

std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const char* prelude_path() {
#ifdef CALLIOPE_PRELUDE_PATH
    return CALLIOPE_PRELUDE_PATH;
#else
    return "";
#endif
}

// Output backends. `ir` is the only one implemented; the rest are placeholders
// for the score-IR-driven pipeline (spec §13 / O9).
enum class Emit { Ir, Midi, Wav, Mp3, Mp4, MusicXml };

bool parse_emit(const char* s, Emit& out) {
    if (!std::strcmp(s, "ir"))       { out = Emit::Ir;       return true; }
    if (!std::strcmp(s, "midi"))     { out = Emit::Midi;     return true; }
    if (!std::strcmp(s, "wav"))      { out = Emit::Wav;      return true; }
    if (!std::strcmp(s, "mp3"))      { out = Emit::Mp3;      return true; }
    if (!std::strcmp(s, "mp4"))      { out = Emit::Mp4;      return true; }
    if (!std::strcmp(s, "musicxml")) { out = Emit::MusicXml; return true; }
    return false;
}

const char* emit_name(Emit e) {
    switch (e) {
        case Emit::Ir:       return "ir";
        case Emit::Midi:     return "midi";
        case Emit::Wav:      return "wav";
        case Emit::Mp3:      return "mp3";
        case Emit::Mp4:      return "mp4";
        case Emit::MusicXml: return "musicxml";
    }
    return "?";
}

void usage(const char* prog) {
    std::fprintf(stderr,
        "usage: %s [options] <file.cal>\n"
        "\n"
        "  --emit <fmt>    output format: ir (default), midi, wav, mp3, mp4, musicxml\n"
        "  --dump <what>   debug dump to stdout: tokens, ast, types (repeatable)\n"
        "  -h, --help      show this help\n"
        "\n"
        "Only 'ir' is implemented; it prints the evaluated Music IR to stdout.\n",
        prog);
}

// Debug dumps reflect the user program alone (the prelude would drown them).
void dump_tokens(const std::string& src) {
    std::printf("== tokens ==\n");
    for (const calliope::Token& t : calliope::lex::tokenize(src))
        std::printf("%3d:%-3d %-11s '%.*s'\n", t.line, t.col,
                    calliope::token_kind_name(t.kind),
                    static_cast<int>(t.text.size()), t.text.data());
}

void dump_ast(const std::string& src) {
    calliope::ast::Ast a = calliope::parse::parse_program(calliope::lex::tokenize(src), nullptr);
    std::printf("== ast ==\n");
    calliope::ast::ast_print(a, a.root, 0);
}

} // namespace

int main(int argc, char** argv) {
    Emit emit = Emit::Ir;
    bool want_tokens = false, want_ast = false, want_types = false;
    const char* input = nullptr;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) { usage(argv[0]); return 0; }
        if (!std::strcmp(a, "--emit")) {
            if (++i >= argc || !parse_emit(argv[i], emit)) {
                std::fprintf(stderr, "error: --emit needs one of ir|midi|wav|mp3|mp4|musicxml\n");
                return 2;
            }
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

    std::string src = read_file(input);
    if (src.empty()) {
        std::fprintf(stderr, "error: cannot read '%s'\n", input);
        return 2;
    }

    if (want_tokens) dump_tokens(src);
    if (want_ast)    dump_ast(src);

    // Files do not get the prelude automatically — they must `#load "prelude"`.
    // Relative `#load` paths resolve against the file's own directory.
    std::string base = calliope::driver::directory_of(input);
    calliope::driver::LoadOptions opts{prelude_path(), base, false};
    calliope::driver::Compilation c;
    calliope::driver::compile(src, opts, c);

    if (want_types) {
        std::printf("== types ==\n");
        if (!c.main_type.empty()) std::printf("main :: %s\n", c.main_type.c_str());
    }

    for (const std::string& e : c.parse_errors) {
        std::fprintf(stderr, "parse error: %s\n", e.c_str());
    }
    for (const std::string& e : c.type_errors) {
        std::fprintf(stderr, "type error: %s\n", e.c_str());
    }
    for (const std::string& e : c.runtime_errors) {
        std::fprintf(stderr, "runtime error: %s\n", e.c_str());
    }

    if (!c.has_main) {
        std::fprintf(stderr, "error: no 'main' to compile\n");
        return 1;
    }

    if (emit != Emit::Ir) {
        std::fprintf(stderr,
            "error: emitting '%s' is not implemented yet — the score IR and synth\n"
            "       backends are still to come. Use '--emit ir' for now.\n",
            emit_name(emit));
        return 1;
    }

    // The only wired backend: print the evaluated Music IR.
    std::printf("%s\n", calliope::eval::show_value(c.main_value).c_str());
    return calliope::driver::ok(c) ? 0 : 1;
}
