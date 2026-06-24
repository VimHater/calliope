#include "backend/transcribe.hpp"

#define HOXML_IMPLEMENTATION
#include "hoxml.h"
#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace calliope::transcribe {

namespace {

// ---- small helpers ------------------------------------------------------
bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const char* s) {
    if (!s) return std::string();
    std::string t(s);
    std::size_t a = t.find_first_not_of(" \t\r\n");
    std::size_t b = t.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? std::string() : t.substr(a, b - a + 1);
}

std::vector<char> read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
}

// ---- model --------------------------------------------------------------
// A note carries an absolute position (`start_div`) and length (`dur_div`) in
// MusicXML divisions, so multi-voice measures (driven by <backup>/<forward>) and
// chords (pinned to the previous note's start) reconstruct on one clock.
struct Note {
    int start_div = 0;
    int dur_div = 0;
    char letter = 'c';      // a..g (lowercase)
    int alter = 0;          // -2..+2 semitones
    int octave = 4;         // MusicXML octave (4 = our c')
    bool rest = false;
    bool chord = false;     // pinned to the previous note's start
    bool tie_start = false; // tied into the next same-pitch note (`~`)
    bool pedaled = false;   // sounding under a depressed damper pedal
    int oshift = 0;         // octave-shift in effect (signed octaves: 8vb = -1, 8va = +1)
    int wedge = 0;          // inside a hairpin: +1 crescendo, -1 diminuendo, 0 none
    int vel = -1;           // baked velocity from a wedge ramp (-1 = none, use the dynamic)
    bool rolled = false;    // an arpeggiated (rolled) chord
    std::string artic;      // "staccato"/"accent"/"tenuto"/"marcato"
    std::string ornament;   // "trill"/"mordent"/"turn"
    std::string dynamic;    // a dynamic taking effect here ("forte"/…)
    bool has_grace = false; // a grace note precedes this one (acciaccatura)
    char g_letter = 'c'; int g_alter = 0; int g_octave = 4;
};

using Line = std::vector<std::vector<Note>>;   // one (part,voice): measures of notes

struct Score {
    std::map<std::pair<int, int>, Line> lines;  // key (part, voice)
    std::map<int, std::string> measure_dyn;     // measure index -> dynamic (part-wide)
    int divisions = 1;
    bool has_key = false;  int fifths = 0;
    bool has_time = false; int beats = 4, beat_type = 4;
    bool has_tempo = false; int tempo = 0;
    std::vector<bool> implicit;   // [measure index] true = pickup/anacrusis (no pad)
    // repeat structure (1-based measure index). forward = a |: start; backward maps
    // a :| end to its repeat count (times+1 total plays); volta_* record endings.
    std::set<int> repeat_forward;
    std::map<int, int> repeat_backward;      // measure -> extra repeats (1 = play twice)
    std::map<int, int> volta_start;          // measure -> ending number (1,2,…)
    std::set<int> volta_stop;                // measure index where an ending stops
    std::map<std::pair<int, int>, int> line_staff;  // (part,voice) -> its staff number
    std::map<int, std::set<int>> part_staves;       // part -> the staves it uses
    std::set<std::string> seen;
};

// ---- .mxl: pick the score XML out of the zip ----------------------------
bool unzip_score(const std::vector<char>& bytes, std::string& xml, std::string& err) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        err = "not a valid .mxl (zip) archive";
        return false;
    }
    int count = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    int chosen = -1;
    for (int i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        if (name.rfind("META-INF", 0) == 0) continue;       // skip the container
        std::string lname = lower(name);
        if (ends_with(lname, ".xml") || ends_with(lname, ".musicxml")) { chosen = i; break; }
    }
    if (chosen < 0) { mz_zip_reader_end(&zip); err = "no score XML inside the .mxl"; return false; }
    std::size_t sz = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, chosen, &sz, 0);
    if (!p) { mz_zip_reader_end(&zip); err = "could not extract the score XML"; return false; }
    xml.assign(static_cast<char*>(p), sz);
    mz_free(p);
    mz_zip_reader_end(&zip);
    return true;
}

// a MusicXML dynamics child tag (<f/>, <pp/>, <sf/>, …) -> the stdlib dynamic
// function. The plain ladder maps directly; extremes past our 8 levels clamp to
// the ends; accent marks (sf/fz/fp/rf families — a sudden stress, not a level)
// approximate to the nearest loud level, the soft-after-accent ones to piano.
std::string dyn_name(const std::string& tag) {
    // plain ladder
    if (tag == "ppp") return "pianississimo";
    if (tag == "pp")  return "pianissimo";
    if (tag == "p")   return "piano";
    if (tag == "mp")  return "mezzoPiano";
    if (tag == "mf")  return "mezzoForte";
    if (tag == "f")   return "forte";
    if (tag == "ff")  return "fortissimo";
    if (tag == "fff") return "fortississimo";
    // beyond the 8 levels we have -> clamp to the ends
    if (tag == "pppp" || tag == "ppppp" || tag == "pppppp" || tag == "n")
        return "pianississimo";  // n = niente (silent) -> our softest
    if (tag == "ffff" || tag == "fffff" || tag == "ffffff")
        return "fortississimo";
    // accents: sudden stress -> approximate by a loud level
    if (tag == "sf" || tag == "sfz" || tag == "sffz" || tag == "fz" ||
        tag == "sforzando" || tag == "sforzato")
        return "fortissimo";
    if (tag == "rf" || tag == "rfz" || tag == "fp")  // rinforzando / forte-piano attack
        return "forte";
    if (tag == "pf")                                  // poco forte
        return "mezzoForte";
    if (tag == "sfp" || tag == "sfpp" || tag == "sfzp")  // accent then soft -> the soft tail
        return "piano";
    return "";
}

// the velocity of a stdlib dynamic function name (mirrors prelude.cal); 80 = default mf
int dyn_vel(const std::string& f) {
    if (f == "pianississimo") return 16;
    if (f == "pianissimo")    return 33;
    if (f == "piano")         return 49;
    if (f == "mezzoPiano")    return 64;
    if (f == "mezzoForte")    return 80;
    if (f == "forte")         return 96;
    if (f == "fortissimo")    return 112;
    if (f == "fortississimo") return 127;
    return 80;
}
int clamp_vel(int v) { return v < 1 ? 1 : v > 127 ? 127 : v; }

