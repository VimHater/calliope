// calliopev — the Calliope live visualizer (a separate binary).
//
// Reuses the calliopei/compiler API: it compiles a .cal with `driver::compile`,
// flattens `main` to absolute-timed notes with `backend::flatten`, plays the piece
// through the non-blocking `backend::play_start` handle, and draws a raylib window
// in sync with the audio playhead (polled via `playback_seconds`).
//
//   calliopev song.cal               # piano-roll (DAW) view
//   calliopev song.cal --mode piano  # piano-keyboard view
//
// Controls (Space = pause, Tab = toggle view, Esc = quit; window resizable):
//   DAW   — H/L (←/→) seek, J/K (↓/↑) time-zoom, [ ] note size
//   Piano — J/K (↓/↑) seek, H/L (←/→) scroll pitch, [ ] scale the pitch range
//           (the keyboard is a movable/resizable window, not the fixed 88 keys)
//
// This is the *live* view. Rendering a visualization to a video file is a separate,
// later concern — see backend/visualize.hpp for that (intentionally still a stub).

#include "core/driver.hpp"
#include "backend/score.hpp"
#include "backend/audio.hpp"
#include "helper.hpp"

#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using calliope::backend::TimedNote;

enum class Mode { Daw, Piano };

double secs(const calliope::Rational& r) {
    return static_cast<double>(r.num) / static_cast<double>(r.den);
}

// A note prepared for drawing (seconds + a per-instrument lane/color index).
struct VizNote {
    double start, end;
    int key;       // MIDI key
    int lane;      // color index
};

// One voice (a top-level `:=:` branch of the piece) -> its own DAW lane. Rows are the
// *distinct pitches the voice actually uses* (sorted), not absolute MIDI keys — so the
// lane packs densely (a note's row is its rank, not its true pitch height; the actual
// pitch is on the label).
struct Voice {
    std::vector<VizNote> notes;
    std::vector<int> pitches;   // sorted distinct keys -> row index
    std::string label;          // lane label (the line/staff name, e.g. "rightHand")
};

// One color per instrument lane (cycled). Distinct, readable on a dark background.
Color lane_color(int lane) {
    static const Color palette[] = {
        {122, 162, 247, 255}, {158, 206, 106, 255}, {224, 175, 104, 255},
        {187, 154, 247, 255}, {125, 207, 255, 255}, {247, 118, 142, 255},
        {115, 218, 202, 255}, {224, 124, 175, 255},
    };
    int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    return palette[((lane % n) + n) % n];
}

bool sounding(const VizNote& v, double t) { return t >= v.start && t < v.end; }

