#include "test.hpp"

#include "pitch.hpp"

using namespace calliope;

namespace {

// letters: 0=C 1=D 2=E 3=F 4=G 5=A 6=B
Pitch C4() { return pitch(0, 0, 4); }

// transpose built from ONLY the core projections — demonstrates the thin core
// is sufficient for the stdlib's interval arithmetic.
//   dstep = diatonic distance, dsemi = chromatic distance (e.g. P5 = 4,7)
Pitch transpose(Pitch p, int dstep, int dsemi) {
    int ns = diatonic_step(p) + dstep;
    int acc = (semitones(p) + dsemi) - chromatic_of(ns);
    return mk_pitch(ns, acc);
}

} // namespace

void run_pitch_tests() {
    // projections
    CHECK(semitones(C4()) == 48);          // C0 = 0, so C4 = 48
    CHECK(diatonic_step(C4()) == 28);      // 4*7 + 0
    CHECK(semitones(pitch(5, 0, 4)) == 57); // A4
    CHECK(chromatic_of(28) == 48);
    CHECK(pitch_eq(mk_pitch(28, 0), C4()));

    // enharmonic: C#4 and Db4 sound the same but spell differently
    Pitch cis = pitch(0, 1, 4);
    Pitch des = pitch(1, -1, 4);
    CHECK(same_sound(cis, des));
    CHECK(!pitch_eq(cis, des));

    // interval transpose, spelled correctly
    CHECK(pitch_eq(transpose(C4(), 4, 7), pitch(4, 0, 4)));  // P5 up -> G4
    CHECK(pitch_eq(transpose(C4(), 2, 4), pitch(2, 0, 4)));  // M3 up -> E4
    CHECK(pitch_eq(transpose(C4(), 3, 5), pitch(3, 0, 4)));  // P4 up -> F4
    CHECK(pitch_eq(transpose(C4(), -7, -12), pitch(0, 0, 3)));// octave down -> C3

    // a transpose that needs an accidental: C4 up a minor third -> Eb4
    Pitch eb = transpose(C4(), 2, 3); // diatonic 3rd, 3 semitones
    CHECK(pitch_eq(eb, pitch(2, -1, 4)));
}
