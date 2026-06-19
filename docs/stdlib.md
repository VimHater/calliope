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
| `onInstrument` | `Instrument -> Music -> Music` | play a phrase on a given instrument (a `Control` node) |

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