int denom_of_type(const std::string& type) {
    if (type == "whole")   return 1;
    if (type == "half")    return 2;
    if (type == "quarter") return 4;
    if (type == "eighth")  return 8;
    if (type == "16th")    return 16;
    if (type == "32nd")    return 32;
    if (type == "64th")    return 64;
    return 0;
}

// ---- hoxml SAX parse: build the per-voice note lists on one measure clock ----
bool parse_xml(const std::string& xml, Score& score, std::string& err) {
    std::size_t bufsz = xml.size() + 8192;
    std::vector<char> buf(bufsz);
    hoxml_context_t ctx;
    hoxml_init(&ctx, buf.data(), buf.size());

    // document-cursor state (MusicXML semantics: one cursor per measure that all
    // voices share; <backup>/<forward> move it, <chord/> pins to the prev start)
    int part_index = 0, measure_index = 0, voice = 1;
    int cursor = 0, prev_start = 0;
    bool in_part = false, in_note = false, in_pitch = false;
    bool in_dynamics = false, in_metronome = false;
    int dur_target = 0;       // 0 note, 1 backup, 2 forward
    std::string mm_beat_unit;
    int mm_per_minute = 0, mm_dots = 0;
    bool note_is_grace = false, pedal_down = false, have_grace = false;
    int grace_voice = 0, note_staff = 1;
    int os_dir = 0, os_oct = 1, active_oshift = 0;  // octave-shift span state
    int active_wedge = 0;   // crescendo/diminuendo hairpin span state
    std::string rep_dir, end_type; int rep_times = 1, end_num = 0;  // barline repeat/ending
    Note note, grace_note;

    auto tag_is = [&](const char* t) { return ctx.tag && std::strcmp(ctx.tag, t) == 0; };
    auto has = [&](const std::string& tag, const char* sub) { return tag.find(sub) != std::string::npos; };
    auto push_note = [&](const Note& n) {
        Line& line = score.lines[{part_index, voice}];
        while (static_cast<int>(line.size()) < measure_index) line.emplace_back();
        if (measure_index >= 1) line[measure_index - 1].push_back(n);
    };

    for (;;) {
        hoxml_code_t code = hoxml_parse(&ctx, xml.data(), xml.size());
        if (code == HOXML_END_OF_DOCUMENT) break;
        if (code == HOXML_ERROR_INSUFFICIENT_MEMORY) {
            bufsz *= 2; buf.resize(bufsz); hoxml_realloc(&ctx, buf.data(), buf.size());
            continue;
        }
        if (code < 0) { err = "MusicXML parse error near line " + std::to_string(ctx.line); return false; }

        if (code == HOXML_ELEMENT_BEGIN) {
            if (ctx.tag) score.seen.insert(ctx.tag);
            if (tag_is("part"))           { in_part = true; ++part_index; measure_index = 0; voice = 1; }
            else if (tag_is("measure"))   { ++measure_index; cursor = 0; prev_start = 0; }
            else if (tag_is("note"))      { in_note = true; note = Note{}; note_is_grace = false; note_staff = 1; }
            else if (tag_is("pitch"))     { in_pitch = true; }
            else if (in_note && tag_is("grace")) { note_is_grace = true; }
            else if (tag_is("backup"))    { dur_target = 1; }
            else if (tag_is("forward"))   { dur_target = 2; }
            else if (tag_is("repeat"))    { rep_dir.clear(); rep_times = 1; }
            else if (tag_is("ending"))    { end_type.clear(); end_num = 0; }
            else if (tag_is("dynamics"))  { in_dynamics = true; }
            else if (tag_is("metronome")) { in_metronome = true; mm_beat_unit.clear(); mm_per_minute = 0; mm_dots = 0; }
        } else if (code == HOXML_ATTRIBUTE) {
            const char* a = ctx.attribute;
            const char* v = ctx.value;
            if (tag_is("sound") && a && std::strcmp(a, "tempo") == 0) {
                if (!score.has_tempo) { score.has_tempo = true; score.tempo = std::atoi(v ? v : "0"); }
            } else if (in_note && (tag_is("tie") || tag_is("tied")) && a &&
                       std::strcmp(a, "type") == 0 && v && std::strcmp(v, "start") == 0) {
                note.tie_start = true;
            } else if (tag_is("measure") && a && std::strcmp(a, "implicit") == 0 &&
                       v && std::strcmp(v, "yes") == 0 && measure_index >= 1) {
                while (static_cast<int>(score.implicit.size()) < measure_index) score.implicit.push_back(false);
                score.implicit[measure_index - 1] = true;
            } else if (tag_is("pedal") && a && std::strcmp(a, "type") == 0 && v) {
                std::string t = v;
                if (t == "start" || t == "change" || t == "sostenuto") pedal_down = true;
                else if (t == "stop") pedal_down = false;
            } else if (tag_is("octave-shift") && a && v) {
                // <octave-shift type=up/down size=8/15/22>: written pitch is on the
                // staff, the shift moves the SOUNDING octave (down = 8vb, sounds lower)
                std::string n = a, t = v;
                if (n == "type") {
                    if (t == "stop") os_dir = 0;
                    else if (t == "up") os_dir = 1;
                    else if (t == "down") os_dir = -1;
                    // "continue" keeps the current direction
                } else if (n == "size") {
                    int sz = std::atoi(v);
                    os_oct = sz >= 8 ? (sz - 1) / 7 : 1;   // 8->1, 15->2, 22->3
                }
                active_oshift = os_dir * os_oct;
            } else if (tag_is("wedge") && a && std::strcmp(a, "type") == 0 && v) {
                // hairpin: crescendo/diminuendo span -> a per-note velocity ramp
                std::string t = v;
                if (t == "crescendo") active_wedge = 1;
                else if (t == "diminuendo") active_wedge = -1;
                else if (t == "stop") active_wedge = 0;
                // "continue" keeps it
            } else if (tag_is("repeat") && a && v) {
                if (std::strcmp(a, "direction") == 0) rep_dir = v;
                else if (std::strcmp(a, "times") == 0) rep_times = std::atoi(v);
            } else if (tag_is("ending") && a && v) {
                if (std::strcmp(a, "number") == 0) end_num = std::atoi(v);
                else if (std::strcmp(a, "type") == 0) end_type = v;
            }
        } else if (code == HOXML_ELEMENT_END) {
            std::string content = trim(ctx.content);
            if (in_note && tag_is("rest"))  note.rest = true;
            else if (in_note && tag_is("chord")) note.chord = true;
            else if (in_note && tag_is("voice")) voice = std::atoi(content.c_str());
            else if (in_note && tag_is("staff")) note_staff = std::atoi(content.c_str());
            else if (in_note && tag_is("duration")) note.dur_div = std::atoi(content.c_str());
            else if (in_note && (tag_is("staccato") || tag_is("accent") || tag_is("tenuto") ||
                                 tag_is("strong-accent")))
                note.artic = tag_is("strong-accent") ? "marcato" : std::string(ctx.tag);
            else if (in_note && (tag_is("trill-mark") || tag_is("mordent") ||
                                 tag_is("inverted-mordent") || tag_is("turn") || tag_is("inverted-turn")))
                note.ornament = has(ctx.tag, "mordent") ? "mordent" : has(ctx.tag, "turn") ? "turn" : "trill";
            else if (in_note && tag_is("arpeggiate")) note.rolled = true;
            else if (in_pitch && tag_is("step") && !content.empty())
                note.letter = static_cast<char>(std::tolower(static_cast<unsigned char>(content[0])));
            else if (in_pitch && tag_is("alter"))  note.alter = std::atoi(content.c_str());
            else if (in_pitch && tag_is("octave")) note.octave = std::atoi(content.c_str());
            else if (in_dynamics && !dyn_name(ctx.tag).empty()) {
                // a dynamic is part-wide: snap it to this measure for every voice
                if (measure_index >= 1) score.measure_dyn[measure_index] = dyn_name(ctx.tag);
            }
            else if (in_metronome && tag_is("beat-unit"))  mm_beat_unit = lower(content);
            else if (in_metronome && tag_is("beat-unit-dot")) mm_dots++;
            else if (in_metronome && tag_is("per-minute")) mm_per_minute = std::atoi(content.c_str());
            else if (!in_note && tag_is("duration")) {   // <backup>/<forward> duration
                int d = std::atoi(content.c_str());
                if (dur_target == 1) cursor -= d;
                else if (dur_target == 2) cursor += d;
                dur_target = 0;
            }
            else if (tag_is("divisions")) { int d = std::atoi(content.c_str()); if (d > 0) score.divisions = d; }
            else if (tag_is("fifths")) { if (!score.has_key) { score.has_key = true; score.fifths = std::atoi(content.c_str()); } }
            else if (tag_is("beats")) { if (!score.has_time) score.beats = std::atoi(content.c_str()); }
            else if (tag_is("beat-type")) { if (!score.has_time) { score.has_time = true; score.beat_type = std::atoi(content.c_str()); } }
            else if (tag_is("repeat") && measure_index >= 1) {
                if (rep_dir == "forward") score.repeat_forward.insert(measure_index);
                else if (rep_dir == "backward")
                    score.repeat_backward[measure_index] = rep_times >= 1 ? rep_times : 1;
            }
            else if (tag_is("ending") && measure_index >= 1) {
                if (end_type == "start") score.volta_start[measure_index] = end_num >= 1 ? end_num : 1;
                else if (end_type == "stop" || end_type == "discontinue")
                    score.volta_stop.insert(measure_index);
            }
            // structural closes
            else if (tag_is("pitch"))    in_pitch = false;
            else if (tag_is("dynamics")) in_dynamics = false;
            else if (tag_is("metronome")) {
                int d = denom_of_type(mm_beat_unit);
                if (d > 0 && mm_per_minute > 0 && !score.has_tempo) {
                    // quarter BPM = per-minute * (4/d) * dot-factor, where one dot
                    // lengthens the beat by 3/2 (k dots: (2^(k+1)-1)/2^k). A dotted
                    // beat unit (♩.= 96) is a faster quarter tempo (= 144), not slower.
                    int num = 1, den = 1;
                    if (mm_dots > 0) { den = 1 << mm_dots; num = (1 << (mm_dots + 1)) - 1; }
                    long long t = static_cast<long long>(mm_per_minute) * 4 * num;
                    long long dd = static_cast<long long>(d) * den;
                    score.has_tempo = true;
                    score.tempo = static_cast<int>((t + dd / 2) / dd);
                }
                in_metronome = false;
            }
            else if (tag_is("note")) {
                in_note = false;
                if (note_is_grace) {
                    // graces carry no <duration>: stash for the next host, no cursor move
                    if (!have_grace) { grace_note = note; grace_voice = voice; have_grace = true; }
                } else {
                    int start = note.chord ? prev_start : cursor;
                    note.start_div = start;
                    note.pedaled = pedal_down;
                    note.oshift = active_oshift;
                    note.wedge = active_wedge;
                    if (have_grace && grace_voice == voice && !note.chord && !note.rest) {
                        note.has_grace = true;
                        note.g_letter = grace_note.letter;
                        note.g_alter = grace_note.alter;
                        note.g_octave = grace_note.octave;
                        have_grace = false;
                    }
                    push_note(note);
                    score.line_staff[{part_index, voice}] = note_staff;
                    score.part_staves[part_index].insert(note_staff);
                    if (!note.chord) { prev_start = start; cursor += note.dur_div; }
                }
            }
            else if (tag_is("part")) in_part = false;
        }
    }
    (void)in_part;
    if (score.lines.empty()) { err = "no parts found in the score"; return false; }
    return true;
}

