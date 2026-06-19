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

// The next token, looking past any newlines (does not advance).
const Token& peek_nonnl(Parser& p) {
    std::size_t i = p.pos;
    while (i < p.toks.size() && p.toks[i].kind == TokenKind::Newline) i++;
    if (i >= p.toks.size()) i = p.toks.size() - 1;
    return p.toks[i];
}

// At a newline whose following line is indented past the current binding margin:
// the expression continues onto it (offside rule). A line at or left of the
// margin ends the binding.
bool at_continuation(Parser& p) {
    if (cur(p).kind != TokenKind::Newline) return false;
    const Token& nx = peek_nonnl(p);
    if (nx.kind == TokenKind::End) return false;
    return nx.col > p.margin;
}

void error(Parser& p, const char* msg) {
    const Token& t = cur(p);
    char buf[256];
    // Render the location nicely: an End/Newline token has no text, so the old
    // "(near '')" was useless — name the place instead.
    if (t.kind == TokenKind::End)
        std::snprintf(buf, sizeof buf, "%d:%d: %s (at end of input)", t.line, t.col, msg);
    else if (t.kind == TokenKind::Newline)
        std::snprintf(buf, sizeof buf, "%d:%d: %s (at end of line)", t.line, t.col, msg);
    else
        std::snprintf(buf, sizeof buf, "%d:%d: %s (near '%.*s')", t.line, t.col, msg,
                      static_cast<int>(t.text.size()), t.text.data());
    p.errors.emplace_back(buf);
}

// A binary operator was given no right operand (e.g. a dangling `c'4 ~`).
void error_after_op(Parser& p, const Token& op) {
    char m[128];
    std::snprintf(m, sizeof m, "expected an expression after operator '%.*s'",
                  static_cast<int>(op.text.size()), op.text.data());
    error(p, m);
}

bool is_keyword(std::string_view s) {
    return s == "let" || s == "in" || s == "where" || s == "if" ||
           s == "then" || s == "else" || s == "data" || s == "type" || s == "of" ||
           s == "class" || s == "instance" || s == "case" ||
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
    if (op == "|>")  { prec = 1; right = false; return true; } // pipe: x |> f = f x
    if (op == "==" || op == "<" || op == ">" || op == "/=" ||
        op == "<=" || op == ">=") { prec = 4; right = false; return true; } // compare
    if (op == ":=:") { prec = 3; right = true;  return true; }
    if (op == ":")   { prec = 5; right = true;  return true; } // cons
    if (op == ":+:") { prec = 5; right = true;  return true; }
    if (op == ":*:") { prec = 6; right = false; return true; } // repeat n times (phrase :*: n)
    if (op == "~")   { prec = 7; right = false; return true; } // tie same-pitch notes
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
NodeId parse_pattern(Parser& p);
NodeId parse_case(Parser& p);
NodeId parse_type_sig(Parser& p, Token name);
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

// A `<` opens a chord only when it hugs its first note (no space): `<c e g>`.
// A spaced `<` (`a < b`) is the comparison operator, not a chord. This is how we
// disambiguate the two uses of `<` without scope/type lookup.
bool opens_chord(Parser& p) {
    const Token& lt = cur(p);
    if (lt.kind != TokenKind::Less) return false;
    const Token& nx = peek_at(p, 1);
    bool adjacent = (nx.line == lt.line && nx.col == lt.col + 1);
    return adjacent && (nx.kind == TokenKind::Pitch || nx.kind == TokenKind::Rest ||
                        nx.kind == TokenKind::Less);
}

// Does the current token continue a run of adjacent notes? A `<` only does so
// when it actually opens a chord (so `a < b` ends the run and `<` becomes infix).
bool continues_pitch_run(Parser& p) {
    TokenKind k = cur(p).kind;
    if (k == TokenKind::Pitch || k == TokenKind::Rest) return true;
    if (k == TokenKind::Less) return opens_chord(p);
    return false;
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
    int saved = p.margin;
    p.margin = -1; // newlines insignificant inside the brackets
    skip_newlines(p);
    if (cur(p).kind != TokenKind::RBracket) {
        n.kids.push_back(parse_expr(p, 0));
        skip_newlines(p);
        while (cur(p).kind == TokenKind::Comma) {
            advance(p);
            skip_newlines(p);
            n.kids.push_back(parse_expr(p, 0));
            skip_newlines(p);
        }
    }
    p.margin = saved;
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
        TokenKind k = cur(p).kind;
        // a chord holds pitches (and rests / nested chords) — not operators like `~`.
        if (k != TokenKind::Pitch && k != TokenKind::Rest && k != TokenKind::Less) {
            error(p, "unexpected token inside chord (expected a pitch or '>')");
            advance(p); // skip the offender and keep scanning for the closing '>'
            continue;
        }
        n.kids.push_back(parse_primary(p));
    }
    Token close = cur(p);
    if (cur(p).kind == TokenKind::Greater) advance(p);
    else error(p, "expected '>' to close chord");

    // optional chord-level duration immediately after '>' (no space): <c e g>4.
    // applies to every note in the chord. Encoded in `extra` as base*4 + dots
    // (0 = none): decoded back to a Rational by the evaluator.
    if (cur(p).kind == TokenKind::Int &&
        cur(p).line == close.line && cur(p).col == close.col + 1) {
        const Token& numtok = cur(p);
        long long base = 0;
        for (char ch : numtok.text) base = base * 10 + (ch - '0');
        int nextcol = numtok.col + static_cast<int>(numtok.text.size());
        int line = numtok.line;
        advance(p);
        int dots = 0;
        // following dot-operator tokens ("." / ".."), still adjacent
        while (cur(p).kind == TokenKind::Operator &&
               cur(p).text.find_first_not_of('.') == std::string_view::npos &&
               cur(p).line == line && cur(p).col == nextcol) {
            dots += static_cast<int>(cur(p).text.size());
            nextcol += static_cast<int>(cur(p).text.size());
            advance(p);
        }
        if (base >= 1) n.extra = static_cast<int>(base * 4 + (dots > 3 ? 3 : dots));
    }
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
    skip_newlines(p);
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
    skip_newlines(p);
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
    skip_newlines(p);
    n.kids.push_back(parse_expr(p, 0));
    skip_newlines(p); // 'then' may sit on its own line
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "then") advance(p);
    else error(p, "expected 'then'");
    skip_newlines(p);
    n.kids.push_back(parse_expr(p, 0));
    skip_newlines(p); // 'else' may sit on its own line
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "else") advance(p);
    else error(p, "expected 'else'");
    skip_newlines(p);
    n.kids.push_back(parse_expr(p, 0));
    return ast_add(p.ast, n);
}

