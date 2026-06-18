#include "parser.hpp"

#include <cstdio>
#include <utility>

namespace calliope::parse {

using namespace calliope::ast;

namespace {

// ---- token cursor -------------------------------------------------------

const Token& cur(Parser& p) { return p.toks[p.pos]; }

const Token& peek_at(Parser& p, std::size_t off) {
    std::size_t i = p.pos + off;
    if (i >= p.toks.size()) i = p.toks.size() - 1;
    return p.toks[i];
}

void advance(Parser& p) {
    if (p.toks[p.pos].kind != TokenKind::End) p.pos++;
}

void skip_newlines(Parser& p) {
    while (cur(p).kind == TokenKind::Newline) advance(p);
}

void error(Parser& p, const char* msg) {
    const Token& t = cur(p);
    char buf[256];
    std::snprintf(buf, sizeof buf, "%d:%d: %s (near '%.*s')",
                  t.line, t.col, msg,
                  static_cast<int>(t.text.size()), t.text.data());
    p.errors.emplace_back(buf);
}

bool is_keyword(std::string_view s) {
    return s == "let" || s == "in" || s == "where" || s == "if" ||
           s == "then" || s == "else" || s == "data" || s == "type" || s == "of" ||
           s == "and" || s == "or";  // 'and'/'or' are infix boolean operators
}

NodeId mk(Parser& p, NodeKind k, const Token& t) {
    Node n;
    n.kind = k;
    n.tok = t;
    return ast_add(p.ast, n);
}

// ---- operator fixity ----------------------------------------------------
// Provisional precedences (higher binds tighter). Real per-operator fixity
// declarations come later; see language_spec.md §9.
bool op_fixity(std::string_view op, int& prec, bool& right) {
    if (op == "$")   { prec = 0; right = true;  return true; }
    if (op == "==" || op == "<" || op == ">" || op == "/=" ||
        op == "<=" || op == ">=") { prec = 4; right = false; return true; } // compare
    if (op == ":=:") { prec = 3; right = true;  return true; }
    if (op == ":+:") { prec = 5; right = true;  return true; }
    if (op == "^+" || op == "^-") { prec = 6; right = false; return true; } // transpose
    if (op == "+" || op == "-") { prec = 6; right = false; return true; } // left-assoc
    if (op == "*" || op == "/") { prec = 7; right = false; return true; }
    if (op == ".")   { prec = 9; right = true;  return true; }
    prec = 9; right = false; return true; // default for other symbolic ops
}

// ---- forward declarations ----------------------------------------------
NodeId parse_expr(Parser& p, int min_prec);
NodeId parse_app(Parser& p);
NodeId parse_primary(Parser& p);
NodeId parse_binding(Parser& p, bool allow_where);
std::vector<NodeId> parse_block(Parser& p);

// ---- predicates ---------------------------------------------------------
bool starts_primary(TokenKind k) {
    switch (k) {
        case TokenKind::Pitch:    case TokenKind::Rest:    case TokenKind::Int:
        case TokenKind::Str:      case TokenKind::Ident:   case TokenKind::Upper:
        case TokenKind::LParen:   case TokenKind::LBracket:
        case TokenKind::Less:     case TokenKind::Backslash:
            return true;
        default:
            return false;
    }
}

bool starts_pitchish(TokenKind k) {
    return k == TokenKind::Pitch || k == TokenKind::Rest || k == TokenKind::Less;
}

bool can_start_arg(Parser& p) {
    const Token& t = cur(p);
    if (!starts_primary(t.kind)) return false;
    if (t.kind == TokenKind::Ident && is_keyword(t.text)) return false;
    // '<' after an operand is comparison, not a chord argument; a chord used as
    // an argument must be parenthesized: f (<c e g>).
    if (t.kind == TokenKind::Less) return false;
    return true;
}

// ---- compound primaries -------------------------------------------------
NodeId parse_list(Parser& p) {
    Token open = cur(p);
    advance(p); // [
    Node n;
    n.kind = NodeKind::ListLit;
    n.tok = open;
    if (cur(p).kind != TokenKind::RBracket) {
        n.kids.push_back(parse_expr(p, 0));
        while (cur(p).kind == TokenKind::Comma) {
            advance(p);
            n.kids.push_back(parse_expr(p, 0));
        }
    }
    if (cur(p).kind == TokenKind::RBracket) advance(p);
    else error(p, "expected ']'");
    return ast_add(p.ast, n);
}

NodeId parse_chord(Parser& p) {
    Token open = cur(p);
    advance(p); // <
    Node n;
    n.kind = NodeKind::Chord;
    n.tok = open;
    while (cur(p).kind != TokenKind::Greater &&
           cur(p).kind != TokenKind::End &&
           cur(p).kind != TokenKind::Newline) {
        n.kids.push_back(parse_primary(p));
    }
    if (cur(p).kind == TokenKind::Greater) advance(p);
    else error(p, "expected '>' to close chord");
    return ast_add(p.ast, n);
}

NodeId parse_lambda(Parser& p) {
    Token bs = cur(p);
    advance(p); // backslash
    Node n;
    n.kind = NodeKind::Lambda;
    n.tok = bs;
    int params = 0;
    while (cur(p).kind == TokenKind::Ident && !is_keyword(cur(p).text)) {
        n.kids.push_back(mk(p, NodeKind::Param, cur(p)));
        advance(p);
        params++;
    }
    if (cur(p).kind == TokenKind::Arrow) advance(p);
    else error(p, "expected '->' in lambda");
    n.kids.push_back(parse_expr(p, 0));
    n.extra = params;
    return ast_add(p.ast, n);
}

NodeId parse_let(Parser& p) {
    Token kw = cur(p);
    advance(p); // let
    Node n;
    n.kind = NodeKind::Let;
    n.tok = kw;
    for (;;) {
        skip_newlines(p);
        if (cur(p).kind == TokenKind::Ident && cur(p).text == "in") break;
        if (cur(p).kind != TokenKind::Ident) break;
        n.kids.push_back(parse_binding(p, false));
        skip_newlines(p);
    }
    int bindings = static_cast<int>(n.kids.size());
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "in") advance(p);
    else error(p, "expected 'in'");
    n.kids.push_back(parse_expr(p, 0)); // body
    n.extra = bindings;
    return ast_add(p.ast, n);
}

NodeId parse_if(Parser& p) {
    Token kw = cur(p);
    advance(p); // if
    Node n;
    n.kind = NodeKind::If;
    n.tok = kw;
    n.kids.push_back(parse_expr(p, 0));
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "then") advance(p);
    else error(p, "expected 'then'");
    n.kids.push_back(parse_expr(p, 0));
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "else") advance(p);
    else error(p, "expected 'else'");
    n.kids.push_back(parse_expr(p, 0));
    return ast_add(p.ast, n);
}

