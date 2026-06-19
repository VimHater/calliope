#include "eval.hpp"

#include "instrument.hpp"

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
    B_EQ, B_NE, B_LT, B_GT, B_LE, B_GE, B_NOT,
    B_TRANSPOSE_UP, B_TRANSPOSE_DOWN,
    B_SEMITONES,
    B_NULL, B_HEAD, B_TAIL, B_CONS,
    // pitch projections (axioms the stdlib builds pitch transforms on)
    B_MKPITCH, B_DIASTEP, B_CHROMOF,
    // Music IR axioms: constructors, predicates, accessors
    B_NOTE, B_NOTEWITH, B_NOTEDUR, B_NOTEPITCH,
    B_SEQM, B_PARM,
    B_ISNOTE, B_ISREST, B_ISSEQ, B_ISPAR,
    B_MLEFT, B_MRIGHT,
    B_TUPLET,
    B_WITHINST, B_SFZ,
};

struct BuiltinInfo { const char* name; int id; int arity; };

const BuiltinInfo kBuiltins[] = {
    {"+", B_ADD, 2}, {"-", B_SUB, 2}, {"*", B_MUL, 2}, {"/", B_DIV, 2},
    {"==", B_EQ, 2}, {"/=", B_NE, 2},
    {"<", B_LT, 2}, {">", B_GT, 2}, {"<=", B_LE, 2}, {">=", B_GE, 2},
    {"not", B_NOT, 1},
    {"^+", B_TRANSPOSE_UP, 2}, {"^-", B_TRANSPOSE_DOWN, 2},
    {"semitones", B_SEMITONES, 1},
    // list axioms (the stdlib builds map/filter/… on top of these)
    {"null", B_NULL, 1}, {"head", B_HEAD, 1}, {"tail", B_TAIL, 1},
    {"cons", B_CONS, 2}, {":", B_CONS, 2},
    {"makePitch", B_MKPITCH, 2}, {"diatonicStep", B_DIASTEP, 1}, {"chromaticOf", B_CHROMOF, 1},
    {"note", B_NOTE, 1}, {"noteWith", B_NOTEWITH, 2},
    {"noteDur", B_NOTEDUR, 1}, {"notePitch", B_NOTEPITCH, 1},
    {"sequence", B_SEQM, 2}, {"parallel", B_PARM, 2},
    {"isNote", B_ISNOTE, 1}, {"isRest", B_ISREST, 1},
    {"isSeq", B_ISSEQ, 1}, {"isPar", B_ISPAR, 1},
    {"leftChild", B_MLEFT, 1}, {"rightChild", B_MRIGHT, 1},
    {"tuplet", B_TUPLET, 3},
    {"withInstrument", B_WITHINST, 2},
    {"sfz", B_SFZ, 1},
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

Value music_value(Interp& I, music::MusicId root) {
    Value v;
    v.kind = ValueKind::Music;
    v.mus = I.music;
    v.mroot = root;
    return v;
}

Value v_rat(Rational r) {
    Value v;
    v.kind = ValueKind::Rat;
    v.rat = r;
    return v;
}

// Inspect a Music value's root node; returns false if `v` is not Music.
bool music_node(const Value& v, music::MusicNode& out) {
    if (v.kind != ValueKind::Music || !v.mus) return false;
    if (v.mroot < 0 || v.mroot >= static_cast<music::MusicId>(v.mus->nodes.size())) return false;
    out = v.mus->nodes[v.mroot];
    return true;
}

// defined in the notation section below, used by the Music builtins
music::MusicId to_music(Interp& I, const Value& v);
Value v_pitch_dur(Pitch p, Rational dur);

// Structural equality for `==` / `/=`. Same kind throughout (the type checker
// rules out mixing): pitches by spelling, Music deeply, lists element-wise.
bool values_equal(Interp& I, const Value& x, const Value& y) {
    if (x.kind != y.kind) return false;
    switch (x.kind) {
        case ValueKind::Pitch: return pitch_eq(x.pitch, y.pitch);
        case ValueKind::Music: return music::equal(*I.music, x.mroot, y.mroot);
        case ValueKind::List:
            if (x.items.size() != y.items.size()) return false;
            for (std::size_t k = 0; k < x.items.size(); k++)
                if (!values_equal(I, x.items[k], y.items[k])) return false;
            return true;
        default: return x.i == y.i; // Int, Bool
    }
}

Value call_builtin(Interp& I, int id, std::vector<Value>& a) {
    switch (id) {
        case B_ADD: return v_int(a[0].i + a[1].i);
        case B_SUB: return v_int(a[0].i - a[1].i);
        case B_MUL: return v_int(a[0].i * a[1].i);
        case B_DIV:
            if (a[1].i == 0) { I.errors.push_back("division by zero"); return v_unit(); }
            return v_int(a[0].i / a[1].i);
        case B_EQ: case B_NE: {
            bool eq = values_equal(I, a[0], a[1]);
            return v_bool(id == B_EQ ? eq : !eq);
        }
        case B_LT: case B_GT: case B_LE: case B_GE: {
            // ordering: pitches compare by height (semitones), ints by value.
            long long l = (a[0].kind == ValueKind::Pitch) ? semitones(a[0].pitch) : a[0].i;
            long long r = (a[1].kind == ValueKind::Pitch) ? semitones(a[1].pitch) : a[1].i;
            bool res = id == B_LT ? l <  r
                     : id == B_GT ? l >  r
                     : id == B_LE ? l <= r
                                  : l >= r;
            return v_bool(res);
        }
        case B_NOT: return v_bool(a[0].i == 0);
        case B_TRANSPOSE_UP:
        case B_TRANSPOSE_DOWN: {
            int dstep, dsemi;
            if (a[1].kind != ValueKind::Con || !interval_steps(a[1].str, dstep, dsemi)) {
                I.errors.push_back("transpose expects an Interval");
                return v_unit();
            }
            if (id == B_TRANSPOSE_DOWN) { dstep = -dstep; dsemi = -dsemi; }
            if (a[0].kind == ValueKind::Pitch) {
                Value v = v_pitch(transpose_pitch(a[0].pitch, dstep, dsemi));
                v.rat = a[0].rat;  // preserve the note's duration
                return v;
            }
            if (a[0].kind == ValueKind::Music) {
                Value v;
                v.kind = ValueKind::Music;
                v.mus = a[0].mus;
                v.mroot = music::transpose(*a[0].mus, a[0].mroot, dstep, dsemi);
                return v;
            }
            I.errors.push_back("transpose expects a Pitch or Music");
            return v_unit();
        }
        case B_SEMITONES:
            if (a[0].kind != ValueKind::Pitch) {
                I.errors.push_back("semitones expects a Pitch");
                return v_unit();
            }
            return v_int(semitones(a[0].pitch));
        case B_NULL:
            if (a[0].kind != ValueKind::List) {
                I.errors.push_back("null expects a list");
                return v_unit();
            }
            return v_bool(a[0].items.empty());
        case B_HEAD:
            if (a[0].kind != ValueKind::List || a[0].items.empty()) {
                I.errors.push_back("head of an empty list");
                return v_unit();
            }
            return a[0].items.front();
        case B_TAIL: {
            if (a[0].kind != ValueKind::List || a[0].items.empty()) {
                I.errors.push_back("tail of an empty list");
                return v_unit();
            }
            Value v;
            v.kind = ValueKind::List;
            v.items.assign(a[0].items.begin() + 1, a[0].items.end());
            return v;
        }
        case B_CONS: {
            if (a[1].kind != ValueKind::List) {
                I.errors.push_back("cons expects a list as its second argument");
                return v_unit();
            }
            Value v;
            v.kind = ValueKind::List;
            v.items.reserve(a[1].items.size() + 1);
            v.items.push_back(a[0]);
            v.items.insert(v.items.end(), a[1].items.begin(), a[1].items.end());
            return v;
        }

        // ---- pitch projections ------------------------------------------
        case B_MKPITCH:   return v_pitch(mk_pitch(static_cast<int>(a[0].i), static_cast<int>(a[1].i)));
        case B_DIASTEP:   return v_int(diatonic_step(a[0].pitch));
        case B_CHROMOF:   return v_int(chromatic_of(static_cast<int>(a[0].i)));

        // ---- Music constructors -----------------------------------------
        case B_NOTE: {
            Rational d = (a[0].rat.num > 0) ? a[0].rat : rational(1, 4);
            return music_value(I, music::note(*I.music, a[0].pitch, d));
        }
        case B_NOTEWITH:
            return music_value(I, music::note(*I.music, a[0].pitch, a[1].rat));
        case B_SEQM:
            return music_value(I, music::seq(*I.music, to_music(I, a[0]), to_music(I, a[1])));
        case B_PARM:
            return music_value(I, music::par(*I.music, to_music(I, a[0]), to_music(I, a[1])));

        // ---- Music predicates -------------------------------------------
        case B_ISNOTE: case B_ISREST: case B_ISSEQ: case B_ISPAR: {
            music::MusicNode n;
            if (!music_node(a[0], n)) { I.errors.push_back("expected Music"); return v_unit(); }
            music::MusicKind want = id == B_ISNOTE ? music::MusicKind::Note
                                  : id == B_ISREST ? music::MusicKind::Rest
                                  : id == B_ISSEQ  ? music::MusicKind::Seq
                                                   : music::MusicKind::Par;
            return v_bool(n.kind == want);
        }

        // ---- Music accessors --------------------------------------------
        case B_NOTEPITCH: {
            music::MusicNode n;
            if (!music_node(a[0], n) || n.kind != music::MusicKind::Note) {
                I.errors.push_back("notePitch expects a note");
                return v_unit();
            }
            return v_pitch_dur(n.pitch, n.dur);
        }
        case B_NOTEDUR: {
            music::MusicNode n;
            if (!music_node(a[0], n) ||
                (n.kind != music::MusicKind::Note && n.kind != music::MusicKind::Rest)) {
                I.errors.push_back("noteDur expects a note or rest");
                return v_unit();
            }
            return v_rat(n.dur);
        }
        case B_MLEFT: case B_MRIGHT: {
            music::MusicNode n;
            if (!music_node(a[0], n) ||
                (n.kind != music::MusicKind::Seq && n.kind != music::MusicKind::Par)) {
                I.errors.push_back("leftChild/rightChild expect a Seq or Par");
                return v_unit();
            }
            return music_value(I, id == B_MLEFT ? n.left : n.right);
        }

        // ---- tuplet: n notes in the time of m (scale durations by m/n) ---
        case B_TUPLET: {
            long long n = a[0].i, mm = a[1].i;
            if (n <= 0 || mm <= 0) {
                I.errors.push_back("tuplet: counts must be >= 1");
                return a[2];
            }
            Rational factor = rational(mm, n);
            music::MusicId src = to_music(I, a[2]);
            return music_value(I, music::scale_dur(*I.music, src, factor));
        }
        // ---- withInstrument: wrap a phrase in a Control node ----------------
        // The Instrument value is a Con: a named-instrument constructor (its name is
        // in the table) or a custom `sfz "<path>"` (any other string — a .sfz path).
        case B_WITHINST: {
            if (a[0].kind != ValueKind::Con) {
                I.errors.push_back("withInstrument expects an Instrument");
                return a[1];
            }
            const std::string& name = a[0].str;
            int id = instrument::id_of(name);
            music::MusicId child = to_music(I, a[1]);
            if (id >= 0) return music_value(I, music::control(*I.music, id, child));
            return music_value(I, music::control(*I.music, -1, name, child)); // custom .sfz
        }
        // ---- sfz: lift a path string into a (custom) Instrument value -------
        case B_SFZ:
            if (a[0].kind != ValueKind::Str) {
                I.errors.push_back("sfz expects a string path");
                return v_unit();
            }
            return v_con(a[0].str, {});
    }
    I.errors.push_back("unknown builtin");
    return v_unit();
}

// ---- notation decode ----------------------------------------------------
// "c", "fis", "g'", "ees,", "c'4." -> Pitch + duration.
// letters: c=0 d=1 e=2 f=3 g=4 a=5 b=6 ; bare letter is octave 3 (so c' = C4).
// duration: trailing LilyPond number (4 = 1/4, 8 = 1/8, 1 = whole) with optional
// dots; absent => quarter note (1/4).
// Decode a trailing LilyPond duration ("4", "8.", "2..") at t[i]. On a leading
// digit, writes out_dur and advances i past digits+dots; otherwise leaves both.
void decode_dur(std::string_view t, std::size_t& i, Rational& out_dur) {
    long long base = 0;
    bool any = false;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') { base = base * 10 + (t[i] - '0'); i++; any = true; }
    if (!any || base <= 0) return;
    Rational d = rational(1, base);
    // dots: each adds half of the running value (dotted = 3/2, double = 7/4).
    Rational add = d;
    while (i < t.size() && t[i] == '.') { add = rat_div(add, rational(2, 1)); d = rat_add(d, add); i++; }
    out_dur = d;
}

void decode_pitch(std::string_view t, Pitch& out_p, Rational& out_dur) {
    static const int letter_of[7] = {5, 6, 0, 1, 2, 3, 4}; // a b c d e f g -> index
    out_p = pitch(0, 0, 3);
    out_dur = rational(1, 4);
    if (t.empty()) return;
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
    out_p = pitch(li, accidental, octave);
    decode_dur(t, i, out_dur);
}

// A rest literal "r" / "R" / "s" with an optional duration ("r2", "r4.").
Rational decode_rest_dur(std::string_view t) {
    Rational d = rational(1, 4);
    std::size_t i = 1; // skip the r/R/s letter
    decode_dur(t, i, d);
    return d;
}

Value v_pitch_dur(Pitch p, Rational dur) {
    Value v = v_pitch(p);
    v.rat = dur;
    return v;
}


// Lift a value into the Music IR: a Pitch becomes a Note (carrying its literal
// duration, default quarter), Music passes through, a Rest becomes a rest.
music::MusicId to_music(Interp& I, const Value& v) {
    music::Music& M = *I.music;
    switch (v.kind) {
        case ValueKind::Music:
            return v.mroot;
        case ValueKind::Pitch: {
            Rational d = (v.rat.num > 0) ? v.rat : rational(1, 4);
            return music::note(M, v.pitch, d);
        }
        case ValueKind::Con:
            if (v.str == "Rest") return music::rest(M, rational(1, 4));
            // fallthrough
        default:
            I.errors.push_back("value is not Music");
            return music::rest(M, rational(1, 4));
    }
}

long long parse_int(std::string_view t) {
    std::string s(t);
    return std::strtoll(s.c_str(), nullptr, 10);
}

// ---- class-method dispatch ----------------------------------------------
// Runtime type tag used to pick a class instance (single-parameter classes,
// dispatched on the method's first argument). Music constructors collapse to
// "Music"; other Con values use their constructor name as the type.
std::string value_type_tag(const Value& v) {
    switch (v.kind) {
        case ValueKind::Int:   return "Int";
        case ValueKind::Bool:  return "Bool";
        case ValueKind::Pitch: return "Pitch";
        case ValueKind::Music: return "Music";
        case ValueKind::Rat:   return "Rational";
        case ValueKind::Str:   return "Str";
        case ValueKind::List:  return "List";
        case ValueKind::Con: {
            const std::string& s = v.str;
            if (s == "Seq" || s == "Par" || s == "Rest" || s == "Chord") return "Music";
            return s;
        }
        default: return "";
    }
}

bool lookup_instance(Interp& I, const std::string& method, const std::string& type,
                     Value& out) {
    for (MethodImpl& mi : I.instances)
        if (mi.method == method && mi.type == type) { out = mi.impl; return true; }
    return false;
}

// ---- evaluation ---------------------------------------------------------
Value eval(Interp& I, NodeId id, const std::shared_ptr<Env>& env);

Value apply(Interp& I, Value f, Value arg) {
    if (f.kind == ValueKind::Method) {
        std::string tag = value_type_tag(arg);
        Value impl;
        if (!lookup_instance(I, f.str, tag, impl)) {
            I.errors.push_back("no instance: " + f.str + " for " + tag);
            return v_unit();
        }
        return apply(I, std::move(impl), std::move(arg));
    }
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

// Match `v` against a pattern, binding any pattern variables into `env`.
// Returns false (leaving partial bindings, discarded by the caller) on mismatch.
bool match_pattern(Interp& I, NodeId patId, const Value& v,
                   const std::shared_ptr<Env>& env) {
    const Node& pat = I.ast->nodes[patId];
    switch (pat.kind) {
        case NodeKind::PatWild:
            return true;
        case NodeKind::PatVar:
            env_define(env, std::string(pat.tok.text), v);
            return true;
        case NodeKind::PatInt:
            return v.kind == ValueKind::Int && v.i == parse_int(pat.tok.text);
        case NodeKind::PatCon: {
            std::string_view name = pat.tok.text;
            if (name == "True")  return v.kind == ValueKind::Bool && v.i != 0;
            if (name == "False") return v.kind == ValueKind::Bool && v.i == 0;
            if (name == "[")     return v.kind == ValueKind::List && v.items.empty();
            if (name == ":") {
                if (v.kind != ValueKind::List || v.items.empty()) return false;
                Value tail;
                tail.kind = ValueKind::List;
                tail.items.assign(v.items.begin() + 1, v.items.end());
                return match_pattern(I, pat.kids[0], v.items.front(), env) &&
                       match_pattern(I, pat.kids[1], tail, env);
            }
            return false; // unknown constructor
        }
        default:
            return false;
    }
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
        case NodeKind::PitchLit: {
            Pitch p; Rational d;
            decode_pitch(n.tok.text, p, d);
            return v_pitch_dur(p, d);
        }
        case NodeKind::RestLit:  return music_value(I, music::rest(*I.music, decode_rest_dur(n.tok.text)));
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
            music::MusicId acc = music::NoMusic;
            for (NodeId k : n.kids) {
                music::MusicId m = to_music(I, eval(I, k, env));
                acc = (acc == music::NoMusic) ? m : music::seq(*I.music, acc, m);
            }
            return music_value(I, acc);
        }
        case NodeKind::Chord: {
            music::MusicId acc = music::NoMusic;
            for (NodeId k : n.kids) {
                music::MusicId m = to_music(I, eval(I, k, env));
                acc = (acc == music::NoMusic) ? m : music::par(*I.music, acc, m);
            }
            // chord-level duration `<c e g>4.` (encoded in `extra`) overrides every
            // note's duration: base = extra/4, dots = extra%4.
            if (n.extra != 0) {
                Rational d = rational(1, n.extra / 4);
                Rational add = d;
                for (int k = 0; k < n.extra % 4; k++) { add = rat_div(add, rational(2, 1)); d = rat_add(d, add); }
                acc = music::set_dur(*I.music, acc, d);
            }
            return music_value(I, acc);
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
            if (op == "|>") return apply(I, std::move(r), std::move(l)); // x |> f = f x
            if (op == ":+:") return music_value(I, music::seq(*I.music, to_music(I, l), to_music(I, r)));
            if (op == ":=:") return music_value(I, music::par(*I.music, to_music(I, l), to_music(I, r)));
            if (op == "~") {
                // tie: matching phrases (note, chord, …) join with summed durations.
                // Operands lift to Music, so it works on pitches, chords, and ties
                // chain (the Music result is itself a valid left operand).
                music::MusicId a = to_music(I, l);
                music::MusicId b = to_music(I, r);
                bool ok = true;
                music::MusicId t = music::tie(*I.music, a, b, ok);
                if (!ok) {
                    I.errors.push_back("(~): tied phrases must have the same pitches and shape");
                    return v_unit();
                }
                return music_value(I, t);
            }
            if (op == ":*:") {
                // phrase :*: n  —  n copies in a row (right-leaning, like `times`)
                long long n = r.i;
                music::MusicId base = to_music(I, l);
                if (n < 1) { I.errors.push_back("(:*:): repeat count must be >= 1"); return music_value(I, base); }
                music::MusicId acc = base;
                for (long long k = 1; k < n; k++) acc = music::seq(*I.music, base, acc);
                return music_value(I, acc);
            }
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
        case NodeKind::Case: {
            Value scrut = eval(I, n.kids[0], env);
            for (std::size_t k = 1; k < n.kids.size(); k++) {
                const Node& alt = I.ast->nodes[n.kids[k]];
                auto child = make_env(env);
                if (match_pattern(I, alt.kids[0], scrut, child))
                    return eval(I, alt.kids[1], child);
            }
            I.errors.push_back("non-exhaustive patterns in case");
            return v_unit();
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
        case NodeKind::Alt:     // handled within Case
        case NodeKind::PatVar:
        case NodeKind::PatWild:
        case NodeKind::PatInt:
        case NodeKind::PatCon:
        case NodeKind::TypeSig:
        case NodeKind::TypeAtom:
        case NodeKind::Directive:
        case NodeKind::ClassDecl:
        case NodeKind::InstanceDecl:
        case NodeKind::MethodSig:
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
        std::string name = b.name;
        if (name == "^+" || name == "^-") {
            // Transposable has builtin instances for Pitch and Music; the operator
            // itself is a dispatched class method (the same builtin handles both,
            // branching on the argument kind).
            interp.instances.push_back({name, "Pitch", v});
            interp.instances.push_back({name, "Music", v});
            Value m;
            m.kind = ValueKind::Method;
            m.str = name;
            env_define(globals, name, std::move(m));
        } else {
            env_define(globals, name, std::move(v));
        }
    }

    if (a.root == ast::NoNode) return globals;
    const Node& prog = a.nodes[a.root];

    // user class declarations: each method name becomes a dispatched Method value
    for (NodeId d : prog.kids) {
        const Node& cd = a.nodes[d];
        if (cd.kind != NodeKind::ClassDecl) continue;
        for (std::size_t i = 1; i < cd.kids.size(); i++) {
            const Node& sig = a.nodes[cd.kids[i]];
            Value m;
            m.kind = ValueKind::Method;
            m.str = std::string(sig.tok.text);
            env_define(globals, m.str, m);
        }
    }
    // instance declarations: each method impl becomes a closure in the dispatch table
    for (NodeId d : prog.kids) {
        const Node& inst = a.nodes[d];
        if (inst.kind != NodeKind::InstanceDecl) continue;
        std::string ty(a.nodes[inst.kids[0]].tok.text);
        for (std::size_t i = 1; i < inst.kids.size(); i++) {
            const Node& b = a.nodes[inst.kids[i]];
            Value v;
            v.kind = ValueKind::Closure;
            v.clo = std::make_shared<Closure>();
            for (int p = 0; p < b.extra; p++)
                v.clo->params.push_back(std::string(a.nodes[b.kids[p]].tok.text));
            v.clo->body = b.kids[b.extra];
            v.clo->env = globals;
            interp.instances.push_back({std::string(b.tok.text), ty, std::move(v)});
        }
    }

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
        case ValueKind::Music:   return music::show(*v.mus, v.mroot);
        case ValueKind::Closure: return "<closure>";
        case ValueKind::Builtin: return "<builtin>";
        case ValueKind::Method:  return "<method " + v.str + ">";
    }
    return "?";
}

} // namespace calliope::eval
