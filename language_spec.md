# Calliope — Language Specification (Draft)

> A functional DSL for composing music programmatically.
>
> Status: **design draft**. Nothing here is final — this document exists so we
> can argue about syntax and semantics *before* committing to an implementation.

---

## 1. Philosophy

Calliope lets a composer write *what they mean*, not *what they play*.

A composer does not think:

```
note C4 quarter, note E4 quarter, note G4 half, ...
```

A composer thinks:

```haskell
-- "the development takes the subject, inverts it,
--  and sequences it against itself at the fifth"
development :: Phrase -> Phrase
development subject = subject `par` (invert subject + P5)
```

The language is built on two convictions:

1. **Music is a value.** A motif, a phrase, a whole symphony — all are
   first-class values that can be named, passed to functions, transformed, and
   combined. Composition (of music) *is* function composition (of code).

2. **Transformation is the unit of thought.** Inversion, retrograde,
   augmentation, sequence, transposition — these are the verbs of counterpoint
   and development. They are primitives, not things you hand-spell note by note.

### Design goals

| Goal | Consequence |
|------|-------------|
| Read like a composer's intent | Haskell-like surface syntax, point-free where natural |
| Notation should feel familiar | Note/rhythm literals modeled on LilyPond |
| Total & referentially transparent | No hidden state; the same expression is the same music |
| Strongly typed | A `Pitch` is not a `Duration`; intervals compose by type |
| Composable backends | One score → MIDI playback, MusicXML, live render |

### Inspirations (prior art we are explicitly borrowing from)

- **Haskell** — surface syntax, type classes, purity, laziness.
- **Haskore / Euterpea** — music-as-algebraic-data-type; `:+:` / `:=:`.
- **LilyPond** — the *notation literal* sublanguage (pitch names, octave ticks,
  duration numbers, articulations).

---

## 2. Lexical convention — notation is the default register

Calliope has **no wrapper around notation**. Bare note tokens *are* music. A
token is a *pitch literal* when it matches the pitch grammar: a lowercase pitch
name, optionally followed by accidental, octave, and duration.

```
c   d   e   f   g   a   b            -- naturals
cis dis ...    ces des ...           -- sharps (-is) / flats (-es)
c'  c''        c,  c,,               -- octave up / down
c4  d8  g2.                          -- with duration
```

**The pitch class is reserved.** Any token matching the pitch grammar is *always*
a pitch — it can never be an identifier. Conversely, identifiers may not be
spelled like a pitch. So `c`, `f`, `cis`, `e4` are off-limits as variable names;
every other name is free (`subject`, `motif`, `x`, `n`, `development`).

That single rule removes the only ambiguity — a lone `c` is unmistakably the
note C, decided at lex time, before types or scope. No sigil, no delimiter.

| Token shape | Meaning |
|---|---|
| pitch-shaped (`c`, `fis`, `g'`, `e4`) | pitch literal — **always**, reserved |
| `r`  `R`  `s` | rest · multimeasure rest · invisible spacer |
| any other lowercase word (`subject`, `invert`, `where`) | identifier or keyword |
| Uppercase (`P5`, `M3`, `Major`, `F`) | interval · constructor · dynamic |

Lowercase pitches vs. uppercase constructors is deliberate: `f` is the note F,
`F` is the dynamic *forte*; `c` is the note, `C` is the `PitchClass` constructor.

The motto example needs no delimiter and no sigil:

```haskell
subject :: Phrase
subject = c'4 d'8 e'8 g'4 a'2

development subject = subject `par` (invert subject + P5)
```

Here `subject` is a normal identifier (not pitch-shaped), and the `c d e g a`
inside it are pitches — never confused, because the names you choose simply
cannot collide with the pitch class.

> **Rule — adjacency is sequence.** A run of adjacent *pitch literals* composes
> sequentially: `c d e` ≡ `c :+: d :+: e`. To sequence *named* values you use an
> explicit operator (`a :+: b` is illegal — `a`,`b` are pitches; use
> `motif1 :+: motif2` or `line [motif1, motif2]`). Juxtaposition with a non-pitch
> head (`invert subject`) is always function application. All decidable
> lexically, so parsing stays context-free. See §6.

---

## 3. Core types

