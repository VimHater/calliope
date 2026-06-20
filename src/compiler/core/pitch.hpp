#pragma once

// Spelled-pitch representation primitives (language_spec.md §14.1, O8).
//
// A Pitch keeps its spelling (C#  is not  Db): a diatonic letter, an accidental
// in semitones, and an octave. The core exposes only the *projections* and
// constructors below — enough for the Calliope standard library to build all
// interval / scale / chord arithmetic on top. Interval math itself is NOT here.
//
// Conventions:
//   letter:     0..6  =  C D E F G A B
//   accidental: semitone offset (e.g. -1 flat, +1 sharp, +2 double sharp)
//   octave:     C4 (octave 4) is the reference "middle C"; semitone scale has
//               C0 = 0 (so this differs from MIDI by a constant — that mapping
//               is applied later, at the playback boundary).

namespace calliope {

struct Pitch {
    int letter = 0;     // 0..6
    int accidental = 0; // semitones
    int octave = 0;
    // A bare notation letter (`f`, no is/es) carries a *floating* natural: it is
    // still F natural everywhere, but an enclosing `inKey` may respell it to the
    // key's accidental. An explicit accidental, or any computed pitch, is fixed
    // (floating = false). Ignored by spelling equality / projections.
    bool floating = false;
};

Pitch pitch(int letter, int accidental, int octave);

int semitones(Pitch p);      // chromatic position; C0 = 0
int diatonic_step(Pitch p);  // staff position = octave*7 + letter

Pitch mk_pitch(int diatonic_step, int accidental); // from (staff step, accidental)
int   chromatic_of(int diatonic_step);             // semitones of that step, natural

bool pitch_eq(Pitch a, Pitch b);   // identical spelling
bool same_sound(Pitch a, Pitch b); // enharmonic: equal semitones

} // namespace calliope
