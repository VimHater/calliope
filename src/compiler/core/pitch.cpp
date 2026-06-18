#include "pitch.hpp"

namespace calliope {
namespace {

// Semitone offset of each diatonic letter within an octave: C D E F G A B.
const int kLetterChromatic[7] = {0, 2, 4, 5, 7, 9, 11};

// Floor-divide / non-negative modulo for staff steps (handles negatives).
int floor_div(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q -= 1;
    return q;
}
int floor_mod(int a, int b) {
    return a - floor_div(a, b) * b;
}

} // namespace

Pitch pitch(int letter, int accidental, int octave) {
    return Pitch{letter, accidental, octave};
}

int semitones(Pitch p) {
    return p.octave * 12 + kLetterChromatic[p.letter] + p.accidental;
}

int diatonic_step(Pitch p) {
    return p.octave * 7 + p.letter;
}

Pitch mk_pitch(int diatonic_step, int accidental) {
    int letter = floor_mod(diatonic_step, 7);
    int octave = floor_div(diatonic_step, 7);
    return Pitch{letter, accidental, octave};
}

int chromatic_of(int diatonic_step) {
    int letter = floor_mod(diatonic_step, 7);
    int octave = floor_div(diatonic_step, 7);
    return octave * 12 + kLetterChromatic[letter];
}

bool pitch_eq(Pitch a, Pitch b) {
    return a.letter == b.letter && a.accidental == b.accidental &&
           a.octave == b.octave;
}

bool same_sound(Pitch a, Pitch b) {
    return semitones(a) == semitones(b);
}

} // namespace calliope