```haskell
data Pitch                       -- a tuned pitch, e.g. C#5
data PitchClass = C | D | E | F | G | A | B
data Accidental = Natural | Sharp | Flat | DoubleSharp | DoubleFlat
data Interval                    -- a directed distance, e.g. P5, m3
data Duration                    -- a span of musical time (a rational)
data Dynamic = PPP | PP | P | MP | MF | F | FF | FFF
data Articulation = Staccato | Tenuto | Accent | Marcato | Legato

-- The central recursive structure: a tree of music in time.
data Music
  = Prim Primitive               -- a single note or rest
  | Music :+: Music              -- sequential   (one after another)
  | Music :=: Music              -- parallel     (at the same time)
  | Modify Control Music         -- a context applied to a subtree

data Primitive = Note Duration Pitch
               | Rest Duration

data Control = Tempo Ratio | Transpose Interval | Instrument Instr
             | Dyn Dynamic | Phrasing Articulation | KeySig Key

type Phrase = Music              -- alias used for intent; same type
type Motif  = Music
```

The `:+:` / `:=:` tree is the *core IR*. Notation literals and every combinator
in §6 desugar into it.

---

## 4. Pitch & interval algebra

Pitches and intervals form a small algebra so that transposition reads like
arithmetic.

```haskell
-- Intervals as named constructors (quality + number)
P1 P4 P5 P8        -- perfect
M2 M3 M6 M7        -- major
m2 m3 m6 m7        -- minor
A4 d5              -- augmented / diminished (tritone spellings)

-- Pitch + Interval = Pitch
(+) :: Pitch -> Interval -> Pitch
-- Pitch - Pitch = Interval
(-) :: Pitch -> Pitch -> Interval
-- Intervals add
(+) :: Interval -> Interval -> Interval   -- M3 + m3 = P5

-- And the same `+` lifts over whole pieces of music:
(+) :: Music -> Interval -> Music         -- transpose every pitch
```

This is what makes `invert subject + P5` legal: `invert subject :: Music`, and
`+ P5` transposes the whole result up a perfect fifth.

