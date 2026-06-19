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

// Parse errors in the prelude itself (e.g. a reserved letter used as a name).
bool prelude_parse_errors() {
    std::vector<Token> toks = lex::tokenize(prelude());
    std::vector<std::string> errs;
    parse::parse_program(std::move(toks), &errs);
    return !errs.empty();
}

} // namespace

void run_stdlib_tests() {
    // the prelude parses cleanly and is well typed
    CHECK(!prelude_parse_errors());
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

    // append (now defined with case + cons in the prelude)
    CHECK_EQ_STR(eval::show_value(run("main = append [1, 2] [3, 4]")), "[ 1 2 3 4 ]");
    CHECK_EQ_STR(eval::show_value(run("main = append [] [3, 4]")), "[ 3 4 ]");

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

    // ---- music transforms ------------------------------------------------
    // build a phrase from a list of pitches, sequence / stack it
    CHECK_EQ_STR(eval::show_value(run("main = line (notes [c', e', g'])")),
                 "(C4:1/4 :+: (E4:1/4 :+: G4:1/4))");
    CHECK_EQ_STR(eval::show_value(run("main = chord (notes [c', e', g'])")),
                 "(C4:1/4 :=: (E4:1/4 :=: G4:1/4))");

    // transpose a whole phrase up a perfect fifth
    CHECK_EQ_STR(eval::show_value(run("main = transpose P5 (c' e' g')")),
                 "((G4:1/4 :+: B4:1/4) :+: D5:1/4)");
    CHECK_EQ_STR(type_of("main = 0", "transpose"),
                 "Transposable t0 => Interval -> t0 -> t0");

    // retrograde reverses the order in time
    CHECK_EQ_STR(eval::show_value(run("main = retrograde (c' e' g')")),
                 "(G4:1/4 :+: (E4:1/4 :+: C4:1/4))");
    CHECK_EQ_STR(type_of("main = 0", "retrograde"), "Music -> Music");

    // melodic inversion about the first pitch — spelling mirrors too
    // (about C4: E4 -> Ab3 [major third below], G4 -> F3 [fifth below])
    CHECK_EQ_STR(eval::show_value(run("main = invert (c' e' g')")),
                 "((C4:1/4 :+: Ab3:1/4) :+: F3:1/4)");

    // repeat a phrase
    CHECK_EQ_STR(eval::show_value(run("main = times 3 c'")),
                 "(C4:1/4 :+: (C4:1/4 :+: C4:1/4))");
    // triplet = tuplet 3 2: three notes in the time of two (quarter -> 1/6)
    CHECK_EQ_STR(eval::show_value(run("main = triplet (c' c' c')")),
                 "((C4:1/6 :+: C4:1/6) :+: C4:1/6)");
    CHECK_EQ_STR(type_of("main = 0", "line"), "[Music] -> Music");

    // the headline composer example: a subject in parallel with its inversion,
    // answered up a fifth. Subject C D E G -> inverted+transposed answer G F Eb C.
    {
        std::string dev =
            "subject = c' d' e' g'\n"
            "development subj = subj `par` (invert subj ^+ P5)\n"
            "main = development subject";
        CHECK(!type_errors(dev));
        eval::Value v = run(dev);
        CHECK(v.kind == eval::ValueKind::Music);
        CHECK_EQ_STR(eval::show_value(v),
                     "((((C4:1/4 :+: D4:1/4) :+: E4:1/4) :+: G4:1/4)"
                     " :=: (((G4:1/4 :+: F4:1/4) :+: Eb4:1/4) :+: C4:1/4))");
    }

    // onInstrument: assigns an instrument, producing a Control wrapper.
    {
        const std::string prog = "main = onInstrument Cello (c d e)";
        CHECK(!type_errors(prog));
        CHECK_EQ_STR(type_of(prog, "main"), "Music");
        eval::Value v = run(prog);
        CHECK(v.kind == eval::ValueKind::Music);
        CHECK_EQ_STR(eval::show_value(v),
                     "inst(Cello, ((C3:1/4 :+: D3:1/4) :+: E3:1/4))");
    }

    // a custom soundfont: sfz "<path>" is an Instrument; onInstrument carries it.
    {
        const std::string prog = "main = onInstrument (sfz \"cello.sfz\") (c d e)";
        CHECK(!type_errors(prog));
        CHECK_EQ_STR(type_of(prog, "main"), "Music");
        eval::Value v = run(prog);
        CHECK_EQ_STR(eval::show_value(v),
                     "inst(\"cello.sfz\", ((C3:1/4 :+: D3:1/4) :+: E3:1/4))");
    }

    // a raw GM-program instrument: gm n is an Instrument; onInstrument carries it.
    {
        const std::string prog = "main = onInstrument (gm 42) (c d e)";
        CHECK(!type_errors(prog));
        CHECK_EQ_STR(type_of(prog, "main"), "Music");
        eval::Value v = run(prog);
        CHECK_EQ_STR(eval::show_value(v),
                     "inst(gm 42, ((C3:1/4 :+: D3:1/4) :+: E3:1/4))");
    }

    // tempo / velocity build Control nodes (shown as tempo(...)/vel(...)).
    {
        eval::Value t = run("main = tempo 90 (c d)");
        CHECK_EQ_STR(eval::show_value(t), "tempo(90, (C3:1/4 :+: D3:1/4))");
        eval::Value v = run("main = velocity 100 (c d)");
        CHECK_EQ_STR(eval::show_value(v), "vel(100, (C3:1/4 :+: D3:1/4))");
    }

    // a bare pitch lifts to a one-note phrase in the Control builders (no wrapper)
    {
        CHECK_EQ_STR(eval::show_value(run("main = onInstrument Cello c")),
                     "inst(Cello, C3:1/4)");
        CHECK_EQ_STR(eval::show_value(run("main = tempo 90 c'")), "tempo(90, C4:1/4)");
        CHECK_EQ_STR(eval::show_value(run("main = velocity 40 c'")), "vel(40, C4:1/4)");
        CHECK(!type_errors("main = onInstrument Cello c'"));
    }
}
