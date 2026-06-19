#include "core/driver.hpp"
#include "core/eval.hpp"
#include "helper.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// Calliope interpreter (`calliopei`) — an I/O shell around calliope::driver.
//
//   calliopei              start a REPL (the prelude is preloaded)
//   calliopei file.cal     run a file: play `main` live on the audio device
//                          (a file must `#load "prelude"` itself to use the stdlib)
//   calliopei --debug file run a file and print its Music IR instead of playing
//
//   REPL commands:  :type <expr>   show a type without evaluating
//                   :debug <expr>  evaluate and print the full value (the Music IR)
//                   :play <expr>   evaluate and sound it live (audio builds only)
//                   :quit / :q     exit
//
// A bare expression prints just its type when the result is Music (the IR tree is
// verbose — see :debug); scalar results print their value and type.
//   #load "..."     loads a file into the session (relative to the cwd)
//
// A bare expression is evaluated directly (driver::compile_expr) — not pasted
// into a `main =` binding — so it can't collide with a user-defined `main`.
//
// TODO: line editing + history (readline). Use a cross-platform line-editing
// library (e.g. replxx, or linenoise with a Win32 fallback) — std::getline below
// has no editing/history. Also: multi-line input.

namespace {

// Load options for the REPL: prelude preloaded, relative #load paths against cwd.
calliope::driver::LoadOptions repl_opts() {
    return calliope::driver::LoadOptions{calliope::cli::prelude_path(), "", /*preload=*/true};
}

// Run a whole file: evaluate `main` and print its value (the Music IR). The file
// is responsible for `#load`-ing the prelude if it wants the standard library.
int run_file(const char* path, bool debug) {
    std::string src = calliope::cli::read_file(path);
    if (src.empty()) {
        std::fprintf(stderr, "error: cannot read '%s'\n", path);
        return 2;
    }
    std::string base = calliope::driver::directory_of(path);
    calliope::driver::LoadOptions opts{calliope::cli::prelude_path(), base, false};
    calliope::driver::Compilation c;
    calliope::driver::compile(src, opts, c);
    calliope::cli::print_errors(c);
    if (!c.has_main) {
        std::fprintf(stderr, "error: no 'main' to run\n");
        return 1;
    }
    // --debug prints the Music IR; otherwise run it = play `main` live.
    if (debug) {
        std::printf("%s\n", calliope::eval::show_value(c.main_value).c_str());
        return calliope::driver::ok(c) ? 0 : 1;
    }
    return calliope::cli::play(c, calliope::cli::default_soundfont(), base);
}

// ---- REPL ---------------------------------------------------------------
// `session` accumulates the definitions typed so far; the prelude is preloaded,
// so the standard library is always in scope.

void repl_show_type(const std::string& session, const std::string& expr) {
    std::vector<std::string> errs;
    std::string ty = calliope::driver::type_of_expr(session, expr, repl_opts(), errs);
    for (const std::string& e : errs) std::printf("  type error: %s\n", e.c_str());
    if (!ty.empty()) std::printf("  :: %s\n", ty.c_str());
}

void repl_eval(const std::string& session, const std::string& expr) {
    calliope::driver::Compilation c;
    calliope::driver::compile_expr(session, expr, repl_opts(), c);
    if (!c.parse_errors.empty()) {
        for (const std::string& e : c.parse_errors) std::printf("  parse error: %s\n", e.c_str());
        return; // don't report a value/type for something that didn't parse
    }
    if (!c.type_errors.empty()) {
        for (const std::string& e : c.type_errors) std::printf("  type error: %s\n", e.c_str());
        return; // don't evaluate ill-typed code (ghci-style: no junk value/runtime error)
    }
    // A Music result's "value" is its verbose IR tree — that's debug output
    // (`:debug <expr>`, or `calliopei --debug`). For a Music expression just print
    // the type; for a scalar (Int / Bool / Pitch / list / …) print value + type.
    if (c.has_main) {
        if (c.main_value.kind == calliope::eval::ValueKind::Music) {
            std::printf("  :: %s\n", c.main_type.empty() ? "Music" : c.main_type.c_str());
        } else {
            std::string val = calliope::eval::show_value(c.main_value);
            if (!c.main_type.empty()) std::printf("  %s :: %s\n", val.c_str(), c.main_type.c_str());
            else std::printf("  %s\n", val.c_str());
        }
    } else if (!c.main_type.empty()) {
        std::printf("  :: %s\n", c.main_type.c_str());
    }
    for (const std::string& e : c.runtime_errors) std::printf("  runtime error: %s\n", e.c_str());
}

// :debug <expr> — evaluate and print the full value (the Music IR tree) + type.
void repl_debug(const std::string& session, const std::string& expr) {
    calliope::driver::Compilation c;
    calliope::driver::compile_expr(session, expr, repl_opts(), c);
    if (!c.parse_errors.empty()) {
        for (const std::string& e : c.parse_errors) std::printf("  parse error: %s\n", e.c_str());
        return;
    }
    if (!c.type_errors.empty()) {
        for (const std::string& e : c.type_errors) std::printf("  type error: %s\n", e.c_str());
        return;
    }
    if (c.has_main) {
        std::string val = calliope::eval::show_value(c.main_value);
        if (!c.main_type.empty()) std::printf("  %s :: %s\n", val.c_str(), c.main_type.c_str());
        else std::printf("  %s\n", val.c_str());
    }
    for (const std::string& e : c.runtime_errors) std::printf("  runtime error: %s\n", e.c_str());
}

// :play <expr> — evaluate the expression and sound it live on the audio device.
void repl_play(const std::string& session, const std::string& expr) {
    calliope::driver::Compilation c;
    calliope::driver::compile_expr(session, expr, repl_opts(), c);
    if (!c.parse_errors.empty()) {
        for (const std::string& e : c.parse_errors) std::printf("  parse error: %s\n", e.c_str());
        return;
    }
    if (!c.type_errors.empty()) {
        for (const std::string& e : c.type_errors) std::printf("  type error: %s\n", e.c_str());
        return;
    }
    for (const std::string& e : c.runtime_errors) std::printf("  runtime error: %s\n", e.c_str());
    if (!c.runtime_errors.empty()) return;
    calliope::cli::play(c, calliope::cli::default_soundfont(), /*base_dir=*/"");
}

// The bound name of a definition line: the first whitespace/`=`-delimited token.
std::string definition_name(const std::string& def) {
    std::size_t end = def.find_first_of(" \t=");
    return def.substr(0, end);
}

// Add a definition to the session if it compiles cleanly; otherwise report and
// reject. We check runtime errors too, so a circular value binding like
// `h = h * 2` (type-correct but non-terminating without laziness) is rejected
// instead of poisoning every later evaluation.
void repl_define(std::string& session, const std::string& def) {
    std::string trial = session + def + "\n";
    calliope::driver::Compilation c;
    calliope::driver::compile(trial, repl_opts(), c);
    if (!calliope::driver::ok(c)) {
        for (const std::string& e : c.parse_errors)   std::printf("  parse error: %s\n", e.c_str());
        for (const std::string& e : c.type_errors)    std::printf("  type error: %s\n", e.c_str());
        for (const std::string& e : c.runtime_errors) std::printf("  runtime error: %s\n", e.c_str());
        std::string name = definition_name(def);
        std::string self_unbound = "unbound name: " + name;
        for (const std::string& e : c.runtime_errors) {
            if (e == self_unbound) {
                std::printf("  hint: '%s' is defined in terms of itself; a plain value can't be\n"
                            "        recursive. Did you mean a function, e.g. `%s x = ...`?\n",
                            name.c_str(), name.c_str());
                break;
            }
        }
        return; // reject: leave the session unchanged
    }
    session = trial; // accept silently, like a binding in ghci
}

int repl() {
    std::printf("Calliope interpreter — define (x = ...), evaluate, "
                ":type <expr>, :debug <expr>, :play <expr>, or :quit\n");
    std::string session;
    std::string line;
    for (;;) {
        std::printf("λ> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) { std::printf("\n"); break; }

        std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        std::string s = line.substr(b);

        if (s == ":quit" || s == ":q") break;
        if (s.rfind(":type ", 0) == 0) { repl_show_type(session, s.substr(6)); continue; }
        if (s.rfind(":debug ", 0) == 0) { repl_debug(session, s.substr(7)); continue; }
        if (s.rfind(":play ", 0) == 0) { repl_play(session, s.substr(6)); continue; }
        if (s[0] == ':') { std::printf("  unknown command '%s'\n", s.c_str()); continue; }

        // a definition (`x = ...`) or a directive (`#load ...`) is remembered for
        // the session; anything else is evaluated as an expression.
        if (s[0] == '#' || calliope::driver::is_definition(s)) repl_define(session, s);
        else repl_eval(session, s);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // calliopei [--debug] [file.cal]  — play a file (or print its IR), else REPL.
    bool debug = false;
    const char* path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--debug") == 0) { debug = true; continue; }
        if (argv[i][0] == '-') {
            std::fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            return 2;
        }
        path = argv[i];
    }
    if (path) return run_file(path, debug);
    return repl();
}