NodeId parse_primary(Parser& p) {
    Token t = cur(p);
    switch (t.kind) {
        case TokenKind::Pitch: advance(p); return mk(p, NodeKind::PitchLit, t);
        case TokenKind::Rest:  advance(p); return mk(p, NodeKind::RestLit, t);
        case TokenKind::Int:   advance(p); return mk(p, NodeKind::IntLit, t);
        case TokenKind::Str:   advance(p); return mk(p, NodeKind::StrLit, t);
        case TokenKind::Upper: advance(p); return mk(p, NodeKind::Con, t);
        case TokenKind::LParen: {
            advance(p);
            NodeId e = parse_expr(p, 0);
            if (cur(p).kind == TokenKind::RParen) advance(p);
            else error(p, "expected ')'");
            return e;
        }
        case TokenKind::LBracket:  return parse_list(p);
        case TokenKind::Less:      return parse_chord(p);
        case TokenKind::Backslash: return parse_lambda(p);
        case TokenKind::Ident:
            if (t.text == "let") return parse_let(p);
            if (t.text == "if")  return parse_if(p);
            advance(p);
            return mk(p, NodeKind::Var, t);
        default:
            error(p, "unexpected token in expression");
            advance(p);
            return mk(p, NodeKind::Error, t);
    }
}