// ---- patterns -----------------------------------------------------------
NodeId parse_pattern_atom(Parser& p) {
    Token t = cur(p);
    switch (t.kind) {
        case TokenKind::Int: advance(p); return mk(p, NodeKind::PatInt, t);
        case TokenKind::Ident:
            advance(p);
            if (t.text == "_") return mk(p, NodeKind::PatWild, t);
            return mk(p, NodeKind::PatVar, t);
        case TokenKind::Upper:
            advance(p);
            return mk(p, NodeKind::PatCon, t); // nullary constructor (True/False/…)
        case TokenKind::LBracket: {
            advance(p);
            if (cur(p).kind == TokenKind::RBracket) advance(p);
            else error(p, "expected ']' in pattern (only [] is supported)");
            return mk(p, NodeKind::PatCon, t); // tok '[' stands in for the empty list
        }
        case TokenKind::LParen: {
            advance(p);
            NodeId inner = parse_pattern(p);
            if (cur(p).kind == TokenKind::RParen) advance(p);
            else error(p, "expected ')' in pattern");
            return inner;
        }
        default:
            error(p, "expected a pattern");
            advance(p);
            return mk(p, NodeKind::Error, t);
    }
}

// patterns: cons ':' is right-associative, like the expression operator
NodeId parse_pattern(Parser& p) {
    NodeId left = parse_pattern_atom(p);
    if (cur(p).kind == TokenKind::Operator && cur(p).text == ":") {
        Token op = cur(p);
        advance(p);
        NodeId right = parse_pattern(p);
        Node n;
        n.kind = NodeKind::PatCon;
        n.tok = op;
        n.kids.push_back(left);
        n.kids.push_back(right);
        return ast_add(p.ast, n);
    }
    return left;
}

bool starts_pattern(const Token& t) {
    return t.kind == TokenKind::Int || t.kind == TokenKind::Upper ||
           t.kind == TokenKind::LBracket || t.kind == TokenKind::LParen ||
           (t.kind == TokenKind::Ident && t.text != "of");
}

