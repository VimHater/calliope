#include "music.hpp"

namespace calliope::music {

MusicId add(Music& m, MusicNode n) {
    m.nodes.push_back(n);
    return static_cast<MusicId>(m.nodes.size() - 1);
}

MusicId note(Music& m, Pitch p, Rational dur) {
    MusicNode n;
    n.kind = MusicKind::Note;
    n.pitch = p;
    n.dur = dur;
    return add(m, n);
}

MusicId rest(Music& m, Rational dur) {
    MusicNode n;
    n.kind = MusicKind::Rest;
    n.dur = dur;
    return add(m, n);
}

MusicId seq(Music& m, MusicId a, MusicId b) {
    MusicNode n;
    n.kind = MusicKind::Seq;
    n.left = a;
    n.right = b;
    return add(m, n);
}

MusicId par(Music& m, MusicId a, MusicId b) {
    MusicNode n;
    n.kind = MusicKind::Par;
    n.left = a;
    n.right = b;
    return add(m, n);
}

MusicId transpose(Music& m, MusicId id, int dstep, int dsemi) {
    if (id == NoMusic) return NoMusic;
    // copy the node out first: add() may reallocate the pool mid-recursion.
    MusicNode n = m.nodes[id];
    switch (n.kind) {
        case MusicKind::Note: {
            int ns = diatonic_step(n.pitch) + dstep;
            int acc = (semitones(n.pitch) + dsemi) - chromatic_of(ns);
            return note(m, mk_pitch(ns, acc), n.dur);
        }
        case MusicKind::Rest:
            return rest(m, n.dur);
        case MusicKind::Seq: {
            MusicId a = transpose(m, n.left, dstep, dsemi);
            MusicId b = transpose(m, n.right, dstep, dsemi);
            return seq(m, a, b);
        }
        case MusicKind::Par: {
            MusicId a = transpose(m, n.left, dstep, dsemi);
            MusicId b = transpose(m, n.right, dstep, dsemi);
            return par(m, a, b);
        }
    }
    return NoMusic;
}

namespace {
std::string show_dur(const Rational& d) {
    return std::to_string(d.num) + "/" + std::to_string(d.den);
}
std::string show_pitch(const Pitch& p) {
    static const char* letters = "CDEFGAB";
    std::string s(1, letters[p.letter]);
    for (int k = 0; k < p.accidental; k++) s += "#";
    for (int k = 0; k < -p.accidental; k++) s += "b";
    return s + std::to_string(p.octave);
}
} // namespace

std::string show(const Music& m, MusicId id) {
    if (id == NoMusic || id >= static_cast<MusicId>(m.nodes.size())) return "<>";
    const MusicNode& n = m.nodes[id];
    switch (n.kind) {
        case MusicKind::Note: return show_pitch(n.pitch) + ":" + show_dur(n.dur);
        case MusicKind::Rest: return "r:" + show_dur(n.dur);
        case MusicKind::Seq:
            return "(" + show(m, n.left) + " :+: " + show(m, n.right) + ")";
        case MusicKind::Par:
            return "(" + show(m, n.left) + " :=: " + show(m, n.right) + ")";
    }
    return "<>";
}

} // namespace calliope::music
