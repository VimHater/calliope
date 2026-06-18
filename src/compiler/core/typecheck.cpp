#include "typecheck.hpp"

#include <utility>

namespace calliope::types {

using namespace calliope::ast;

namespace {

// ---- type construction --------------------------------------------------
TypeId new_var(Ctx& c) {
    int v = static_cast<int>(c.binding.size());
    c.binding.push_back(NoType);
    TypeNode n;
    n.is_var = true;
    n.var = v;
    c.pool.push_back(std::move(n));
    return static_cast<TypeId>(c.pool.size() - 1);
}

TypeId new_con(Ctx& c, std::string name, std::vector<TypeId> args) {
    TypeNode n;
    n.con = std::move(name);
    n.args = std::move(args);
    c.pool.push_back(std::move(n));
    return static_cast<TypeId>(c.pool.size() - 1);
}

TypeId t_con0(Ctx& c, const char* name) { return new_con(c, name, {}); }
TypeId t_arrow(Ctx& c, TypeId a, TypeId b) { return new_con(c, "->", {a, b}); }
TypeId t_list(Ctx& c, TypeId a) { return new_con(c, "List", {a}); }

TypeId resolve(Ctx& c, TypeId t) {
    for (;;) {
        const TypeNode& n = c.pool[t];
        if (n.is_var && c.binding[n.var] != NoType) { t = c.binding[n.var]; continue; }
        return t;
    }
}

bool occurs(Ctx& c, int var, TypeId t) {
    t = resolve(c, t);
    const TypeNode& n = c.pool[t];
    if (n.is_var) return n.var == var;
    for (TypeId a : n.args)
        if (occurs(c, var, a)) return true;
    return false;
}

// ---- environment --------------------------------------------------------
struct EnvEntry {
    std::string name;
    Scheme scheme;
};
using Env = std::vector<EnvEntry>;

// ---- checker state ------------------------------------------------------
struct Checker {
    const ast::Ast* ast = nullptr;
    Ctx ctx;
    std::vector<std::string>* errors = nullptr;
};

void fail(Checker& ck, const std::string& msg) {
    if (ck.errors) ck.errors->push_back(msg);
}

bool unify(Checker& ck, TypeId a, TypeId b) {
    Ctx& c = ck.ctx;
    a = resolve(c, a);
    b = resolve(c, b);
    if (a == b) return true;
    TypeNode na = c.pool[a];
    TypeNode nb = c.pool[b];
    if (na.is_var) {
        if (occurs(c, na.var, b)) { fail(ck, "infinite type"); return false; }
        c.binding[na.var] = b;
        return true;
    }
    if (nb.is_var) {
        if (occurs(c, nb.var, a)) { fail(ck, "infinite type"); return false; }
        c.binding[nb.var] = a;
        return true;
    }
    if (na.con != nb.con || na.args.size() != nb.args.size()) {
        fail(ck, "type mismatch: " + na.con + " vs " + nb.con);
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < na.args.size(); i++)
        ok = unify(ck, na.args[i], nb.args[i]) && ok;
    return ok;
}

// ---- free variables, generalization, instantiation ----------------------
void free_vars(Ctx& c, TypeId t, std::vector<int>& out) {
    t = resolve(c, t);
    const TypeNode& n = c.pool[t];
    if (n.is_var) {
        for (int v : out) if (v == n.var) return;
        out.push_back(n.var);
        return;
    }
    for (TypeId a : n.args) free_vars(c, a, out);
}

void env_free_vars(Ctx& c, const Env& env, std::vector<int>& out) {
    for (const EnvEntry& e : env) {
        std::vector<int> fv;
        free_vars(c, e.scheme.type, fv);
        for (int v : fv) {
            bool quantified = false;
            for (int q : e.scheme.vars) if (q == v) { quantified = true; break; }
            if (quantified) continue;
            bool seen = false;
            for (int o : out) if (o == v) { seen = true; break; }
            if (!seen) out.push_back(v);
        }
    }
}

Scheme generalize(Ctx& c, const Env& env, TypeId t) {
    std::vector<int> ftv;
    free_vars(c, t, ftv);
    std::vector<int> efv;
    env_free_vars(c, env, efv);
    Scheme s;
    s.type = t;
    for (int v : ftv) {
        bool in_env = false;
        for (int e : efv) if (e == v) { in_env = true; break; }
        if (!in_env) s.vars.push_back(v);
    }
    return s;
}

TypeId inst_copy(Ctx& c, TypeId t, std::vector<std::pair<int, TypeId>>& m) {
    t = resolve(c, t);
    const TypeNode& n = c.pool[t];
    if (n.is_var) {
        for (auto& kv : m) if (kv.first == n.var) return kv.second;
        return t; // free var: shared
    }
    std::vector<TypeId> args;
    std::vector<TypeId> src = n.args; // copy: pool may reallocate during recursion
    for (TypeId a : src) args.push_back(inst_copy(c, a, m));
    std::string name = c.pool[t].con;
    return new_con(c, name, args);
}

TypeId instantiate(Ctx& c, const Scheme& s) {
    if (s.vars.empty()) return s.type;
    std::vector<std::pair<int, TypeId>> m;
    for (int v : s.vars) m.emplace_back(v, new_var(c));
    return inst_copy(c, s.type, m);
}

bool env_find(const Env& env, std::string_view name, Scheme& out) {
    for (std::size_t i = env.size(); i-- > 0;) {
        if (env[i].name == name) { out = env[i].scheme; return true; }
    }
    return false;
}

// ---- inference ----------------------------------------------------------
bool is_interval_name(std::string_view s) {
    static const char* names[] = {
        "P1", "m2", "M2", "m3", "M3", "P4", "A4", "d5", "P5", "m6",
        "M6", "m7", "M7", "P8",
    };
    for (const char* n : names) if (s == n) return true;
    return false;
}

TypeId infer(Checker& ck, Env& env, NodeId id);

// operator type for a BinOp token; returns NoType if unknown
TypeId operator_type(Checker& ck, Env& env, std::string_view op) {
    Ctx& c = ck.ctx;
    if (op == ":+:" || op == ":=:") {
        TypeId m = t_con0(c, "Music");
        return t_arrow(c, m, t_arrow(c, m, m));
    }
    Scheme s;
    if (env_find(env, op, s)) return instantiate(c, s);
    return NoType;
}

TypeId infer(Checker& ck, Env& env, NodeId id) {
    Ctx& c = ck.ctx;
    const Node& n = ck.ast->nodes[id];
    switch (n.kind) {
        case NodeKind::IntLit:  return t_con0(c, "Int");
        case NodeKind::StrLit:  return t_con0(c, "Str");
        case NodeKind::PitchLit:return t_con0(c, "Pitch");
        case NodeKind::RestLit: return t_con0(c, "Music");
        case NodeKind::Seq:
        case NodeKind::Chord:
            for (NodeId k : n.kids) infer(ck, env, k);
            return t_con0(c, "Music");
        case NodeKind::Con:
            if (n.tok.text == "True" || n.tok.text == "False") return t_con0(c, "Bool");
            if (is_interval_name(n.tok.text)) return t_con0(c, "Interval");
            return new_var(c); // unknown constructor: leave open (WIP)
        case NodeKind::Var: {
            Scheme s;
            if (env_find(env, n.tok.text, s)) return instantiate(c, s);
            fail(ck, "unbound name in type check: " + std::string(n.tok.text));
            return new_var(c);
        }
        case NodeKind::App: {
            TypeId tf = infer(ck, env, n.kids[0]);
            for (std::size_t k = 1; k < n.kids.size(); k++) {
                TypeId targ = infer(ck, env, n.kids[k]);
                TypeId res = new_var(c);
                unify(ck, tf, t_arrow(c, targ, res));
                tf = resolve(c, res);
            }
            return tf;
        }
        case NodeKind::BinOp: {
            TypeId top = operator_type(ck, env, n.tok.text);
            if (top == NoType) {
                fail(ck, "unknown operator in type check: " + std::string(n.tok.text));
                return new_var(c);
            }
            TypeId tl = infer(ck, env, n.kids[0]);
            TypeId tr = infer(ck, env, n.kids[1]);
            TypeId res = new_var(c);
            unify(ck, top, t_arrow(c, tl, t_arrow(c, tr, res)));
            return resolve(c, res);
        }
        case NodeKind::If: {
            TypeId tc = infer(ck, env, n.kids[0]);
            unify(ck, tc, t_con0(c, "Bool"));
            TypeId tt = infer(ck, env, n.kids[1]);
            TypeId te = infer(ck, env, n.kids[2]);
            unify(ck, tt, te);
            return resolve(c, tt);
        }
        case NodeKind::ListLit: {
            TypeId el = new_var(c);
            for (NodeId k : n.kids) unify(ck, el, infer(ck, env, k));
            return t_list(c, el);
        }
        case NodeKind::Lambda: {
            std::size_t base = env.size();
            std::vector<TypeId> ptypes;
            for (int k = 0; k < n.extra; k++) {
                TypeId pv = new_var(c);
                ptypes.push_back(pv);
                Scheme s; s.type = pv;
                env.push_back({std::string(ck.ast->nodes[n.kids[k]].tok.text), s});
            }
            TypeId tbody = infer(ck, env, n.kids[n.extra]);
            env.resize(base);
            TypeId t = tbody;
            for (std::size_t k = ptypes.size(); k-- > 0;) t = t_arrow(c, ptypes[k], t);
            return t;
        }
        case NodeKind::Let: {
            std::size_t base = env.size();
            int nb = n.extra;
            // monomorphic let bindings (generalization is WIP; top-level only)
            for (int k = 0; k < nb; k++) {
                const Node& b = ck.ast->nodes[n.kids[k]];
                TypeId pv = new_var(c);
                Scheme s; s.type = pv;
                env.push_back({std::string(b.tok.text), s});
            }
            for (int k = 0; k < nb; k++) {
                const Node& b = ck.ast->nodes[n.kids[k]];
                // build a lambda-like type for function bindings
                std::size_t lbase = env.size();
                std::vector<TypeId> ptypes;
                for (int pp = 0; pp < b.extra; pp++) {
                    TypeId pv = new_var(c);
                    ptypes.push_back(pv);
                    Scheme s; s.type = pv;
                    env.push_back({std::string(ck.ast->nodes[b.kids[pp]].tok.text), s});
                }
                TypeId tbody = infer(ck, env, b.kids[b.extra]);
                env.resize(lbase);
                TypeId t = tbody;
                for (std::size_t pp = ptypes.size(); pp-- > 0;) t = t_arrow(c, ptypes[pp], t);
                Scheme bs;
                if (env_find(env, b.tok.text, bs)) unify(ck, bs.type, t);
            }
            TypeId tbody = infer(ck, env, n.kids[nb]);
            env.resize(base);
            return resolve(c, tbody);
        }
        default:
            return new_var(c);
    }
}

// Build the base environment of builtin operator/function types.
void seed_builtins(Checker& ck, Env& env) {
    Ctx& c = ck.ctx;
    auto add_mono = [&](const char* name, TypeId t) {
        Scheme s; s.type = t; env.push_back({name, s});
    };
    TypeId Int = t_con0(c, "Int");
    TypeId Bool = t_con0(c, "Bool");
    TypeId Pitch = t_con0(c, "Pitch");
    TypeId Interval = t_con0(c, "Interval");
    auto iii = [&]() { return t_arrow(c, t_con0(c, "Int"), t_arrow(c, t_con0(c, "Int"), t_con0(c, "Int"))); };
    add_mono("+", iii());
    add_mono("-", iii());
    add_mono("*", iii());
    add_mono("/", iii());
    // comparisons: Int -> Int -> Bool
    for (const char* op : {"<", ">", "<=", ">="})
        add_mono(op, t_arrow(c, t_con0(c, "Int"), t_arrow(c, t_con0(c, "Int"), t_con0(c, "Bool"))));
    // equality: forall a. a -> a -> Bool
    {
        TypeId a = new_var(c);
        Scheme s; s.vars.push_back(c.pool[a].var);
        s.type = t_arrow(c, a, t_arrow(c, a, t_con0(c, "Bool")));
        env.push_back({"==", s});
        Scheme s2 = s; env.push_back({"/=", s2});
    }
    // boolean operators / function
    add_mono("and", t_arrow(c, t_con0(c, "Bool"), t_arrow(c, t_con0(c, "Bool"), t_con0(c, "Bool"))));
    add_mono("or",  t_arrow(c, t_con0(c, "Bool"), t_arrow(c, t_con0(c, "Bool"), t_con0(c, "Bool"))));
    add_mono("not", t_arrow(c, t_con0(c, "Bool"), t_con0(c, "Bool")));
    // transpose: Pitch -> Interval -> Pitch
    add_mono("^+", t_arrow(c, Pitch, t_arrow(c, Interval, t_con0(c, "Pitch"))));
    add_mono("^-", t_arrow(c, t_con0(c, "Pitch"), t_arrow(c, t_con0(c, "Interval"), t_con0(c, "Pitch"))));
    add_mono("semitones", t_arrow(c, t_con0(c, "Pitch"), t_con0(c, "Int")));
    (void)Int; (void)Bool;
}

// Infer the type of one top-level binding (function or value).
TypeId infer_binding(Checker& ck, Env& env, const Node& b) {
    Ctx& c = ck.ctx;
    std::size_t base = env.size();
    std::vector<TypeId> ptypes;
    for (int k = 0; k < b.extra; k++) {
        TypeId pv = new_var(c);
        ptypes.push_back(pv);
        Scheme s; s.type = pv;
        env.push_back({std::string(ck.ast->nodes[b.kids[k]].tok.text), s});
    }
    TypeId tbody = infer(ck, env, b.kids[b.extra]);
    env.resize(base);
    TypeId t = tbody;
    for (std::size_t k = ptypes.size(); k-- > 0;) t = t_arrow(c, ptypes[k], t);
    return t;
}

// Typecheck all top-level bindings; leaves their generalized schemes in `env`.
void check_top(Checker& ck, Env& env) {
    if (ck.ast->root == ast::NoNode) return;
    const Node& prog = ck.ast->nodes[ck.ast->root];

    std::vector<NodeId> bindings;
    for (NodeId d : prog.kids)
        if (ck.ast->nodes[d].kind == NodeKind::Binding) bindings.push_back(d);

    // placeholders for recursion / forward references
    std::vector<std::size_t> slot;
    for (NodeId d : bindings) {
        TypeId pv = new_var(ck.ctx);
        Scheme s; s.type = pv;
        slot.push_back(env.size());
        env.push_back({std::string(ck.ast->nodes[d].tok.text), s});
    }
    for (std::size_t i = 0; i < bindings.size(); i++) {
        TypeId t = infer_binding(ck, env, ck.ast->nodes[bindings[i]]);
        unify(ck, env[slot[i]].scheme.type, t);
    }
    // generalize each binding's type against the rest of the environment
    for (std::size_t i = 0; i < bindings.size(); i++)
        env[slot[i]].scheme = generalize(ck.ctx, env, env[slot[i]].scheme.type);
}

// ---- rendering ----------------------------------------------------------
void render(Ctx& c, TypeId t, std::string& out, std::vector<int>& names) {
    t = resolve(c, t);
    const TypeNode& n = c.pool[t];
    if (n.is_var) {
        int idx = -1;
        for (std::size_t i = 0; i < names.size(); i++) if (names[i] == n.var) { idx = static_cast<int>(i); break; }
        if (idx < 0) { idx = static_cast<int>(names.size()); names.push_back(n.var); }
        out += "t";
        out += std::to_string(idx);
        return;
    }
    if (n.con == "->" && n.args.size() == 2) {
        TypeId l = resolve(c, n.args[0]);
        bool lparen = (c.pool[l].con == "->");
        if (lparen) out += "(";
        render(c, n.args[0], out, names);
        if (lparen) out += ")";
        out += " -> ";
        render(c, n.args[1], out, names);
        return;
    }
    if (n.con == "List" && n.args.size() == 1) {
        out += "[";
        render(c, n.args[0], out, names);
        out += "]";
        return;
    }
    out += n.con;
    for (TypeId a : n.args) { out += " "; render(c, a, out, names); }
}

} // namespace

bool typecheck_program(const ast::Ast& a, std::vector<std::string>& errors) {
    Checker ck;
    ck.ast = &a;
    ck.errors = &errors;
    Env env;
    seed_builtins(ck, env);
    check_top(ck, env);
    return errors.empty();
}

std::string infer_named_type(const ast::Ast& a, std::string_view name,
                             std::vector<std::string>& errors) {
    Checker ck;
    ck.ast = &a;
    ck.errors = &errors;
    Env env;
    seed_builtins(ck, env);
    check_top(ck, env);

    Scheme s;
    if (!env_find(env, name, s)) return "";
    TypeId t = instantiate(ck.ctx, s);
    std::string out;
    std::vector<int> names;
    render(ck.ctx, t, out, names);
    return out;
}

} // namespace calliope::types
