#include "backend/midi.hpp"

#include "backend/score.hpp"
#include "core/instrument.hpp"
#include "core/rational.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace calliope::backend {

namespace {

// 480 ticks per quarter note → 1920 per whole note. 1920 = 2^7·3·5, so dyadic
// durations, triplets (÷3,÷6,÷12) and quintuplets (÷5) land on exact ticks;
// rarer tuplets round to the nearest tick.
constexpr int TPQN = 480;
constexpr long long TICKS_PER_WHOLE = 4 * TPQN;
constexpr std::uint8_t VELOCITY = 80;

// Whole-note fraction -> ticks (also used to place a note's absolute start, since
// `TimedNote.start` is in whole notes too). Rounds to the nearest tick.
long long dur_ticks(const Rational& d) {
    long long num = d.num * TICKS_PER_WHOLE;
    return (num + d.den / 2) / d.den;
}

void put_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back((v >> 8) & 0xFF);
    b.push_back(v & 0xFF);
}
void put_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back((v >> 24) & 0xFF);
    b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 8) & 0xFF);
    b.push_back(v & 0xFF);
}
// MIDI variable-length quantity (7 bits per byte, big-endian, high bit = more).
void put_vlq(std::vector<std::uint8_t>& b, std::uint32_t v) {
    std::uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    v >>= 7;
    while (v) { buf[n++] = (v & 0x7F) | 0x80; v >>= 7; }
    for (int i = n - 1; i >= 0; i--) b.push_back(buf[i]);
}

struct RawEv { long long tick; std::uint8_t status, d1, d2; };

} // namespace

bool write_midi(const music::Music& m, music::MusicId root,
                const std::string& path, std::string& err) {
    std::vector<TimedNote> notes = flatten(m, root);

    // Assign a MIDI channel per distinct instrument (insertion order), skipping
    // channel 9 (GM drums) and clamping at 15. Named instruments key on their id; a
    // custom .sfz keys on its path (no GM program — it keeps the default voice), as
    // do bare notes (`instrument == -1`, no path).
    struct Chan { std::string key; int ch; int inst; };
    std::vector<Chan> chans;
    int next_ch = 0;
    auto channel_of = [&](const TimedNote& ne) -> int {
        std::string key = ne.sfz_path.empty() ? ("#" + std::to_string(ne.instrument))
                                              : ("@" + ne.sfz_path);
        for (const Chan& cn : chans)
            if (cn.key == key) return cn.ch;
        int ch = next_ch;
        if (ch == 9) ch = 10; // skip the GM drum channel
        if (ch > 15) ch = 15; // clamp: extra instruments share the last channel
        chans.push_back({key, ch, ne.sfz_path.empty() ? ne.instrument : -1});
        next_ch = ch + 1;
        return ch;
    };

    // each note → a note-on and a note-off; sort by tick, note-off first on ties
    std::vector<RawEv> evs;
    evs.reserve(notes.size() * 2);
    for (const TimedNote& ne : notes) {
        long long start = dur_ticks(ne.start);
        long long dur = dur_ticks(ne.dur);
        std::uint8_t ch = static_cast<std::uint8_t>(channel_of(ne));
        evs.push_back({start, static_cast<std::uint8_t>(0x90 | ch),
                       static_cast<std::uint8_t>(ne.key), VELOCITY});
        evs.push_back({start + dur, static_cast<std::uint8_t>(0x80 | ch),
                       static_cast<std::uint8_t>(ne.key), 0});
    }
    std::stable_sort(evs.begin(), evs.end(), [](const RawEv& a, const RawEv& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return (a.status & 0xF0) < (b.status & 0xF0); // 0x80 (off) before 0x90 (on)
    });

    // ---- track chunk ----
    std::vector<std::uint8_t> trk;
    // tempo meta at t=0: 500000 µs/quarter = 120 bpm
    put_vlq(trk, 0);
    trk.push_back(0xFF); trk.push_back(0x51); trk.push_back(0x03);
    trk.push_back(0x07); trk.push_back(0xA1); trk.push_back(0x20);
    // program change per instrument channel (all at t=0, before any note)
    for (const Chan& cn : chans) {
        const instrument::Info* info = instrument::by_id(cn.inst);
        if (!info) continue; // bare / custom .sfz: leave the channel's default voice
        put_vlq(trk, 0);
        trk.push_back(static_cast<std::uint8_t>(0xC0 | cn.ch));
        trk.push_back(static_cast<std::uint8_t>(info->gm & 0x7F));
    }
    long long prev = 0;
    for (const RawEv& e : evs) {
        put_vlq(trk, static_cast<std::uint32_t>(e.tick - prev));
        prev = e.tick;
        trk.push_back(e.status); trk.push_back(e.d1); trk.push_back(e.d2);
    }
    // end-of-track meta
    put_vlq(trk, 0);
    trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0x00);

    // ---- file: header + the one track ----
    std::vector<std::uint8_t> out;
    out.push_back('M'); out.push_back('T'); out.push_back('h'); out.push_back('d');
    put_u32(out, 6);
    put_u16(out, 0); // format 0 (single track)
    put_u16(out, 1); // one track
    put_u16(out, TPQN);
    out.push_back('M'); out.push_back('T'); out.push_back('r'); out.push_back('k');
    put_u32(out, static_cast<std::uint32_t>(trk.size()));
    out.insert(out.end(), trk.begin(), trk.end());

    std::ofstream f(path, std::ios::binary);
    if (!f) { err = "cannot open '" + path + "' for writing"; return false; }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) { err = "failed writing '" + path + "'"; return false; }
    return true;
}

} // namespace calliope::backend
