#include "helper.hpp"

#include "backend/midi.hpp"
#ifdef CALLIOPE_WITH_AUDIO
#include "backend/audio.hpp"
#endif
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

std::string default_soundfont() {
#ifdef CALLIOPE_SOUNDFONT_DIR
    std::string dir = CALLIOPE_SOUNDFONT_DIR;
    if (dir.empty()) return std::string();
    return dir + "/Sonatina Symphonic Orchestra/Grand Piano/Grand Piano.sfz";
#else
    return std::string();
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
    if (e == "wav")                { out = Emit::Wav;  return true; }
    if (e == "mp3")                { out = Emit::Mp3;  return true; }
    if (e == "mp4")                { out = Emit::Mp4;  return true; }
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

namespace {

// Get a Music subtree from `main`'s value, lifting a bare Pitch to a single note
// into `storage`. Returns nullptr if `main` is neither Music nor Pitch.
const music::Music* as_music(const driver::Compilation& c, music::Music& storage,
                             music::MusicId& root) {
    if (c.main_value.kind == eval::ValueKind::Music) {
        root = c.main_value.mroot;
        return c.main_value.mus.get();
    }
    if (c.main_value.kind == eval::ValueKind::Pitch) {
        Rational d = (c.main_value.rat.num > 0) ? c.main_value.rat : rational(1, 4);
        root = music::note(storage, c.main_value.pitch, d);
        return &storage;
    }
    return nullptr;
}

} // namespace

int emit_output(const driver::Compilation& c, Emit emit, const std::string& out_path,
                const std::string& soundfont, const std::string& base_dir) {
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
        music::Music lifted;
        music::MusicId root = music::NoMusic;
        const music::Music* mp = as_music(c, lifted, root);
        if (!mp) {
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

    if (emit == Emit::Wav) {
#ifdef CALLIOPE_WITH_AUDIO
        if (soundfont.empty()) {
            std::fprintf(stderr, "error: no soundfont for the audio backend "
                                 "(pass --soundfont <file.sfz>; no default available)\n");
            return 2;
        }
        music::Music lifted;
        music::MusicId root = music::NoMusic;
        const music::Music* mp = as_music(c, lifted, root);
        if (!mp) {
            std::fprintf(stderr, "error: --emit wav needs `main` to be Music (got %s)\n",
                         c.main_type.empty() ? "?" : c.main_type.c_str());
            return 1;
        }
        backend::AudioOptions opt;
        opt.sfz_path = soundfont;
        opt.base_dir = base_dir;
        std::string err;
        if (!backend::write_wav(*mp, root, opt, out_path, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::fprintf(stderr, "wrote %s\n", out_path.c_str());
        return status;
#else
        (void)soundfont;
        (void)base_dir;
        std::fprintf(stderr, "error: audio backend not built in this binary\n");
        return 1;
#endif
    }

    std::fprintf(stderr,
        "error: emitting '%s' is not implemented yet (ir, midi, wav are wired).\n",
        emit_name(emit));
    return 1;
}

} // namespace calliope::cli
