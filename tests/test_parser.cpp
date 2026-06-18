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

    // transposition operator binds tighter than sequence (:+: is 5, ^+ is 6)
    CHECK_EQ_STR(
        sx("main = subj :+: motif ^+ P5"),
        "(Program (Binding 'main' (BinOp ':+:' (Var 'subj') "
        "(BinOp '^+' (Var 'motif') (Con 'P5')))))");

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

    // a where block may carry a local type signature, which is parsed away so
    // only the binding remains in the desugared let
    CHECK_EQ_STR(
        sx("foo = go\n  where\n    go :: Int\n    go = 1"),
        "(Program (Binding 'foo' (Let 'foo' (Binding 'go' (IntLit '1')) (Var 'go'))))");

    // function with a parameter
    CHECK_EQ_STR(
        sx("dev m = invert m"),
        "(Program (Binding 'dev' (Param 'm') (App 'invert' (Var 'invert') (Var 'm'))))");

    // where desugars to a let wrapping the body ('fn'/'x': 'f' is the pitch F)
    CHECK_EQ_STR(
        sx("fn = x\n  where\n    x = c"),
        "(Program (Binding 'fn' (Let 'fn' (Binding 'x' (PitchLit 'c')) (Var 'x'))))");

    // class declaration: name, type variable (Param), method signatures
    // (type variables can't be pitch letters, so we use 't')
    CHECK_EQ_STR(
        sx("class Describable t where\n  describe :: t -> Int"),
        "(Program (ClassDecl 'Describable' (Param 't') "
        "(MethodSig 'describe' (TypeAtom 't') (TypeAtom '->') (TypeAtom 'Int'))))");

    // instance declaration: class, instance type (Con), method bindings
    CHECK_EQ_STR(
        sx("instance Describable Pitch where\n  describe p = semitones p"),
        "(Program (InstanceDecl 'Describable' (Con 'Pitch') "
        "(Binding 'describe' (Param 'p') (App 'semitones' (Var 'semitones') (Var 'p')))))");

    // an operator-defined instance method (infix form)
    CHECK_EQ_STR(
        sx("instance Transposable Music where\n  m ^+ i = up m i"),
        "(Program (InstanceDecl 'Transposable' (Con 'Music') "
        "(Binding '^+' (Param 'm') (Param 'i') "
        "(App 'up' (Var 'up') (Var 'm') (Var 'i')))))");

    // ---- multi-line expressions (offside continuation) -------------------
    // application continues onto an indented line
    CHECK_EQ_STR(
        sx("main = foo\n  bar"),
        "(Program (Binding 'main' (App 'foo' (Var 'foo') (Var 'bar'))))");

    // a trailing operator carries the expression onto the next line
    CHECK_EQ_STR(
        sx("main = 1 +\n  2"),
        "(Program (Binding 'main' (BinOp '+' (IntLit '1') (IntLit '2'))))");

    // if / then / else split across lines
    CHECK_EQ_STR(
        sx("main = if p\n  then x\n  else y"),
        "(Program (Binding 'main' (If 'if' (Var 'p') (Var 'x') (Var 'y'))))");

    // a binding body may start on the next (indented) line
    CHECK_EQ_STR(
        sx("foo =\n  subj"),
        "(Program (Binding 'foo' (Var 'subj')))");

    // a sibling binding at the same column is NOT swallowed as a continuation
    CHECK_EQ_STR(
        sx("foo = x\nbar = y"),
        "(Program (Binding 'foo' (Var 'x')) (Binding 'bar' (Var 'y')))");

    // ---- pipe, cons, case ------------------------------------------------
    CHECK_EQ_STR(
        sx("main = x |> fn"),
        "(Program (Binding 'main' (BinOp '|>' (Var 'x') (Var 'fn'))))");

    CHECK_EQ_STR(
        sx("main = x : xs"),
        "(Program (Binding 'main' (BinOp ':' (Var 'x') (Var 'xs'))))");

    // case with an empty-list pattern and a cons pattern; alt body after it is
    // not swallowed (the case stops at the dedented next binding)
    CHECK_EQ_STR(
        sx("classify xs = case xs of\n  [] -> 0\n  h : t -> h\nmain = classify []"),
        "(Program (Binding 'classify' (Param 'xs') "
        "(Case 'case' (Var 'xs') "
        "(Alt '[' (PatCon '[') (IntLit '0')) "
        "(Alt 'h' (PatCon ':' (PatVar 'h') (PatVar 't')) (Var 'h')))) "
        "(Binding 'main' (App 'classify' (Var 'classify') (ListLit '['))))");
}
