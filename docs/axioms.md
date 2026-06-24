# Axioms

The **axioms** are the primitives the C++ core exposes to the language. They are
*irreducible*: each one touches a representation the language itself cannot see — a
machine number, an exact `Rational`, a spelled `Pitch`, a list cell, a `Music` IR
node, an `Instrument`, a `Control` axis — so it cannot be written in Calliope.

Everything else is **standard library**, written in Calliope on top of these (see
[`stdlib.md`](./stdlib.md)). The split is deliberate (CLAUDE.md / spec §14): the
C++ core stays thin and holds only axioms; all *music theory* — intervals, scales,
keys, chords, transforms, dynamics, articulations, ornaments — lives in
[`../standard_library/prelude.cal`](../standard_library/prelude.cal).

There are ~50 axioms below supporting ~75 stdlib functions. Type variables are
written `a`, `t`, `u` (they cannot be spelled like a pitch). For a task-oriented
view of the same primitives see [`builtins.md`](./builtins.md).

---

## Programming axioms

### Arithmetic

`+ - *` are a single-parameter class **`Num`** (instances `Int`, `Rational`), so
the same operators work on whole numbers and exact fractions. Operands share a type
— no implicit `Int`/`Rational` mixing; lift with `toRational`.

| Axiom | Type | Notes |
|-------|------|-------|
| `+` `-` `*` | `Num a => a -> a -> a` | add / subtract / multiply |
| `/` | `Int -> Int -> Rational` | fractional division (`7 / 2` = `7/2`) |
| `div` | `Int -> Int -> Int` | integer division (infix: `` 7 `div` 2 ``) |
| `mod` | `Int -> Int -> Int` | remainder |
| `toRational` | `Int -> Rational` | lift an `Int` into the `Rational` world |

### Comparison

| Axiom | Type | Notes |
|-------|------|-------|
| `==` `/=` | `a -> a -> Bool` | structural equality (on `Int`/`Bool`/`Pitch`/`Music`/lists) |
| `<` `>` `<=` `>=` | `Ord t => t -> t -> Bool` | class **`Ord`** (instances `Int`, `Pitch` — pitches by height); `Music` has no order |

### Boolean

| Axiom | Type | Notes |
|-------|------|-------|
| `and` `or` | `Bool -> Bool -> Bool` | short-circuiting |
| `not` | `Bool -> Bool` | negation |
| `True` `False` | `Bool` | the boolean literals |

### Lists

The list representation primitives; the stdlib (`map`, `length`, `foldr`, …) is
built entirely from these.

| Axiom | Type | Notes |
|-------|------|-------|
| `null` | `[a] -> Bool` | is the list empty? |
| `head` | `[a] -> a` | first element |
| `tail` | `[a] -> [a]` | all but the first |
| `cons` | `a -> [a] -> [a]` | prepend (the `:` operator) |

### Plumbing

| Axiom | Type | Notes |
|-------|------|-------|
| `\|>` | `a -> (a -> b) -> b` | pipe: `x \|> f` = `f x` |

---

## Music axioms

### Pitch projections

The bridge between the spelled `Pitch` (letter + accidental + octave, kept exact —
C♯ ≠ D♭) and integers. All music theory is built on these.

| Axiom | Type | Notes |
|-------|------|-------|
| `semitones` | `Pitch -> Int` | absolute semitone height |
| `diatonicStep` | `Pitch -> Int` | absolute diatonic step (letter + octave) |
| `chromaticOf` | `Int -> Int` | the chromatic offset of a diatonic step |
| `makePitch` | `Int -> Int -> Pitch` | `makePitch step accidental` builds a spelled pitch |
| `keyAccidental` | `Int -> Int -> Int` | the accidental a key (fifths) gives a letter |

### Music IR — constructors

The `Music` tree (`Note \| Rest \| Seq \| Par \| Control`) is a C++ index-pooled
structure; these build its leaves and inner nodes.

| Axiom | Type | Notes |
|-------|------|-------|
| `note` | `Pitch -> Music` | a note at the default (quarter) duration |
| `noteWith` | `Pitch -> Rational -> Music` | a note with an explicit duration |
| `sequence` | `Music -> Music -> Music` | play one after another (`Seq`) |
| `parallel` | `Music -> Music -> Music` | play together (`Par`) |

### Music IR — predicates & accessors

