#include "test.hpp"

#include "instrument.hpp"
#include "music.hpp"
#include "pitch.hpp"
#include "rational.hpp"

using namespace calliope;

void run_music_tests() {
    music::Music m;

    // a Note carries pitch + duration; show renders "<pitch>:<dur>"
    music::MusicId c4 = music::note(m, pitch(0, 0, 4), rational(1, 4)); // C4 quarter
    CHECK_EQ_STR(music::show(m, c4), "C4:1/4");

    // Seq / Par structure
    music::MusicId e4 = music::note(m, pitch(2, 0, 4), rational(1, 8));
    music::MusicId s = music::seq(m, c4, e4);
    CHECK_EQ_STR(music::show(m, s), "(C4:1/4 :+: E4:1/8)");
    music::MusicId chord = music::par(m, c4, e4);
    CHECK_EQ_STR(music::show(m, chord), "(C4:1/4 :=: E4:1/8)");

    // a Rest
    CHECK_EQ_STR(music::show(m, music::rest(m, rational(1, 2))), "r:1/2");

    // transpose maps over every Note, preserving structure + durations;
    // C4 up a perfect fifth (4 diatonic steps, 7 semitones) = G4.
    music::MusicId up = music::transpose(m, s, 4, 7);
    CHECK_EQ_STR(music::show(m, up), "(G4:1/4 :+: B4:1/8)");

    // the original is untouched (transpose builds a fresh subtree)
    CHECK_EQ_STR(music::show(m, s), "(C4:1/4 :+: E4:1/8)");

    // spelling is preserved: C up a minor third (2 steps, 3 semitones) = Eb, not D#
    music::MusicId eb = music::transpose(m, c4, 2, 3);
    CHECK_EQ_STR(music::show(m, eb), "Eb4:1/4");

    // a Control node wraps a phrase with an instrument; show renders inst(Name, …)
    int viola = instrument::id_of("Viola");
    CHECK(viola >= 0);
    music::MusicId voiced = music::control(m, viola, s);
    CHECK_EQ_STR(music::show(m, voiced), "inst(Viola, (C4:1/4 :+: E4:1/8))");

    // transpose passes through the Control, preserving the wrapper + instrument
    music::MusicId voiced_up = music::transpose(m, voiced, 4, 7);
    CHECK_EQ_STR(music::show(m, voiced_up), "inst(Viola, (G4:1/4 :+: B4:1/8))");

    // equal: same wrapper + child is equal; a different instrument is not
    CHECK(music::equal(m, voiced, music::control(m, viola, music::seq(m, c4, e4))));
    CHECK(!music::equal(m, voiced, music::control(m, instrument::id_of("Cello"),
                                                  music::seq(m, c4, e4))));
}
