#pragma once

#include <string>

// MusicXML -> Calliope transcription.
//
// Reads a MusicXML score (`.mxl` compressed zip, or a raw `.xml` / `.musicxml`)
// and renders idiomatic Calliope source. The reverse of the audio/MIDI backends:
// those lower the Music IR to a playable form; this lifts a foreign score *up* to
// the language. `.mxl` is unzipped with miniz; the XML is read with hoxml (a SAX
// parser); the note model is rendered to spelled pitch literals, chords (`<...>`),
// rests, barlines (`|`), parts (`chord [...]`), and `meter` / `inKey` / `tempo`
// wrappers.
//
// The parse runs on one MusicXML measure clock: every note gets an absolute
// `start_div` and `dur_div` (in <divisions>), `<backup>`/`<forward>` move the cursor,
// and `<chord/>` pins to the previous start. Each measure is then *reconstructed* —
// gaps and the bar tail are filled with rests — so multi-voice timing is exact and
// the voices stay aligned. Durations come straight from <duration>/<divisions>, and
// since Calliope accepts any denominator a triplet eighth is simply `:12` (1/12) —
// tuplets are native, no wrapper or <type> guessing.
//
// Covered: pitches, durations, rests, chords, multiple parts AND <voice>s (each a
// parallel `:=:` line, correctly offset), pickups (<measure implicit>), time
// signature, tempo (<sound> and <metronome>, incl. a dotted beat-unit), **ties**
// (`~`), **articulations** (staccato/accent/tenuto/strong-accent), **ornaments**
// (trill/mordent/turn), **dynamics** (forte/piano/sf/fp/… part-wide, hoisted to the
// whole piece when constant, else wrapped once per same-dynamic region),
// **grace notes** (-> `acciaccatura`), the **damper pedal** (<pedal> -> `sustain`),
// **octave-shift** (8va/8vb -> `ottava`), **cresc/dim wedges** (-> a baked velocity
// ramp), **arpeggiate** (-> a staggered roll), and **repeats / endings** (unrolled
// into the play order). Tags seen but not transcribed are reported on stderr.
//
// Output is structured for reading: each line is named by its **hand/staff** (a
// grand-staff piano becomes `rightHand` / `leftHand`, by `<staff>`; extra voices get
// a numeric suffix, a single staff stays `line`, multiple parts get a `partN_`
// prefix), with a comment above; one **bar per line**, joined by the `|` barline
// when every measure provably fills the meter (else `:+:`, since a `|` is validated
// at compile time); a **repeated bar is named once and reused** (`m1`, `m2`, …); and
// transforms read left-to-right through the **pipe** (`run |> forte`, `… |> tempo`).
// When the score is fully diatonic to its key, pitches are spelled bare and a single
// `inKey <fifths>` resolves them; otherwise accidentals are explicit (the language
// has no natural sign, so a key contrary to a note can't be respelled safely).
//
// TODO: slurs, fermata, glissando/slide, coda/segno (D.S./D.C.), lyrics;
// mid-piece key changes.

namespace calliope::transcribe {

// Convert the MusicXML file at `input_path` to Calliope source, returned in `out`.
// Returns false and fills `err` on failure (bad zip, parse error, unsupported).
bool musicxml_to_calliope(const std::string& input_path, std::string& out, std::string& err);

} // namespace calliope::transcribe