// ---- emission -----------------------------------------------------------
// a span of `divs` divisions -> a Calliope duration suffix (e.g. "4", "8.", "12").
// Calliope accepts any denominator, so a triplet eighth is just "12" (1/12) — no
// tuplet wrapper needed. Falls back to the nearest base when not exact.
std::string format_duration(const Score& s, int divs) {
    int whole = 4 * s.divisions;
    if (divs <= 0) return "4";
    if (whole % divs == 0) return std::to_string(whole / divs);
    if ((3 * whole) % (2 * divs) == 0) return std::to_string((3 * whole) / (2 * divs)) + ".";
    int base = (whole + divs / 2) / divs;        // nearest
    return std::to_string(base > 0 ? base : 1);
}

// global spelling context, set per-score in render(). When g_bare is on (the score
// is safe under its key — every accidental matches the signature), pitches are
// written as bare letters and a single `inKey` resolves them, which reads far
// cleaner than explicit `is`/`es` on every note.
bool g_bare = false;
int  g_fifths = 0;

// the key's accidental for a letter (C=0 D=1 E=2 F=3 G=4 A=5 B=6), mirroring
// music::key_accidental — kept local so the string emitter has no IR dependency.
int key_acc(int fifths, char letter) {
    static const int sharp_order[7] = {3, 0, 4, 1, 5, 2, 6}; // F C G D A E B
    static const int flat_order[7]  = {6, 2, 5, 1, 4, 0, 3}; // B E A D G C F
    int li = ((letter - 'c') % 7 + 7) % 7;                   // 'c'->0 … 'b'->6
    if (fifths > 0) for (int k = 0; k < fifths && k < 7; k++) if (sharp_order[k] == li) return 1;
    if (fifths < 0) for (int k = 0; k < -fifths && k < 7; k++) if (flat_order[k] == li) return -1;
    return 0;
}

