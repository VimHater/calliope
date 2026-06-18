#pragma once

#include "ast.hpp"

#include <string>
#include <string_view>
#include <vector>

// Hindley–Milner type inference (Algorithm W) over a subset of the language:
// Int / Bool / Pitch / Interval / Music literals, variables with let-style
// generalization, lambda, application, if, lists, and the arithmetic / compare /
// transpose / sequence-parallel operators. Plain single-parameter style.
//
// WIP / not yet covered: user-defined type classes & instances, full music
// typing beyond the builtins, pattern matching in parameters, data declarations.
//
// Data-oriented C-style API: index-pooled types, free functions.

namespace calliope::types {

using TypeId = int;
constexpr TypeId NoType = -1;

struct TypeNode {
    bool is_var = false;
    int var = -1;                 // variable number (if is_var)
    std::string con;              // constructor name (if !is_var): Int, ->, Music, …
    std::vector<TypeId> args;     // constructor arguments
};

struct Scheme {
    std::vector<int> vars;        // quantified variable numbers
    TypeId type = NoType;
};

struct Ctx {
    std::vector<TypeNode> pool;
    std::vector<TypeId> binding;  // per variable number: bound type or NoType
};

// Typecheck a whole program. Returns true if no type errors; appends messages to
// `errors`. (Top-level bindings are inferred with generalization.)
bool typecheck_program(const ast::Ast& a, std::vector<std::string>& errors);

// For tests: infer the type of one top-level binding and render it as a string
// (e.g. "Int", "Int -> Int", "t0 -> t0"). Empty string on error.
std::string infer_named_type(const ast::Ast& a, std::string_view name,
                             std::vector<std::string>& errors);

} // namespace calliope::types
