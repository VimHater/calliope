# Built-in functions (the C++ core)

The core is deliberately thin: it exposes only **axioms**. Everything else —
including all music theory — is written in Calliope in the
[standard library](./stdlib.md). The builtins below are always in scope.

## Operators

| Operator | Type | Notes |
|----------|------|-------|
| `+` `-` `*` | `Num t => t -> t -> t` | arithmetic over the `Num` class — instances `Int` and `Rational`; a mismatched `Int`/`Rational` pair coerces up to `Rational` (`1 + 1/2` = `3/2`) |
| `/` | `Int -> Int -> Rational` | fractional division: two ints make an exact `Rational` (`7 / 2` = `7/2`); divide by zero is an error |
| `` `div` `` `` `mod` `` | `Int -> Int -> Int` | integer division and remainder, used infix (`` 7 `div` 2 ``); bind like `*`; divide by zero is an error |
| `toRational` | `Int -> Rational` | lift an `Int` into the `Rational` world (mismatched literals already coerce; use this to force a value through a `Rational`-typed parameter) |
| `==` `/=` | `a -> a -> Bool` | structural equality: `Int`, `Bool`, `Pitch` (spelled: `fis' /= ges'`), and `Music` (deep: same shape, pitches, durations) |
| `<` `>` `<=` `>=` | `Ord t => t -> t -> Bool` | ordering on `Int` and `Pitch` (pitches by height / semitones, so `fis' <= ges'`); no instance for `Music` |
| `and` `or` | `Bool -> Bool -> Bool` | keyword operators, **short-circuit** |
| `^+` `^-` | `Transposable t => t -> Interval -> t` | transpose; class method (see below) |
| `:` | `a -> [a] -> [a]` | list cons (`x : xs`) |
| `\|>` | `a -> (a -> b) -> b` | pipe: `x \|> f = f x` |
| `:+:` | `Phrase t => Phrase u => t -> u -> Music` | sequential composition |
| `:=:` | `Phrase t => Phrase u => t -> u -> Music` | parallel composition |
| `:*:` | `Phrase t => t -> Int -> Music` | repeat a phrase n times in a row (`motif :*: 4`); binds tighter than `:+:`; `n >= 1` |
| `~` | `Phrase t => Phrase u => t -> u -> Music` | tie two **matching** phrases (notes, chords, …) into one with summed durations (`c'4 ~ c'8` = `C4:3/8`; `<c e g> ~ <c e g>` = a held chord); ties chain (`c'4 ~ c'8 ~ c'8`). Operands must have the same pitches and shape, else a runtime error |
| `\|` | `Phrase t => Phrase u => t -> u -> Music` | **barline**: sequence two measures with a boundary marker between them. Lowest precedence (looser than `:+:`). Under an enclosing `meter` each measure is checked (see below); without a meter it is an inert boundary |

`not :: Bool -> Bool` is a prefix function. `:+:`/`:=:` are methods of the builtin
`Phrase` class (instances `Pitch` and `Music`): a bare `Pitch` operand lifts to a
one-note phrase, so `c' :+: d'` is `Music`. A non-phrase operand (`1 :+: 2`) is a
`no instance for Phrase Int` error.

## Transposition — the `Transposable` class

`^+` and `^-` are methods of a builtin single-parameter class:

```
class Transposable t where
  (^+) :: t -> Interval -> t
  (^-) :: t -> Interval -> t
```

with builtin instances for **`Pitch`** (transpose one pitch, keeping spelling) and
**`Music`** (transpose every note in a phrase). You can add more instances. The
result type equals the left operand, so no annotation is needed:

```
c' ^+ P5            -- G4            :: Pitch
(c' e' g') ^+ P5    -- whole phrase  :: Music
```

### Intervals

Intervals are written as named constructors (quality + number):

```
P1  m2 M2  m3 M3  P4  A4 d5  P5  m6 M6  m7 M7  P8
```

(perfect, minor, major, augmented/diminished). They span up to one octave today.

## Pitch projections

The bridge between spelled pitches and numbers; the prelude builds pitch
arithmetic on these.

| Function | Type | Meaning |
|----------|------|---------|
| `semitones` | `Pitch -> Int` | chromatic value (C0 = 0, so C4 = 48) |
| `diatonicStep` | `Pitch -> Int` | staff position (octave·7 + letter) |
| `chromaticOf` | `Int -> Int` | semitones of a diatonic step (the natural at that position) |
| `makePitch` | `Int -> Int -> Pitch` | build a pitch from a diatonic step and an accidental |

```
-- transpose by raw semitones, respelled at a chosen staff position
reflectPitch p axis =
  makePitch (2 * diatonicStep axis - diatonicStep p)
          (2 * semitones axis - semitones p
             - chromaticOf (2 * diatonicStep axis - diatonicStep p))
```

## List axioms

The four primitives the list library is built on. They are polymorphic.

| Function | Type | Meaning |
|----------|------|---------|
| `null` | `[a] -> Bool` | true on the empty list |
| `head` | `[a] -> a` | first element (error if empty) |
| `tail` | `[a] -> [a]` | everything after the first (error if empty) |
| `cons` | `a -> [a] -> [a]` | prepend one element |

List literals (`[1, 2, 3]`, `[]`) are built-in syntax.

## Music tree axioms

The `Music` value is a tree of `Note | Rest | Seq | Par`. These constructors,
predicates, and accessors let the prelude define structural transforms (`invert`,
`retrograde`, `mapPitches`, …) in Calliope.

### Constructors

