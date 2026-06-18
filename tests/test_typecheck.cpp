#include "test.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"

#include <utility>

using namespace calliope;

namespace {

std::string type_of(std::string_view src, std::string_view name) {
    std::vector<Token> toks = lex::tokenize(src);
    ast::Ast a = parse::parse_program(std::move(toks), nullptr);
    std::vector<std::string> errs;
    return types::infer_named_type(a, name, errs);
}

bool has_type_error(std::string_view src) {
    std::vector<Token> toks = lex::tokenize(src);
    ast::Ast a = parse::parse_program(std::move(toks), nullptr);
    std::vector<std::string> errs;
    types::typecheck_program(a, errs);
    return !errs.empty();
}

} // namespace

void run_typecheck_tests() {
    // literals and arithmetic
    CHECK_EQ_STR(type_of("main = 1 + 2 * 3", "main"), "Int");
    CHECK_EQ_STR(type_of("main = 1 < 2", "main"), "Bool");

    // function types
    CHECK_EQ_STR(type_of("inc x = x + 1", "inc"), "Int -> Int");
    CHECK_EQ_STR(type_of("k x y = x + y", "k"), "Int -> Int -> Int");

    // polymorphic identity (let-generalized at top level)
    CHECK_EQ_STR(type_of("id x = x", "id"), "t0 -> t0");

    // if and recursion
    CHECK_EQ_STR(type_of("fac n = if n == 0 then 1 else n * fac (n - 1)", "fac"),
                 "Int -> Int");

    // music: transpose
    CHECK_EQ_STR(type_of("main = c' ^+ P5", "main"), "Pitch");
    CHECK_EQ_STR(type_of("main = semitones (c' ^+ P5)", "main"), "Int");
    CHECK_EQ_STR(type_of("main = c d e", "main"), "Music");

    // lists
    CHECK_EQ_STR(type_of("main = [1, 2, 3]", "main"), "[Int]");

    // booleans
    CHECK_EQ_STR(type_of("main = True and False", "main"), "Bool");
    CHECK_EQ_STR(type_of("main = not (1 < 2)", "main"), "Bool");
    CHECK_EQ_STR(type_of("isPos n = n < 0 or n == 0", "isPos"), "Int -> Bool");

    // type errors are detected
    CHECK(has_type_error("main = 1 + c'"));        // Int + Pitch
    CHECK(has_type_error("main = if 1 then 2 else 3")); // non-Bool condition
    CHECK(has_type_error("main = semitones 5"));   // semitones wants a Pitch
    CHECK(has_type_error("main = 1 and True"));    // Int where Bool expected

    // well-typed programs report no error
    CHECK(!has_type_error("inc x = x + 1\nmain = inc 41"));

    // pipe, cons, case
    CHECK_EQ_STR(type_of("main = 1 : [2, 3]", "main"), "[Int]");
    CHECK_EQ_STR(type_of("main = 5 |> (\\x -> x + 1)", "main"), "Int");
    CHECK_EQ_STR(type_of("sumList xs = case xs of\n  []    -> 0\n  h : t -> h + sumList t",
                         "sumList"), "[Int] -> Int");
    CHECK(has_type_error("main = case 1 of\n  [] -> 0\n  _ -> 1"));  // Int vs [a]

    // ---- type classes ----------------------------------------------------
    // (type variables can't be pitch letters a-g/r/s, so we use `t`.)
    const char* desc =
        "class Describable t where\n"
        "  describe :: t -> Int\n"
        "instance Describable Pitch where\n"
        "  describe p = semitones p\n";

    // a class method has a qualified (constrained) scheme
    CHECK_EQ_STR(type_of(desc, "describe"), "Describable t0 => t0 -> Int");

    // using a method at a type with an instance type-checks to that result
    CHECK_EQ_STR(type_of(std::string(desc) + "main = describe c'", "main"), "Int");

    // the constraint propagates into a function that uses the method
    CHECK_EQ_STR(type_of(std::string(desc) + "twice x = describe x + describe x", "twice"),
                 "Describable t0 => t0 -> Int");

    // builtin Transposable: ^+ is a real class method with a Pitch instance
    CHECK_EQ_STR(type_of("main = c' ^+ P5", "main"), "Pitch");

    // using a method at a type with NO instance is an error
    CHECK(has_type_error(std::string(desc) + "main = describe True"));
    // ^+ on a type with no Transposable instance is an error
    CHECK(has_type_error("main = 1 ^+ P5"));
    // an instance whose impl has the wrong type is an error
    CHECK(has_type_error(
        "class Describable t where\n"
        "  describe :: t -> Int\n"
        "instance Describable Pitch where\n"
        "  describe p = p\n"));   // returns Pitch, not Int

    // a user class with two instances; both well typed
    CHECK(!has_type_error(
        std::string(desc) +
        "instance Describable Bool where\n"
        "  describe flag = if flag then 1 else 0\n"
        "main = describe c' + describe False"));
}