std::string note_name(int key) {
    static const char* n[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int pc = ((key % 12) + 12) % 12;
    int oct = key / 12 - 1;
    return std::string(n[pc]) + std::to_string(oct);
}

// The names of the top-level `:=:` operands in `main`, in spine order — i.e. the line
// names (`rightHand`, `leftHand2`, …) that `flatten_voices`'s branches correspond to.
// Walks `main`'s body: down the `|>` pipe chain to the `:=:` spine, collecting each
// operand's variable name ("" for a non-variable operand). Used to group lanes by staff.
std::vector<std::string> voice_names(const calliope::ast::Ast& ast) {
    using namespace calliope::ast;
    std::vector<std::string> names;
    if (ast.root == NoNode) return names;
    NodeId body = NoNode;
    for (NodeId d : ast.nodes[ast.root].kids) {
        const Node& n = ast.nodes[d];
        if (n.kind == NodeKind::Binding && n.tok.text == "main") {
            if (static_cast<int>(n.kids.size()) > n.extra) body = n.kids[n.extra];  // skip params
            break;
        }
    }
    if (body == NoNode) return names;
    auto is_op = [&](NodeId id, const char* op) {
        const Node& x = ast.nodes[id];
        return x.kind == NodeKind::BinOp && x.tok.text == op;
    };
    auto name_of = [&](NodeId id) {
        const Node& x = ast.nodes[id];
        return x.kind == NodeKind::Var ? std::string(x.tok.text) : std::string();
    };
    NodeId node = body;
    while (is_op(node, "|>")) node = ast.nodes[node].kids[0];   // leftmost of the pipe chain
    while (is_op(node, ":=:")) {                                 // the parallel spine
        names.push_back(name_of(ast.nodes[node].kids[0]));
        node = ast.nodes[node].kids[1];
    }
    names.push_back(name_of(node));
    return names;
}

// A line name with its trailing digits stripped: "rightHand2" -> "rightHand" (the
// staff/hand it belongs to). "" stays "".
std::string staff_of(const std::string& name) {
    std::size_t e = name.size();
    while (e > 0 && name[e - 1] >= '0' && name[e - 1] <= '9') e--;
    return name.substr(0, e);
}

// ---- DAW: one horizontal lane per voice (a top-level `:=:` branch). A lane's height
// is *proportional to the rows it needs* (its distinct-pitch count), so every pitch
// row is the same height across the whole view — a voice using few pitches gets a thin
// lane, a dense one gets a tall lane. Time scrolls under a fixed playhead; `scale`
// (<=1) shrinks the bars within their row.
void draw_daw(const std::vector<Voice>& voices, double t, int W, int H,
              double px_per_sec, double scale, const Font& font) {
    const double playhead_x = W * 0.28;
    int V = static_cast<int>(voices.size());
    if (V == 0) return;
    const double view_top = 20.0, view_h = H - 40.0;
    int total_rows = 0;
    for (const Voice& vc : voices) total_rows += vc.pitches.empty() ? 1 : (int)vc.pitches.size();
    double rowH = view_h / total_rows;                 // one uniform row height everywhere
    auto x_of = [&](double time) { return playhead_x + (time - t) * px_per_sec; };

    double lt = view_top;
    for (int vi = 0; vi < V; vi++) {
        const Voice& vc = voices[vi];
        int rows = vc.pitches.empty() ? 1 : (int)vc.pitches.size();
        double laneH = rows * rowH;
        if (vi % 2) DrawRectangle(0, (int)lt, W, (int)laneH, {255, 255, 255, 8});  // zebra
        DrawLine(0, (int)lt, W, (int)lt, {255, 255, 255, 30});                     // lane edge

        double h = (rowH - 1) * scale; if (h < 2) h = 2;
        double pad = (rowH - h) / 2;                       // center the bar in its row
        double lane_lt = lt;
        // a note's row is its pitch *rank* in this voice (dense), high pitch on top
        auto y_of = [&](int key) {
            int rank = static_cast<int>(
                std::lower_bound(vc.pitches.begin(), vc.pitches.end(), key) - vc.pitches.begin());
            return lane_lt + (rows - 1 - rank) * rowH + pad;
        };

        for (const VizNote& v : vc.notes) {
            double x = x_of(v.start), w = (v.end - v.start) * px_per_sec, y = y_of(v.key);
            if (x + w < 0 || x > W) continue;
            Color c = lane_color(v.lane);
            if (!sounding(v, t)) c = Fade(c, 0.55f);
            DrawRectangleRounded({(float)x, (float)y, (float)(w < 2 ? 2 : w), (float)h}, 0.4f, 4, c);
            if (h >= 11 && w >= 26) {                          // note-name label, fit to the bar
                float fs = (float)(h - 4); if (fs > 40) fs = 40;
                std::string nm = note_name(v.key);
                float tw = MeasureTextEx(font, nm.c_str(), fs, 0.5f).x;
                while (fs > 8 && tw > w - 6) { fs -= 1; tw = MeasureTextEx(font, nm.c_str(), fs, 0.5f).x; }
                DrawTextEx(font, nm.c_str(), {(float)x + 4, (float)y + (float)(h - fs) / 2}, fs, 0.5f,
                           {20, 20, 28, 235});
            }
        }
        lt += laneH;
    }
    DrawLine((int)playhead_x, 0, (int)playhead_x, H, {255, 255, 255, 180});
}

// ---- piano view: a small keyboard along the bottom, with notes falling from the
// top (Synthesia-style) and landing on their key as they sound.
void draw_piano(const std::vector<VizNote>& notes, double t, int W, int H, const Font& font,
                int K_LO, int K_HI) {
    const double L = 4.0;                   // seconds of lookahead shown above the keys
    auto is_black = [](int k) { int n = ((k % 12) + 12) % 12; return n==1||n==3||n==6||n==8||n==10; };

    int whites = 0;
    for (int k = K_LO; k <= K_HI; k++) if (!is_black(k)) whites++;
    if (whites < 1) whites = 1;
    double ww = static_cast<double>(W) / whites;
    double kb_h = H * 0.16;                  // a slim keyboard
    double kb_top = H - kb_h;
    double roll_h = kb_top;                  // the falling-note area above it
    double bw = ww * 0.62, bh = kb_h * 0.62;

    // white-key left edges (over the visible range), and a key -> {x, width} resolver
    // (black keys straddle the boundary above the white key just below them).
    std::vector<double> wx(128, -1);
    { double x = 0; for (int k = K_LO; k <= K_HI; k++) if (k >= 0 && k < 128 && !is_black(k)) { wx[k] = x; x += ww; } }
    auto key_box = [&](int k) -> std::pair<double, double> {
        if (!is_black(k)) return {wx[k], ww};
        int below = k - 1; while (below >= K_LO && is_black(below)) below--;
        double cx = (below >= 0 && below < 128 && wx[below] >= 0) ? wx[below] + ww : 0;
        return {cx - bw / 2, bw};
    };

    // which keys sound now, and in what color
    std::vector<Color> lit(128, {0, 0, 0, 0});
    std::vector<bool> on(128, false);
    for (const VizNote& v : notes)
        if (sounding(v, t) && v.key >= 0 && v.key < 128) { on[v.key] = true; lit[v.key] = lane_color(v.lane); }

    // falling notes (behind the keyboard): time flows downward, onset lands at kb_top
    for (const VizNote& v : notes) {
        if (v.start > t + L || v.end < t || v.key < K_LO || v.key > K_HI) continue;
        std::pair<double, double> box = key_box(v.key);
        double bot = kb_top - (v.start - t) / L * roll_h;   // leading edge (onset)
        double top = kb_top - (v.end - t) / L * roll_h;     // trailing edge (later = higher)
        if (bot > kb_top) bot = kb_top;                     // already pressed: clip at keys
        if (top < 0) top = 0;
        if (bot - top < 2 || bot <= 0) continue;
        DrawRectangleRounded({(float)box.first + 1, (float)top, (float)box.second - 2,
                              (float)(bot - top)}, 0.3f, 4, lane_color(v.lane));
    }

    // keyboard on top: white keys, then black keys over them
    for (int k = K_LO; k <= K_HI; k++) {
        if (k < 0 || k >= 128 || is_black(k)) continue;
        Color c = on[k] ? lit[k] : Color{245, 245, 245, 255};
        DrawRectangle((int)wx[k], (int)kb_top, (int)(ww - 1), (int)kb_h, c);
        DrawRectangleLines((int)wx[k], (int)kb_top, (int)(ww - 1), (int)kb_h, {40, 40, 40, 255});
    }
    for (int k = K_LO; k <= K_HI; k++) {
        if (k < 0 || k >= 128 || !is_black(k)) continue;
        std::pair<double, double> box = key_box(k);
        Color c = on[k] ? lit[k] : Color{25, 25, 30, 255};
        DrawRectangle((int)box.first, (int)kb_top, (int)box.second, (int)bh, c);
    }
    // C-octave labels under the keyboard, if the keys are wide enough
    if (ww >= 12)
        for (int k = K_LO; k <= K_HI; k++)
            if (k >= 0 && k < 128 && k % 12 == 0) {  // every C
                DrawTextEx(font, note_name(k).c_str(),
                           {(float)wx[k] + 1, (float)(kb_top + kb_h - 14)}, 20, 0.5f, {90, 90, 100, 255});
            }
}

void usage() {
    std::fprintf(stderr,
        "usage: calliopev <file.cal> [--mode daw|piano]\n"
    );
}

} // namespace