std::string pitch_of(char letter, int alter, int octave) {
    std::string s(1, letter);
    // under a safe key, drop the accidental spelling (the key resolves the bare
    // letter); otherwise spell it explicitly. `alter` already matches the key here.
    if (!g_bare) {
        for (int k = 0; k < alter; k++)  s += "is";
        for (int k = 0; k < -alter; k++) s += "es";
    }
    int marks = octave - 3;                 // MusicXML octave 3 = bare letter (C3)
    for (int k = 0; k < marks; k++)  s += '\'';
    for (int k = 0; k < -marks; k++) s += ',';
    return s;
}
std::string pitch_lit(const Note& n) { return pitch_of(n.letter, n.alter, n.octave); }

// one chord-unit notes[i..j) as a bare literal (with its duration)
std::string literal(const Score& s, const std::vector<Note>& n, std::size_t i, std::size_t j) {
    std::string dur = format_duration(s, n[i].dur_div);
    if (n[i].rest) return "r" + dur;
    if (j - i == 1) return pitch_lit(n[i]) + dur;
    std::string out = "<";
    for (std::size_t k = i; k < j; k++) { if (k > i) out += ' '; out += pitch_lit(n[k]); }
    return out + ">" + dur;
}

// a rolled (arpeggiated) chord notes[i..j): stagger each note's onset by a small
// step (bottom first), each held to the chord's end so the unit still spans dur D
// and stays aligned — `(c4 :=: (r32 :+: e16.) :=: (r16 :+: g8))`-shaped.
std::string arpeggio(const Score& s, const std::vector<Note>& n, std::size_t i, std::size_t j) {
    int D = n[i].dur_div, count = static_cast<int>(j - i);
    int unit = s.divisions / 8 > 0 ? s.divisions / 8 : 1;     // ~a 32nd note
    int step = std::max(1, std::min(unit, D / (count * 2 > 0 ? count * 2 : 1)));
    std::string out = "(";
    for (std::size_t k = i; k < j; k++) {
        int off = static_cast<int>(k - i) * step;
        std::string voice;
        if (off <= 0) {
            voice = pitch_lit(n[k]) + format_duration(s, D);
        } else {
            int hold = D - off; if (hold < 1) hold = 1;
            voice = "(r" + format_duration(s, off) + " :+: " +
                    pitch_lit(n[k]) + format_duration(s, hold) + ")";
        }
        if (k > i) out += " :=: ";
        out += voice;
    }
    return out + ")";
}

// reconstruct one measure: walk chord-units in time order, fill any gap before a
// unit with rests, and pad the tail up to the bar length — so the measure sums to
// a whole bar and the voices stay aligned.
std::vector<Note> reconstruct(const Score& s, const std::vector<Note>& notes, int bar_divs) {
    std::vector<Note> out;
    // fill a gap with rest Notes, largest clean (power-of-two / dotted) piece first
    auto add_rests = [&](int divs) {
        int whole = 4 * s.divisions, remaining = divs;
        static const int bases[] = {1, 2, 4, 8, 16, 32};
        while (remaining > 0) {
            int best = 0;
            for (int b : bases) {
                int span = whole / b;
                if (span > 0 && whole % b == 0 && span <= remaining && span > best) best = span;
                int dotted = span + span / 2;
                if (span % 2 == 0 && dotted <= remaining && dotted > best) best = dotted;
            }
            if (best <= 0) best = remaining;
            Note r; r.rest = true; r.dur_div = best;
            out.push_back(r);
            remaining -= best;
        }
    };
    int cursor = 0;
    std::size_t i = 0;
    while (i < notes.size()) {
        std::size_t j = i + 1;
        while (j < notes.size() && notes[j].chord) j++;
        const Note& head = notes[i];
        if (head.start_div > cursor) add_rests(head.start_div - cursor);
        for (std::size_t k = i; k < j; k++) out.push_back(notes[k]);
        cursor = head.start_div + head.dur_div;
        i = j;
    }
    if (bar_divs > 0 && cursor < bar_divs) add_rests(bar_divs - cursor);
    return out;
}

