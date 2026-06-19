#include "test.hpp"

#include "backend/score.hpp"
#include "core/music.hpp"
#include "core/pitch.hpp"
#include "core/rational.hpp"

#include <vector>

using namespace calliope;

void run_score_tests() {
    const Rational q = rational(1, 4); // a quarter note

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
        CHECK(rat_eq(ns[1].start, q));                  // D4 starts at 1/4
        CHECK(rat_eq(ns[0].dur, q) && rat_eq(ns[1].dur, q));
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
        CHECK(rat_eq(ns[0].start, q));                  // pushed past the rest
    }
}
