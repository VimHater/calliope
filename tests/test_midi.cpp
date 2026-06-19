#include "test.hpp"

#include "backend/midi.hpp"
#include "core/instrument.hpp"
#include "core/music.hpp"
#include "core/pitch.hpp"
#include "core/rational.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace calliope;

namespace {
std::vector<unsigned char> read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}
} // namespace

void run_midi_tests() {
    music::Music m;
    // C4 then D4, two quarter notes in sequence
    music::MusicId c4 = music::note(m, pitch(0, 0, 4), rational(1, 4));
    music::MusicId d4 = music::note(m, pitch(1, 0, 4), rational(1, 4));
    music::MusicId s = music::seq(m, c4, d4);

    const std::string path = "test_out.mid";
    std::string err;
    CHECK(backend::write_midi(m, s, path, err));
    CHECK(err.empty());

    std::vector<unsigned char> b = read_bytes(path);
    CHECK(b.size() > 22);
    // header: "MThd", then format 0, one track, division 480 (0x01E0)
    CHECK(b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd');
    CHECK(b[8] == 0x00 && b[9] == 0x00);            // format 0
    CHECK(b[10] == 0x00 && b[11] == 0x01);          // one track
    CHECK(b[12] == 0x01 && b[13] == 0xE0);          // division 480
    CHECK(b[14] == 'M' && b[15] == 'T' && b[16] == 'r' && b[17] == 'k');
    // a note-on for C4 (key 60 = 0x3C) appears somewhere in the track
    bool has_c4_on = false;
    for (std::size_t i = 0; i + 2 < b.size(); i++)
        if (b[i] == 0x90 && b[i + 1] == 0x3C) { has_c4_on = true; break; }
    CHECK(has_c4_on);
    // ends with the end-of-track meta FF 2F 00
    std::size_t n = b.size();
    CHECK(b[n - 3] == 0xFF && b[n - 2] == 0x2F && b[n - 1] == 0x00);

    std::remove(path.c_str());

    // a Control node emits a program-change for its instrument's GM number.
    music::Music m2;
    int cello = instrument::id_of("Cello"); // GM 42 = 0x2A
    music::MusicId voiced = music::control(m2, cello, music::note(m2, pitch(0, 0, 4), rational(1, 4)));
    const std::string p2 = "test_out_inst.mid";
    CHECK(backend::write_midi(m2, voiced, p2, err));
    std::vector<unsigned char> b2 = read_bytes(p2);
    bool has_prog = false;
    for (std::size_t i = 0; i + 1 < b2.size(); i++)
        if (b2[i] == 0xC0 && b2[i + 1] == 0x2A) { has_prog = true; break; }
    CHECK(has_prog);
    std::remove(p2.c_str());

    // a raw `gm n` Control emits a program-change for that exact GM number.
    music::Music m3;
    music::MusicId raw = music::control_gm(m3, 19, music::note(m3, pitch(0, 0, 4), rational(1, 4)));
    const std::string p3 = "test_out_gm.mid";
    CHECK(backend::write_midi(m3, raw, p3, err));
    std::vector<unsigned char> b3 = read_bytes(p3);
    bool has_gm = false;
    for (std::size_t i = 0; i + 1 < b3.size(); i++)
        if (b3[i] == 0xC0 && b3[i + 1] == 0x13) { has_gm = true; break; } // GM 19 = 0x13
    CHECK(has_gm);
    std::remove(p3.c_str());
}
