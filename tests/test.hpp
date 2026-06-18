#pragma once

// Minimal C-style test harness: counters + two check functions, driven by
// macros. No framework, no templates, no registration magic — test functions
// are plain free functions called explicitly from test_main.cpp.

#include "ast.hpp"

#include <string>
#include <string_view>

namespace calliope::test {

inline int g_checks = 0;
inline int g_fails = 0;

void check(bool ok, const char* expr, const char* file, int line);
void check_str_eq(std::string_view got, std::string_view want,
                  const char* expr, const char* file, int line);

// Compact one-line S-expression of an AST subtree, for assertions.
std::string ast_sexpr(const ast::Ast& a, ast::NodeId id);

} // namespace calliope::test

#define CHECK(cond) \
    ::calliope::test::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ_STR(got, want) \
    ::calliope::test::check_str_eq((got), (want), #got, __FILE__, __LINE__)