int main(int argc, char** argv) {
    using namespace calliope;

    std::string file;
    Mode mode = Mode::Daw;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) {
            std::string m = argv[++i];
            mode = (m == "piano") ? Mode::Piano : Mode::Daw;
        } else if (a == "-h" || a == "--help") {
            usage(); return 0;
        } else if (!a.empty() && a[0] != '-') {
            file = a;
        }
    }
    if (file.empty()) { usage(); return 2; }

    // compile the file (same path the interpreter uses)
    std::string src = cli::read_file(file);
    if (src.empty()) { std::fprintf(stderr, "error: cannot read '%s'\n", file.c_str()); return 1; }
    std::string base = driver::directory_of(file);
    driver::LoadOptions opts{cli::prelude_path(), base, false};
    driver::Compilation c;
    driver::compile(src, opts, c);
    if (!driver::ok(c)) { cli::print_errors(c); return 1; }
    if (!c.has_main || c.main_value.kind != eval::ValueKind::Music || !c.main_value.mus) {
        std::fprintf(stderr, "error: `main` must be Music to visualize (got %s)\n",
                     c.main_type.empty() ? "?" : c.main_type.c_str());
        return 1;
    }

    const music::Music& M = *c.main_value.mus;
    music::MusicId root = c.main_value.mroot;

    // prepare draw data: a flat list (piano view) + one list per voice (DAW rows)
    std::vector<backend::TimedNote> timed = backend::flatten(M, root);
    if (timed.empty()) { std::fprintf(stderr, "error: nothing to visualize (no notes)\n"); return 1; }
    std::vector<VizNote> notes;
    notes.reserve(timed.size());
    for (const backend::TimedNote& n : timed) {
        int lane = n.instrument < 0 ? 0 : n.instrument;
        double s = secs(n.start);
        notes.push_back({s, s + secs(n.dur), n.key, lane});
    }

    // one note list per voice (top-level `:=:` branches), grouped into lanes by staff:
    // sub-voices that share a base name (rightHand / rightHand2 / …) merge into one lane,
    // so a grand-staff piano shows two lanes (right/left hand) even when a staff is
    // engraved with several MusicXML voices. Falls back to one lane per voice when the
    // names aren't available (e.g. a hand-written `main` that isn't `a :=: b :=: …`).
    std::vector<std::vector<backend::TimedNote>> vtv = backend::flatten_voices(M, root);
    std::vector<std::string> names = voice_names(c.ast);
    bool group = names.size() == vtv.size();

    std::vector<Voice> voices;
    std::vector<std::set<int>> pitchsets;
    std::map<std::string, int> staff_lane;     // base name -> lane index
    for (std::size_t vi = 0; vi < vtv.size(); vi++) {
        std::string nm = vi < names.size() ? names[vi] : std::string();
        std::string staff = group ? staff_of(nm) : std::string();
        int idx;
        if (group && !staff.empty() && staff_lane.count(staff)) {
            idx = staff_lane[staff];           // merge into the existing hand's lane
        } else {
            idx = static_cast<int>(voices.size());
            voices.emplace_back();
            pitchsets.emplace_back();
            voices[idx].label = nm.empty() ? ("voice " + std::to_string(idx + 1)) : staff_of(nm);
            if (group && !staff.empty()) staff_lane[staff] = idx;
        }
        for (const backend::TimedNote& n : vtv[vi]) {
            double s = secs(n.start);
            voices[idx].notes.push_back({s, s + secs(n.dur), n.key, idx});
            pitchsets[idx].insert(n.key);
        }
    }
    for (std::size_t i = 0; i < voices.size(); i++)
        voices[i].pitches.assign(pitchsets[i].begin(), pitchsets[i].end());
    // drop voices that ended up empty, keep at least one
    voices.erase(std::remove_if(voices.begin(), voices.end(),
                 [](const Voice& v) { return v.notes.empty(); }), voices.end());
    if (voices.empty()) { Voice vc; vc.pitches.push_back(60); vc.label = "voice 1"; voices.push_back(std::move(vc)); }

    // start playback (non-blocking) and drive the window from its playhead
    backend::AudioOptions opt;
    opt.sfz_path = cli::default_soundfont();
    opt.base_dir = base;
    std::string err;
    backend::Playback* pb = backend::play_start(M, root, opt, err);
    if (!pb) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);   // must precede InitWindow
    InitWindow(1200, 700, "Calliope visualizer");
    SetTargetFPS(60);

    // bundled font (Zed Mono Nerd Font), loaded at a high size + filtered for crisp
    // scaling; falls back to raylib's built-in font if the file is missing.
    Font font = GetFontDefault();
