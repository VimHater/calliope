#include "test.hpp"

#include "driver.hpp"
#include "eval.hpp"

#include <string>
#include <vector>

using namespace calliope;

void run_driver_tests() {
    const char* PRELUDE = CALLIOPE_PRELUDE_PATH; // path, resolved by the driver
    const driver::LoadOptions none{};            // no prelude, no preload
    const driver::LoadOptions with_prelude{PRELUDE, "", true};   // preload (REPL-style)
    const driver::LoadOptions can_load{PRELUDE, "", false};      // #load resolvable

    // compile a whole program (no prelude needed for plain arithmetic)
    {
        driver::Compilation c;
        driver::compile("main = 1 + 2 * 3", none, c);
        CHECK(driver::ok(c));
        CHECK(c.has_main);
        CHECK(c.main_value.i == 7);
        CHECK_EQ_STR(c.main_type, "Int");
    }

    // pitch builtins, still no prelude
    {
        driver::Compilation c;
        driver::compile("main = semitones (c' ^+ P5)", none, c);
        CHECK(driver::ok(c));
        CHECK(c.main_value.i == 55);
    }

    // type of main only, without evaluating
    {
        std::vector<std::string> errs;
        CHECK_EQ_STR(driver::type_of_main("main = \\x -> x", none, errs), "t0 -> t0");
        CHECK(errs.empty());
    }

    // errors land in the right bucket; ok() reflects them
    {
        driver::Compilation c;
        driver::compile("main = 1 + c'", none, c);   // Int + Pitch
        CHECK(!driver::ok(c));
        CHECK(!c.type_errors.empty());
    }

    // the stdlib is NOT in scope without loading it
    {
        driver::Compilation c;
        driver::compile("main = length [1, 2, 3]", can_load, c);
        CHECK(!driver::ok(c));            // `length` is unknown
    }

    // ...but preloading the prelude (as the REPL does) brings it into scope
    {
        driver::Compilation c;
        driver::compile("main = length (reverse [1, 2, 3])", with_prelude, c);
        CHECK(driver::ok(c));
        CHECK(c.main_value.i == 3);
    }

    // ...and so does an explicit #load directive in the program
    {
        driver::Compilation c;
        driver::compile("#load \"prelude\"\nmain = length [10, 20]", can_load, c);
        CHECK(driver::ok(c));
        CHECK(c.main_value.i == 2);
    }

    // directory_of is cross-platform (handles '/' and '\\')
    CHECK_EQ_STR(driver::directory_of("a/b/c.cal"), "a/b");
    CHECK_EQ_STR(driver::directory_of("a\\b\\c.cal"), "a\\b");
    CHECK_EQ_STR(driver::directory_of("c.cal"), "");

    // is_definition distinguishes definitions from expressions (REPL session)
    CHECK(driver::is_definition("x = 1"));
    CHECK(driver::is_definition("inc x = x + 1"));
    CHECK(driver::is_definition("main :: Int"));       // a signature
    CHECK(!driver::is_definition("1 + 2"));            // expression
    CHECK(!driver::is_definition("x == 3"));           // expression (not a binding)
    CHECK(!driver::is_definition("c d e"));            // bare notation
}
