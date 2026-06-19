#pragma once

#include <string_view>

// The instrument lexicon — the single source of truth shared by the evaluator
// (constructor name → id), the type checker (which names are valid `Instrument`
// constructors), and the backends (id → GM program for MIDI/SF2, id → `.sfz`
// subpath for the audio backend).
//
// `Instrument` is a closed, typed enum in the language: each entry is a builtin
// nullary constructor (`Piano`, `Violin`, …) of type `Instrument`, exactly like the
// `Interval` constructors (`P5`, `M3`). The IR never stores a GM number or a path —
// only the abstract id; each backend resolves it through this table.

namespace calliope::instrument {

struct Info {
    const char* name;     // the constructor spelling (uppercase)
    int gm;               // General MIDI program number (0-based), for MIDI / SF2
    const char* sfz_rel;  // path under "<SSO>/Sonatina Symphonic Orchestra/", for sfizz
};

// Constructor name → id (index into the table), or -1 if it is not an instrument.
int id_of(std::string_view name);

// id → its Info, or nullptr if `id` is out of range.
const Info* by_id(int id);

// Number of instruments in the table.
int count();

} // namespace calliope::instrument
