#include "helper.hpp"

#include "backend/midi.hpp"
#include "core/ast.hpp"
#include "core/eval.hpp"
#include "core/lexer.hpp"
#include "core/music.hpp"
#include "core/parser.hpp"
#include "core/rational.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace calliope::cli {

std::string read_file(const std::string& path) {
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

void print_errors(const driver::Compilation& c) {
    for (const std::string& e : c.parse_errors)   std::fprintf(stderr, "parse error: %s\n", e.c_str());
    for (const std::string& e : c.type_errors)    std::fprintf(stderr, "type error: %s\n", e.c_str());
    for (const std::string& e : c.runtime_errors) std::fprintf(stderr, "runtime error: %s\n", e.c_str());
}

void dump_tokens(const std::string& src) {
    std::printf("== tokens ==\n");
    for (const Token& t : lex::tokenize(src))
        std::printf("%3d:%-3d %-11s '%.*s'\n", t.line, t.col, token_kind_name(t.kind),
                    static_cast<int>(t.text.size()), t.text.data());
}

void dump_ast(const std::string& src) {
    ast::Ast a = parse::parse_program(lex::tokenize(src), nullptr);
    std::printf("== ast ==\n");
    ast::ast_print(a, a.root, 0);
}

// ---- output backends -----------------------------------------------------

bool parse_emit(const std::string& s, Emit& out) {
    if (s == "ir")       { out = Emit::Ir;       return true; }
    if (s == "midi")     { out = Emit::Midi;     return true; }
    if (s == "wav")      { out = Emit::Wav;      return true; }
    if (s == "mp3")      { out = Emit::Mp3;      return true; }
    if (s == "mp4")      { out = Emit::Mp4;      return true; }
    if (s == "musicxml") { out = Emit::MusicXml; return true; }
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

bool emit_from_ext(const std::string& e, Emit& out) {
    if (e == "mid" || e == "midi") { out = Emit::Midi; return true; }
    if (e == "ir"  || e == "txt")  { out = Emit::Ir;   return true; }
    return false;
}

std::string ext_of(const std::string& p) {
    std::size_t slash = p.find_last_of("/\\");
    std::size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = p.substr(dot + 1);
    for (char& ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return e;
}

std::string replace_ext(const std::string& p, const std::string& newext) {
    std::size_t slash = p.find_last_of("/\\");
    std::size_t dot = p.find_last_of('.');
    std::string base = (dot == std::string::npos || (slash != std::string::npos && dot < slash))
                           ? p : p.substr(0, dot);
    return base + newext;
}

int emit_output(const driver::Compilation& c, Emit emit, const std::string& out_path) {
    const int status = driver::ok(c) ? 0 : 1;

    if (emit == Emit::Ir) {
        std::string text = eval::show_value(c.main_value) + "\n";
        if (out_path.empty()) {
            std::fputs(text.c_str(), stdout);
        } else {
            std::ofstream f(out_path, std::ios::binary);
            if (!f) { std::fprintf(stderr, "error: cannot write '%s'\n", out_path.c_str()); return 1; }
            f << text;
            std::fprintf(stderr, "wrote %s\n", out_path.c_str());
        }
        return status;
    }

    if (emit == Emit::Midi) {
        // the MIDI backend needs a Music value; lift a bare Pitch to one note.
        music::Music lifted;
        const music::Music* mp = nullptr;
        music::MusicId root = music::NoMusic;
        if (c.main_value.kind == eval::ValueKind::Music) {
            mp = c.main_value.mus.get();
            root = c.main_value.mroot;
        } else if (c.main_value.kind == eval::ValueKind::Pitch) {
            Rational d = (c.main_value.rat.num > 0) ? c.main_value.rat : rational(1, 4);
            root = music::note(lifted, c.main_value.pitch, d);
            mp = &lifted;
        } else {
            std::fprintf(stderr, "error: --emit midi needs `main` to be Music (got %s)\n",
                         c.main_type.empty() ? "?" : c.main_type.c_str());
            return 1;
        }
        std::string err;
        if (!backend::write_midi(*mp, root, out_path, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::fprintf(stderr, "wrote %s\n", out_path.c_str());
        return status;
    }

    std::fprintf(stderr,
        "error: emitting '%s' is not implemented yet (only ir and midi are wired).\n",
        emit_name(emit));
    return 1;
}

} // namespace calliope::cli
