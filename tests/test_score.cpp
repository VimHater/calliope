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

    // A raw-GM Control stamps its program onto the notes (instrument -1, path "").
    {
        music::Music m;
        music::MusicId g = music::control_gm(m, 42, music::note(m, pitch(0, 0, 4), q));
        std::vector<backend::TimedNote> ns = backend::flatten(m, g);
        CHECK(ns.size() == 1);
        CHECK(ns[0].gm == 42 && ns[0].instrument == -1 && ns[0].sfz_path.empty());
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

    // A meter accents strong beats (audible): four quarters under 4/4 -> the
    // downbeat is loudest (80+18), the mid-bar beat next (80+10), the rest weak.
    {
        music::Music m;
        auto qn = [&](int letter) { return music::note(m, pitch(letter, 0, 4), q); };
        music::MusicId run =
            music::seq(m, music::seq(m, music::seq(m, qn(0), qn(1)), qn(2)), qn(3));
        music::MusicId mtr = music::control_meter(m, 4, 4, run);
        std::vector<backend::TimedNote> ns = backend::flatten(m, mtr);
        CHECK(ns.size() == 4);
        CHECK(ns[0].velocity == 98); // beat 1 (downbeat)
        CHECK(ns[1].velocity == 80); // beat 2 (weak)
        CHECK(ns[2].velocity == 90); // beat 3 (mid-bar accent)
        CHECK(ns[3].velocity == 80); // beat 4 (weak)
        // 3/4 over the same notes accents only the downbeat (no mid-bar at bi=1.5)
        music::MusicId m34 = music::control_meter(m, 3, 4, run);
        std::vector<backend::TimedNote> n3 = backend::flatten(m, m34);
        CHECK(n3[0].velocity == 98 && n3[1].velocity == 80 && n3[2].velocity == 80);
    }

    // Barline validation (only when an errors sink is supplied): a `|` measure must
    // fill exactly one bar of the active meter.
    {
        music::Music m;
        auto qn = [&](int l) { return music::note(m, pitch(l, 0, 4), q); };
        auto four = [&]() {
            return music::seq(m, music::seq(m, music::seq(m, qn(0), qn(1)), qn(2)), qn(3));
        };
        // two full 4/4 measures separated by a barline -> no error
        music::MusicId good = music::control_meter(
            m, 4, 4, music::seq(m, four(), music::seq(m, music::barline(m), four())));
        std::vector<std::string> errs;
        backend::flatten(m, good, &errs);
        CHECK(errs.empty());
        // accent still applies with no errors sink (audible path)
        CHECK(!backend::flatten(m, good).empty());

        // first measure overfull (5 quarters) -> exactly one bar error
        music::MusicId five = music::seq(m, four(), qn(0));
        music::MusicId bad = music::control_meter(
            m, 4, 4, music::seq(m, five, music::seq(m, music::barline(m), four())));
        std::vector<std::string> errs2;
        backend::flatten(m, bad, &errs2);
        CHECK(errs2.size() == 1);
    }

    // Articulation: the gate shortens the sounding length but not the slot; the
    // accent shifts velocity.
    {
        music::Music m;
        music::MusicId run = music::seq(m, music::note(m, pitch(0, 0, 4), q),
                                           music::note(m, pitch(1, 0, 4), q));
        // staccato (gate 1/2): each note sounds half a beat, but the slot is full
        music::MusicId stac = music::control_articulate(m, rational(1, 2), 0, run);
        std::vector<backend::TimedNote> ns = backend::flatten(m, stac);
        CHECK(ns.size() == 2);
        CHECK(rat_eq(ns[0].dur, rational(1, 4)));   // sounds 1/4 s (gated short)
        CHECK(rat_eq(ns[1].start, qs));             // ...next note still a full beat later
        CHECK(ns[0].velocity == 80);                // gate leaves velocity alone

        // accent (+15): full length, louder
        music::MusicId acc = music::control_articulate(
            m, rational(1, 1), 15, music::note(m, pitch(0, 0, 4), q));
        std::vector<backend::TimedNote> na = backend::flatten(m, acc);
        CHECK(na[0].velocity == 95 && rat_eq(na[0].dur, qs));
    }

    // Sustain (damper pedal): every note rings until the wrapped subtree ends.
    {
        music::Music m;
        music::MusicId run = music::seq(m, music::seq(m, music::note(m, pitch(0, 0, 4), q),
                                                         music::note(m, pitch(1, 0, 4), q)),
                                           music::note(m, pitch(2, 0, 4), q));
        music::MusicId ped = music::control_sustain(m, run);
        std::vector<backend::TimedNote> ns = backend::flatten(m, ped);
        CHECK(ns.size() == 3);
        // span is 3 quarters = 1.5 s; each note holds from its onset to the end
        CHECK(rat_eq(ns[0].dur, rational(3, 2)));   // 0.0 -> 1.5
        CHECK(rat_eq(ns[1].dur, rational(1, 1)));   // 0.5 -> 1.5
        CHECK(rat_eq(ns[2].dur, rational(1, 2)));   // 1.0 -> 1.5
    }
}