// render a reconstructed note list: plain notes/chords/rests join by adjacency
// (a Seq); a tie is `~`, an articulation / ornament wraps that note — those break
// the run, so tokens recombine with `:+:`.
std::string render_run(const Score& s, const std::vector<Note>& n) {
    std::vector<std::string> tokens, plain;
    auto flush = [&]() {
        if (plain.empty()) return;
        std::string a;
        for (std::size_t k = 0; k < plain.size(); k++) { if (k) a += ' '; a += plain[k]; }
        tokens.push_back(a); plain.clear();
    };
    auto same_pitch = [&](std::size_t x, std::size_t y) {
        return !n[x].rest && !n[y].rest && n[x].letter == n[y].letter &&
               n[x].alter == n[y].alter && n[x].octave == n[y].octave;
    };
    auto unit_end = [&](std::size_t a) {
        std::size_t b = a + 1; while (b < n.size() && n[b].chord) b++; return b;
    };
    // a unit (note or chord) is tied forward if any of its notes carries a tie start
    auto unit_tied = [&](std::size_t a, std::size_t b) {
        for (std::size_t k = a; k < b; k++) if (!n[k].rest && n[k].tie_start) return true;
        return false;
    };
    // two units are the same shape: equal note count, element-wise same pitch (chords
    // are listed bottom-up consistently, so positional comparison holds)
    auto same_unit = [&](std::size_t a, std::size_t b, std::size_t c, std::size_t d) {
        if (b - a != d - c) return false;
        for (std::size_t t = 0; t < b - a; t++)
            if (!same_pitch(a + t, c + t)) return false;
        return true;
    };
    std::size_t i = 0;
    while (i < n.size()) {
        std::size_t j = unit_end(i);

        // tie chain: `~`-join consecutive same-shape units (notes OR chords) while the
        // current unit is tied forward and the next unit matches its pitches. Without
        // this a tied chord would re-attack -> an audible duplicate.
        std::size_t jn = j < n.size() ? unit_end(j) : j;
        if (!n[i].rest && unit_tied(i, j) && j < n.size() && same_unit(i, j, j, jn)) {
            flush();
            std::string tok = literal(s, n, i, j);
            std::size_t a = i, b = j;
            while (b < n.size()) {
                std::size_t c = unit_end(b);
                tok += " ~ " + literal(s, n, b, c);
                bool cont = unit_tied(b, c) && c < n.size() && same_unit(b, c, c, unit_end(c));
                a = b; b = c; (void)a;
                if (!cont) break;
            }
            tokens.push_back(tok);
            i = b;
            continue;
        }

        // a rolled chord (arpeggiate) on any unit member -> a staggered figure
        bool rolled = (j - i > 1) && !n[i].rest;
        if (rolled) { rolled = false; for (std::size_t k = i; k < j; k++) if (n[k].rolled) rolled = true; }
        std::string lit = rolled ? arpeggio(s, n, i, j) : literal(s, n, i, j);
        bool single = (j - i == 1) && !n[i].rest;
        // collect the transforms on this unit, applied left-to-right as a pipe chain
        // `(lit |> ornament |> artic |> velocity N)` — reads as "note, then …".
        std::vector<std::string> fns;
        if (single && n[i].has_grace) {
            fns.push_back("acciaccatura " + pitch_of(n[i].g_letter, n[i].g_alter, n[i].g_octave));
        } else if (single && !n[i].ornament.empty()) {
            fns.push_back(n[i].ornament);
            if (!n[i].artic.empty()) fns.push_back(n[i].artic);
        } else if (!n[i].rest && !n[i].artic.empty()) {
            fns.push_back(n[i].artic);
        }
        if (!n[i].rest && n[i].vel >= 0) fns.push_back("velocity " + std::to_string(n[i].vel));
        if (fns.empty() && !rolled) {
            plain.push_back(lit);
        } else if (fns.empty()) {       // a bare roll is already a parenthesised expr
            flush(); tokens.push_back(lit);
        } else {
            std::string tok = lit;
            for (const std::string& f : fns) tok += " |> " + f;
            flush(); tokens.push_back("(" + tok + ")");
        }
        i = j;
    }
    flush();
    std::string out;
    for (std::size_t k = 0; k < tokens.size(); k++) { if (k) out += " :+: "; out += tokens[k]; }
    return out.empty() ? "r4" : out;
}

int measure_divs(const Score& s) {
    if (!s.has_time) return 0;
    return s.beats * (4 * s.divisions / s.beat_type);
}

// split a note list into octave-shift segments and wrap each shifted run in
// `ottava k (...)` (8va = +1, 8vb = -1). A rest inherits the current segment's
// shift so a gap-rest inside an 8va span doesn't fragment the run.
std::string render_octave(const Score& s, const std::vector<Note>& notes) {
    std::vector<std::pair<int, std::vector<Note>>> segs;
    for (const Note& nt : notes) {
        int sh = nt.rest ? (segs.empty() ? 0 : segs.back().first) : nt.oshift;
        if (segs.empty() || sh != segs.back().first) segs.push_back({sh, {}});
        segs.back().second.push_back(nt);
    }
    std::string out;
    for (std::size_t k = 0; k < segs.size(); k++) {
        std::string run = render_run(s, segs[k].second);
        if (segs[k].first != 0) {
            int sh = segs[k].first;   // no unary minus in the grammar -> use `negate`
            std::string arg = sh < 0 ? "(negate " + std::to_string(-sh) + ")"
                                     : std::to_string(sh);
            run = "(" + run + " |> ottava " + arg + ")";
        }
        if (k) out += " :+: ";
        out += run;
    }
    return out.empty() ? "r4" : out;
}

// bake a wedge (crescendo/diminuendo) into per-note velocities: ramp linearly from
// the dynamic before the hairpin to the one after it (or a level up/down if none),
// across the notes in the span. Each ramped note then renders as `velocity N (...)`.
void apply_wedges(std::vector<Note>& all) {
    std::vector<int> level(all.size(), 80);   // active dynamic velocity per note
    int cur = 80;
    for (std::size_t k = 0; k < all.size(); k++) {
        if (!all[k].dynamic.empty()) cur = dyn_vel(all[k].dynamic);
        level[k] = cur;
    }
    std::size_t k = 0;
    while (k < all.size()) {
        if (all[k].wedge == 0) { k++; continue; }
        std::size_t p = k; int w = all[k].wedge;
        while (k < all.size() && all[k].wedge == w) k++;
        std::size_t q = k;                          // span [p, q)
        int start_v = p > 0 ? level[p - 1] : level[p];
        int end_v;
        if (q < all.size() && !all[q].dynamic.empty()) end_v = dyn_vel(all[q].dynamic);
        else end_v = clamp_vel(start_v + (w > 0 ? 28 : -28));
        int span = static_cast<int>(q - p);
        for (std::size_t t = p; t < q; t++) {
            double f = span > 1 ? static_cast<double>(t - p) / (span - 1) : 1.0;
            all[t].vel = clamp_vel(static_cast<int>(start_v + (end_v - start_v) * f + 0.5));
        }
    }
}

// render one measure's notes to a bar string: pedal segments (a `(… |> sustain)`
// wraps pedalled runs), dynamics inside each. Wedge velocities are already baked.
std::string render_bar(const Score& s, const std::vector<Note>& notes) {
    if (notes.empty()) return "r1";
    std::vector<std::pair<bool, std::vector<Note>>> segs;
    for (const Note& nt : notes) {
        bool p = nt.rest ? (segs.empty() ? false : segs.back().first) : nt.pedaled;
        if (segs.empty() || p != segs.back().first) segs.push_back({p, {}});
        segs.back().second.push_back(nt);
    }
    std::string out;
    for (std::size_t k = 0; k < segs.size(); k++) {
        // dynamics are not wrapped here — they're hoisted to whole same-dynamic
        // regions at voice assembly, so the mark appears once per region, not per bar
        std::string run = render_octave(s, segs[k].second);
        if (segs[k].first) run = "(" + run + " |> sustain)";
        if (k) out += " :+: ";
        out += run;
    }
    return out.empty() ? "r1" : out;
}

