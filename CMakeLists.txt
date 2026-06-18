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
    src/compiler/core/lexer.cpp
    src/compiler/core/ast.cpp
    src/compiler/core/parser.cpp
)

add_executable(calliope
    src/compiler/compiler.cpp
    ${CALLIOPE_CORE}
)

target_include_directories(calliope PRIVATE src/compiler)

if(MSVC)
    target_compile_options(calliope PRIVATE /W4)
else()
    target_compile_options(calliope PRIVATE -Wall -Wextra)
endif()

# --- tests (separate binary; does not link the main driver) ---------------
enable_testing()

add_executable(calliope_tests
    tests/test.cpp
    tests/test_lexer.cpp
    tests/test_parser.cpp
    tests/test_rational.cpp
    tests/test_pitch.cpp
    tests/test_main.cpp
    ${CALLIOPE_CORE}
)

target_include_directories(calliope_tests PRIVATE src/compiler/core tests)

if(MSVC)
    target_compile_options(calliope_tests PRIVATE /W4)
else()
    target_compile_options(calliope_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME calliope_tests COMMAND calliope_tests)
