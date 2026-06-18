#pragma once

#include "ast.hpp"
#include "eval.hpp"

#include <string>
#include <string_view>
#include <vector>

// Compilation pipeline API. Entry points (the CLI, the REPL) do only I/O —
// read source, write results — and call into here for everything else: combine
// the prelude with the program, lex, parse, type-check, and evaluate `main`.
//
// Data-oriented C-style API: a plain result struct filled by free functions.

namespace calliope::driver {

// The result of compiling a program. Self-contained: `source` owns the combined
// text, `ast` holds string_views into it, `interp` owns the evaluation
// environment, and `main_value` is the evaluated result. Fill it in place with
// `compile`/`compile_expr` and do not move it afterwards (the views would dangle).
struct Compilation {
    std::string source;                       // prelude + program (owned)
    ast::Ast ast;                             // views into `source`
    eval::Interp interp;                      // owns globals env + Music pool
    std::string main_type;                    // inferred type of `main` ("" if none)
    eval::Value main_value;                   // evaluated `main`
    bool has_main = false;
    std::vector<std::string> parse_errors;
    std::vector<std::string> type_errors;
    std::vector<std::string> runtime_errors;
};

// True when nothing went wrong at any stage.
bool ok(const Compilation& c);

// Compile `<prelude>\n<program>` into `out` (filled in place).
void compile(std::string_view prelude, std::string_view program, Compilation& out);

// Compile a single expression as `main = <expr>` (used by the REPL).
void compile_expr(std::string_view prelude, std::string_view expr, Compilation& out);

// Infer the type of `main` only (no evaluation). Returns "" on error / absence;
// any diagnostics are appended to `errors`.
std::string type_of_main(std::string_view prelude, std::string_view program,
                         std::vector<std::string>& errors);

// As above, for a single expression (`main = <expr>`).
std::string type_of_expr(std::string_view prelude, std::string_view expr,
                         std::vector<std::string>& errors);

} // namespace calliope::driver
