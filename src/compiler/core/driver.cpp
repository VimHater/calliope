#include "driver.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"

namespace calliope::driver {

namespace {

std::string combine(std::string_view prelude, std::string_view program) {
    std::string s;
    s.reserve(prelude.size() + program.size() + 1);
    s.append(prelude.data(), prelude.size());
    s.push_back('\n');
    s.append(program.data(), program.size());
    return s;
}

std::string wrap_expr(std::string_view expr) {
    std::string s = "main = ";
    s.append(expr.data(), expr.size());
    return s;
}

} // namespace

bool ok(const Compilation& c) {
    return c.parse_errors.empty() && c.type_errors.empty() && c.runtime_errors.empty();
}

void compile(std::string_view prelude, std::string_view program, Compilation& out) {
    out.source = combine(prelude, program);
    out.ast = parse::parse_program(lex::tokenize(out.source), &out.parse_errors);
    out.main_type = types::infer_named_type(out.ast, "main", out.type_errors);
    auto env = eval::eval_program(out.ast, out.interp);
    out.has_main = eval::env_lookup(env, "main", out.main_value);
    out.runtime_errors = out.interp.errors;
}

void compile_expr(std::string_view prelude, std::string_view expr, Compilation& out) {
    compile(prelude, wrap_expr(expr), out);
}

std::string type_of_main(std::string_view prelude, std::string_view program,
                         std::vector<std::string>& errors) {
    std::string src = combine(prelude, program);
    ast::Ast a = parse::parse_program(lex::tokenize(src), nullptr);
    return types::infer_named_type(a, "main", errors);
}

std::string type_of_expr(std::string_view prelude, std::string_view expr,
                         std::vector<std::string>& errors) {
    return type_of_main(prelude, wrap_expr(expr), errors);
}

} // namespace calliope::driver
