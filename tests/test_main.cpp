#include "test.hpp"

#include <cstdio>

// Defined in the per-area test files.
void run_lexer_tests();
void run_parser_tests();
void run_rational_tests();
void run_pitch_tests();
void run_music_tests();
void run_eval_tests();
void run_typecheck_tests();
void run_stdlib_tests();
void run_driver_tests();
void run_midi_tests();

int main() {
    run_lexer_tests();
    run_parser_tests();
    run_rational_tests();
    run_pitch_tests();
    run_music_tests();
    run_eval_tests();
    run_typecheck_tests();
    run_stdlib_tests();
    run_driver_tests();
    run_midi_tests();

    std::printf("\n%d checks, %d failures\n",
                calliope::test::g_checks, calliope::test::g_fails);
    return calliope::test::g_fails == 0 ? 0 : 1;
}