> **Open question (O2):** do we keep enharmonic spelling (C# ≠ Db) for correct
> notation, or collapse to 12-TET pitch numbers for simplicity? Spelling matters
> for LilyPond/MusicXML output; pitch numbers are easier for playback. Proposal:
> keep spelling in the type, expose `semitones :: Pitch -> Int` for playback.

---

## 5. Notation literals (LilyPond register)

Pitch and rhythm notation mirrors LilyPond. These tokens are the default
register (§2) — they need no wrapper.

### Pitches

```
c d e f g a b          -- natural pitch classes
cis dis ...            -- sharps  (-is)
ces des ...            -- flats   (-es)
c' c'' c'''            -- octave up   (each ' = +1 octave)
c, c,, c,,,            -- octave down (each , = -1 octave)
```

Reference octave: `c'` = middle C (C4). Octaves are **absolute by default**; a
**relative** mode (§5.1) lets you omit most ticks the way LilyPond does.

### Durations

A number after a pitch is its duration: `1` whole, `2` half, `4` quarter,
`8` eighth, `16`, `32`. A trailing `.` dots it; a duration persists until
changed (LilyPond behavior).

```
c'4 d' e' f'           -- four quarter notes (4 carries over)
g'2. a'4               -- dotted half, then quarter
```

### Rests, chords, ties, tuplets

```
r4                     -- quarter rest
<c' e' g'>2            -- a chord (C major triad), half note
c'4~ c'8               -- tie
\tuplet 3/2 { c'8 d' e' }   -- triplet
```

### Bars and structure

```
c'4 d' e' f' | g'1     -- | is a barline check (validates the meter)
```

> Notation is *sugar* over the IR. `c'4 e'4 g'2` desugars to
> `Note (1/4) C4 :+: Note (1/4) E4 :+: Note (1/2) G4`.

### 5.1 Octave modes — absolute (default) and relative (opt-in)

Octaves are **absolute by default**: `c'` is middle C (C4), each `'`/`,` an
absolute octave above/below the fixed reference. No declaration needed.

A composer opts into **relative octaves** with the `#relative` directive (§5.3),
read the way LilyPond reads it but without the brace syntax: **each letter takes
the octave nearest the previous pitch** (within a fourth — three staff steps);
`'`/`,` then nudge by whole octaves *from that nearest choice*. `#absolute`
switches back.

**The argument is the anchor.** `#relative <ref>` takes an **absolute pitch**
(pitch class + octave ticks, *no duration*: `c'`, `g`, `d''`, `c,`). It seeds
"previous pitch" before the *first* note — that note picks the octave nearest
`<ref>`; everything after threads from the resolved previous note. Only the
anchor's octave position matters. **The argument is optional and defaults to
`c'`** (middle C), so `#relative` ≡ `#relative c'`.

```
              c'4 d'' a' g'        -- absolute (default): ticks are literal

#relative c'                       -- anchor C4 (also the default if omitted)
ascent = c d a g                   -- first c nearest C4 -> c', then climb
twoUp  = c f                       -- f within a 4th up        -> f'
down   = c g                       -- nearest g is a 4th DOWN  -> g
forced = c g'                      -- ' overrides the choice   -> g'

#relative c                        -- anchor C3 -> whole run an octave lower
lower  = c d e                     -- -> c d e  (C3 D3 E3)

#relative g                        -- anchor G3
leap   = c                         -- nearest C: C4 (4th up) beats C3 (5th) -> c'
#absolute
```

For chords, the first note is relative to the previous pitch; the rest of the
chord is relative to that first note.

**Implementation — an AST elaboration pass (not lexer, parser, or runtime).**
Relative resolution needs sequential context (a left-to-right fold carrying the
previous resolved pitch) and is purely syntactic, so it runs as a pass over the
parsed AST, after the preprocessor has marked which octave mode each region is in:

```
preprocess ─► parse ─► octave-resolution pass ─► typecheck ─► desugar ─► Music IR
```

The parser does **not** emit a final `Pitch`. It emits a raw token that keeps the
parts separate, so the pass can interpret it under whichever mode the region is in:

```haskell
data RawPitch = RawPitch
  { letter      :: PitchClass
  , accidental  :: Accidental
  , octaveMarks :: Int          -- net of ' (+1) and , (-1)
  , absolute    :: Bool }       -- did the token fix its own octave?
```

The pass rewrites `RawPitch → Pitch`; nothing relative survives into the IR,
which is always absolute. In absolute mode the pass is trivial
(`octave = reference + octaveMarks`); relative mode is the same pass threading
"previous pitch" through a `#relative` region. So the two modes share one
mechanism and adding/removing either touches no other stage.

> **Threading detail (to pin down):** within a `#relative` region, "previous
> pitch" carries along a contiguous notation run and re-seeds from the directive's
> reference at each new top-level binding. Threading *across* function boundaries
> is intentionally undefined — relative input is for linear melodic writing, not
> for re-reading already-named phrases.

> A runtime `octaveShift :: Int -> Music -> Music` exists to move an
> *already-built* phrase by octaves — a different, later operation, unrelated to
> how notation is read.

### 5.2 Key (and musical context generally)

There is **no notation directive for key, tempo, dynamics, or instrument**.
Musical context is expressed with the ordinary term-level combinators of §8 —
`inKey`, `tempo`, `dynamic`, `instrument` — each producing a `Modify Control`
node. (We deliberately drop LilyPond's `\key`/`\tempo` backslash forms: they read
like parser fodder, not like something a human wants to write.)

```haskell
piece = inKey (major D) (tempo allegro melody)   -- not \key, not \tempo
```

Key is **context metadata**: printed signature, playback context, and the anchor
for an optional future scale-degree input layer. It does **not** rewrite letters
— `f` is F-natural in every key; write `fis` for F♯ even in D major.
LilyPond-faithful in *meaning* (a key changes what is *printed around* a note,
never what the letter sounds), without the backslash syntax.

### 5.3 Preprocessor directives

Directives begin with `#` and are handled by a **preprocess pass that runs before
parsing**. They concern *how the source text is read* — never musical semantics
(that is the job of §8 combinators). Think C preprocessor, not LilyPond.

| Directive | Effect |
|---|---|
| `#relative [<ref>]` | enter relative-octave mode, anchored at absolute pitch `<ref>` — optional, defaults to `c'` (§5.1) |
| `#absolute` | return to absolute-octave mode (the default) |
| `#load "<file>"` | pull in another `.cal` source's top-level definitions |

`<ref>` is a bare absolute pitch token — pitch class, accidental, and octave
ticks, but **no duration** (`#relative c'`, never `#relative c'4`).

Directives are **region-scoped toggles**: a mode directive applies from its line
until the next mode directive (or end of file), not a brace-delimited block.

This set is deliberately small and **expected to grow** (e.g. `#define` macros,
conditional inclusion, tuning/temperament selection). The design rule for adding
one: it belongs here only if it changes how text is *read or assembled*; anything
that changes what the music *is* must be a value/combinator instead. The exact
directive grammar and the `#load` model (textual include now, possibly a scoped
module system later) are open — see O12.

---

## 6. Combinators — putting music together

Operators are primary; each has an English-word alias (use whichever reads
better at the call site).

```haskell
-- Sequential: b begins when a ends
(:+:)   :: Music -> Music -> Music
andThen :: Music -> Music -> Music     -- = (:+:)
line    :: [Music] -> Music            -- fold (:+:)

-- Parallel: a and b sound together
(:=:)   :: Music -> Music -> Music
par     :: Music -> Music -> Music     -- = (:=:), reads as "against"
chord   :: [Music] -> Music            -- fold (:=:)

-- Repetition
times   :: Int -> Music -> Music       -- repeat n times sequentially
```

So `motif1 :+: motif2` and ``motif1 `andThen` motif2`` are the same;
`upper :=: lower` and ``upper `par` lower`` are the same.

Example — a I–IV–V–I progression as block chords over a melody:

```haskell
progression :: Music
progression = chords `par` melody
  where chords = line [cMaj, fMaj, gMaj, cMaj]
        melody = e'4 f' g' g'1
```

---

## 7. Transformations — the verbs of composition

```haskell
transpose   :: Interval -> Music -> Music   -- (or: m + interval)
invert      :: Music -> Music               -- melodic inversion about 1st pitch
invertAbout :: Pitch -> Music -> Music       -- inversion about a chosen axis
retrograde  :: Music -> Music               -- reverse in time
augment     :: Ratio -> Music -> Music      -- stretch durations
diminish    :: Ratio -> Music -> Music      -- compress durations

-- A musical *sequence*: restate a motif, each time shifted by an interval.
sequenceBy  :: [Interval] -> Music -> Music
-- e.g. sequenceBy [P1, M2, M2] motif  -- motif, then up a 2nd, then again
```

These let development sections read as English:

```haskell
-- a stretto: the subject entering against itself, a fifth up, delayed a bar
stretto :: Phrase -> Phrase
stretto subj = subj `par` (rest 1 :+: (subj + P5))

-- a classic fugal answer (tonal): invert and answer at the fifth
answer :: Phrase -> Phrase
answer = (+ P5) . invert
```

---

## 8. Context: tempo, dynamics, instruments, key

These wrap a subtree in a `Control` without touching its notes.

```haskell
tempo      :: BPM -> Music -> Music
dynamic    :: Dynamic -> Music -> Music
instrument :: Instr -> Music -> Music
inKey      :: Key -> Music -> Music
withArt    :: Articulation -> Music -> Music

andante, allegro :: BPM            -- named tempi
violin, cello, flute :: Instr      -- General MIDI instruments
```

```haskell
opening :: Music
opening =
  tempo allegro $
  inKey (major A) $
    instrument violin melody `par` instrument cello bassline
```

---

## 9. The term language (Haskell subset)

Calliope's non-musical syntax is a pragmatic subset of Haskell:

- **Bindings & functions:** `name args = expr`, `where`, `let ... in`.
- **Types & signatures:** `name :: Type`, `data`, `type` aliases.
- **Type classes:** at least the ones the music algebra needs —
  `Transposable`, `Invertible`, `Reversible`, `Temporal` (`durationOf`).
- **Lists & ranges, lambdas, guards, pattern matching, `if/then/else`.**
- **Operators** with fixity declarations (`:+:` `:=:` `+` need precedences so
  `m1 :+: m2 + P5` parses as intended).

> **Decided (O4 / O6):** full **Hindley–Milner** type inference, with parametric
> polymorphism and (user-definable) type classes — so signatures are optional and
> the music algebra's classes (`Transposable`, `Invertible`, …) are real, not
> built-in special cases. **No laziness (O7):** evaluation is strict/eager. So:
> *typed like Haskell, evaluated like ML.* See §13 for the execution model.

Proposed minimum operator fixities:

```haskell
infixr 6 +        -- transposition / interval addition
infixr 5 :+:      -- sequence
infixr 4 :=:      -- parallel  (binds looser than sequence)
```

---

## 10. A worked example

```haskell
-- Frère Jacques as a four-voice round.
-- Phrase names are ordinary identifiers; they just can't be pitch-shaped, so we
-- name the four lines by their lyric rather than a, b, c, d (those are pitches).

frere :: Phrase
frere = line [ jacques, dormez, sonnez, dindan ]
  where
    jacques = c'4 d' e' c'           -- "Frère Jacques"
    dormez  = e'4 f' g'2             -- "Dormez-vous?"
    sonnez  = g'8 a' g' f' e'4 c'    -- "Sonnez les matines"
    dindan  = c'4 g c'2              -- "Din dan don"

round4 :: Music
round4 = chord
  [ delay 0 frere
  , delay 2 frere       -- second voice enters 2 bars later
  , delay 4 frere
  , delay 6 frere
  ]
  where delay n m = rest n :+: m

main :: Music
main = tempo andante (instrument piano round4)
```

`main :: Music` is the score the toolchain renders (§11).

---

## 11. Implementation — open questions (to decide together)

The host is **C++** (this repo: CMake, audio + notation libs already vendored).
Calliope source is read by a C++ frontend and lowered to the `Music` IR (§3),
then fanned out to backends.

```
 .cal source
     │  preprocess (#directives) → lex → parse → octave-resolve
     │  → type check → desugar notation literals
     ▼
  Music IR  (Prim / :+: / :=: / Modify  tree)
     │  schedule: flatten to timed events
     ▼
  Score (list of timed Note events with controls)
     ├── MIDI / synth events ──► fluidsynth · sfizz · tsf  → miniaudio  (playback)
     ├── MusicXML ──────────────► (hoxml is a *reader*; we need a writer) (engraving)
     └── live view ─────────────► raylib  (piano-roll / staff render)
```

Decisions we should make explicitly:

- **O5 — Evaluation model. _Decided:_** tree-walking interpreter for v0.1 (music
  programs are tiny; synthesis, not evaluation, is the bottleneck). A bytecode VM
  is a later option — opcode sketch kept in §13.3. The **score IR** (§13.2) is
  built regardless of how layer 1 evaluates.
- **O6 — Type system depth. _Decided:_** full Hindley–Milner inference with type
  classes (folded into O4 above).
- **O7 — Laziness. _Decided:_** none — strict/eager evaluation. Infinite
  generative processes use explicit producers (e.g. `take n (cycle motif)` style
  combinators) rather than implicit laziness.
- **O8 — Pitch representation.** Spelled pitch (C# vs Db) vs. semitone integer
  (O2). Recommendation: spelled in IR, `semitones` for playback.
- **O9 — Output target priority.** Which backend first? Recommendation: audio
  playback (we already vendor three synths + miniaudio) — instant feedback
  loop. MusicXML/engraving second. Note hoxml *parses* MusicXML; we'd write our
  own emitter.
- **O10 — Octave mode. _Decided:_** absolute by default, with opt-in `#relative
  <ref>` / `#absolute` region directives (§5.1, §5.3). Both modes share one **AST
  octave-resolution pass** (`preprocess → parse → resolve → typecheck → desugar`);
  the parser emits `RawPitch` (letter / accidental / octave-marks / absolute-flag)
  so the pass can interpret octaves under whichever region mode applies. Nothing
  relative survives — the IR is always absolute. **Key/tempo/dynamics** are *not*
  directives: they are the §8 term-level combinators (`inKey`, `tempo`, …)
  producing `Control` nodes; key is metadata only and letters stay literal (§5.2).
  *(Two related questions also resolved earlier: O1 — no notation delimiter; and
  pitch/identifier disambiguation — the pitch lexical class is reserved, no
  sigil. See §2.)*
- **O12 — Preprocessor / directive system.** The `#` directive set (§5.3) is
  intentionally minimal now (`#relative`, `#absolute`, `#load`) and will grow.
  Open: the exact directive grammar; the `#load` model (textual include vs. a
  scoped module system); whether to add macros (`#define`) and conditional
  inclusion; and the precise "previous pitch" threading rule inside `#relative`
  regions (§5.1). Guiding constraint: directives may only affect how text is
  *read or assembled*, never what the music *is*.
- **O11 — Time model.** Durations as exact rationals (recommended — avoids
  floating-point drift in tuplets/ties) vs. ticks-per-quarter integer grid.

---

## 12. Glossary of intent

| Composer says | Calliope expression |
|---|---|
| "the subject" | a named `Phrase` value, `subject` |
| "against itself" | `par` / `:=:` |
| "at the fifth" | `+ P5` |
| "inverted" | `invert` |
| "in retrograde" | `retrograde` |
| "augmented" | `augment 2` |
| "a bar later" | `rest 1 :+: ...` |
| "sequenced upward" | `sequenceBy [M2, M2, ...]` |

---

## 13. Execution model

The word "bytecode" can mean two different things here. Keep them separate:

```
source ──► [layer 1] evaluate ──► Music IR tree ──► [layer 2] lower ──► score IR ──► backends
           the functional program  (§3)             schedule to time    (timed events)
```

- **Layer 1** runs the *functional program* — function calls, transforms, list
  folds — and its result is a `Music` value (the IR tree of §3).
- **Layer 2** walks that finished tree and flattens it into a sorted stream of
  timed events — the thing the synths actually play.

Most people who picture a "music bytecode" are picturing layer 2. The two are
designed independently.

### 13.1 Layer 1 — evaluation (tree-walking, v0.1)

Strict, Hindley–Milner-typed (O4/O6/O7). For v0.1 we **tree-walk** the typed
AST; music programs are small (a symphony ≈ thousands of nodes) and the real cost
is synthesis, not evaluation.

`Music` nodes are immutable and **shared**: using `subject` twice references the
same subtree; a transform allocates only new parent nodes. In C++ this needs no
GC — an **arena** holds the whole build-and-render pass and is freed wholesale at
the end. The IR is build-once, consume-once.

### 13.2 Layer 2 — the score IR (build this regardless)

Lowering resolves `:+:` (sequence), `:=:` (parallel), and `Modify` (context)
into absolute onset times, producing a flat list sorted by time. It is
deliberately **MIDI-shaped**, because fluidsynth / sfizz / tsf consume exactly
this — and it can be emitted as a real `.mid` for free.

```
t (beats)   event
  0.0       TEMPO     120
  0.0       PROGRAM   ch0, harpsichord
  0.0       NOTE_ON   ch0, C5, vel 80
  0.25      NOTE_OFF  ch0, C5
  0.25      NOTE_ON   ch0, D5, vel 80
  ...
```

Times are exact **rationals** through lowering, converted to integer ticks only
at this boundary (O11) — tuplets and ties stay drift-free. Every backend (audio,
MusicXML, live view) branches from this one IR.

### 13.3 Future option — an evaluation bytecode VM

Only worth it if heavy generative/algorithmic composition makes tree-walking the
bottleneck. If so, the shape is a compact **stack machine**: generic FP core
(`PUSH_CONST`, `LOAD`/`STORE`, `MAKE_CLOSURE`, `CALL`/`TAILCALL`, `RET`, `JMP`,
`JMP_FALSE`, `CONS`/`NIL`) plus a handful of music-construction opcodes, since
the IR algebra is small and hot:

```
NOTE / REST          pop dur (+ pitch)  → push a Prim
SEQ / PAR            pop b, a           → a :+: b   /   a :=: b
MODIFY c             wrap TOS in a Control
TRANSPOSE            pop interval, m    → shifted m      (the `+ P5`)
INVERT / RETRO / AUGMENT r              structural transforms
```

`subject = c'4 d'8` compiles to
`PUSH_DUR 1/4 · PUSH_PITCH c' · NOTE · PUSH_DUR 1/8 · PUSH_PITCH d' · NOTE · SEQ`;
`subject `par` (invert subject + P5)` to
`LOAD 0 · LOAD 0 · INVERT · PUSH_INTERVAL P5 · TRANSPOSE · PAR`.

Note this VM still only *builds the IR tree*; layer 2 is unchanged. So adopting
it later is non-breaking.
