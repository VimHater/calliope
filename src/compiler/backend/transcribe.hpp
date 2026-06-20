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
// Covered: pitches (spelled exactly — key is a comment, no `inKey`), durations, rests,
// chords, multiple parts AND <voice>s (each a parallel `:=:` line, correctly offset),
// pickups (<measure implicit>), time signature, tempo (<sound> and <metronome>),
// **ties** (`~`), **articulations** (staccato/accent/tenuto/strong-accent),
// **ornaments** (trill/mordent/turn), **dynamics** (forte/piano/… per run),
// **grace notes** (-> `acciaccatura`), the **damper pedal** (<pedal> -> `sustain`),
// and **dynamics part-wide** — a `<dynamics>` snaps to its measure and applies to
// every voice (a `pp` lowers both hands), like an engraver intends.
// Tags seen but not transcribed are reported on stderr.
//
// Barlines are not emitted: an approximated duration (a 7-tuplet at low divisions)
// can still make a measure not sum to a whole bar, which a `|` would reject.
//
// TODO: slurs / cresc-dim wedges, repeats / endings, fermata, lyrics, octave-shift;
// mid-piece meter/key/tempo changes; then validated barlines.

namespace calliope::transcribe {

// Convert the MusicXML file at `input_path` to Calliope source, returned in `out`.
// Returns false and fills `err` on failure (bad zip, parse error, unsupported).
bool musicxml_to_calliope(const std::string& input_path, std::string& out, std::string& err);

} // namespace calliope::transcribe
