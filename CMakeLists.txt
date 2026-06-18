cmake_minimum_required(VERSION 3.16)
project(calliope CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Core sources shared by the driver and the test binary.
set(CALLIOPE_CORE
    src/compiler/core/rational.cpp
    src/compiler/core/pitch.cpp
    src/compiler/core/music.cpp
    src/compiler/core/lexer.cpp
    src/compiler/core/ast.cpp
    src/compiler/core/parser.cpp
    src/compiler/core/eval.cpp
    src/compiler/core/typecheck.cpp
    src/compiler/core/driver.cpp
)

add_executable(calliope
    src/compiler/compiler.cpp
    ${CALLIOPE_CORE}
)

target_include_directories(calliope PRIVATE src/compiler)

# Path to the Calliope standard library (loaded as a prelude before user code).
set(CALLIOPE_STDLIB_DIR "${CMAKE_SOURCE_DIR}/standard_library")
target_compile_definitions(calliope PRIVATE
    CALLIOPE_PRELUDE_PATH="${CALLIOPE_STDLIB_DIR}/prelude.cal")

if(MSVC)
    target_compile_options(calliope PRIVATE /W4)
else()
    target_compile_options(calliope PRIVATE -Wall -Wextra)
endif()

# --- interactive interpreter / REPL ---------------------------------------
add_executable(calliope-repl
    src/compiler/interactive.cpp
    ${CALLIOPE_CORE}
)
target_include_directories(calliope-repl PRIVATE src/compiler)
target_compile_definitions(calliope-repl PRIVATE
    CALLIOPE_PRELUDE_PATH="${CALLIOPE_STDLIB_DIR}/prelude.cal")
if(MSVC)
    target_compile_options(calliope-repl PRIVATE /W4)
else()
    target_compile_options(calliope-repl PRIVATE -Wall -Wextra)
endif()

# --- tests (separate binary; does not link the main driver) ---------------
enable_testing()

add_executable(calliope_tests
    tests/test.cpp
    tests/test_lexer.cpp
    tests/test_parser.cpp
    tests/test_rational.cpp
    tests/test_pitch.cpp
    tests/test_music.cpp
    tests/test_eval.cpp
    tests/test_typecheck.cpp
    tests/test_stdlib.cpp
    tests/test_driver.cpp
    tests/test_main.cpp
    ${CALLIOPE_CORE}
)

target_include_directories(calliope_tests PRIVATE src/compiler/core tests)
target_compile_definitions(calliope_tests PRIVATE
    CALLIOPE_PRELUDE_PATH="${CALLIOPE_STDLIB_DIR}/prelude.cal")

if(MSVC)
    target_compile_options(calliope_tests PRIVATE /W4)
else()
    target_compile_options(calliope_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME calliope_tests COMMAND calliope_tests)
