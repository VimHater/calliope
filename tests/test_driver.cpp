#include "test.hpp"

#include "driver.hpp"
#include "eval.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace calliope;

namespace {
std::string prelude() {
    std::ifstream f(CALLIOPE_PRELUDE_PATH, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

void run_driver_tests() {
    // compile a whole program (no prelude needed for plain arithmetic)
    {
        driver::Compilation c;
        driver::compile("", "main = 1 + 2 * 3", c);
        CHECK(driver::ok(c));
        CHECK(c.has_main);
        CHECK(c.main_value.i == 7);
        CHECK_EQ_STR(c.main_type, "Int");
    }

    // compile a single expression (the REPL path)
    {
        driver::Compilation c;
        driver::compile_expr("", "semitones (c' ^+ P5)", c);
        CHECK(driver::ok(c));
        CHECK(c.main_value.i == 55);
    }

    // type of an expression, without evaluating
    {
        std::vector<std::string> errs;
        CHECK_EQ_STR(driver::type_of_expr("", "\\x -> x", errs), "t0 -> t0");
        CHECK(errs.empty());
    }

    // errors land in the right bucket; ok() reflects them
    {
        driver::Compilation c;
        driver::compile("", "main = 1 + c'", c);   // Int + Pitch
        CHECK(!driver::ok(c));
        CHECK(!c.type_errors.empty());
    }

    // the prelude argument really brings the stdlib into scope
    {
        driver::Compilation c;
        driver::compile(prelude(), "main = length (reverse [1, 2, 3])", c);
        CHECK(driver::ok(c));
        CHECK(c.main_value.i == 3);
    }
}