// render a whole voice to one bar string per played measure (the playorder). Wedges
// ramp across the entire voice (so a hairpin spanning bars stays continuous), then
// each bar is rendered independently.
std::vector<std::string> render_voice(const Score& s, const Line& measures,
        const std::vector<int>& playorder, const std::vector<int>& target, int mdiv,
        const std::vector<std::string>& dyn_at) {
    std::vector<std::vector<Note>> bars;
    for (int m : playorder) {
        // always reconstruct — a measure this voice doesn't have becomes a full-bar
        // rest (sized to the meter), so an empty bar isn't a stray whole note.
        std::vector<Note> src;
        if (m >= 0 && m < static_cast<int>(measures.size())) src = measures[m];
        int bar = (m >= 0 && m < static_cast<int>(target.size())) ? target[m] : mdiv;
        std::vector<Note> r = reconstruct(s, src, bar);
        const std::string& d = (m >= 0 && m < static_cast<int>(dyn_at.size())) ? dyn_at[m] : std::string();
        if (!d.empty()) for (Note& nt : r) nt.dynamic = d;
        bars.push_back(std::move(r));
    }
    std::vector<Note> all;
    std::vector<std::pair<std::size_t, std::size_t>> rng;
    for (const std::vector<Note>& b : bars) {
        std::size_t st = all.size();
        for (const Note& nt : b) all.push_back(nt);
        rng.push_back({st, all.size()});
    }
    apply_wedges(all);
    for (std::size_t bi = 0; bi < bars.size(); bi++) {
        std::size_t st = rng[bi].first;
        for (std::size_t k = 0; k < bars[bi].size(); k++) bars[bi][k].vel = all[st + k].vel;
    }
    std::vector<std::string> out;
    for (const std::vector<Note>& b : bars) out.push_back(render_bar(s, b));
    return out;
}

// is a div-span exactly one of our notatable durations (plain or single-dotted)?
// Only then can a barred measure be guaranteed to sum to the meter.
bool exact_dur(const Score& s, int divs) {
    int whole = 4 * s.divisions;
    if (divs <= 0) return false;
    if (whole % divs == 0) return true;
    if ((3 * whole) % (2 * divs) == 0) return true;
    return false;
}

// is a reconstructed measure barline-safe — every duration (notes + gap rests)
// exactly notatable, the timeline summing to exactly one bar, and no rolled chord
// (an arpeggio restaggers durations through rounding, so its bar can overflow)?
bool bar_exact(const Score& s, const std::vector<Note>& meas, int barlen) {
    for (const Note& nt : meas) if (nt.rolled) return false;
    std::vector<Note> r = reconstruct(s, meas, barlen);
    int sum = 0;
    for (const Note& nt : r) {
        if (!exact_dur(s, nt.dur_div)) return false;
        if (!nt.chord) sum += nt.dur_div;     // chord members share a slot
    }
    return sum == barlen;
}

// the score is safe to spell under its key iff every note's accidental already
// matches the key signature — then bare letters + a single `inKey` are faithful.
bool key_safe(const Score& s) {
    if (!s.has_key || s.fifths == 0) return false;
    for (const std::pair<const std::pair<int, int>, Line>& e : s.lines)
        for (const std::vector<Note>& meas : e.second)
            for (const Note& nt : meas)
                if (!nt.rest && nt.alter != key_acc(s.fifths, nt.letter)) return false;
    return true;
}

// expand repeats / voltas into the order measures are actually played (0-based
// indices). A |: … :| section replays; an ending number N plays only on pass N.
std::vector<int> compute_playorder(const Score& s, int N) {
    std::vector<int> order;
    if (N <= 0) return order;
    std::map<int, int> plays;            // section start (1-based) -> passes done
    int i = 1, section = 1, guard = 0;
    while (i <= N && guard++ < N * 64 + 1000) {
        if (s.repeat_forward.count(i)) section = i;
        int pass = plays[section] + 1;   // the pass we are about to play
        std::map<int, int>::const_iterator vs = s.volta_start.find(i);
        if (vs != s.volta_start.end() && vs->second != pass) {
            int j = i;                   // wrong ending for this pass: skip past its stop
            while (j <= N && !s.volta_stop.count(j)) j++;
            i = j + 1;
            continue;
        }
        order.push_back(i - 1);
        std::map<int, int>::const_iterator rb = s.repeat_backward.find(i);
        if (rb != s.repeat_backward.end()) {
            plays[section] += 1;
            int total = rb->second < 2 ? 2 : rb->second;
            if (plays[section] < total) { i = section; continue; }
        }
        i++;
    }
    return order;
}

std::string key_desc(int fifths) {
    if (fifths == 0) return "no sharps or flats";
    int n = fifths < 0 ? -fifths : fifths;
    return std::to_string(n) + (fifths > 0 ? " sharp" : " flat") + (n == 1 ? "" : "s");
}

