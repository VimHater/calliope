#include "driver.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace calliope::driver {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- cross-platform path helpers (POSIX '/' and Windows '\\') -----------
bool is_sep(char c) { return c == '/' || c == '\\'; }

bool path_is_absolute(const std::string& p) {
    if (p.empty()) return false;
    if (is_sep(p[0])) return true;                       // /foo  or  \foo
    if (p.size() >= 2 && p[1] == ':') return true;       // C:\foo  (Windows drive)
    return false;
}

// Resolve `rel` against `base`: absolute paths and an empty base are used as-is.
std::string path_resolve(std::string_view base, const std::string& rel) {
    if (base.empty() || path_is_absolute(rel)) return rel;
    std::string out(base);
    if (!is_sep(out.back())) out += '/';                 // '/' is accepted on both OSes
    out += rel;
    return out;
}

// The argument of a `#load` directive (a string literal or a bare name).
std::string directive_arg(const ast::Ast& a, const ast::Node& dir) {
    if (dir.kids.empty()) return std::string();
    const ast::Node& arg = a.nodes[dir.kids[0]];
    std::string t(arg.tok.text);
    if (arg.kind == ast::NodeKind::StrLit && t.size() >= 2)
        return t.substr(1, t.size() - 2); // strip surrounding quotes
    return t;
}

// Copy one parsed unit's nodes into `dest` (offsetting every NodeId reference),
// and collect that unit's top-level declarations into `prog_kids`.
void append_unit(ast::Ast& dest, const ast::Ast& unit, std::vector<ast::NodeId>& prog_kids) {
    int off = static_cast<int>(dest.nodes.size());
    for (ast::Node node : unit.nodes) {
        for (ast::NodeId& k : node.kids) k += off;
        dest.nodes.push_back(std::move(node));
    }
    if (unit.root != ast::NoNode)
        for (ast::NodeId k : unit.nodes[unit.root].kids) prog_kids.push_back(k + off);
}

// Parse the program and every unit it loads, then merge their declarations into
// `out_ast`. Source text is owned by `sources` (stable addresses).
void assemble(std::string_view program, const LoadOptions& opts,
              std::deque<std::string>& sources, ast::Ast& out_ast,
              std::vector<std::string>& parse_errors) {
    // the program is its own unit (parsed first, to discover its #load directives)
    sources.push_back(std::string(program));
    std::vector<std::string> program_errors;
    ast::Ast prog_ast = parse::parse_program(lex::tokenize(sources.back()), &program_errors);

    std::vector<ast::Ast> loaded;
    auto load_text = [&](const std::string& text) {
        sources.push_back(text);
        loaded.push_back(parse::parse_program(lex::tokenize(sources.back()), &parse_errors));
    };

    if (opts.preload_prelude && !opts.prelude_path.empty()) {
        std::string s = read_file(std::string(opts.prelude_path));
        if (!s.empty()) load_text(s);
    }
    if (prog_ast.root != ast::NoNode) {
        for (ast::NodeId d : prog_ast.nodes[prog_ast.root].kids) {
            const ast::Node& n = prog_ast.nodes[d];
            if (n.kind != ast::NodeKind::Directive || n.tok.text != "load") continue;
            std::string arg = directive_arg(prog_ast, n);
            std::string path = (arg == "prelude") ? std::string(opts.prelude_path)
                                                  : path_resolve(opts.base_dir, arg);
            std::string s = read_file(path);
            if (s.empty()) parse_errors.push_back("cannot #load '" + arg + "'");
            else load_text(s);
        }
    }
    for (const std::string& e : program_errors) parse_errors.push_back(e);

    // merge: loaded units first (so the program can use them), then the program
    std::vector<ast::NodeId> prog_kids;
    for (const ast::Ast& u : loaded) append_unit(out_ast, u, prog_kids);
    append_unit(out_ast, prog_ast, prog_kids);

    ast::Node prog;
    prog.kind = ast::NodeKind::Program;
    prog.kids = std::move(prog_kids);
    out_ast.root = static_cast<ast::NodeId>(out_ast.nodes.size());
    out_ast.nodes.push_back(std::move(prog));
}

} // namespace

bool ok(const Compilation& c) {
    return c.parse_errors.empty() && c.type_errors.empty() && c.runtime_errors.empty();
}

void compile(std::string_view program, const LoadOptions& opts, Compilation& out) {
    assemble(program, opts, out.sources, out.ast, out.parse_errors);
    out.main_type = types::infer_named_type(out.ast, "main", out.type_errors);
    auto env = eval::eval_program(out.ast, out.interp);
    out.has_main = eval::env_lookup(env, "main", out.main_value);
    out.runtime_errors = out.interp.errors;
}

std::string type_of_main(std::string_view program, const LoadOptions& opts,
                         std::vector<std::string>& errors) {
    std::deque<std::string> sources;
    ast::Ast a;
    assemble(program, opts, sources, a, errors);
    return types::infer_named_type(a, "main", errors);
}

std::string directory_of(std::string_view path) {
    std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string_view::npos) return std::string();
    return std::string(path.substr(0, pos));
}

bool is_definition(std::string_view program) {
    std::vector<std::string> errs;
    ast::Ast a = parse::parse_program(lex::tokenize(program), &errs);
    if (!errs.empty() || a.root == ast::NoNode) return false;
    const ast::Node& prog = a.nodes[a.root];
    if (prog.kids.empty()) return false;
    ast::NodeKind k = a.nodes[prog.kids[0]].kind;
    return k == ast::NodeKind::Binding || k == ast::NodeKind::TypeSig ||
           k == ast::NodeKind::ClassDecl || k == ast::NodeKind::InstanceDecl;
}

} // namespace calliope::driver
