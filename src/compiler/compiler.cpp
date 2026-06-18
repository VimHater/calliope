#include "core/ast.hpp"
#include "core/eval.hpp"
#include "core/lexer.hpp"
#include "core/parser.hpp"
#include "core/typecheck.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv) {
    std::string src;
    if (argc > 1) {
        src = read_file(argv[1]);
    } else {
        // Default sample exercising pitch runs, application, operators, where,
        // and a directive.
        src =
            "#relative c'\n"
            "subject :: Phrase\n"
            "subject = c d e g a\n"
            "\n"
            "development subject = subject `par` (invert subject ^+ P5)\n"
            "\n"
            "frere = line [jacques, dormez]\n"
            "  where\n"
            "    jacques = c'4 d' e' c'\n"
            "    dormez  = e'4 f' g'2\n";
    }

    std::vector<calliope::Token> toks = calliope::lex::tokenize(src);

    std::printf("== tokens ==\n");
    for (const calliope::Token& t : toks) {
        std::printf("%3d:%-3d %-11s '%.*s'\n",
                    t.line, t.col, calliope::token_kind_name(t.kind),
                    static_cast<int>(t.text.size()), t.text.data());
    }

    std::vector<std::string> errors;
    calliope::ast::Ast ast = calliope::parse::parse_program(toks, &errors);

    std::printf("\n== ast ==\n");
    calliope::ast::ast_print(ast, ast.root, 0);

    if (!errors.empty()) {
        std::printf("\n== parse errors ==\n");
        for (const std::string& e : errors) std::printf("%s\n", e.c_str());
    }

    std::vector<std::string> type_errors;
    std::string main_type = calliope::types::infer_named_type(ast, "main", type_errors);
    std::printf("\n== types ==\n");
    if (!main_type.empty()) std::printf("main :: %s\n", main_type.c_str());
    for (const std::string& e : type_errors) std::printf("%s\n", e.c_str());

    calliope::eval::Interp interp;
    auto env = calliope::eval::eval_program(ast, interp);
    calliope::eval::Value main_val;
    std::printf("\n== eval ==\n");
    if (calliope::eval::env_lookup(env, "main", main_val))
        std::printf("main = %s\n", calliope::eval::show_value(main_val).c_str());
    for (const std::string& e : interp.errors) std::printf("%s\n", e.c_str());

    return 0;
}
