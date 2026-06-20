# Standard library

The standard library lives in [`standard_library/prelude.cal`](../standard_library/prelude.cal)
and is written entirely in Calliope on top of the [core builtins](./builtins.md).
The driver loads it automatically, so every function below is in scope in any
program.

Type variables are shown as `t`, `u`, … (they cannot be spelled like a pitch).

## List helpers

| Function | Type | Description |
|----------|------|-------------|
| `length` | `[t] -> Int` | number of elements |
| `map` | `(t -> u) -> [t] -> [u]` | apply a function to every element |
| `filter` | `(t -> Bool) -> [t] -> [t]` | keep the elements satisfying a predicate |
| `reverse` | `[t] -> [t]` | reverse a list |
| `drop` | `Int -> [t] -> [t]` | drop the first n elements (or all, if shorter) |
| `append` | `[t] -> [t] -> [t]` | concatenate two lists |
| `foldr` | `(t -> u -> u) -> u -> [t] -> u` | right fold with a seed |
| `foldr1` | `(t -> t -> t) -> [t] -> t` | right fold of a non-empty list (no seed) |
| `flip` | `(t -> u -> v) -> u -> t -> v` | swap a function's first two arguments |

```
double x = x * 2
big x    = 3 < x

map double [1, 2, 3]              -- [2, 4, 6]
filter big [1, 2, 3, 4, 5]        -- [4, 5]
reverse [1, 2, 3, 4]              -- [4, 3, 2, 1]
length (drop 2 [1, 2, 3, 4, 5])  -- 3
```

Because top-level bindings are generalized in dependency order, a polymorphic
helper can be used at several types in one program:

```
main = length [1, 2, 3] + length [True, False]   -- 5
```

## Math

Built on the numeric axioms (`+ - *`, `/`, `div`, `mod`, the Ord comparisons).

| Function | Type | Description |
|----------|------|-------------|
| `negate` | `Int -> Int` | arithmetic negation (`0 - n`) |
| `abs` | `Int -> Int` | absolute value |
| `signum` | `Int -> Int` | sign as `-1`, `0`, or `1` |
| `even` `odd` | `Int -> Bool` | parity (via `mod n 2`) |
| `gcd` | `Int -> Int -> Int` | greatest common divisor (Euclid) |
| `lcm` | `Int -> Int -> Int` | least common multiple (`0` if either is `0`) |
| `power` | `Int -> Int -> Int` | `power n k` = nᵏ for `k >= 0` (`k <= 0` gives `1`) |
| `min` `max` | `Ord t => t -> t -> t` | smaller / larger of two ordered values |
| `clamp` | `Ord t => t -> t -> t -> t` | `clamp lo hi x` confines `x` to `[lo, hi]` |
| `sum` `product` | `[Int] -> Int` | sum / product of a list |
| `maximum` `minimum` | `Ord t => [t] -> t` | largest / smallest of a non-empty list |

`min` / `max` / `maximum` / `minimum` are `Ord`-polymorphic, so they order pitches
by height too (`max c' e'` is `E4`).

```
gcd 24 36          -- 12
power 2 10         -- 1024
clamp 0 10 15      -- 10
sum [1, 2, 3, 4]   -- 10
maximum [3, 9, 2]  -- 9
```

## Building phrases

| Function | Type | Description |
|----------|------|-------------|
| `notes` | `[Pitch] -> [Music]` | lift a list of pitches to notes |
| `line` | `[Music] -> Music` | play a list of phrases in sequence (`:+:`) |
| `chord` | `[Music] -> Music` | sound a list of phrases together (`:=:`) |
| `seqn` | `Music -> Music -> Music` | sequence two phrases (named `:+:`) |
| `par` | `Music -> Music -> Music` | layer two phrases (named `:=:`) |

```
line (notes [c', e', g'])    -- C4 :+: E4 :+: G4   (an arpeggio)
chord (notes [c', e', g'])   -- C4 :=: E4 :=: G4   (a triad)
melody `par` bassline        -- the two at once
```

