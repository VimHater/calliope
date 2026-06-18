#include "eval.hpp"

#include <cstdlib>
#include <utility>

namespace calliope::eval {

using namespace calliope::ast;

namespace {

// ---- value constructors -------------------------------------------------
Value v_unit()              { return Value{}; }
Value v_int(long long n)    { Value v; v.kind = ValueKind::Int;  v.i = n; return v; }
Value v_bool(bool b)        { Value v; v.kind = ValueKind::Bool; v.i = b ? 1 : 0; return v; }
Value v_pitch(Pitch p)      { Value v; v.kind = ValueKind::Pitch; v.pitch = p; return v; }
Value v_str(std::string s)  { Value v; v.kind = ValueKind::Str;  v.str = std::move(s); return v; }

Value v_con(std::string name, std::vector<Value> args) {
    Value v;
    v.kind = ValueKind::Con;
    v.str = std::move(name);
    v.items = std::move(args);
    return v;
}

// ---- environments -------------------------------------------------------
std::shared_ptr<Env> make_env(std::shared_ptr<Env> parent) {
    auto e = std::make_shared<Env>();
    e->parent = std::move(parent);
    return e;
}

void env_define(const std::shared_ptr<Env>& e, std::string name, Value v) {
    e->names.push_back(std::move(name));
    e->vals.push_back(std::move(v));
}

// ---- builtins -----------------------------------------------------------
enum BuiltinId {
    B_ADD, B_SUB, B_MUL, B_DIV,
    B_EQ, B_LT, B_NOT,
    B_TRANSPOSE_UP, B_TRANSPOSE_DOWN,
    B_SEMITONES,
};

struct BuiltinInfo { const char* name; int id; int arity; };

const BuiltinInfo kBuiltins[] = {
    {"+", B_ADD, 2}, {"-", B_SUB, 2}, {"*", B_MUL, 2}, {"/", B_DIV, 2},
    {"==", B_EQ, 2}, {"<", B_LT, 2}, {"not", B_NOT, 1},
    {"^+", B_TRANSPOSE_UP, 2}, {"^-", B_TRANSPOSE_DOWN, 2},
    {"semitones", B_SEMITONES, 1},
};

// Interval name -> (diatonic steps, semitones). Enough common ones to be useful.
bool interval_steps(std::string_view name, int& dstep, int& dsemi) {
    struct E { const char* n; int d; int s; };
    static const E table[] = {
        {"P1", 0, 0}, {"m2", 1, 1}, {"M2", 1, 2}, {"m3", 2, 3}, {"M3", 2, 4},
        {"P4", 3, 5}, {"A4", 3, 6}, {"d5", 4, 6}, {"P5", 4, 7}, {"m6", 5, 8},
        {"M6", 5, 9}, {"m7", 6, 10}, {"M7", 6, 11}, {"P8", 7, 12},
    };
    for (const E& e : table) {
        if (name == e.n) { dstep = e.d; dsemi = e.s; return true; }
    }
    return false;
}

Pitch transpose_pitch(Pitch p, int dstep, int dsemi) {
    int ns = diatonic_step(p) + dstep;
    int acc = (semitones(p) + dsemi) - chromatic_of(ns);
    return mk_pitch(ns, acc);
}

Value call_builtin(Interp& I, int id, std::vector<Value>& a) {
    switch (id) {
        case B_ADD: return v_int(a[0].i + a[1].i);
        case B_SUB: return v_int(a[0].i - a[1].i);
        case B_MUL: return v_int(a[0].i * a[1].i);
        case B_DIV:
            if (a[1].i == 0) { I.errors.push_back("division by zero"); return v_unit(); }
            return v_int(a[0].i / a[1].i);
        case B_EQ:
            if (a[0].kind == ValueKind::Pitch && a[1].kind == ValueKind::Pitch)
                return v_bool(pitch_eq(a[0].pitch, a[1].pitch));
            return v_bool(a[0].i == a[1].i);
        case B_LT: return v_bool(a[0].i < a[1].i);
        case B_NOT: return v_bool(a[0].i == 0);
        case B_TRANSPOSE_UP:
        case B_TRANSPOSE_DOWN: {
            int dstep, dsemi;
            if (a[0].kind != ValueKind::Pitch || a[1].kind != ValueKind::Con ||
                !interval_steps(a[1].str, dstep, dsemi)) {
                I.errors.push_back("transpose expects (Pitch, Interval)");
                return v_unit();
            }
            if (id == B_TRANSPOSE_DOWN) { dstep = -dstep; dsemi = -dsemi; }
            return v_pitch(transpose_pitch(a[0].pitch, dstep, dsemi));
        }
        case B_SEMITONES:
            if (a[0].kind != ValueKind::Pitch) {
                I.errors.push_back("semitones expects a Pitch");
                return v_unit();
            }
            return v_int(semitones(a[0].pitch));
    }
    I.errors.push_back("unknown builtin");
    return v_unit();
}

// ---- notation decode ----------------------------------------------------
// "c", "fis", "g'", "ees,", "c'4" -> Pitch (duration ignored for now).
// letters: c=0 d=1 e=2 f=3 g=4 a=5 b=6 ; bare letter is octave 3 (so c' = C4).
Value decode_pitch(std::string_view t) {
    static const int letter_of[7] = {5, 6, 0, 1, 2, 3, 4}; // a b c d e f g -> index
    if (t.empty()) return v_unit();
    int li = letter_of[t[0] - 'a'];
    std::size_t i = 1;
    int accidental = 0;
    while (i + 1 < t.size() && (t[i] == 'i' || t[i] == 'e') && t[i + 1] == 's') {
        accidental += (t[i] == 'i') ? 1 : -1;
        i += 2;
    }
    int octave = 3;
    while (i < t.size() && t[i] == '\'') { octave++; i++; }
    while (i < t.size() && t[i] == ',')  { octave--; i++; }
    return v_pitch(pitch(li, accidental, octave));
}

long long parse_int(std::string_view t) {
    std::string s(t);
    return std::strtoll(s.c_str(), nullptr, 10);
}

// ---- evaluation ---------------------------------------------------------
Value eval(Interp& I, NodeId id, const std::shared_ptr<Env>& env);

Value apply(Interp& I, Value f, Value arg) {
    if (f.kind == ValueKind::Closure) {
        auto c = f.clo;
        auto child = make_env(c->env);
        env_define(child, c->params[0], std::move(arg));
        if (c->params.size() == 1) return eval(I, c->body, child);
        Value nf;
        nf.kind = ValueKind::Closure;
        nf.clo = std::make_shared<Closure>();
        nf.clo->params.assign(c->params.begin() + 1, c->params.end());
        nf.clo->body = c->body;
        nf.clo->env = child;
        return nf;
    }
    if (f.kind == ValueKind::Builtin) {
        Value nf = f;
        nf.items.push_back(std::move(arg));
        if (static_cast<int>(nf.items.size()) == nf.arity)
            return call_builtin(I, static_cast<int>(nf.i), nf.items);
        return nf;
    }
    I.errors.push_back("cannot apply a non-function value");
    return v_unit();
}

bool lookup_operator(Interp& I, std::string_view op, Value& out) {
    return env_lookup(I.globals, op, out);
}

Value eval(Interp& I, NodeId id, const std::shared_ptr<Env>& env) {
    const Node& n = I.ast->nodes[id];
    switch (n.kind) {
        case NodeKind::IntLit:  return v_int(parse_int(n.tok.text));
        case NodeKind::StrLit: {
            std::string_view t = n.tok.text;
            if (t.size() >= 2) t = t.substr(1, t.size() - 2); // strip quotes
            return v_str(std::string(t));
        }
        case NodeKind::PitchLit: return decode_pitch(n.tok.text);
        case NodeKind::RestLit:  return v_con("Rest", {});
        case NodeKind::Con:
            if (n.tok.text == "True")  return v_bool(true);
            if (n.tok.text == "False") return v_bool(false);
            return v_con(std::string(n.tok.text), {});
        case NodeKind::Var: {
            Value out;
            if (env_lookup(env, n.tok.text, out)) return out;
            I.errors.push_back("unbound name: " + std::string(n.tok.text));
            return v_unit();
        }
        case NodeKind::App: {
            Value f = eval(I, n.kids[0], env);
            for (std::size_t k = 1; k < n.kids.size(); k++)
                f = apply(I, std::move(f), eval(I, n.kids[k], env));
            return f;
        }
        case NodeKind::Seq: {
            std::vector<Value> items;
            for (NodeId k : n.kids) items.push_back(eval(I, k, env));
            return v_con("Seq", std::move(items));
        }
        case NodeKind::Chord: {
            std::vector<Value> items;
            for (NodeId k : n.kids) items.push_back(eval(I, k, env));
            return v_con("Chord", std::move(items));
        }
        case NodeKind::ListLit: {
            Value v;
            v.kind = ValueKind::List;
            for (NodeId k : n.kids) v.items.push_back(eval(I, k, env));
            return v;
        }
        case NodeKind::BinOp: {
            std::string_view op = n.tok.text;
            // short-circuiting boolean keyword operators
            if (op == "and") {
                if (eval(I, n.kids[0], env).i == 0) return v_bool(false);
                return v_bool(eval(I, n.kids[1], env).i != 0);
            }
            if (op == "or") {
                if (eval(I, n.kids[0], env).i != 0) return v_bool(true);
                return v_bool(eval(I, n.kids[1], env).i != 0);
            }
            Value l = eval(I, n.kids[0], env);
            Value r = eval(I, n.kids[1], env);
            if (op == ":+:") return v_con("Seq", {l, r});
            if (op == ":=:") return v_con("Par", {l, r});
            Value f;
            if (!lookup_operator(I, op, f)) {
                I.errors.push_back("unknown operator: " + std::string(op));
                return v_unit();
            }
            return apply(I, apply(I, std::move(f), std::move(l)), std::move(r));
        }
        case NodeKind::If: {
            Value c = eval(I, n.kids[0], env);
            bool t = (c.i != 0); // Bool stored in i; nonzero Int also truthy
            return eval(I, n.kids[t ? 1 : 2], env);
        }
        case NodeKind::Lambda: {
            Value v;
            v.kind = ValueKind::Closure;
            v.clo = std::make_shared<Closure>();
            for (int k = 0; k < n.extra; k++)
                v.clo->params.push_back(std::string(I.ast->nodes[n.kids[k]].tok.text));
            v.clo->body = n.kids[n.extra];
            v.clo->env = env;
            return v;
        }
        case NodeKind::Let: {
            auto child = make_env(env);
            int nb = n.extra;
            // define function bindings first (so they can be mutually recursive)
            for (int k = 0; k < nb; k++) {
                const Node& b = I.ast->nodes[n.kids[k]];
                if (b.extra > 0) {
                    Value v;
                    v.kind = ValueKind::Closure;
                    v.clo = std::make_shared<Closure>();
                    for (int p = 0; p < b.extra; p++)
                        v.clo->params.push_back(std::string(I.ast->nodes[b.kids[p]].tok.text));
                    v.clo->body = b.kids[b.extra];
                    v.clo->env = child;
                    env_define(child, std::string(b.tok.text), std::move(v));
                }
            }
            for (int k = 0; k < nb; k++) {
                const Node& b = I.ast->nodes[n.kids[k]];
                if (b.extra == 0)
                    env_define(child, std::string(b.tok.text), eval(I, b.kids[0], child));
            }
            return eval(I, n.kids[nb], child); // body
        }
        case NodeKind::Binding: // evaluated by eval_program / Let, not standalone
        case NodeKind::Param:
        case NodeKind::TypeSig:
        case NodeKind::TypeAtom:
        case NodeKind::Directive:
        case NodeKind::Program:
        case NodeKind::Error:
            return v_unit();
    }
    return v_unit();
}

} // namespace

bool env_lookup(const std::shared_ptr<Env>& env, std::string_view name, Value& out) {
    for (const Env* e = env.get(); e != nullptr; e = e->parent.get()) {
        for (std::size_t k = e->names.size(); k-- > 0;) {
            if (e->names[k] == name) { out = e->vals[k]; return true; }
        }
    }
    return false;
}

std::shared_ptr<Env> eval_program(const ast::Ast& a, Interp& interp) {
    interp.ast = &a;
    auto globals = make_env(nullptr);
    interp.globals = globals;

    for (const BuiltinInfo& b : kBuiltins) {
        Value v;
        v.kind = ValueKind::Builtin;
        v.i = b.id;
        v.arity = b.arity;
        env_define(globals, b.name, v);
    }

    if (a.root == ast::NoNode) return globals;
    const Node& prog = a.nodes[a.root];

    // pass 1: function bindings (so recursion / forward refs resolve)
    for (NodeId d : prog.kids) {
        const Node& b = a.nodes[d];
        if (b.kind == NodeKind::Binding && b.extra > 0) {
            Value v;
            v.kind = ValueKind::Closure;
            v.clo = std::make_shared<Closure>();
            for (int p = 0; p < b.extra; p++)
                v.clo->params.push_back(std::string(a.nodes[b.kids[p]].tok.text));
            v.clo->body = b.kids[b.extra];
            v.clo->env = globals;
            env_define(globals, std::string(b.tok.text), std::move(v));
        }
    }
    // pass 2: value bindings
    for (NodeId d : prog.kids) {
        const Node& b = a.nodes[d];
        if (b.kind == NodeKind::Binding && b.extra == 0)
            env_define(globals, std::string(b.tok.text), eval(interp, b.kids[0], globals));
    }

    return globals;
}

std::string show_value(const Value& v) {
    switch (v.kind) {
        case ValueKind::Unit:  return "()";
        case ValueKind::Int:   return std::to_string(v.i);
        case ValueKind::Bool:  return v.i ? "True" : "False";
        case ValueKind::Rat:   return std::to_string(v.rat.num) + "/" + std::to_string(v.rat.den);
        case ValueKind::Str:   return "\"" + v.str + "\"";
        case ValueKind::Pitch: {
            static const char* letters = "CDEFGAB";
            std::string s(1, letters[v.pitch.letter]);
            for (int k = 0; k < v.pitch.accidental; k++) s += "#";
            for (int k = 0; k < -v.pitch.accidental; k++) s += "b";
            return s + std::to_string(v.pitch.octave);
        }
        case ValueKind::List: case ValueKind::Con: {
            std::string s = (v.kind == ValueKind::Con) ? ("(" + v.str) : "[";
            for (const Value& it : v.items) s += " " + show_value(it);
            return s + (v.kind == ValueKind::Con ? ")" : " ]");
        }
        case ValueKind::Closure: return "<closure>";
        case ValueKind::Builtin: return "<builtin>";
    }
    return "?";
}

} // namespace calliope::eval