| Function | Type | Meaning |
|----------|------|---------|
| `note` | `Pitch -> Music` | a note (uses the pitch's duration, default quarter) |
| `noteWith` | `Pitch -> Rational -> Music` | a note of an explicit duration |
| `sequence` | `Music -> Music -> Music` | sequence two phrases (same as `:+:`) |
| `parallel` | `Music -> Music -> Music` | layer two phrases (same as `:=:`) |
| `tuplet` | `Int -> Int -> Music -> Music` | `tuplet n m` fits n notes in the time of m (scales every duration by m/n); a triplet is `tuplet 3 2` |
| `withInstrument` | `Phrase t => Instrument -> t -> Music` | wrap a phrase in a `Control` node assigning an instrument (stdlib `onInstrument` is a thin alias); a bare pitch lifts, so `withInstrument Cello c` works |
| `sfz` | `Str -> Instrument` | a custom instrument from a `.sfz` file path (relative to the source file, or absolute) |
| `gm` | `Int -> Instrument` | a custom instrument from a raw General-MIDI program number (0..127) |

### Instruments

`Instrument` is a typed enum of named constructors (uppercase, like the intervals —
lowercase names like `cello` would collide with reserved pitch letters):

```
Piano Harpsichord Harp  Violin Viola Cello Contrabass Strings
Trumpet Trombone Horn Tuba  Flute Oboe Clarinet Bassoon            -- :: Instrument
```

`withInstrument Cello (c d e)` tags the phrase; the MIDI backend emits the matching
General-MIDI program (on its own channel) and the audio backend plays the matching
Sonatina Symphonic Orchestra `.sfz` — or a placeholder GM SF2 if that patch is
missing. Notes outside any `withInstrument` keep the default voice.

For a voice not in the enum there are two custom-instrument constructors, both
producing an ordinary `Instrument` value — so bind a name and reuse it:

```
myCello = sfz "instruments/my-cello.sfz"   -- a custom .sfz sampler patch
nylon   = gm 24                            -- a raw General-MIDI program number
onInstrument myCello subject
onInstrument nylon   bass
```

(Naming gotcha: a binding can't be spelled like a pitch — `cello`/`e4` are reserved
notation — so use `myCello`, `nylon`, `cello2`, …)

- **`sfz "<path>"`** — a relative path resolves against the source `.cal` file's
  directory (so a project and its soundfonts travel together); an absolute path is
  used as-is. The audio backend plays it through sfizz; **MIDI has no program mapping
  for a `.sfz`**, so it stays on a plain channel (default voice).
- **`gm <n>`** — a General-MIDI program number. MIDI emits that program-change
  directly (its own channel); audio plays it through the fallback GM soundfont. Use
  `gm` when you want a named voice outside the enum that still exports cleanly to MIDI.

### Tempo, velocity & meter

| Function | Type | Meaning |
|----------|------|---------|
| `tempo` | `Phrase t => Int -> t -> Music` | play a phrase at the given tempo (beats per minute) |
| `velocity` | `Phrase t => Int -> t -> Music` | set the note-on velocity (0..127) for a phrase |
| `meter` | `Phrase t => Int -> Int -> t -> Music` | wrap a phrase in a time signature (`meter 3 4 …`) |

All three are `Control` nodes, like instruments — they wrap a sub-phrase and apply
only within it (an inner one overrides an outer). Tempo is resolved into real time,
so different tempi can even run at once under `par`:

```
tempo 90 subject                          -- the subject at 90 bpm
velocity 40 accompaniment                 -- softer accompaniment
par (tempo 120 melody) (tempo 80 drone)   -- two simultaneous tempi
```

The default (no wrapper) is 120 bpm, velocity 80, no meter.

**Meter is functional, not a label.** Under a `meter n d` the timing pass does two
real things:

- **Strong-beat accent (audible).** Onsets on the downbeat (and the mid-bar beat of
  simple meters, every third beat of compound ones) are played louder, so the same
  notes sound different under a different meter.
- **Bar validation.** The `|` barline checks the measure before it: it must fill
  exactly one bar of the active meter, else compiling reports it (`bar 3: 5/4 in a
  4/4 meter`). Without an enclosing `meter`, `|` is just an inert boundary.

```
meter 4 4 (c'4 d'4 e'4 f'4 | g'4 a'4 b'4 c''4)   -- two checked 4/4 bars, accented
meter 3 4 (c'4 d'4 e'4 | f'4 g'4 a'4)            -- waltz feel (downbeat accent)
```

Note durations stay absolute (a quarter is `1/4` whatever the meter); meter affects
accent and bar-checking, not how long a note is. The stdlib adds the named sugar
`commonTime` (4/4), `cutTime` (2/2) and `waltz` (3/4).

### Predicates

| Function | Type |
|----------|------|
| `isNote` `isRest` `isSeq` `isPar` | `Music -> Bool` |

### Accessors

| Function | Type | Meaning |
|----------|------|---------|
| `notePitch` | `Music -> Pitch` | the pitch of a note |
| `noteDur` | `Music -> Rational` | the duration of a note or rest |
| `leftChild` | `Music -> Music` | left child of a `Seq`/`Par` |
| `rightChild` | `Music -> Music` | right child of a `Seq`/`Par` |

```
-- walk and rebuild a phrase, rewriting each pitch
mapPitches func phrase =
  if isNote phrase then noteWith (func (notePitch phrase)) (noteDur phrase)
  else if isSeq phrase then sequence (mapPitches func (leftChild phrase)) (mapPitches func (rightChild phrase))
  else if isPar phrase then parallel (mapPitches func (leftChild phrase)) (mapPitches func (rightChild phrase))
  else phrase
```

`semitones`, the list axioms, and the comparison/arithmetic operators are the only
non-music primitives; the rest of the language's power comes from composing these
in Calliope.