// Juxtaposition layer. A run of adjacent pitch literals composes sequentially;
// a non-pitch head applies to following primaries (function application).
NodeId parse_app(Parser& p) {
    NodeId first = parse_primary(p);
    NodeKind first_kind = p.ast.nodes[first].kind;
    Token first_tok = p.ast.nodes[first].tok;

    bool pitchish = first_kind == NodeKind::PitchLit ||
                    first_kind == NodeKind::RestLit ||
                    first_kind == NodeKind::Chord;

    if (pitchish) {
        std::vector<NodeId> run;
        run.push_back(first);
        while (starts_pitchish(cur(p).kind)) run.push_back(parse_primary(p));
        if (run.size() == 1) return first;
        Node n;
        n.kind = NodeKind::Seq;
        n.tok = first_tok;
        n.kids = std::move(run);
        return ast_add(p.ast, n);
    }

    std::vector<NodeId> args;
    while (can_start_arg(p)) args.push_back(parse_primary(p));
    if (args.empty()) return first;
    Node n;
    n.kind = NodeKind::App;
    n.tok = first_tok;
    n.kids.push_back(first);
    for (NodeId a : args) n.kids.push_back(a);
    return ast_add(p.ast, n);
}

// Precedence climbing over infix operators (symbolic and backtick).
NodeId parse_expr(Parser& p, int min_prec) {
    NodeId lhs = parse_app(p);
    for (;;) {
        Token t = cur(p);
        int prec;
        bool right;
        Token op_tok;

        if (t.kind == TokenKind::Operator) {
            op_fixity(t.text, prec, right);
            if (prec < min_prec) break;
            op_tok = t;
            advance(p);
        } else if (t.kind == TokenKind::Less || t.kind == TokenKind::Greater) {
            // '<'/'>' in infix position are comparisons ('<' in primary position
            // opens a chord, handled by parse_primary).
            prec = 4; right = false;
            if (prec < min_prec) break;
            op_tok = t;
            advance(p);
        } else if (t.kind == TokenKind::Ident && (t.text == "and" || t.text == "or")) {
            // boolean keyword operators (short-circuit handled in the evaluator)
            prec = (t.text == "or") ? 2 : 3;
            right = true;
            if (prec < min_prec) break;
            op_tok = t;
            advance(p);
        } else if (t.kind == TokenKind::Backtick) {
            prec = 4; right = false; // provisional: like the music combinators
            if (prec < min_prec) break;
            advance(p); // opening `
            op_tok = cur(p);
            if (cur(p).kind == TokenKind::Ident || cur(p).kind == TokenKind::Upper)
                advance(p);
            else
                error(p, "expected name in backtick operator");
            if (cur(p).kind == TokenKind::Backtick) advance(p);
            else error(p, "expected closing backtick");
        } else {
            break;
        }

        int next_min = right ? prec : prec + 1;
        NodeId rhs = parse_expr(p, next_min);
        Node n;
        n.kind = NodeKind::BinOp;
        n.tok = op_tok;
        n.kids.push_back(lhs);
        n.kids.push_back(rhs);
        lhs = ast_add(p.ast, n);
    }
    return lhs;
}

