#pragma once

#include "ast.hpp"
#include "eval.hpp"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

// Compilation pipeline API. Entry points (the CLI, the REPL) do only I/O and call
// into here for everything else: read `#load` directives, lex, parse, type-check,
// and evaluate `main`.
//
// Each source unit (the program and every loaded file) is lexed and parsed
// SEPARATELY, then their declarations are merged into one program. So a token's
// line/column stays relative to the file it came from — loaded units (the prelude,
// other `#load`s) never shift the program's reported line numbers.

namespace calliope::driver {

// The result of compiling a program. Self-contained: `sources` owns the text of
// every unit (the program + loaded files), `ast` holds string_views into them
// (a std::deque keeps element addresses stable), `interp` owns the evaluation
// environment, and `main_value` is the evaluated result. Fill it in place.
struct Compilation {
    std::deque<std::string> sources;          // program + loaded unit sources (owned)
    ast::Ast ast;                             // merged declarations; views into sources
    eval::Interp interp;                      // owns globals env + Music pool
    std::string main_type;                    // inferred type of `main` ("" if none)
    eval::Value main_value;                   // evaluated `main`
    bool has_main = false;
    std::vector<std::string> parse_errors;
    std::vector<std::string> type_errors;
    std::vector<std::string> runtime_errors;
};

// How `#load` is resolved for one compilation.
struct LoadOptions {
    std::string_view prelude_path;     // resolves `#load "prelude"`
    std::string_view base_dir;         // directory for relative `#load` paths ("" = cwd)
    bool preload_prelude = false;      // load the prelude with no directive (the REPL)
};

// True when nothing went wrong at any stage.
bool ok(const Compilation& c);

// Compile `program`. `#load "<name>"` directives are resolved and merged in first:
// `#load "prelude"` -> opts.prelude_path; any other name -> a file path, taken
// relative to opts.base_dir unless absolute. When opts.preload_prelude is true the
// prelude is loaded automatically (REPL) with no directive and no line-number shift.
void compile(std::string_view program, const LoadOptions& opts, Compilation& out);

// Infer the type of `main` only (no evaluation). Diagnostics appended to `errors`.
std::string type_of_main(std::string_view program, const LoadOptions& opts,
                         std::vector<std::string>& errors);

// The directory part of a path (cross-platform: handles '/' and '\\'). "" if none.
std::string directory_of(std::string_view path);

// True if `program` parses cleanly as one or more top-level declarations
// (binding / signature / class / instance) rather than a bare expression.
bool is_definition(std::string_view program);

} // namespace calliope::driver