## Transforms

| Function | Type | Description |
|----------|------|-------------|
| `transpose` | `Transposable t => Interval -> t -> t` | shift a pitch or phrase by an interval (`flip (^+)`) |
| `mapPitches` | `(Pitch -> Pitch) -> Music -> Music` | rewrite every pitch, keeping structure and durations |
| `firstPitch` | `Music -> Pitch` | the leftmost sounding pitch (inversion's axis) |
| `reflectPitch` | `Pitch -> Pitch -> Pitch` | mirror a pitch about an axis pitch (spelling mirrors too) |
| `invert` | `Music -> Music` | melodic inversion about the phrase's first pitch |
| `retrograde` | `Music -> Music` | play a phrase backwards in time |
| `times` | `Int -> Music -> Music` | repeat a phrase n times (n ≥ 1) |
| `onInstrument` | `Phrase t => Instrument -> t -> Music` | play a phrase on a given instrument (a `Control` node); a bare pitch lifts, so `onInstrument Cello c` works |
| `commonTime` | `Phrase t => t -> Music` | wrap a phrase in 4/4 (`meter 4 4`) — strong-beat accent + `\|` bar checking |
| `cutTime` | `Phrase t => t -> Music` | wrap a phrase in 2/2 (`meter 2 2`) |
| `waltz` | `Phrase t => t -> Music` | wrap a phrase in 3/4 (`meter 3 4`) |

### Dynamics

Named loudness levels — sugar over the [`velocity`](./builtins.md) Control axis. All
have type `Phrase t => t -> Music`, so a bare pitch lifts (`forte c'`). Spelled out
because the single-letter forte `f` collides with the pitch F. The default (no
dynamic) is velocity 80 (≈ *mezzo-forte*).

| Function | Mark | Velocity |
|----------|------|----------|
| `pianississimo` | ppp | 16 |
| `pianissimo` | pp | 33 |
| `piano` | p | 49 |
| `mezzoPiano` | mp | 64 |
| `mezzoForte` | mf | 80 |
| `forte` | f | 96 |
| `fortissimo` | ff | 112 |
| `fortississimo` | fff | 127 |

```
forte melody                 -- play the melody loud
piano accompaniment          -- ...the accompaniment soft
commonTime (forte subject)   -- dynamics nest with meter / instrument freely
```

### Articulations

Performance marks over the [`articulate`](./builtins.md) Control axis (a sounding-
length **gate** + a velocity **accent**). All are `Phrase t => t -> Music`, so a bare
pitch lifts (`staccato c'`) and wrapping a phrase articulates every note in it.

| Function | Effect | gate · accent |
|----------|--------|---------------|
| `staccato` | short, detached | `1/2 · 0` |
| `legato` / `slur` | smooth, full length | `1/1 · 0` |
| `tenuto` | full length, slight stress | `1/1 · +5` |
| `accent` | stressed (louder) | `1/1 · +15` |
| `marcato` | marked: strong + a touch short | `3/4 · +20` |

```
staccato (c' d' e' f')       -- four crisp detached notes
legato melody                -- ...or smooth and connected
accent (firstNote) :+: rest  -- stress one note
```

### Grace notes & ornaments

Grace notes steal time from the main note; ornaments expand a note into a fast figure
filling its duration. The main note is a **pitch** (it carries its own duration), the
neighbour a whole step (M2) away. (Key-aware ornaments wait on key support.)

| Function | Type | Result of `… d' c'4` / `… c'4` |
|----------|------|--------------------------------|
| `acciaccatura` | `Pitch -> Pitch -> Music` | `D4:1/16 :+: C4:3/16` (crushed grace) |
| `appoggiatura` | `Pitch -> Pitch -> Music` | `D4:1/8 :+: C4:1/8` (takes half) |
| `trill` | `Pitch -> Music` | `C D C D`, each a sixteenth |
| `mordent` | `Pitch -> Music` | `C D C` (last note holds the rest) |
| `turn` | `Pitch -> Music` | `D C Bb C` (upper, main, lower, main) |

```
acciaccatura b' c''4         -- a crushed grace B before C
trill g'2                    -- a half-note trill G–A–G–A
```

### Keys & scales

A **key respells the floating (bare) accidentals** in a phrase: under D major a bare
`f` becomes F#, while an explicit `fis`/`fes` is left alone, and with no key a bare
letter stays natural. A key is just its signature in **fifths** (+ sharps, − flats);
`major`/`minor` compute it from a tonic.

| Function | Type | Description |
|----------|------|-------------|
| `major` / `minor` | `Pitch -> Int` | the key signature (fifths) of the major / minor key on a tonic |
| `inKey` | `Phrase t => Int -> t -> Music` | play a phrase in a key — its floating accidentals snap to it, and the signature is tagged |
| `scaleDegree` | `Pitch -> Int -> Pitch` | the *n*th degree (1 = tonic) of the major key on a tonic |
| `diatonicUp` / `diatonicDown` | `Int -> Int -> Music -> Music` | transpose every pitch by *n* scale steps, respelled for the key |
| `trillIn` / `mordentIn` / `turnIn` | `Int -> Pitch -> Music` | key-aware ornaments — the neighbour is the next scale note in the key, not a fixed M2 |
| `keyPitch` | `Int -> Int -> Pitch` | a pitch at an absolute diatonic step, spelled for the key (the building block) |

```
inKey (major d') (c d e f g)    -- D major: C# D E F# G  (bare f -> F#)
inKey (minor a') melody         -- A minor (= no sharps/flats)
scaleDegree d' 5                -- A4 (the fifth degree of D major)
diatonicUp (major c') 2 motif   -- up a third within C major
trillIn (major d') f'4          -- a trill F#–G (the scale neighbour, not F#–G#)
```

Key is **orthogonal to octave mode** — `#relative`/`#absolute` choose how octave
marks resolve; a key chooses how bare accidentals resolve. They don't interact.

```
transpose P5 (c' e' g')   -- G4 B4 D5
retrograde (c' e' g')     -- G4 E4 C4
invert (c' e' g')         -- C4 Ab3 F3   (E and G mirror below C, respelled)
times 3 c'                -- C4 C4 C4
onInstrument Cello (c d e)            -- the cellos play C3 D3 E3
par (onInstrument Cello bass) (onInstrument Flute melody)   -- two voices, two instruments
```

`Instrument` is a typed enum (`Piano`, `Violin`, `Viola`, `Cello`, `Contrabass`,
`Strings`, `Trumpet`, `Trombone`, `Horn`, `Tuba`, `Flute`, `Oboe`, `Clarinet`,
`Bassoon`, `Harp`, `Harpsichord`). The MIDI/audio backends sound each tagged phrase
on its instrument; un-tagged notes use the default voice. For voices outside the
enum, `sfz "path/to.sfz"` (a `.sfz` patch) and `gm 24` (a raw GM program number) are
also `Instrument` values (see `docs/builtins.md`):

```
onInstrument (sfz "sounds/my-cello.sfz") subject
onInstrument (gm 24) bass                          -- a named GM voice, exports to MIDI
```

## A worked example

The classic fugal development — a subject sounded against its own inversion,
answered up a perfect fifth:

```
subject = c' d' e' g'
development subj = subj `par` (invert subj ^+ P5)
main = development subject
```

evaluates to

```
(C4 D4 E4 G4) :=: (G4 F4 Eb4 C4)
```

the subject `C D E G` layered with its inversion `(C Bb Ab F)` transposed up a
fifth to `G F Eb C`.

## Not yet in the library

Intervals as a monoid (`<>`, `intervalBetween`), scales and keys, chords by
quality / roman numerals, named durations and rhythm helpers, and musical context
(`tempo`, `dynamic`, `inKey`) are planned but not implemented yet. See
[`../language_spec.md`](../language_spec.md) §14.