std::string render(const Score& s) {
    std::string out = "-- transcribed from MusicXML by calliope\n";
    if (s.has_key) out += "-- key signature: " + key_desc(s.fifths) + "\n";
    out += "#load \"prelude\"\n\n";

    int max_measures = 0;
    for (const std::pair<const std::pair<int, int>, Line>& e : s.lines)
        max_measures = std::max(max_measures, static_cast<int>(e.second.size()));

    // Size each measure from its actual span (longest voice's last note) — robust to
    // mid-piece meter changes; the parsed meter is the fallback for an empty measure.
    int parsed = measure_divs(s);
    std::vector<int> target(max_measures, parsed);
    for (int m = 0; m < max_measures; m++) {
        int span = 0;
        for (const std::pair<const std::pair<int, int>, Line>& e : s.lines)
            if (m < static_cast<int>(e.second.size()))
                for (const Note& nt : e.second[m]) span = std::max(span, nt.start_div + nt.dur_div);
        if (span > 0) target[m] = span;
    }

    // part-wide dynamics: forward-fill each measure's dynamic across every voice
    std::vector<std::string> dyn_at(max_measures);
    std::string cur_dyn;
    for (int m = 0; m < max_measures; m++) {
        std::map<int, std::string>::const_iterator it = s.measure_dyn.find(m + 1);
        if (it != s.measure_dyn.end()) cur_dyn = it->second;
        dyn_at[m] = cur_dyn;
    }

    // hoist a globally-constant dynamic: if every measure carries the same one, apply
    // it once at the end (`… |> piano`) instead of wrapping every bar with it.
    std::string global_dyn;
    if (!dyn_at.empty()) {
        bool all_same = !dyn_at[0].empty();
        for (int m = 0; all_same && m < max_measures; m++)
            if (dyn_at[m] != dyn_at[0]) all_same = false;
        if (all_same) {
            global_dyn = dyn_at[0];
            for (int m = 0; m < max_measures; m++) dyn_at[m].clear();
        }
    }

    g_fifths = s.fifths;
    g_bare = key_safe(s);
    std::vector<int> playorder = compute_playorder(s, max_measures);

    // `|` barlines are validated at compile time, so emit them only when every played
    // measure provably fills exactly one bar (meter known, no pickup, conforming span,
    // exactly notatable durations); otherwise fall back to `:+:` (no validation).
    int mdiv = measure_divs(s);
    static const std::vector<Note> empty_measure;
    bool use_bars = s.has_time && mdiv > 0;
    if (use_bars)
        for (int m : playorder) {
            if (m < static_cast<int>(s.implicit.size()) && s.implicit[m]) { use_bars = false; break; }
            if (m >= static_cast<int>(target.size()) || target[m] != mdiv) { use_bars = false; break; }
            // every voice's version of this measure must fill exactly one bar with
            // exactly notatable durations (gap rests included), else `|` would reject
            for (const std::pair<const std::pair<int, int>, Line>& e : s.lines) {
                const std::vector<Note>& meas =
                    m < static_cast<int>(e.second.size()) ? e.second[m] : empty_measure;
                if (!bar_exact(s, meas, mdiv)) { use_bars = false; break; }
            }
            if (!use_bars) break;
        }

    // render every voice to its bar strings, keeping each line's (part,voice) key so
    // it can be named by which hand/staff it belongs to
    std::vector<std::vector<std::string>> voices;
    std::vector<std::pair<int, int>> keys;
    for (const std::pair<const std::pair<int, int>, Line>& e : s.lines) {
        keys.push_back(e.first);
        voices.push_back(render_voice(s, e.second, playorder, target, mdiv, dyn_at));
    }
    int nparts = 0;
    if (!keys.empty()) {
        std::set<int> ps;
        for (const std::pair<int, int>& k : keys) ps.insert(k.first);
        nparts = static_cast<int>(ps.size());
    }

    // factor repeated bars: a bar string occurring >= 2 times (across all voices) is
    // named once and referenced — so a repeated section collapses to a name reused.
    std::map<std::string, int> freq;
    for (const std::vector<std::string>& v : voices)
        for (const std::string& b : v) freq[b]++;
    auto trivial = [](const std::string& b) {
        return !b.empty() && b[0] == 'r' && b.find(' ') == std::string::npos;
    };
    std::map<std::string, std::string> name_of;
    int bn = 0;
    for (const std::vector<std::string>& v : voices)
        for (const std::string& b : v)
            if (freq[b] >= 2 && !trivial(b) && !name_of.count(b)) {
                std::string nm = "m" + std::to_string(++bn);  // 'm'=measure (not a pitch letter)
                name_of[b] = nm;
                out += nm + " = " + b + "\n";
            }
    if (bn > 0) out += "\n";

    // name each line by its hand/staff so a grand-staff piano reads right- vs
    // left-hand (staff 1 = top/RH, 2 = bottom/LH); a single staff stays `line`,
    // multiple parts get a `partN_` prefix. Duplicate bases get a numeric suffix.
    std::vector<std::string> names, line_comment;
    std::map<std::string, int> used;
    for (std::size_t i = 0; i < voices.size(); i++) {
        int part = keys[i].first;
        int staff = s.line_staff.count(keys[i]) ? s.line_staff.at(keys[i]) : 1;
        int nstaves = s.part_staves.count(part) ? static_cast<int>(s.part_staves.at(part).size()) : 1;
        std::string base, note;
        if (nstaves >= 2) {
            if (staff == 1)      { base = "rightHand"; note = "right hand"; }
            else if (staff == 2) { base = "leftHand";  note = "left hand"; }
            else { base = "staff" + std::to_string(staff); note = "staff " + std::to_string(staff); }
        } else {
            base = "line"; note = "";
        }
        if (nparts > 1) { base = "part" + std::to_string(part) + "_" + base;
                          note = (note.empty() ? "" : note + ", ") + "part " + std::to_string(part); }
        int c = ++used[base];
        names.push_back(c == 1 ? base : base + std::to_string(c));
        line_comment.push_back(note);
    }

    // the dynamic active at each played entry (part-wide, so shared across voices) —
    // consecutive equal-dynamic bars become one `(region |> piano)` group, so the
    // mark shows once per region instead of once per bar.
    std::vector<std::string> entry_dyn(playorder.size());
    for (std::size_t i = 0; i < playorder.size(); i++) {
        int m = playorder[i];
        entry_dyn[i] = (m >= 0 && m < static_cast<int>(dyn_at.size())) ? dyn_at[m] : std::string();
    }

    // each voice: its bars joined by `|` (or `:+:` when unsafe), packed several to a
    // line and wrapped at a column width (one bar per line is too many for a long
    // piece). A wrap leads with the operator on the indented next line.
    std::string sep  = use_bars ? " | "    : " :+: ";
    std::string wrap = use_bars ? "\n    | " : "\n    :+: ";
    const std::size_t MAXCOL = 92;
    for (std::size_t i = 0; i < voices.size(); i++) {
        const std::vector<std::string>& v = voices[i];
        const std::string& nm = names[i];
        if (!line_comment[i].empty()) out += "-- " + line_comment[i] + "\n";
        out += nm + " =\n  ";
        if (v.empty()) { out += "r1\n\n"; continue; }
        std::size_t col = 2;
        bool first = true;
        auto emit = [&](const std::string& piece) {
            if (first) { out += piece; col += piece.size(); first = false; }
            else if (col + sep.size() + piece.size() > MAXCOL) { out += wrap + piece; col = 4 + piece.size(); }
            else { out += sep + piece; col += sep.size() + piece.size(); }
        };
        std::size_t k = 0;
        while (k < v.size()) {
            // extent of the current dynamic region
            std::string dyn = k < entry_dyn.size() ? entry_dyn[k] : std::string();
            std::size_t g = k + 1;
            while (g < v.size() && (g < entry_dyn.size() ? entry_dyn[g] : std::string()) == dyn) g++;
            // collapse runs of identical named bars within the region into `cell :*: n`
            std::vector<std::string> pieces;
            for (std::size_t kk = k; kk < g;) {
                std::map<std::string, std::string>::const_iterator it = name_of.find(v[kk]);
                bool named = it != name_of.end();
                std::string cell = named ? it->second : v[kk];
                // collapse a run of identical single-token cells into `cell :*: n` — a
                // name (`m5`) or a bare rest/pitch (`r2.`), but not a multi-token run or
                // a parenthesised expression (`:*:` would misbind). Not when barlined
                // (`:*:` drops the internal `|` the meter validation needs).
                bool simple = cell.find(' ') == std::string::npos && cell.find('(') == std::string::npos;
                std::size_t r = kk + 1;
                if (simple && !use_bars) while (r < g && v[r] == v[kk]) r++;
                int n = static_cast<int>(r - kk);
                pieces.push_back(n >= 2 ? cell + " :*: " + std::to_string(n) : cell);
                kk = r;
            }
            // wrap the whole region in one `(… |> dyn)` (the open paren rides the first
            // piece, the `|> dyn)` the last) — pipe is loosest, so the region's `|` /
            // `:+:` bind inside the parens
            for (std::size_t pi = 0; pi < pieces.size(); pi++) {
                std::string p = pieces[pi];
                if (!dyn.empty() && pi == 0) p = "(" + p;
                if (!dyn.empty() && pi + 1 == pieces.size()) p += " |> " + dyn + ")";
                emit(p);
            }
            k = g;
        }
        out += "\n\n";
    }

    // compose the voices, then pipe through the score-wide context
    std::string music = "(" + names[0];
    for (std::size_t k = 1; k < names.size(); k++) music += " :=: " + names[k];
    music += ")";
    if (!global_dyn.empty()) music += " |> " + global_dyn;
    if (s.has_tempo) music += " |> tempo " + std::to_string(s.tempo);
    if (s.has_time)  music += " |> meter " + std::to_string(s.beats) + " " + std::to_string(s.beat_type);
    if (g_bare)      music += " |> inKey " + std::to_string(s.fifths);

    out += "main = " + music + "\n";
    return out;
}

