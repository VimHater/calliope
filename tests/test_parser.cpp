#include "test.hpp"

#include "lexer.hpp"
#include "parser.hpp"

#include <utility>

using namespace calliope;

namespace {
std::string sx(std::string_view src) {
    std::vector<Token> toks = lex::tokenize(src);
    ast::Ast a = parse::parse_program(std::move(toks), nullptr);
    return test::ast_sexpr(a, a.root);
}
} // namespace

void run_parser_tests() {
    // adjacency of pitch literals -> Seq
    CHECK_EQ_STR(
        sx("main = c d e"),
        "(Program (Binding 'main' (Seq 'c' (PitchLit 'c') (PitchLit 'd') (PitchLit 'e'))))");

    // non-pitch head -> function application
    // (note: s/r/R and a..g are reserved notation, so we name vars 'subj' etc.)
    CHECK_EQ_STR(
        sx("main = invert subj"),
        "(Program (Binding 'main' (App 'invert' (Var 'invert') (Var 'subj'))))");

    // precedence: :+: (5) binds tighter than :=: (4), both right-assoc
    CHECK_EQ_STR(
        sx("main = p :+: q :=: w"),
        "(Program (Binding 'main' (BinOp ':=:' (BinOp ':+:' (Var 'p') (Var 'q')) (Var 'w'))))");

    // backtick infix + transposition, with parens
    CHECK_EQ_STR(
        sx("main = subj `par` (invert subj + foo)"),
        "(Program (Binding 'main' (BinOp 'par' (Var 'subj') "
        "(BinOp '+' (App 'invert' (Var 'invert') (Var 'subj')) (Var 'foo')))))");

    // application of a list argument
    CHECK_EQ_STR(
        sx("main = line [foo, bar]"),
        "(Program (Binding 'main' (App 'line' (Var 'line') "
        "(ListLit '[' (Var 'foo') (Var 'bar')))))");

    // chord
    CHECK_EQ_STR(
        sx("main = <c e g>"),
        "(Program (Binding 'main' (Chord '<' (PitchLit 'c') (PitchLit 'e') (PitchLit 'g'))))");

    // directive
    CHECK_EQ_STR(
        sx("#relative c'"),
        "(Program (Directive 'relative' (PitchLit 'c'')))");

    // type signature
    CHECK_EQ_STR(
        sx("main :: Music"),
        "(Program (TypeSig 'main' (TypeAtom 'Music')))");

    // function with a parameter
    CHECK_EQ_STR(
        sx("dev m = invert m"),
        "(Program (Binding 'dev' (Param 'm') (App 'invert' (Var 'invert') (Var 'm'))))");

    // where desugars to a let wrapping the body ('fn'/'x': 'f' is the pitch F)
    CHECK_EQ_STR(
        sx("fn = x\n  where\n    x = c"),
        "(Program (Binding 'fn' (Let 'fn' (Binding 'x' (PitchLit 'c')) (Var 'x'))))");
}
