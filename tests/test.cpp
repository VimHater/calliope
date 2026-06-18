#include "test.hpp"

#include <cstdio>

namespace calliope::test {

void check(bool ok, const char* expr, const char* file, int line) {
    g_checks++;
    if (!ok) {
        g_fails++;
        std::printf("FAIL  %s:%d\n      %s\n", file, line, expr);
    }
}

void check_str_eq(std::string_view got, std::string_view want,
                  const char* expr, const char* file, int line) {
    g_checks++;
    if (got != want) {
        g_fails++;
        std::printf("FAIL  %s:%d  %s\n   got:  %.*s\n   want: %.*s\n",
                    file, line, expr,
                    static_cast<int>(got.size()), got.data(),
                    static_cast<int>(want.size()), want.data());
    }
}

std::string ast_sexpr(const ast::Ast& a, ast::NodeId id) {
    if (id < 0 || id >= static_cast<ast::NodeId>(a.nodes.size()))
        return "<null>";
    const ast::Node& n = a.nodes[id];
    std::string s = "(";
    s += ast::node_kind_name(n.kind);
    if (!n.tok.text.empty()) {
        s += " '";
        s.append(n.tok.text.data(), n.tok.text.size());
        s += "'";
    }
    for (ast::NodeId k : n.kids) {
        s += " ";
        s += ast_sexpr(a, k);
    }
    s += ")";
    return s;
}

} // namespace calliope::test