// Warn (to stderr) about element tags we saw but do not transcribe.
void report_unsupported(const std::set<std::string>& seen) {
    static const std::set<std::string> handled = {
        "part", "measure", "note", "pitch", "rest", "chord", "dot", "type",
        "duration", "step", "alter", "octave", "divisions", "fifths", "beats",
        "beat-type", "sound", "voice", "backup", "forward",
        "tie", "tied", "time-modification", "actual-notes", "normal-notes", "tuplet",
        "notations", "articulations", "staccato", "accent", "tenuto", "strong-accent",
        "ornaments", "trill-mark", "mordent", "inverted-mordent", "turn", "inverted-turn",
        "direction", "direction-type", "dynamics", "metronome", "beat-unit", "beat-unit-dot", "per-minute",
        "f", "p", "mf", "mp", "ff", "pp", "fff", "ppp",
        "pppp", "ppppp", "pppppp", "ffff", "fffff", "ffffff", "n",
        "sf", "sfz", "sffz", "fz", "sforzando", "sforzato",
        "rf", "rfz", "fp", "pf", "sfp", "sfpp", "sfzp",
        "grace", "pedal", "octave-shift", "wedge", "arpeggiate",
        "barline", "bar-style", "repeat", "ending",
    };
    static const std::set<std::string> benign = {
        "score-partwise", "score-timewise", "part-list", "part-group", "score-part",
        "part-name", "part-abbreviation", "part-name-display", "part-abbreviation-display",
        "display-text", "score-instrument", "instrument-name", "instrument-sound",
        "virtual-instrument", "virtual-library", "virtual-name", "midi-device",
        "midi-instrument", "midi-channel", "midi-program", "midi-unpitched", "volume",
        "pan", "attributes", "key", "time", "clef", "sign", "staves", "staff-details",
        "work", "work-title", "work-number", "movement-title", "movement-number",
        "identification", "creator", "rights", "encoding", "software", "encoding-date",
        "encoder", "source", "supports", "defaults", "scaling", "millimeters", "tenths",
        "concert-score", "page-layout", "page-height", "page-width", "page-margins",
        "left-margin", "right-margin", "top-margin", "bottom-margin", "system-layout",
        "system-margins", "system-distance", "top-system-distance", "staff-layout",
        "staff-distance", "appearance", "line-width", "note-size", "distance", "glyph",
        "credit", "credit-words", "credit-type", "credit-symbol", "print",
        "system-dividers", "measure-numbering", "miscellaneous", "miscellaneous-field",
        "group", "group-symbol", "group-barline", "group-name", "barline", "bar-style",
        "accidental", "stem", "beam", "notehead", "staff", "line", "key-step", "key-alter",
        "grace", "normal-type", "display-step", "display-octave", "mode",
    };
    std::vector<std::string> un;
    for (const std::string& t : seen)
        if (handled.find(t) == handled.end() && benign.find(t) == benign.end())
            un.push_back(t);
    if (un.empty()) return;
    std::fprintf(stderr, "transcribe: ignored unsupported tags (musical detail lost):");
    for (const std::string& t : un) std::fprintf(stderr, " %s", t.c_str());
    std::fprintf(stderr, "\n");
}

} // namespace

bool musicxml_to_calliope(const std::string& input_path, std::string& out, std::string& err) {
    std::vector<char> bytes = read_bytes(input_path);
    if (bytes.empty()) { err = "cannot read '" + input_path + "'"; return false; }

    std::string xml;
    if (ends_with(lower(input_path), ".mxl")) {
        if (!unzip_score(bytes, xml, err)) return false;
    } else {
        xml.assign(bytes.data(), bytes.size());
    }

    Score score;
    if (!parse_xml(xml, score, err)) return false;
    out = render(score);
    report_unsupported(score.seen);
    return true;
}

} // namespace calliope::transcribe
