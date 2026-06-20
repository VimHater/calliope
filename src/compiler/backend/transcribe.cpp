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
    int mm_per_minute = 0;
    bool note_is_grace = false, pedal_down = false, have_grace = false;
    int grace_voice = 0;
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
            else if (tag_is("note"))      { in_note = true; note = Note{}; note_is_grace = false; }
            else if (tag_is("pitch"))     { in_pitch = true; }
            else if (in_note && tag_is("grace")) { note_is_grace = true; }
            else if (tag_is("backup"))    { dur_target = 1; }
            else if (tag_is("forward"))   { dur_target = 2; }
            else if (tag_is("dynamics"))  { in_dynamics = true; }
            else if (tag_is("metronome")) { in_metronome = true; mm_beat_unit.clear(); mm_per_minute = 0; }
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
            }
        } else if (code == HOXML_ELEMENT_END) {
            std::string content = trim(ctx.content);
            if (in_note && tag_is("rest"))  note.rest = true;
            else if (in_note && tag_is("chord")) note.chord = true;
            else if (in_note && tag_is("voice")) voice = std::atoi(content.c_str());
            else if (in_note && tag_is("duration")) note.dur_div = std::atoi(content.c_str());
            else if (in_note && (tag_is("staccato") || tag_is("accent") || tag_is("tenuto") ||
                                 tag_is("strong-accent")))
                note.artic = tag_is("strong-accent") ? "marcato" : std::string(ctx.tag);
            else if (in_note && (tag_is("trill-mark") || tag_is("mordent") ||
                                 tag_is("inverted-mordent") || tag_is("turn") || tag_is("inverted-turn")))
                note.ornament = has(ctx.tag, "mordent") ? "mordent" : has(ctx.tag, "turn") ? "turn" : "trill";
            else if (in_pitch && tag_is("step") && !content.empty())
                note.letter = static_cast<char>(std::tolower(static_cast<unsigned char>(content[0])));
            else if (in_pitch && tag_is("alter"))  note.alter = std::atoi(content.c_str());
            else if (in_pitch && tag_is("octave")) note.octave = std::atoi(content.c_str());
            else if (in_dynamics && !dyn_name(ctx.tag).empty()) {
                // a dynamic is part-wide: snap it to this measure for every voice
                if (measure_index >= 1) score.measure_dyn[measure_index] = dyn_name(ctx.tag);
            }
            else if (in_metronome && tag_is("beat-unit"))  mm_beat_unit = lower(content);
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
            // structural closes
            else if (tag_is("pitch"))    in_pitch = false;
            else if (tag_is("dynamics")) in_dynamics = false;
            else if (tag_is("metronome")) {
                int d = denom_of_type(mm_beat_unit);
                if (d > 0 && mm_per_minute > 0 && !score.has_tempo) {
                    score.has_tempo = true; score.tempo = mm_per_minute * 4 / d;
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
                    if (have_grace && grace_voice == voice && !note.chord && !note.rest) {
                        note.has_grace = true;
                        note.g_letter = grace_note.letter;
                        note.g_alter = grace_note.alter;
                        note.g_octave = grace_note.octave;
                        have_grace = false;
                    }
                    push_note(note);
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

std::string pitch_of(char letter, int alter, int octave) {
    std::string s(1, letter);
    for (int k = 0; k < alter; k++)  s += "is";
    for (int k = 0; k < -alter; k++) s += "es";
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
    std::size_t i = 0;
    while (i < n.size()) {
        std::size_t j = i + 1;
        while (j < n.size() && n[j].chord) j++;

        // tie chain (single notes only): `~`-join consecutive same-pitch notes
        if (n[i].tie_start && j - i == 1 && j < n.size() && (j + 1 >= n.size() || !n[j + 1].chord) &&
            same_pitch(i, j)) {
            flush();
            std::string tok = literal(s, n, i, j);
            std::size_t k = j;
            while (k < n.size()) {
                std::size_t kj = k + 1;
                tok += " ~ " + literal(s, n, k, kj);
                bool cont = n[k].tie_start && kj < n.size() &&
                            (kj + 1 >= n.size() || !n[kj + 1].chord) && same_pitch(k, kj);
                k = kj;
                if (!cont) break;
            }
            tokens.push_back(tok);
            i = k;
            continue;
        }

        std::string lit = literal(s, n, i, j);
        bool single = (j - i == 1) && !n[i].rest;
        if (single && n[i].has_grace) {
            // a grace note before the host -> acciaccatura grace host
            std::string g = pitch_of(n[i].g_letter, n[i].g_alter, n[i].g_octave);
            flush(); tokens.push_back("acciaccatura " + g + " (" + lit + ")");
        } else if (single && !n[i].ornament.empty()) {
            std::string t = n[i].ornament + " (" + lit + ")";
            if (!n[i].artic.empty()) t = n[i].artic + " (" + t + ")";
            flush(); tokens.push_back(t);
        } else if (!n[i].rest && !n[i].artic.empty()) {
            flush(); tokens.push_back(n[i].artic + " (" + lit + ")");
        } else {
            plain.push_back(lit);
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

// split a note list into dynamic segments and wrap each (forte (...), piano (...))
std::string render_dynamics(const Score& s, const std::vector<Note>& notes) {
    std::vector<std::pair<std::string, std::vector<Note>>> segs;
    for (const Note& nt : notes) {
        if (segs.empty() || (!nt.dynamic.empty() && nt.dynamic != segs.back().first))
            segs.push_back({nt.dynamic, {}});
        segs.back().second.push_back(nt);
    }
    std::string out;
    for (std::size_t k = 0; k < segs.size(); k++) {
        std::string run = render_run(s, segs[k].second);
        if (!segs[k].first.empty()) run = segs[k].first + " (" + run + ")";
        if (k) out += " :+: ";
        out += run;
    }
    return out.empty() ? "r4" : out;
}

// a whole voice: flatten its reconstructed measures (each padded to its bar target),
// split into pedal segments (a `sustain (...)` wraps the pedalled runs), and render
// the dynamics inside each.
std::string render_line(const Score& s, const Line& measures, const std::vector<int>& target,
                        const std::vector<std::string>& dyn_at) {
    std::vector<Note> all;
    for (std::size_t m = 0; m < measures.size(); m++) {
        int bar = m < target.size() ? target[m] : 0;
        std::vector<Note> r = reconstruct(s, measures[m], bar);
        const std::string& d = m < dyn_at.size() ? dyn_at[m] : std::string();
        for (Note nt : r) { if (!d.empty()) nt.dynamic = d; all.push_back(nt); }
    }
    // pedal segments: a rest continues the current segment (don't break a pedal)
    std::vector<std::pair<bool, std::vector<Note>>> segs;
    for (const Note& nt : all) {
        bool p = nt.rest ? (segs.empty() ? false : segs.back().first) : nt.pedaled;
        if (segs.empty() || p != segs.back().first) segs.push_back({p, {}});
        segs.back().second.push_back(nt);
    }
    std::string out;
    for (std::size_t k = 0; k < segs.size(); k++) {
        std::string run = render_dynamics(s, segs[k].second);
        if (segs[k].first) run = "sustain (" + run + ")";
        if (k) out += " :+: ";
        out += run;
    }
    return out.empty() ? "r4" : out;
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

    // per-measure bar length (divisions): the parsed meter, except a pickup
    // (implicit) or an overshooting bar sizes from the actual span — so padding
    // never shifts the following notes, and voices stay aligned.
    int max_measures = 0;
    for (const std::pair<const std::pair<int, int>, Line>& e : s.lines)
        max_measures = std::max(max_measures, static_cast<int>(e.second.size()));
    // Size each measure from its actual span (the longest voice's last note) rather
    // than a single parsed meter — robust to mid-piece meter changes (a 2/4 theme
    // and a 3/4 variation each get the right bar length). The parsed meter is only a
    // fallback for a measure no voice fills.
    int parsed = measure_divs(s);
    std::vector<int> target(max_measures, parsed);
    for (int m = 0; m < max_measures; m++) {
        int span = 0;
        for (const std::pair<const std::pair<int, int>, Line>& e : s.lines)
            if (m < static_cast<int>(e.second.size()))
                for (const Note& nt : e.second[m]) span = std::max(span, nt.start_div + nt.dur_div);
        if (span > 0) target[m] = span;
    }

    // part-wide dynamics: forward-fill each measure's dynamic so every voice picks
    // up the same level (a `pp` lowers both hands, like an engraver intends)
    std::vector<std::string> dyn_at(max_measures);
    std::string cur_dyn;
    for (int m = 0; m < max_measures; m++) {
        std::map<int, std::string>::const_iterator it = s.measure_dyn.find(m + 1);
        if (it != s.measure_dyn.end()) cur_dyn = it->second;
        dyn_at[m] = cur_dyn;
    }

    // each (part, voice) is one simultaneous line; barlines are omitted (an
    // approximated duration can still make a measure not sum to a whole bar)
    std::vector<std::string> names;
    int idx = 0;
    for (const std::pair<const std::pair<int, int>, Line>& e : s.lines) {
        std::string name = "line" + std::to_string(++idx);
        names.push_back(name);
        out += name + " =\n  " + render_line(s, e.second, target, dyn_at) + "\n\n";
    }

    std::string music = names[0];
    for (std::size_t k = 1; k < names.size(); k++) music += " :=: " + names[k];
    if (s.has_tempo) music = "tempo " + std::to_string(s.tempo) + " (" + music + ")";
    if (s.has_time)  music = "meter " + std::to_string(s.beats) + " " +
                             std::to_string(s.beat_type) + " (" + music + ")";

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
        "direction", "direction-type", "dynamics", "metronome", "beat-unit", "per-minute",
        "f", "p", "mf", "mp", "ff", "pp", "fff", "ppp",
        "pppp", "ppppp", "pppppp", "ffff", "fffff", "ffffff", "n",
        "sf", "sfz", "sffz", "fz", "sforzando", "sforzato",
        "rf", "rfz", "fp", "pf", "sfp", "sfpp", "sfzp",
        "grace", "pedal",
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
