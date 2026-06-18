#pragma once

#include "ast.hpp"
#include "pitch.hpp"
#include "rational.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Tree-walking evaluator (strict / eager — language_spec.md §13.1). Data-oriented
// C-style API: tagged `Value`, free functions. No templates of our own (uses STL
// vector / shared_ptr), no inheritance, all members public.

namespace calliope::eval {

enum class ValueKind {
    Unit,
    Int,       // i
    Rat,       // rat
    Pitch,     // pitch
    Bool,      // i (0/1)
    Str,       // str
    List,      // items
    Con,       // str = constructor name, items = args (Music nodes, intervals, …)
    Closure,   // clo
    Builtin,   // i = builtin id, arity, items = args collected so far
};

struct Env;
struct Closure;

struct Value {
    ValueKind kind = ValueKind::Unit;
    long long i = 0;
    Rational rat;
    Pitch pitch;
    std::string str;
    std::vector<Value> items;
    std::shared_ptr<Closure> clo;
    int arity = 0; // for Builtin
};

struct Closure {
    std::vector<std::string> params;
    ast::NodeId body = ast::NoNode;
    std::shared_ptr<Env> env;
};

struct Env {
    std::shared_ptr<Env> parent;
    std::vector<std::string> names;
    std::vector<Value> vals;
};

struct Interp {
    const ast::Ast* ast = nullptr;
    std::shared_ptr<Env> globals;
    std::vector<std::string> errors;
};

// Evaluate a whole program: define top-level bindings into a fresh global
// environment (seeded with builtins) and return it. Errors are collected in
// `interp.errors`.
std::shared_ptr<Env> eval_program(const ast::Ast& a, Interp& interp);

// Look up a name in an environment chain. Returns false if absent.
bool env_lookup(const std::shared_ptr<Env>& env, std::string_view name, Value& out);

// Debug rendering of a value.
std::string show_value(const Value& v);

} // namespace calliope::eval
