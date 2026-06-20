#include "test.hpp"

#include "eval.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <utility>

using namespace calliope;

namespace {

// Run a program, return the value bound to `name` (Unit if missing/error).
eval::Value run(std::string_view src, std::string_view name) {
    std::vector<Token> toks = lex::tokenize(src);
    ast::Ast a = parse::parse_program(std::move(toks), nullptr);
    eval::Interp I;
    auto env = eval::eval_program(a, I);
    eval::Value v;
    eval::env_lookup(env, name, v);
    return v;
}

// Convenience: evaluate `main = <expr>` and fetch main.
eval::Value run_main(std::string_view expr) {
    std::string src = "main = ";
    src += expr;
    return run(src, "main");
}

} // namespace

void run_eval_tests() {
    // arithmetic + precedence
    CHECK(run_main("1 + 2 * 3").i == 7);
    CHECK(run_main("(1 + 2) * 3").i == 9);
    CHECK(run_main("10 - 4 - 3").i == 3);   // left assoc would be 3; check value
    CHECK(run_main("8 `div` 2").i == 4);    // integer division is now `div`
    CHECK(run_main("7 `mod` 3").i == 1);
    CHECK(run_main("2 + 6 `div` 2").i == 5); // `div` binds tighter than `+`
    // `/` is fractional: two Ints make an exact Rational
    {
        eval::Value r = run_main("7 / 2");
        CHECK(r.kind == eval::ValueKind::Rat && r.rat.num == 7 && r.rat.den == 2);
        eval::Value sum = run_main("1 / 2 + 1 / 4"); // Rational + Rational
        CHECK(sum.kind == eval::ValueKind::Rat && sum.rat.num == 3 && sum.rat.den == 4);
        // mismatched Int + Rational coerces the Int up: 1 + 1/2 = 3/2
        eval::Value mix = run_main("1 + 1 / 2");
        CHECK(mix.kind == eval::ValueKind::Rat && mix.rat.num == 3 && mix.rat.den == 2);
        eval::Value mix2 = run_main("(1 / 2) * 4"); // Rational * Int = 2/1
        CHECK(mix2.kind == eval::ValueKind::Rat && mix2.rat.num == 2 && mix2.rat.den == 1);
    }

    // comparison + if
    CHECK(run_main("if 1 < 2 then 10 else 20").i == 10);
    CHECK(run_main("if 2 < 1 then 10 else 20").i == 20);

    // the full comparison set (< > <= >= == /=)
    CHECK(run_main("5 > 3").i == 1);
    CHECK(run_main("3 > 5").i == 0);
    CHECK(run_main("3 <= 3").i == 1);
    CHECK(run_main("4 <= 3").i == 0);
    CHECK(run_main("4 >= 9").i == 0);
    CHECK(run_main("9 >= 9").i == 1);
    CHECK(run_main("5 /= 3").i == 1);
    CHECK(run_main("3 /= 3").i == 0);

    // application, currying, lambda
    CHECK(run("id x = x\nmain = id 42", "main").i == 42);
    CHECK(run("k x y = x\nmain = k 7 9", "main").i == 7);
    CHECK(run("main = (\\x -> x + 1) 41", "main").i == 42);

    // booleans: literals, and/or/not, short-circuit
    CHECK(run_main("True").i == 1);
    CHECK(run_main("False").i == 0);
    CHECK(run_main("True and False").i == 0);
    CHECK(run_main("True or False").i == 1);
    CHECK(run_main("not True").i == 0);
    CHECK(run_main("1 < 2 and 2 < 3").i == 1);
    CHECK(run_main("not (1 < 2) or 3 < 4").i == 1);
    // short-circuit: right side (division by zero) must not be evaluated
    CHECK(run_main("True or 1 / 0 == 0").i == 1);
    CHECK(run_main("False and 1 / 0 == 0").i == 0);

    // recursion: factorial
    CHECK(run("fac n = if n == 0 then 1 else n * fac (n - 1)\nmain = fac 5",
              "main").i == 120);

    // mutual recursion (+ booleans returned through it)
    CHECK(run("isEven n = if n == 0 then True else isOdd (n - 1)\n"
              "isOdd n = if n == 0 then False else isEven (n - 1)\n"
              "main = isEven 10",
              "main").i == 1);
    CHECK(run("isEven n = if n == 0 then True else isOdd (n - 1)\n"
              "isOdd n = if n == 0 then False else isEven (n - 1)\n"
              "main = isEven 7",
              "main").i == 0);

    // let / where
    CHECK(run("main = let y = 5 in y * y", "main").i == 25);
    // (p/q, not a/b: a and b are the pitches A and B)
    CHECK(run("main = p * q\n  where\n    p = 6\n    q = 7", "main").i == 42);

    // music: transpose a pitch up a fifth, check via semitones builtin
    // c' = C4 = 48 ; up P5 -> G4 = 55
    CHECK(run_main("semitones (c' ^+ P5)").i == 55);
    CHECK(run_main("semitones (c' ^- P8)").i == 36);  // down an octave -> C3

    // a transposed pitch is itself a Pitch value, spelled
    eval::Value g = run_main("c' ^+ P5");
    CHECK(g.kind == eval::ValueKind::Pitch);
    CHECK(g.pitch.letter == 4);   // G
    CHECK(g.pitch.accidental == 0);
    CHECK(g.pitch.octave == 4);

    // ---- type classes: runtime dispatch on the argument's type -----------
    const char* desc =
        "class Describable t where\n"
        "  describe :: t -> Int\n"
        "instance Describable Pitch where\n"
        "  describe p = semitones p\n"
        "instance Describable Bool where\n"
        "  describe flag = if flag then 100 else 0\n";
    // dispatches to the Pitch instance: semitones C4 = 48
    CHECK(run(std::string(desc) + "main = describe c'", "main").i == 48);
    // dispatches to the Bool instance
    CHECK(run(std::string(desc) + "main = describe True", "main").i == 100);
    CHECK(run(std::string(desc) + "main = describe False", "main").i == 0);
    // same call site, both instances reachable through one method value
    CHECK(run(std::string(desc) + "main = describe c' + describe True", "main").i == 148);

    // ---- Music IR: notation desugars into Note/Rest/Seq/Par --------------
    // a run of pitch literals sequences (bare letter = octave 3); durations honored
    CHECK(run_main("c d e").kind == eval::ValueKind::Music);
    CHECK_EQ_STR(eval::show_value(run_main("c'4 d'8 e'")),
                 "((C4:1/4 :+: D4:1/8) :+: E4:1/4)");
    // a chord sounds in parallel
    CHECK_EQ_STR(eval::show_value(run_main("<c' e' g'>")),
                 "((C4:1/4 :=: E4:1/4) :=: G4:1/4)");
    // a chord-level duration sets every note's duration: <c e g>2 = half, >4. = dotted
    CHECK_EQ_STR(eval::show_value(run_main("<c' e' g'>2")),
                 "((C4:1/2 :=: E4:1/2) :=: G4:1/2)");
    CHECK_EQ_STR(eval::show_value(run_main("<c' e' g'>4.")),
                 "((C4:3/8 :=: E4:3/8) :=: G4:3/8)");
    // a bare rest is Music
    CHECK_EQ_STR(eval::show_value(run_main("r")), "r:1/4");
    // transposing a whole phrase via the builtin Transposable Music instance
    CHECK_EQ_STR(eval::show_value(run_main("c d e ^+ P5")),
                 "((G3:1/4 :+: A3:1/4) :+: B3:1/4)");
    // explicit :+: / :=: combinators build the same IR (Pitch operands lift to Notes)
    CHECK_EQ_STR(eval::show_value(run_main("c' :+: d'")), "(C4:1/4 :+: D4:1/4)");
    // `:*:` repeats a phrase n times in a row (right-leaning, like `times`)
    CHECK_EQ_STR(eval::show_value(run_main("c' :*: 3")),
                 "(C4:1/4 :+: (C4:1/4 :+: C4:1/4))");
    // rests carry durations (default quarter, `r2` = half, `r4.` = dotted quarter)
    CHECK_EQ_STR(eval::show_value(run_main("r")),  "r:1/4");
    CHECK_EQ_STR(eval::show_value(run_main("r2")), "r:1/2");
    CHECK_EQ_STR(eval::show_value(run_main("r4.")), "r:3/8");
    // tuplet 3 2: a triplet scales each duration by 2/3 (quarter -> 1/6)
    CHECK_EQ_STR(eval::show_value(run_main("tuplet 3 2 (c' c' c')")),
                 "((C4:1/6 :+: C4:1/6) :+: C4:1/6)");
    // tie: two same-pitch notes join into one of summed duration
    CHECK_EQ_STR(eval::show_value(run_main("c'4 ~ c'8")), "C4:3/8");
    // ties chain (the Music result is itself a valid left operand)
    CHECK_EQ_STR(eval::show_value(run_main("c'4 ~ c'8 ~ c'8")), "C4:1/2");
    // a tied chord: matching chords merge, each note's duration summed
    CHECK_EQ_STR(eval::show_value(run_main("<c' e' g'> ~ <c' e' g'>")),
                 "((C4:1/2 :=: E4:1/2) :=: G4:1/2)");

    // meter wraps a phrase in a time-signature Control (shown as meter(n/d, ...))
    CHECK_EQ_STR(eval::show_value(run_main("meter 3 4 (c' d' e')")),
                 "meter(3/4, ((C4:1/4 :+: D4:1/4) :+: E4:1/4))");
    // `|` barline sequences two measures with a boundary marker between them
    CHECK_EQ_STR(eval::show_value(run_main("c'4 | d'4")),
                 "(C4:1/4 :+: (| :+: D4:1/4))");
    // a bare pitch lifts in meter (Phrase), like tempo/velocity
    CHECK_EQ_STR(eval::show_value(run_main("meter 2 2 c'")), "meter(2/2, C4:1/4)");

    // key signatures respell *floating* (bare) accidentals; explicit ones untouched
    CHECK_EQ_STR(eval::show_value(run_main("withKey 2 (c f g)")),   // D major: C#, F#
                 "key(2, ((C#3:1/4 :+: F#3:1/4) :+: G3:1/4))");
    CHECK_EQ_STR(eval::show_value(run_main("withKey 2 (fis fes)")), // explicit overrides
                 "key(2, (F#3:1/4 :+: Fb3:1/4))");
    CHECK_EQ_STR(eval::show_value(run_main("c f g")),               // no key -> natural
                 "((C3:1/4 :+: F3:1/4) :+: G3:1/4)");

    // ---- comparison: pitch ordering (by height) + deep Music/== --------
    CHECK_EQ_STR(eval::show_value(run_main("c' < d'")), "True");   // C4 below D4
    CHECK_EQ_STR(eval::show_value(run_main("e' > c'")), "True");
    CHECK_EQ_STR(eval::show_value(run_main("a < b")),   "True");   // not a chord
    CHECK_EQ_STR(eval::show_value(run_main("fis' == ges'")), "False"); // spelled: F# != Gb
    CHECK_EQ_STR(eval::show_value(run_main("fis' <= ges'")), "True");  // same height
    CHECK_EQ_STR(eval::show_value(run_main("<c' e' g'> == <c' e' g'>")), "True");
    CHECK_EQ_STR(eval::show_value(run_main("(c d e) == (c d e)")), "True");
    CHECK_EQ_STR(eval::show_value(run_main("(c d e) == (c d f)")), "False");

    // ---- pipe, cons, case ------------------------------------------------
    // pipe: x |> f = f x (left-associative, so it chains)
    CHECK(run_main("5 |> (\\x -> x + 1)").i == 6);
    CHECK(run_main("5 |> (\\x -> x + 1) |> (\\x -> x * 2)").i == 12);
    // cons operator builds a list (right-associative)
    CHECK_EQ_STR(eval::show_value(run_main("1 : 2 : [3]")), "[ 1 2 3 ]");
    // case: recursion over list shape
    CHECK(run("sumAll xs = case xs of\n"
              "  []    -> 0\n"
              "  h : t -> h + sumAll t\n"
              "main = sumAll [10, 20, 30]", "main").i == 60);
    // case: literal + wildcard patterns, first match wins
    CHECK(run("classify n = case n of\n  0 -> 100\n  _ -> 200\nmain = classify 0", "main").i == 100);
    CHECK(run("classify n = case n of\n  0 -> 100\n  _ -> 200\nmain = classify 7", "main").i == 200);
}
