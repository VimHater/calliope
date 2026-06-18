#include "core/driver.hpp"
#include "core/eval.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Calliope interactive interpreter / REPL — an I/O shell around
// calliope::driver. Reads a line, asks the driver to compile it, writes the
// result. All language work lives in the driver/parser/eval APIs.
//
//   :type <expr>   show the type without evaluating
//   :quit / :q     exit
//
// TODO: persistent session bindings, multi-line input, loading files, `:load`.

namespace {

std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string prelude_source() {
#ifdef CALLIOPE_PRELUDE_PATH
    return read_file(CALLIOPE_PRELUDE_PATH);
#else
    return std::string();
#endif
}

void print_errors(const char* kind, const std::vector<std::string>& errs) {
    for (const std::string& e : errs) std::printf("  %s error: %s\n", kind, e.c_str());
}

void show_type(const std::string& prelude, const std::string& expr) {
    std::vector<std::string> errs;
    std::string ty = calliope::driver::type_of_expr(prelude, expr, errs);
    print_errors("type", errs);
    if (!ty.empty()) std::printf("  :: %s\n", ty.c_str());
}

void eval_line(const std::string& prelude, const std::string& expr) {
    calliope::driver::Compilation c;
    calliope::driver::compile_expr(prelude, expr, c);
    print_errors("parse", c.parse_errors);
    print_errors("type", c.type_errors);
    if (c.has_main)
        std::printf("  %s%s%s\n", calliope::eval::show_value(c.main_value).c_str(),
                    c.main_type.empty() ? "" : "  :: ", c.main_type.c_str());
    print_errors("runtime", c.runtime_errors);
}

} // namespace

int main() {
    std::string prelude = prelude_source();

    std::printf("Calliope REPL — type an expression, :type <expr>, or :quit\n");
    std::string line;
    for (;;) {
        std::printf("λ> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) { std::printf("\n"); break; }

        std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        std::string s = line.substr(b);

        if (s == ":quit" || s == ":q") break;
        if (s.rfind(":type ", 0) == 0) { show_type(prelude, s.substr(6)); continue; }
        if (s[0] == ':') { std::printf("  unknown command '%s'\n", s.c_str()); continue; }

        eval_line(prelude, s);
    }
    return 0;
}