// name params... = body  [where block]
NodeId parse_binding(Parser& p, bool allow_where) {
    Token name = cur(p);
    advance(p); // name (caller ensured this is an Ident)
    Node n;
    n.kind = NodeKind::Binding;
    n.tok = name;

    int params = 0;
    while (cur(p).kind == TokenKind::Ident && !is_keyword(cur(p).text)) {
        n.kids.push_back(mk(p, NodeKind::Param, cur(p)));
        advance(p);
        params++;
    }
    n.extra = params;

    if (cur(p).kind == TokenKind::Equals) advance(p);
    else error(p, "expected '=' in binding");

    NodeId body = parse_expr(p, 0);

    if (allow_where) {
        std::size_t save = p.pos;
        skip_newlines(p);
        if (cur(p).kind == TokenKind::Ident && cur(p).text == "where") {
            advance(p);
            std::vector<NodeId> bs = parse_block(p);
            // `where` desugars to a let wrapping the body.
            Node letn;
            letn.kind = NodeKind::Let;
            letn.tok = name;
            for (NodeId b : bs) letn.kids.push_back(b);
            letn.kids.push_back(body);
            letn.extra = static_cast<int>(bs.size());
            body = ast_add(p.ast, letn);
        } else {
            p.pos = save; // no where clause; leave tokens for the caller
        }
    }

    n.kids.push_back(body);
    return ast_add(p.ast, n);
}

// A column-aligned block of bindings (where / let bodies). First binding sets
// the column; siblings must start at the same column.
std::vector<NodeId> parse_block(Parser& p) {
    std::vector<NodeId> bs;
    skip_newlines(p);
    if (cur(p).kind != TokenKind::Ident) return bs;
    int col = cur(p).col;
    for (;;) {
        skip_newlines(p);
        Token t = cur(p);
        if (t.kind != TokenKind::Ident) break;
        if (is_keyword(t.text)) break;
        if (t.col != col) break;
        bs.push_back(parse_binding(p, false));
    }
    return bs;
}

// #name args...   (handled here for now; a real preprocess pass comes later)
NodeId parse_directive(Parser& p) {
    Token hash = cur(p);
    advance(p); // #
    Node n;
    n.kind = NodeKind::Directive;
    if (cur(p).kind == TokenKind::Ident) {
        n.tok = cur(p);
        advance(p);
    } else {
        error(p, "expected directive name after '#'");
        n.tok = hash;
    }
    while (cur(p).kind != TokenKind::Newline && cur(p).kind != TokenKind::End) {
        if (starts_primary(cur(p).kind)) {
            n.kids.push_back(parse_primary(p));
        } else {
            error(p, "unexpected token in directive");
            advance(p);
        }
    }
    return ast_add(p.ast, n);
}

NodeId parse_type_sig(Parser& p, Token name) {
    Node n;
    n.kind = NodeKind::TypeSig;
    n.tok = name;
    advance(p); // ::
    while (cur(p).kind != TokenKind::Newline && cur(p).kind != TokenKind::End) {
        n.kids.push_back(mk(p, NodeKind::TypeAtom, cur(p)));
        advance(p);
    }
    return ast_add(p.ast, n);
}

NodeId parse_toplevel(Parser& p) {
    Token t = cur(p);
    if (t.kind == TokenKind::Hash) return parse_directive(p);
    if (t.kind == TokenKind::Ident) {
        if (peek_at(p, 1).kind == TokenKind::ColonColon) {
            advance(p); // name
            return parse_type_sig(p, t);
        }
        return parse_binding(p, true);
    }
    error(p, "expected a top-level declaration");
    advance(p);
    return mk(p, NodeKind::Error, t);
}

} // namespace

ast::Ast parse_program(std::vector<Token> toks, std::vector<std::string>* errors_out) {
    Parser p;
    p.toks = std::move(toks);

    Node prog;
    prog.kind = NodeKind::Program;
    for (;;) {
        skip_newlines(p);
        if (cur(p).kind == TokenKind::End) break;
        prog.kids.push_back(parse_toplevel(p));
    }
    p.ast.root = ast_add(p.ast, prog);

    if (errors_out) *errors_out = p.errors;
    return std::move(p.ast);
}

} // namespace calliope::parse