// case <expr> of  <pat> -> <expr>  (one alternative per line, column-aligned)
NodeId parse_case(Parser& p) {
    Token kw = cur(p);
    advance(p); // case
    Node n;
    n.kind = NodeKind::Case;
    n.tok = kw;
    n.kids.push_back(parse_expr(p, 0)); // scrutinee
    skip_newlines(p);
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "of") advance(p);
    else error(p, "expected 'of' in case");

    skip_newlines(p);
    if (starts_pattern(cur(p))) {
        int col = cur(p).col;
        for (;;) {
            // Decide whether another alternative follows by looking past the
            // newline WITHOUT consuming it — otherwise a dedented token after the
            // case (e.g. the next top-level binding) loses its newline barrier and
            // gets swallowed by the enclosing application.
            const Token& nx = peek_nonnl(p);
            if (!starts_pattern(nx) || nx.col != col) break;
            skip_newlines(p);
            Node alt;
            alt.kind = NodeKind::Alt;
            alt.tok = cur(p);
            alt.kids.push_back(parse_pattern(p));
            if (cur(p).kind == TokenKind::Arrow) advance(p);
            else error(p, "expected '->' in case alternative");
            int saved = p.margin;
            p.margin = col;             // alt body may span lines, indented past the pattern
            skip_newlines(p);
            alt.kids.push_back(parse_expr(p, 0));
            p.margin = saved;
            n.kids.push_back(ast_add(p.ast, alt));
        }
    }
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
            // inside brackets, newlines are insignificant until the closer.
            int saved = p.margin;
            p.margin = -1;
            skip_newlines(p);
            NodeId e = parse_expr(p, 0);
            skip_newlines(p);
            p.margin = saved;
            if (cur(p).kind == TokenKind::RParen) advance(p);
            else error(p, "expected ')'");
            return e;
        }
        case TokenKind::LBracket:  return parse_list(p);
        case TokenKind::Less:      return parse_chord(p);
        case TokenKind::Backslash: return parse_lambda(p);
        case TokenKind::Ident:
            if (t.text == "let")  return parse_let(p);
            if (t.text == "if")   return parse_if(p);
            if (t.text == "case") return parse_case(p);
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
        while (continues_pitch_run(p)) run.push_back(parse_primary(p));
        if (run.size() == 1) return first;
        Node n;
        n.kind = NodeKind::Seq;
        n.tok = first_tok;
        n.kids = std::move(run);
        return ast_add(p.ast, n);
    }

    std::vector<NodeId> args;
    for (;;) {
        if (cur(p).kind == TokenKind::Newline) {
            if (at_continuation(p)) skip_newlines(p);
            else break;
        }
        if (!can_start_arg(p)) break;
        args.push_back(parse_primary(p));
    }
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
        // a continuation line may carry an infix operator for this expression
        if (cur(p).kind == TokenKind::Newline) {
            if (at_continuation(p)) skip_newlines(p);
            else break;
        }
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
        skip_newlines(p); // operator's right operand may start on the next line
        if (cur(p).kind == TokenKind::End || cur(p).kind == TokenKind::Newline) {
            error_after_op(p, op_tok); // dangling operator, no right operand
            return lhs;
        }
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
    if (is_keyword(name.text))
        error(p, "expected a name to bind, found a keyword");
    advance(p); // name (caller ensured this is an Ident)
    Node n;
    n.kind = NodeKind::Binding;
    n.tok = name;

    // continuation lines of this binding's body must be indented past the name.
    int saved_margin = p.margin;
    p.margin = name.col;

    int params = 0;
    while (cur(p).kind == TokenKind::Ident && !is_keyword(cur(p).text)) {
        n.kids.push_back(mk(p, NodeKind::Param, cur(p)));
        advance(p);
        params++;
    }
    n.extra = params;

    if (cur(p).kind == TokenKind::Equals) advance(p);
    else error(p, "expected '=' in binding");

    skip_newlines(p); // body may begin on the next (indented) line
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
    p.margin = saved_margin;
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
        // a local type signature (`go :: ...`) — parsed and discarded for now
        if (peek_at(p, 1).kind == TokenKind::ColonColon) {
            advance(p); // name
            parse_type_sig(p, t);
            continue;
        }
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

// A method binding inside an `instance` block. Either an ordinary prefix form
// (`name params = body`) or an infix operator definition (`lhs ^+ rhs = body`).
NodeId parse_method_binding(Parser& p) {
    bool infix = cur(p).kind == TokenKind::Ident &&
                 (peek_at(p, 1).kind == TokenKind::Operator ||
                  peek_at(p, 1).kind == TokenKind::Less ||
                  peek_at(p, 1).kind == TokenKind::Greater);
    if (!infix) return parse_binding(p, false);

    Token lhs = cur(p);
    advance(p);
    Token op = cur(p);
    advance(p); // operator
    Node n;
    n.kind = NodeKind::Binding;
    n.tok = op;
    n.kids.push_back(mk(p, NodeKind::Param, lhs));
    if (cur(p).kind == TokenKind::Ident && !is_keyword(cur(p).text)) {
        n.kids.push_back(mk(p, NodeKind::Param, cur(p)));
        advance(p);
    } else {
        error(p, "expected right operand parameter in operator definition");
    }
    n.extra = 2;
    if (cur(p).kind == TokenKind::Equals) advance(p);
    else error(p, "expected '=' in method definition");
    n.kids.push_back(parse_expr(p, 0));
    return ast_add(p.ast, n);
}

