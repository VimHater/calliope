#pragma once

#include "ast.hpp"
#include "token.hpp"

#include <string>
#include <vector>

namespace calliope::parse {

// Recursive-descent + precedence-climbing parser state. All public.
struct Parser {
    std::vector<Token> toks;
    std::size_t pos = 0;
    ast::Ast ast;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;   // non-fatal advice (e.g. the `,` list trap)
    // Offside margin for the binding currently being parsed: a newline inside an
    // expression continues it only when the next line is indented past this
    // column. Top-level bindings use 0 (any indented continuation line counts).
    int margin = 0;
};

// Parse a token stream into an AST. `errors_out` (non-null) receives parse errors
// ("line:col: message"); `warnings_out` (non-null) receives non-fatal advice. The
// returned Ast is always usable (errors produce Error nodes rather than aborting).
ast::Ast parse_program(std::vector<Token> toks,
                       std::vector<std::string>* errors_out = nullptr,
                       std::vector<std::string>* warnings_out = nullptr);

} // namespace calliope::parse