#ifdef CALLIOPE_FONT_PATH
    if (FileExists(CALLIOPE_FONT_PATH)) {
        font = LoadFontEx(CALLIOPE_FONT_PATH, 48, nullptr, 0);
        SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    }
#endif

    // piece pitch range (for the piano view's initial window)
    int piece_lo = 127, piece_hi = 0;
    for (const VizNote& v : notes) {
        if (v.key < piece_lo) piece_lo = v.key;
        if (v.key > piece_hi) piece_hi = v.key;
    }
    if (piece_lo > piece_hi) {
        piece_lo = 48; piece_hi = 72;
    }

    double px_per_sec = 140.0;
    double daw_scale  = 1.0;
    double p_center = (piece_lo + piece_hi) / 2.0;
    double p_range  = piece_hi - piece_lo + 8.0;
    if (p_range < 24.0) p_range = 24.0;

    while (!WindowShouldClose()) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        double t = backend::playback_seconds(pb);
        double total = backend::playback_total_seconds(pb);
        double dt = GetFrameTime();

        // controls
        if (IsKeyPressed(KEY_TAB))   mode = (mode == Mode::Daw) ? Mode::Piano : Mode::Daw;
        if (IsKeyPressed(KEY_SPACE)) backend::playback_set_paused(pb, !backend::playback_paused(pb));
        if (mode == Mode::Daw) {
            // DAW: H/L (←/→) seek, J/K (↓/↑) time-zoom, [ ] note size
            if (IsKeyDown(KEY_H) || IsKeyDown(KEY_LEFT))  backend::playback_seek(pb, t - 8.0 * dt);
            if (IsKeyDown(KEY_L) || IsKeyDown(KEY_RIGHT)) backend::playback_seek(pb, t + 8.0 * dt);
            if (IsKeyDown(KEY_K) || IsKeyDown(KEY_UP))    px_per_sec *= (1.0 + 1.6 * dt);
            if (IsKeyDown(KEY_J) || IsKeyDown(KEY_DOWN))  px_per_sec *= (1.0 - 1.6 * dt);
            if (px_per_sec < 20.0)  px_per_sec = 20.0;
            if (px_per_sec > 600.0) px_per_sec = 600.0;
            if (IsKeyDown(KEY_RIGHT_BRACKET)) daw_scale *= (1.0 + 1.6 * dt);
            if (IsKeyDown(KEY_LEFT_BRACKET))  daw_scale *= (1.0 - 1.6 * dt);
            if (daw_scale < 0.3) daw_scale = 0.3;
            if (daw_scale > 1.0) daw_scale = 1.0;
        } else {
            // Piano: J/K (↓/↑) seek, H/L (←/→) scroll pitch, [ ] scale the pitch range
            if (IsKeyDown(KEY_J) || IsKeyDown(KEY_DOWN))  backend::playback_seek(pb, t - 8.0 * dt);
            if (IsKeyDown(KEY_K) || IsKeyDown(KEY_UP))    backend::playback_seek(pb, t + 8.0 * dt);
            if (IsKeyDown(KEY_L) || IsKeyDown(KEY_RIGHT)) p_center += 24.0 * dt;   // scroll higher
            if (IsKeyDown(KEY_H) || IsKeyDown(KEY_LEFT))  p_center -= 24.0 * dt;   // scroll lower
            if (IsKeyDown(KEY_RIGHT_BRACKET)) p_range *= (1.0 + 1.6 * dt);          // wider range
            if (IsKeyDown(KEY_LEFT_BRACKET))  p_range *= (1.0 - 1.6 * dt);          // narrower
            if (p_range < 12.0)  p_range = 12.0;
            if (p_range > 128.0) p_range = 128.0;
            if (p_center < 0.0)   p_center = 0.0;
            if (p_center > 127.0) p_center = 127.0;
        }

        BeginDrawing();
        ClearBackground({22, 22, 30, 255});
        const char* hints;
        if (mode == Mode::Daw) {
            draw_daw(voices, t, W, H, px_per_sec, daw_scale, font);
            hints = "[Space] pause  [H/L] seek  [J/K] zoom  [ [ / ] ] note size  [Tab] view  [Esc] quit";
        } else {
            int half = static_cast<int>(p_range / 2);

            double lo_c = half, hi_c = 127 - half;
            if (lo_c > hi_c) {
                lo_c = hi_c = 63.5;
            }
            if (p_center < lo_c) {
                p_center = lo_c;
            }
            if (p_center > hi_c) {
                p_center = hi_c;
            }
            int c = static_cast<int>(p_center + 0.5);
            int vlo = c - half, vhi = c + half;
            if (vlo < 0)   vlo = 0;
            if (vhi > 127) vhi = 127;
            if (vhi <= vlo) vhi = vlo + 1;
            draw_piano(notes, t, W, H, font, vlo, vhi);
            hints = "[Space] pause  [J/K] seek  [H/L] scroll  [ [ / ] ] range  [Tab] view  [Esc] quit";
        }
        DrawTextEx(font, TextFormat("%.1f / %.1f s%s   %s", t, total,
                                    backend::playback_paused(pb) ? "  (paused)" : "", hints),
                   {12, 10}, 18, 0.5f, {200, 200, 210, 255});
        EndDrawing();
    }

    if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
    backend::play_stop(pb);
    CloseWindow();
    return 0;
}
