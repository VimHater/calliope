#include "test.hpp"

#include "eval.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <utility>

using namespace calliope;

namespace {

// The Calliope standard library, loaded from disk (path injected by CMake).
const std::string& prelude() {
    static const std::string src = [] {
        std::ifstream f(CALLIOPE_PRELUDE_PATH, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }();
    return src;
}

// Evaluate `main = <expr>` with the prelude in scope; return main's value.
eval::Value run(const std::string& program) {
    std::string full = prelude() + "\n" + program;
    std::vector<Token> toks = lex::tokenize(full);
    ast::Ast a = parse::parse_program(std::move(toks), nullptr);
    eval::Interp I;
    auto env = eval::eval_program(a, I);
    eval::Value v;
    eval::env_lookup(env, "main", v);
    return v;
}

std::string type_of(const std::string& program, std::string_view name) {
    std::string full = prelude() + "\n" + program;
    std::vector<Token> toks = lex::tokenize(full);
    ast::Ast a = parse::parse_program(std::move(toks), nullptr);
    std::vector<std::string> errs;
    return types::infer_named_type(a, name, errs);
}

bool type_errors(const std::string& program) {
    std::string full = prelude() + "\n" + program;
    std::vector<Token> toks = lex::tokenize(full);
    ast::Ast a = parse::parse_program(std::move(toks), nullptr);
    std::vector<std::string> errs;
    types::typecheck_program(a, errs);
    return !errs.empty();
}

} // namespace

void run_stdlib_tests() {
    // the prelude itself is well typed
    CHECK(!type_errors("main = 0"));

    // length
    CHECK(run("main = length [10, 20, 30]").i == 3);
    CHECK(run("main = length []").i == 0);
    CHECK_EQ_STR(type_of("main = length [1]", "length"), "[t0] -> Int");

    // map (double defined in the user program)
    CHECK_EQ_STR(eval::show_value(run("double x = x * 2\nmain = map double [1, 2, 3]")),
                 "[ 2 4 6 ]");
    // map is fully polymorphic
    CHECK_EQ_STR(type_of("main = 0", "map"), "(t0 -> t1) -> [t0] -> [t1]");

    // filter
    CHECK_EQ_STR(eval::show_value(run("big x = 2 < x\nmain = filter big [1, 2, 3, 4]")),
                 "[ 3 4 ]");

    // reverse
    CHECK_EQ_STR(eval::show_value(run("main = reverse [1, 2, 3, 4]")), "[ 4 3 2 1 ]");

    // drop
    CHECK_EQ_STR(eval::show_value(run("main = drop 2 [1, 2, 3, 4, 5]")), "[ 3 4 5 ]");
    CHECK_EQ_STR(eval::show_value(run("main = drop 9 [1, 2]")), "[ ]");

    // core list axioms reachable directly
    CHECK(run("main = head [7, 8, 9]").i == 7);
    CHECK_EQ_STR(eval::show_value(run("main = tail [7, 8, 9]")), "[ 8 9 ]");
    CHECK(run("main = null []").i == 1);
    CHECK(run("main = null [1]").i == 0);
    CHECK_EQ_STR(eval::show_value(run("main = cons 1 [2, 3]")), "[ 1 2 3 ]");

    // composition: length (map ...) etc. infers an Int
    CHECK(run("double x = x * 2\nbig x = 3 < x\n"
              "main = length (map double (filter big [1, 2, 3, 4, 5]))").i == 2);

    // a prelude function used at TWO different types in one program: only works
    // because top-level bindings generalize in dependency order (SCC).
    CHECK(run("main = length [1, 2, 3] + length [True, False]").i == 5);
    CHECK(!type_errors("main = length [1, 2, 3] + length [True, False]"));
    CHECK(run("main = length (reverse [c', e', g'])").i == 3); // length on pitches
}
