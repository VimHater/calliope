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
};

// Parse a token stream into an AST. If `errors_out` is non-null it receives any
// parse diagnostics ("line:col: message"). The returned Ast is always usable
// (errors produce Error nodes rather than aborting).
ast::Ast parse_program(std::vector<Token> toks,
                       std::vector<std::string>* errors_out = nullptr);

} // namespace calliope::parse
