#include "test.hpp"

#include <cstdio>

// Defined in the per-area test files.
void run_lexer_tests();
void run_parser_tests();

int main() {
    run_lexer_tests();
    run_parser_tests();

    std::printf("\n%d checks, %d failures\n",
                calliope::test::g_checks, calliope::test::g_fails);
    return calliope::test::g_fails == 0 ? 0 : 1;
}
