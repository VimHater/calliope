#pragma once

#include "ast.hpp"
#include "music.hpp"
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
    Con,       // str = constructor name, items = args (intervals, Rest, …)
    Music,     // mus = shared IR pool, mroot = root node (Note/Rest/Seq/Par)
    Closure,   // clo
    Builtin,   // i = builtin id, arity, items = args collected so far
    Method,    // str = class-method name; dispatched on first argument's type
};

struct Env;
struct Closure;

struct Value {
    ValueKind kind = ValueKind::Unit;
    long long i = 0;
    Rational rat;                          // Rat; also a Pitch's literal duration
    Pitch pitch;
    std::string str;
    std::vector<Value> items;
    std::shared_ptr<Closure> clo;
    std::shared_ptr<music::Music> mus;     // Music: shared IR pool
    music::MusicId mroot = music::NoMusic; // Music: root node
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

// One concrete class-method implementation: `(method, type) -> impl value`.
// e.g. ("^+", "Pitch") -> builtin transpose ; ("describe", "Bool") -> closure.
struct MethodImpl {
    std::string method;
    std::string type;
    Value impl;
};

struct Interp {
    const ast::Ast* ast = nullptr;
    std::shared_ptr<Env> globals;
    std::vector<std::string> errors;
    std::vector<MethodImpl> instances;   // class-method dispatch table
    // one shared Music IR pool for the whole evaluation, so values built by
    // different sub-expressions compose without merging pools.
    std::shared_ptr<music::Music> music = std::make_shared<music::Music>();
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