// One method signature line in a `class` body: `(^+) :: a -> Interval -> a`
// or `describe :: a -> Int`. The signature tokens are kept raw (TypeAtom) and
// parsed by the typechecker's mini type parser.
NodeId parse_method_sig(Parser& p) {
    Node n;
    n.kind = NodeKind::MethodSig;
    if (cur(p).kind == TokenKind::LParen) {
        advance(p);
        n.tok = cur(p); // operator name inside parens
        advance(p);
        if (cur(p).kind == TokenKind::RParen) advance(p);
        else error(p, "expected ')' after operator name");
    } else {
        n.tok = cur(p);
        advance(p);
    }
    if (cur(p).kind == TokenKind::ColonColon) advance(p);
    else error(p, "expected '::' in method signature");
    while (cur(p).kind != TokenKind::Newline && cur(p).kind != TokenKind::End) {
        n.kids.push_back(mk(p, NodeKind::TypeAtom, cur(p)));
        advance(p);
    }
    return ast_add(p.ast, n);
}

bool sig_starts(const Token& t) {
    return t.kind == TokenKind::LParen ||
           (t.kind == TokenKind::Ident && !is_keyword(t.text));
}

// class Name var where <method signatures>
NodeId parse_class(Parser& p) {
    advance(p); // 'class'
    Node n;
    n.kind = NodeKind::ClassDecl;
    if (cur(p).kind == TokenKind::Upper) { n.tok = cur(p); advance(p); }
    else error(p, "expected class name");
    // single type variable
    if (cur(p).kind == TokenKind::Ident && !is_keyword(cur(p).text))
        n.kids.push_back(mk(p, NodeKind::Param, cur(p))), advance(p);
    else error(p, "expected class type variable");
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "where") advance(p);
    else error(p, "expected 'where' in class declaration");

    skip_newlines(p);
    if (sig_starts(cur(p))) {
        int col = cur(p).col;
        for (;;) {
            skip_newlines(p);
            if (!sig_starts(cur(p)) || cur(p).col != col) break;
            n.kids.push_back(parse_method_sig(p));
        }
    }
    return ast_add(p.ast, n);
}

// instance Class Type where <method bindings>
NodeId parse_instance(Parser& p) {
    advance(p); // 'instance'
    Node n;
    n.kind = NodeKind::InstanceDecl;
    if (cur(p).kind == TokenKind::Upper) { n.tok = cur(p); advance(p); }
    else error(p, "expected class name in instance");
    Node ty;
    ty.kind = NodeKind::Con;
    if (cur(p).kind == TokenKind::Upper) { ty.tok = cur(p); advance(p); }
    else error(p, "expected instance type");
    n.kids.push_back(ast_add(p.ast, ty));
    if (cur(p).kind == TokenKind::Ident && cur(p).text == "where") advance(p);
    else error(p, "expected 'where' in instance declaration");

    skip_newlines(p);
    if (cur(p).kind == TokenKind::Ident && !is_keyword(cur(p).text)) {
        int col = cur(p).col;
        for (;;) {
            skip_newlines(p);
            if (cur(p).kind != TokenKind::Ident || is_keyword(cur(p).text)) break;
            if (cur(p).col != col) break;
            n.kids.push_back(parse_method_binding(p));
        }
    }
    return ast_add(p.ast, n);
}

NodeId parse_toplevel(Parser& p) {
    Token t = cur(p);
    if (t.kind == TokenKind::Hash) return parse_directive(p);
    if (t.kind == TokenKind::Ident && t.text == "class")    return parse_class(p);
    if (t.kind == TokenKind::Ident && t.text == "instance") return parse_instance(p);
    if (t.kind == TokenKind::Ident) {
        if (peek_at(p, 1).kind == TokenKind::ColonColon) {
            advance(p); // name
            return parse_type_sig(p, t);
        }
        return parse_binding(p, true);
    }
    error(p, "expected a top-level declaration");
    // recover: drop the rest of this line so a single stray token doesn't spawn
    // one "expected a top-level declaration" per leftover token (a cascade).
    while (cur(p).kind != TokenKind::Newline && cur(p).kind != TokenKind::End) advance(p);
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