Inspect the tree (there is no `data`/pattern matching on `Music` yet, so these
stand in).

| Axiom | Type | Notes |
|-------|------|-------|
| `isNote` `isRest` `isSeq` `isPar` | `Music -> Bool` | shape tests |
| `leftChild` `rightChild` | `Music -> Music` | the children of a `Seq`/`Par` |
| `notePitch` | `Music -> Pitch` | the pitch of a note |
| `noteDur` | `Music -> Rational` | the duration of a note |

### Phrase composition

`:+:` / `:=:` compose via a single-parameter class **`Phrase`** (instances `Pitch`,
`Music`), so a bare pitch lifts to a one-note phrase. The two operands may differ
(`c :+: (d e)` is `Pitch :+: Music`); the result is always `Music`.

| Axiom | Type | Notes |
|-------|------|-------|
| `:+:` | `Phrase t => Phrase u => t -> u -> Music` | sequence (adjacency `c d e` desugars to this) |
| `:=:` | `Phrase t => Phrase u => t -> u -> Music` | overlay |
| `:*:` | `Phrase t => t -> Int -> Music` | repeat n times in a row (`n >= 1`) |
| `\|` | `Phrase t => Phrase u => t -> u -> Music` | barline (sequence + a bar boundary marker) |
| `asMusic` | `Phrase t => t -> Music` | lift any phrase to `Music` |

### Transposition

A single-parameter class **`Transposable`** (instances `Pitch`, `Music`); a user
`instance Transposable …` extends the same operators.

| Axiom | Type | Notes |
|-------|------|-------|
| `^+` `^-` | `Transposable a => a -> Interval -> a` | transpose up / down by an interval |

### Duration

| Axiom | Type | Notes |
|-------|------|-------|
| `tuplet` | `Int -> Int -> Music -> Music` | `tuplet n m` — n notes in the time of m (scales durations by m/n) |

### Control axes

Each builds a `Control` (Modify) node wrapping a sub-phrase along one axis. Like
`:+:`, the phrase argument is a `Phrase t`, so a bare pitch lifts (`tempo 90 c'`).

| Axiom | Type | Notes |
|-------|------|-------|
| `withInstrument` | `Phrase t => Instrument -> t -> Music` | assign an instrument (stdlib `onInstrument`) |
| `tempo` | `Phrase t => Int -> t -> Music` | tempo in bpm |
| `velocity` | `Phrase t => Int -> t -> Music` | MIDI velocity 0–127 (named dynamics build on this) |
| `meter` | `Phrase t => Int -> Int -> t -> Music` | time signature `meter num den` |
| `articulate` | `Phrase t => Rational -> Int -> t -> Music` | a sounding-length gate + a velocity accent |
| `sustain` | `Phrase t => t -> Music` | damper pedal — notes ring to the phrase end |
| `withKey` | `Phrase t => Int -> t -> Music` | key signature in fifths (stdlib `inKey` resolves floating accidentals over this) |

### Instrument values

First-class `Instrument` values outside the builtin enum.

| Axiom | Type | Notes |
|-------|------|-------|
| `sfz` | `Str -> Instrument` | a custom instrument from a `.sfz` patch path |
| `gm` | `Int -> Instrument` | a custom instrument from a raw GM program number |

---

## Nullary constructors

Recognized at **lex time** (like reserved words), not function builtins — any token
matching the name is the constructor.

- **`Interval`** : `P1 m2 M2 m3 M3 P4 A4 d5 P5 m6 M6 m7 M7 P8`
- **`Instrument`** : `Piano Violin Viola Cello Contrabass Strings Trumpet Trombone
  Horn Tuba Flute Oboe Clarinet Bassoon Harp Harpsichord`

---

## What is *not* an axiom

Everything derivable lives in the stdlib, e.g. `map` `filter` `foldr` `length`,
`transpose` `invert` `retrograde` `ottava` `times`, `line` `chord` `notes`,
`major` `minor` `inKey` `scaleDegree`, the named dynamics (`piano` … `forte`),
articulations (`staccato` `legato` `accent`), grace notes and ornaments
(`acciaccatura` `trill` `mordent` `turn`). See [`stdlib.md`](./stdlib.md).

The boundary is the test for adding a primitive to C++: **if it can be written in
Calliope, it belongs in the prelude, not here.** The one axiom that arguably could
move is `keyAccidental` (pure circle-of-fifths arithmetic).
