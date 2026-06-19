#include "instrument.hpp"

namespace calliope::instrument {

namespace {

// The instrument lexicon. GM = General MIDI program (0-based). sfz_rel points at a
// real Sonatina Symphonic Orchestra "- Performance" patch under the SSO library.
const Info kTable[] = {
    {"Piano",       0,  "Grand Piano/Grand Piano.sfz"},
    {"Harpsichord", 6,  "Harpsichord/Harpsichord Full.sfz"},
    {"Harp",        46, "Concert Harp.sfz"},
    {"Violin",      40, "Strings - Performance/Violin Solo 1 Sustain.sfz"},
    {"Viola",       41, "Strings - Performance/Viola Solo Sustain.sfz"},
    {"Cello",       42, "Strings - Performance/Cello Solo Sustain.sfz"},
    {"Contrabass",  43, "Strings - Performance/Bass Solo Sustain.sfz"},
    {"Strings",     48, "Strings - Performance/All Strings Sustain.sfz"},
    {"Trumpet",     56, "Brass - Performance/Trumpet Solo Sustain.sfz"},
    {"Trombone",    57, "Brass - Performance/Tenor Trombone Solo Sustain.sfz"},
    {"Horn",        60, "Brass - Performance/Horn Solo Sustain.sfz"},
    {"Tuba",        58, "Brass - Performance/Tuba Sustain.sfz"},
    {"Flute",       73, "Woodwinds - Performance/Flute Solo 1 Sustain.sfz"},
    {"Oboe",        68, "Woodwinds - Performance/Oboe Solo Sustain.sfz"},
    {"Clarinet",    71, "Woodwinds - Performance/Clarinet Solo Sustain.sfz"},
    {"Bassoon",     70, "Woodwinds - Performance/Bassoon Solo Sustain.sfz"},
};
constexpr int kCount = static_cast<int>(sizeof(kTable) / sizeof(kTable[0]));

} // namespace

int id_of(std::string_view name) {
    for (int i = 0; i < kCount; i++)
        if (name == kTable[i].name) return i;
    return -1;
}

const Info* by_id(int id) {
    if (id < 0 || id >= kCount) return nullptr;
    return &kTable[id];
}

int count() { return kCount; }

} // namespace calliope::instrument
