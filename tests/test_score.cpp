#include "test.hpp"

#include "backend/score.hpp"
#include "core/instrument.hpp"
#include "core/music.hpp"
#include "core/pitch.hpp"
#include "core/rational.hpp"

#include <vector>

using namespace calliope;

void run_score_tests() {
    const Rational q = rational(1, 4);    // a quarter note (whole-note fraction)
    const Rational qs = rational(1, 2);   // ...is 1/2 second at the default 120 bpm

    // C4 then D4 in sequence: two notes, D4 starting a quarter after C4.
    {
        music::Music m;
        music::MusicId c4 = music::note(m, pitch(0, 0, 4), q);
        music::MusicId d4 = music::note(m, pitch(1, 0, 4), q);
        music::MusicId s = music::seq(m, c4, d4);

        std::vector<backend::TimedNote> ns = backend::flatten(m, s);
        CHECK(ns.size() == 2);
        CHECK(ns[0].key == 60 && ns[1].key == 62);      // C4 = 60, D4 = 62
        CHECK(rat_eq(ns[0].start, rational(0, 1)));
        CHECK(rat_eq(ns[1].start, qs));                 // D4 starts at 0.5 s
        CHECK(rat_eq(ns[0].dur, qs) && rat_eq(ns[1].dur, qs));
    }

    // Par overlays both children at the same start.
    {
        music::Music m;
        music::MusicId c4 = music::note(m, pitch(0, 0, 4), q);
        music::MusicId e4 = music::note(m, pitch(2, 0, 4), q);
        music::MusicId p = music::par(m, c4, e4);

        std::vector<backend::TimedNote> ns = backend::flatten(m, p);
        CHECK(ns.size() == 2);
        CHECK(rat_eq(ns[0].start, rational(0, 1)));
        CHECK(rat_eq(ns[1].start, rational(0, 1)));     // both at t=0
    }

    // A rest emits no note but advances time for what follows.
    {
        music::Music m;
        music::MusicId r = music::rest(m, q);
        music::MusicId c4 = music::note(m, pitch(0, 0, 4), q);
        music::MusicId s = music::seq(m, r, c4);

        std::vector<backend::TimedNote> ns = backend::flatten(m, s);
        CHECK(ns.size() == 1);
        CHECK(ns[0].key == 60);
        CHECK(rat_eq(ns[0].start, qs));                 // pushed past the rest (0.5 s)
    }

    // A Control node stamps its instrument onto the notes it wraps; bare notes = -1.
    {
        music::Music m;
        int cello = instrument::id_of("Cello");
        int flute = instrument::id_of("Flute");
        CHECK(cello >= 0 && flute >= 0);
        music::MusicId c4 = music::note(m, pitch(0, 0, 4), q);
        music::MusicId d4 = music::note(m, pitch(1, 0, 4), q);
        music::MusicId voiced = music::control(m, cello, music::seq(m, c4, d4));
        music::MusicId bare = music::note(m, pitch(2, 0, 4), q);
        // inner Control wins over an outer one
        music::MusicId nested = music::control(m, flute, voiced);
        music::MusicId s = music::seq(m, nested, bare);

        std::vector<backend::TimedNote> ns = backend::flatten(m, s);
        CHECK(ns.size() == 3);
        CHECK(ns[0].instrument == cello && ns[1].instrument == cello); // inner wins
        CHECK(ns[2].instrument == -1);                                 // bare note
    }

    // A custom-.sfz Control (instrument -1, path set) stamps the path onto its notes.
    {
        music::Music m;
        music::MusicId a = music::control(m, -1, "a.sfz", music::note(m, pitch(0, 0, 4), q));
        music::MusicId b = music::control(m, -1, "b.sfz", music::note(m, pitch(1, 0, 4), q));
        std::vector<backend::TimedNote> ns = backend::flatten(m, music::seq(m, a, b));
        CHECK(ns.size() == 2);
        CHECK(ns[0].instrument == -1 && ns[1].instrument == -1);
        CHECK_EQ_STR(ns[0].sfz_path.c_str(), "a.sfz"); // distinct custom voices kept apart
        CHECK_EQ_STR(ns[1].sfz_path.c_str(), "b.sfz");
    }

    // A tempo Control rescales the seconds of its subtree; velocity stamps notes.
    {
        music::Music m;
        // tempo 240 (twice the default 120) -> a quarter is 1/4 s, not 1/2
        music::MusicId fast = music::control_tempo(m, 240, music::note(m, pitch(0, 0, 4), q));
        std::vector<backend::TimedNote> nf = backend::flatten(m, fast);
        CHECK(nf.size() == 1);
        CHECK(rat_eq(nf[0].dur, rational(1, 4)));

        // velocity 100 stamps the note; default elsewhere is 80
        music::MusicId loud = music::control_velocity(m, 100, music::note(m, pitch(0, 0, 4), q));
        std::vector<backend::TimedNote> nl = backend::flatten(m, loud);
        CHECK(nl.size() == 1 && nl[0].velocity == 100);
        std::vector<backend::TimedNote> nd = backend::flatten(m, music::note(m, pitch(0, 0, 4), q));
        CHECK(nd[0].velocity == 80);
    }
}
