# CLAUDE.md

Guidance for Claude Code (and humans) working in this repo.

## What Calliope is

A **functional DSL for composing music programmatically**. The aim is an
*expressive* language that maps how a composer thinks — transformations of
phrases (invert, retrograde, sequence, transpose), not note-by-note data entry.

```
-- the intent we are designing for:
development :: Phrase -> Phrase
development subject = subject `par` (invert subject ^+ P5)
```

- **Surface syntax:** Haskell-like (functions, types, type classes, purity).
- **Notation is the default register:** bare lowercase `a`–`g` (+ accidental /
  octave `'`/`,` / duration) *are* pitch literals; a run of them sequences
  (`c d e` ≡ `c :+: d :+: e`); chords `<c' e' g'>`. No wrapper block.
- **Pitch class is reserved (no sigil):** any token matching the pitch grammar
  is *always* a pitch, decided at lex time. Identifiers therefore may not be
  spelled like a pitch — `c`, `f`, `cis`, `e4` are off-limits as names; every
  other name is free (`subject`, `motif`, `x`). Built-ins (`invert`, `par`,
  `tempo`) and uppercase (`P5`, `F`, `Major`) round out the lexicon. See spec §2.
- The full language design lives in **[language_spec.md](./language_spec.md)** —
  read it before touching language semantics. It is a *draft*; open questions are
  tagged `O1`..`O11`. Several are now resolved (see "Design decisions" below);
  O8–O11 remain open.

## Status (2026-06-19)

Frontend first cut exists; nothing executes music yet.

- **Entry points are thin I/O shells — read source, write results — and delegate
  the whole pipeline to `core/driver.{hpp,cpp}`** (`calliope::driver::compile` /
  `compile_expr` / `type_of_*`, filling a self-contained `Compilation` struct).
  Keep new language logic in the core APIs, not in `main`. Host-side plumbing
  shared by both entry points (file read, prelude path, error printing, `--dump`,
  the output-backend dispatch + `Emit`) lives in **`helper.{hpp,cpp}`**
  (`calliope::cli`), so `compiler.cpp` / `calliopei.cpp` are just arg parsing +
  orchestration.
- `src/compiler/calliope.cpp` — `calliope` is the **compiler** (files only; no live
  playback — that's `calliopei`'s job). **Behavior keys off the input *and* output
  extension.** A **MusicXML input** (`.mxl`/`.xml`/`.musicxml`) is *transcribed* to
  Calliope source (`backend/transcribe`) — `-o <out>.cal`, or stdout with no `-o`.
  The transcriber emits **structured** source: each line **named by hand/staff**
  (grand-staff piano → `rightHand`/`leftHand` via `<staff>`, with a comment);
  one **bar per line** (joined by the
  `|` barline when every measure provably fills the meter, else `:+:`), a **repeated
  bar named once and reused** (`m1`/`m2`/…), transforms read through the **pipe**
  (`run |> forte`, `… |> tempo`), and **repeats/endings unrolled** into the play
  order. Bare-letter spelling + a single `inKey <fifths>` only when the score is
  fully diatonic (no natural sign to override a key), else explicit accidentals.
  A **`.cal` input** compiles as before: `-o <file>` picks the backend by extension
  (`.mid`/`.midi` → MIDI, `.wav` → audio, `.ir` → Music IR text; `.mp3`/`.mp4`
  recognized too); `--emit ir|midi|wav` forces it; **with neither, the default is
  WAV** (`song.cal` → `song.wav`). `--emit midi`/`--emit wav` with no `-o` derives
  the name. `--soundfont <file.sfz>` picks the audio instrument (defaults to the
  bundled SSO Grand Piano); `mp3`/`mp4` are recognized but still error ("not
  implemented"). **`--debug` prints the evaluated Music IR to stdout (emits no
  file)**; `--dump tokens|ast|types` for lexer/parser/type debug.
- `src/compiler/backend/score.{hpp,cpp}` — **shared timing seam** (`calliope::backend
  ::flatten`): walks the Music tree (Seq concatenates, Par overlays, Rest advances)
  into a flat list of absolute-timed `TimedNote`s in **exact Rational seconds** (each
  Control's tempo baked in, so per-region / polytempo works), with pitch collapsed to
  a MIDI key (`semitones + 12`, clamped) and a per-note velocity. A minimal stand-in
  for the score IR; both `midi` and `audio` source notes from it. **`flatten_voices`**
  is a sibling that splits the top-level `:=:` (Par) spine into one note list per
  **voice** — each collected with the enclosing tempo/meter context, chords (inner
  Pars) left intact — for the visualizer's per-voice DAW rows.
- `src/compiler/backend/midi.{hpp,cpp}` — **MIDI backend** (`calliope::backend
  ::write_midi`): turns `flatten`'s notes into a time-sorted note-on/off stream and
  writes a format-0 Standard MIDI File. 480 ticks/quarter; seconds → ticks at a fixed
  120 bpm reference (the notated tempo meta stays constant; tempo shows up as scaled
  note lengths), per-note velocity. Each distinct instrument gets its own channel + a
  GM program-change (bare notes / custom `.sfz` stay on a default-voice channel).
- `src/compiler/backend/audio.{hpp,cpp}` (+ `miniaudio_impl.cpp`, `tsf_impl.cpp`) —
  **audio backend** (`calliope::backend::write_wav`): groups `flatten`'s notes by
  instrument and renders each group on its own synth (seconds → sample positions,
  per-note velocity), mixing the groups into one stereo f32 **WAV** via **miniaudio**'s
  encoder (summed,
  then hard-limited to [-1,1]). Per instrument: the SSO `.sfz` plays through **sfizz**
  when present, else a fetched placeholder GM **SF2** through **tsf** by GM program;
  un-instrumented notes use the `--soundfont` default (`cli::default_soundfont`, the
  SSO Grand Piano). Built with `CALLIOPE_AUDIO` (ON where the sfizz prebuilts exist —
  Linux); other binaries report it unavailable. **Two outputs share one renderer**
  (`render_master` → interleaved stereo f32): `write_wav` encodes it to a file;
  **`play`** (live) streams it through a **miniaudio playback device** (`ma_device`,
  f32 callback over an atomic cursor). A single instrument renders on a **background
  thread** while playback streams behind it (an atomic `ready` fence) — so sound
  starts after just the sfizz sample preload, not the whole render; several
  instruments fall back to render-then-stream. Blocks until the piece drains. **`calliopei`** drives it (`calliopei file.cal` plays; `:play` in the REPL)
  via `cli::play` — `calliope` (the compiler) does not play, only writes files. The audio
  backend is now linked into **both** `calliope` and `calliopei` (CMake
  `calliope_add_audio`); `miniaudio_impl.cpp` keeps the device-I/O layer (only the
  resource manager is stripped). `play` also has a **non-blocking variant** — an
  opaque **`Playback`** handle (`play_start` → poll `playback_seconds` /
  `playback_finished` → `play_stop`) so another loop (a raylib window) can run while
  audio streams; `play` itself is now a thin blocking wrapper over it. The handle also
  has **transport controls** — `playback_set_paused` (cursor freezes + silence) and
  `playback_seek` (a seek request honoured atomically inside the device callback).
- **`src/compiler/visualizer.cpp` — the `calliopev` live visualizer** (a **separate
  binary**). Reuses the compiler API: `driver::compile` a `.cal`, `backend::flatten`
  `main` to timed notes, `backend::play_start` for audio, then a **raylib** window
  synced to the audio playhead — a DAW view (default; **one lane per staff/hand** —
  the top-level `:=:` branches from `backend::flatten_voices`, grouped by line-name
  base from `main`'s AST so a grand-staff piano's sub-voices (`leftHand`/`leftHand2`/…)
  merge into one lane; each lane a dense mini piano-roll (rows = the voice's distinct
  pitches, lane height ∝ that count), bars **labelled with note names**) and a piano view
  (`--mode piano`, Tab toggles) with a **slim keyboard** and
  **Synthesia-style notes falling** from the top onto their keys. Text uses the
  **bundled Zed Mono font** (`CALLIOPE_FONT_PATH`, falls back to raylib's default).
  **Resizable window** (drawn to the live size each frame) + **transport/zoom
  controls**: Space pause, H/L (or ←/→) seek, J/K (or ↓/↑) zoom the DAW; stays open
  after the audio drains so it can be scrubbed/replayed.
  Built only with **`CALLIOPE_VISUAL`** (needs the
  vendored raylib prebuilt + the audio backend; `calliope_add_visual`). It uses only
  raylib's *graphics*, never its bundled audio, so libraylib.a's miniaudio object is
  never pulled (no duplicate-`ma_*` clash with `miniaudio_impl.cpp`). **Rendering the
  visualization to a video file is a separate, later concern — `backend/visualize.hpp`
  stays a stub for that.**
- `src/compiler/backend/visualize.hpp` — **TODO stub**, now reserved for the
  **offline path**: rendering a visualization to a **video file** (shares `flatten`'s
  timed notes + the `VizMode` enum). The *live* window already ships as `calliopev`
  (above); this header is for the not-yet-built video export. Raylib prebuilts vendored
  (`third_party/libs/raylib`).
- `src/compiler/calliopei.cpp` — `calliopei`: the **interpreter / audio player**.
  `calliopei file.cal` runs a file = **plays `main` live** (`--debug` prints its
  Music IR instead); `calliopei` with no args starts a REPL.
  The REPL keeps a **session**: a line that parses as a definition (`x = …`, a
  signature, `class`/`instance`) **or a `#` directive** (`#load "…"`) is remembered
  and rejected if it doesn't compile; other lines are **evaluated as real
  expressions** via `driver::compile_expr` (spliced in as a synthetic `it = (<expr>)`
  binding — **not** string-pasted into `main = …`, so a bare expression can't collide
  with a user-defined `main`), with prelude + session in scope. A **Music** result
  prints just its type (the IR tree is verbose — that's `:debug` / `--debug`
  territory); a scalar (`Int`/`Bool`/`Pitch`/list/…) prints value + type. Commands:
  `:type <expr>`, **`:debug <expr>`** (print the full value / Music IR), **`:play
  <expr>`** (evaluate + sound it live), `:quit`. Uses plain `std::getline` —
  **readline (line editing + history) is a
  TODO**, to be done with a cross-platform library (replxx / linenoise + Win32).
  `#load` paths resolve cross-platform (`/` and `\\`) via `driver::directory_of` /
  `LoadOptions.base_dir` — relative to the running file for files, to the cwd in the REPL.
- `src/compiler/core/` — `token.hpp`, `lexer.{hpp,cpp}` (pitch-class-reserving
  lexer), `ast.{hpp,cpp}` (index-pooled tree + S-expr printer),
  `parser.{hpp,cpp}` (recursive descent + precedence climbing).
- `CMakeLists.txt` — builds the `calliope` executable (C++17, `-Wall -Wextra`,
  clean).
- `third_party/` — vendored dependencies already in place (see below).

Done: arithmetic core (`Rational`, `Pitch` projections — step 1); lex → parse →
AST; **tree-walking evaluator** (currying, recursion + mutual recursion,
let/where, booleans `True`/`False` + `and`/`or`/`not` with short-circuit, builtins
wired to the pitch core — `c' ^+ P5` really transposes) and **HM typechecker** (unify +
Algorithm W, let-generalization) over the pure subset — **step 2**. Plus
**single-parameter type classes**: `class`/`instance` declarations, qualified
schemes (`Describable t => t -> Int`), constraint solving (grounded constraint ⇒
must have an instance, else type error) and **runtime dispatch** on the method's
first-argument type. `Transposable` is a real builtin class with a builtin `Pitch`
instance; `^+`/`^-` are dispatched methods, so a user `instance Transposable Music`
extends them. 194 tests green. End-to-end: `main = semitones (c' ^+ P5)` →
`Int` / `55`; a user `class Describable` with `Pitch`/`Bool` instances dispatches.

> **Lexical gotcha (locked):** type variables are identifiers, so they **cannot be
> pitch-spelled** (`a`–`g`, `r`, `s`). Spec examples like `class Transposable a`
> must be written with a non-pitch variable in real code — convention is `t`
> (`class Transposable t where (^+) :: t -> Interval -> t`).

Type classes are single-parameter only and dispatch on the **first** argument
(the class type variable's position in every method we have). Top-level bindings
are **dependency-sorted into SCCs and generalized in order** (Tarjan), so a
polymorphic stdlib function (`map`, `length`) is generalized before later code
uses it and can be used at several types in one program.

**Pattern matching** via `case … of` (offside-aligned alternatives): patterns are
`_`, variables, int literals, `True`/`False`, `[]`, and cons `h : t` (nested,
parenthesised). First match wins; non-exhaustive match is a runtime error. The
cons operator `:` (infixr 5) builds lists in expressions too, and the **pipe**
`x |> f = f x` (infixl 1) chains left-to-right. The list prelude is now written
idiomatically (`map func xs = case xs of [] -> [] ; first : rest -> func first : map func rest`).

Still WIP: no multi-equation function clauses (use `case`), no `data` decls / user
constructors, no Music patterns (`a :+: b` — use `isSeq`/`leftChild`); no
superclasses, no default-method bodies; `let`/`where` bindings monomorphic (only
top-level generalizes).

**Standard library (bootstrap step 3, in progress).** The thin C++ core exposes
only *axioms*; `standard_library/prelude.cal` builds the rest in Calliope. The
prelude is **not auto-loaded** — a program must `#load "prelude"` to use it
(`#load` resolves `"prelude"` to the CMake-injected `CALLIOPE_PRELUDE_PATH`, any
other name to a file path). The `calliopei` REPL preloads it for convenience.
`driver::compile` parses each unit (program + each `#load`ed file) **separately and
merges the ASTs**, so error line/col numbers stay relative to the file they occur
in (loaded units never shift the program's lines).
- **Core axioms (builtins):** list — `null` / `head` / `tail` / `cons`; pitch
  projections — `semitones` / `diatonicStep` / `chromaticOf` / `makePitch`; Music IR
  — constructors `note` / `noteWith` / `sequence` / `parallel`, predicates `isNote` /
  `isRest` / `isSeq` / `isPar`, accessors `leftChild` / `rightChild` /
  `notePitch` / `noteDur`, `tuplet` (scales durations by m/n via
  `music::scale_dur`), `withInstrument` (wraps a phrase in a `Control` node —
  `music::control`; the stdlib's `onInstrument` is a thin wrapper), `sfz`
  (`Str -> Instrument`, a custom-soundfont instrument from a `.sfz` path) and `gm`
  (`Int -> Instrument`, a custom instrument from a raw GM program number — exports to
  MIDI, unlike `sfz`), and the
  other `Control` axes `tempo` / `velocity` (`Int -> Music -> Music`). Notation carries durations on notes (`c'8`), rests (`r2`),
  and chords — a duration after `>` applies to every note (`<c e g>2`, via the
  parser encoding it in the `Chord` node's `extra`, applied by `music::set_dur`);
  the tie operator `~` (`Phrase t => Phrase u => t
  -> u -> Music`, via `music::tie`) joins two matching phrases — notes, chords
  (`<c e g> ~ <c e g>`), and chains — summing durations; mismatched pitch/shape is
  a runtime error.
- **Prelude (Calliope):** lists — `length` `map` `filter` `reverse` `drop`
  `foldr` `foldr1` `flip` `append`; math — `negate` `abs` `signum` `even` `odd`
  `gcd` `lcm` `power` `min` `max` `clamp` `sum` `product` `maximum` `minimum`
  (`min`/`max`/`maximum`/`minimum` are `Ord`-polymorphic, so they order pitches too);
  music — `note`s/`line`/`chord`/`par`,
  `transpose`, `ottava` (octave shift — 8va/8vb), `mapPitches`, `invert` (spelled
  melodic inversion about the first pitch), `retrograde`, `times`, `triplet`,
  `onInstrument`, `reflectPitch`,
  `firstPitch`, the meter sugar `commonTime`/`cutTime`/`waltz`, named **dynamics**
  `pianissimo`…`fortississimo` (spelled out — `f` is the pitch F — over `velocity`),
  **articulations** `staccato`/`legato`/`slur`/`tenuto`/`accent`/`marcato` (over the
  `articulate` axis), **grace notes** `acciaccatura`/`appoggiatura`, **ornaments**
  `trill`/`mordent`/`turn` (note→figure rewrites, M2 neighbour), and **keys** —
  `major`/`minor` (tonic → fifths), `inKey`, `scaleDegree`, `diatonicUp`/`diatonicDown`,
  and key-aware `trillIn`/`mordentIn`/`turnIn`. The
  headline example type-checks and runs:
  `development subj = subj `par` (invert subj ^+ P5)`.

**Multi-line expressions** work via an **offside rule** (`Parser.margin`): inside
a binding, a newline continues the expression when the next line is indented past
the binding's name column; a line at/left of it ends the binding. "Open" tokens
(trailing infix operator, `=`, `(`/`[`/`,`, `if`/`then`/`else`, `->`, `in`)
continue unconditionally; inside `()`/`[]` newlines are insignificant. Notation
runs (`c d e`) still don't cross lines.

**Music IR is real now** (`music.{hpp,cpp}`): an index-pooled `Note | Rest | Seq
| Par | Control` tree (spec §3) that the evaluator produces as the program value.
Notation desugars into it — a single pitch is a `Pitch`, but a run (`c d e`), a chord
(`<c e g>`), `:+:`/`:=:`, and `r` build `Music`; durations on literals are
honored (`c'8` = 1/8, default quarter). The builtin `Transposable Music` instance
maps `^+`/`^-` over every note, preserving spelling + durations, so `c d e ^+ P5`
transposes the phrase. **`Control` is the `Modify` node** — it wraps a sub-phrase
along one axis: **instrument**, **`tempo` (bpm)**, **`velocity` (0..127)**, **`meter`
(time signature)**, **`articulate` (gate + accent)** — `flatten` reads the gate to
hold each note for `gate · duration` (sounding ≠ slot, so staccato is short with a
gap) and adds the accent to velocity — or **`key` (signature in fifths)**: `inKey`
resolves the *floating* (bare-letter) accidentals in its subtree to the key (`f` →
F# in D major) at construction (`music::apply_key`) and tags the Control for
engraving — or **`sustain` (damper pedal)**: `flatten` holds every note in the
subtree ringing until the subtree ends (extends each sounding length to the span
end). Named **dynamics** and **articulations** are stdlib over `velocity` /
`articulate`; **grace notes / ornaments / scales / diatonic transforms** are stdlib.
`Instrument` is a typed
enum of builtin nullary constructors (`Cello`, `Flute`, … like the `Interval`
constructors — `core/instrument.{hpp,cpp}` is the single name↔GM↔.sfz table); the
stdlib `onInstrument :: Phrase t => Instrument -> t -> Music` (over the
`withInstrument` axiom) builds the node — like `:+:`, the phrase arg is a `Phrase`,
so a bare pitch lifts (`onInstrument Cello c`); `tempo` / `velocity` lift the same way. **Custom instruments** outside the enum: `sfz :: Str ->
Instrument` (`onInstrument (sfz "cello.sfz") phrase`) and `gm :: Int -> Instrument`
(a raw GM program number) — both first-class values, so a user can `name = sfz "…"`
/ `name = gm 24` and reuse it. So the Control node's instrument axis carries one of
*enum id* / *user `.sfz` path* / *raw `gm` program* (relative `.sfz` paths resolve
against the source file's dir; absolute used as-is). The IR carries only that
abstract payload; each backend resolves it (MIDI → a channel per instrument with a
GM program-change for enum / `gm` voices, `.sfz` gets a plain default channel;
audio → an SSO `.sfz`, the user's `.sfz`, or a placeholder GM SF2 via tsf — by the
instrument's GM number, or `gm`'s number directly — when a patch is absent). `:+:`/`:=:` compose via a builtin single-parameter
class **`Phrase`** (instances `Pitch` and `Music`): `(:+:) :: Phrase t => Phrase u
=> t -> u -> Music`. A bare `Pitch` operand lifts to a one-note phrase, so `c' :+:
d'` is `Music`, and a function over `:+:` stays polymorphic (`fn x = x :+: x` has
type `Phrase t => t -> Music`); `1 :+: 2` is rejected (`no instance for Phrase
Int`). Adjacency (`c d e`, a `Seq`) is still the idiomatic spelling. `:*:`
repeats a phrase: `phrase :*: n :: Phrase t => t -> Int -> Music` (n copies in a
row, `n >= 1`; binds tighter than `:+:`).

**Arithmetic.** `+ - *` are a builtin single-parameter class **`Num`** (instances
`Int` and `Rational`), so they add/scale both whole numbers and exact fractions. A
grounded `Int`/`Rational` mismatch **coerces up to `Rational`** (`1 + 1/2` reads as
`(1/1) + (1/2)` = `3/2`) — special-cased in the `BinOp` inferer for `+ - *`; the
evaluator already lifts the `Int` operand. (`toRational :: Int -> Rational` still
forces a value through a `Rational`-typed parameter, where the operator-site coercion
doesn't reach.) `/` is **fractional** — `Int -> Int -> Rational` (`7 / 2` = `7/2`),
so durations fall out of plain division (`noteWith c' (3 / 8)`). Integer division and
remainder are the named `div` / `mod` (`Int -> Int -> Int`, used infix `` 7 `div` 2 ``,
binding like `*` — tighter than `+`). Like `Ord`, these are typecheck-level classes;
eval keeps `+`/`/` as builtins that branch on operand kind.

**Comparison.** `==`/`/=` are polymorphic structural equality — on `Pitch`
(spelled: `fis' /= ges'`) and `Music` (deep, via `music::equal`: same shape,
pitches, durations). Ordering `< > <= >=` is a builtin class **`Ord`** (instances
`Int` and `Pitch`; pitches by height/semitones, so `c' < d'`), no `Music`
instance. **A `<` opens a chord only when it hugs its first note** (`<c e g>`); a
spaced `<` is comparison (`a < b`), resolving the reserved-letter ambiguity at
parse time (`parse::opens_chord`).

Two backends exist. **MIDI** (`backend/midi.cpp`) writes a .mid file; **audio**
(`backend/audio.cpp`) renders a stereo WAV through the sfizz sampler + miniaudio
(`--emit wav --soundfont <file.sfz>`). Both share `backend/score.cpp`'s `flatten`
(Seq concat, Par overlay, Rest advance) — the explicit-but-minimal score IR seed,
so the time walk is no longer duplicated. **`Control` nodes carry instrument, tempo
(`tempo :: Int -> Music -> Music`), velocity (`velocity :: Int -> Music -> Music`)
and meter (`meter :: Int -> Int -> Music -> Music`, a time signature)** — `flatten`
resolves tempo into absolute seconds (polytempo across `par` voices works) and stamps
each note's velocity; both backends honor them.

**Meter + bars are functional, not metadata** (spec §8). `meter n d` is a `Control`
axis (`meter_num`/`meter_den` on `MusicNode`); the `|` **barline** operator (the
previously-unused `Bar` token, lowest infix precedence) builds a `MusicKind::Barline`
marker — `a | b` = `Seq(a, Seq(Barline, b))`. `flatten` *reads* the meter for two
real effects: (1) **strong-beat accent** — it tracks musical (whole-note) position
alongside seconds and boosts the velocity of downbeat / mid-bar (simple) / every-third
(compound) onsets, so the same notes sound different under different meters; (2)
**bar validation** — when given an errors sink, each `|` measure must fill exactly one
bar of the active meter, else a message (`"bar 3: 5/4 in a 4/4 meter"`). The driver
runs the validation after eval (when `main`/`it` is Music), so bar errors surface at
compile time for every entry point. Prelude sugar: `commonTime` (4/4) `cutTime` (2/2)
`waltz` (3/4). Note durations stay absolute exact rationals (O11 untouched). Not yet
built: octave-resolution pass (absolute works inline; `#relative` needs it), a richer
score IR + MusicXML backend, a MIDI time-signature meta event, more prelude (intervals
as a monoid, scales/keys, chords/harmony — and the remaining `Modify` axis: key).
Bootstrap: step 1 ✓ → step 2 ✓ → step 3 (in progress).

## Architecture (intended)

Host language is **C++**. Pipeline:

```
.cal source → preprocess (#directives) → lex → parse → octave-resolve
            → HM typecheck → desugar → Music IR (tree)                  [layer 1, eval]
            → lower/schedule → score IR (timed, MIDI-shaped events)     [layer 2]
            → backends:
                • audio playback (fluidsynth / sfizz / tsf → miniaudio)
                • MusicXML out (engraving)
                • live render (raylib)
                • .mid out (score IR is already MIDI-shaped)
```

Two IRs, designed independently (spec §13):
- **Music IR** (`Prim | :+: | :=: | Modify`, spec §3) — result of evaluating the
  functional program. Every notation literal / combinator desugars into it.
- **Score IR** — flat, time-sorted event stream lowered from the Music IR; what
  the synths actually play. Build this regardless of how layer 1 evaluates.

### Design decisions (locked)

- **Eval model (O5):** tree-walking interpreter for v0.1 (programs are tiny;
  synthesis is the bottleneck). Bytecode VM is a later, non-breaking option —
  opcode sketch in spec §13.3.
- **Types (O4/O6):** full **Hindley–Milner** inference + type classes +
  parametric polymorphism. Signatures optional; the music algebra's classes
  (`Transposable`, `Invertible`, …) are real, not built-ins.
- **Evaluation (O7):** strict / **eager**, no laziness. *Typed like Haskell,
  evaluated like ML.*
- **Memory:** Music IR nodes immutable + shared; arena allocator for the whole
  build-and-render pass, freed wholesale (no GC).
- **Octave mode (O10):** absolute by default; opt-in `#relative [<ref>]` /
  `#absolute` region directives (`<ref>` = absolute pitch anchor, no duration;
  optional, defaults to `c'`). Both resolved by one **AST octave-resolution
  pass** (`preprocess → parse → resolve → typecheck → desugar`). Parser emits
  `RawPitch` (letter / accidental / octave-marks / absolute-flag), *not* a final
  `Pitch`; the pass rewrites `RawPitch → Pitch` (IR is always absolute).
- **Directives are `#` preprocessor, not LilyPond backslash.** `#`-prefixed,
  handled before parsing; they affect how text is *read/assembled* only
  (`#relative`, `#absolute`, `#load "<file>"`; set will grow — O12). Musical
  context (key/tempo/dynamics/instrument) is *never* a directive — it's the §8
  term-level combinators (`inKey`, `tempo`, …) producing `Control` nodes. **Key
  respells (O8 revised):** a bare letter carries a *floating* natural (`Pitch.floating`),
  and `inKey k` resolves the floating pitches in its subtree to the key's accidental
  (`f` → F# in D major) while an explicit `fis`/`fes` is left untouched; spelling is
  still kept (C# ≠ Db), and with no `inKey` a bare letter stays natural (backward
  compatible). The *letter* is still fixed at lex time — only an unmarked accidental
  defers to key resolution. See spec §5.1–5.3.
- **Stdlib in Calliope, thin C++ core (O-boundary, §14).** The C++ core exposes
  only axioms: `Int`/`Rational` + ops; `Pitch` *projections* (`semitones`,
  `diatonicStep`, `makePitch`, `chromaticOf`); the Music IR constructors; the
  evaluator; backend FFI. **All music theory — intervals, scales, chords,
  transforms — is Calliope stdlib** (`.cal` prelude). No `Float` in the language
  (only at the synthesis boundary). Pitch is spelled (C♯≠D♭).
- **Transposition operator `^+`/`^-` (O13).** Dedicated operator via a plain
  single-parameter class `Transposable a` (`(^+) :: a -> Interval -> a`), instances
  `Pitch`/`Music`. *Not* an overload of `+` — `+` stays numeric. So the type
  checker needs only vanilla single-parameter classes — **no** MPTC/fundeps.
  Intervals are a monoid (`<>`); `intervalBetween :: Pitch -> Pitch -> Interval`.
- **Bootstrap order:** (1) number+pitch primitives [arithmetic first] →
  (2) evaluator + typechecker → (3) load `.cal` prelude. Step (1) is standalone
  C++ with its own tests.
- **IO model (O14):** `main :: Music` — pure program, runtime renders the value
  (spec §15). No language-level IO. Grow to an effects list (`main :: [Output]`)
  if multiple outputs are ever needed; IO monad out of scope.
- Still open: O9 backend priority · O11 time model (leaning exact rationals →
  ticks at the score-IR boundary) · O12 preprocessor system design.

## Repo layout

```
src/compiler/
  compiler.cpp        `calliope` CLI: arg parsing → driver → cli::emit_output
  calliopei.cpp       `calliopei` CLI: run a file or REPL session
  visualizer.cpp      `calliopev` CLI: compile + play + draw a synced raylib window
  helper.{hpp,cpp}    cli:: host helpers (read_file/prelude_path/print_errors,
                      dumps, Emit + backend dispatch) shared by both entry points
  core/
    rational.{hpp,cpp}  exact Rational arithmetic (Duration/tuplet/tempo)
    pitch.{hpp,cpp}     spelled Pitch projections (semitones, diatonic_step, mk_pitch)
    instrument.{hpp,cpp} the Instrument lexicon: name ↔ id ↔ GM program / SSO .sfz
    music.{hpp,cpp}     Music IR (Note|Rest|Seq|Par|Control), index-pooled + transpose/show
    token.hpp           TokenKind + Token
    lexer.{hpp,cpp}     tokenize() — reserves the pitch lexical class
    ast.{hpp,cpp}       index-pooled AST (NodeKind/Node/NodeId) + printer
    parser.{hpp,cpp}    parse_program() — recursive descent + precedence climbing
    eval.{hpp,cpp}      tree-walking evaluator (Value/Env/Closure, builtins)
    typecheck.{hpp,cpp} Hindley–Milner inference (unify + Algorithm W, SCC gen)
    driver.{hpp,cpp}    compile / compile_expr (bare REPL expr) / type_of — the
                        full pipeline as one API
  backend/
    score.{hpp,cpp}     Music IR → flat absolute-timed notes (flatten → [TimedNote],
                        exact Rational time) — shared timing seam, score-IR seed
    midi.{hpp,cpp}      Music IR → format-0 Standard MIDI File (write_midi)
    audio.{hpp,cpp}     Music IR → stereo f32 (render_master): write_wav (file) +
                        play (live, miniaudio device); sfizz + tsf SF2 fallback
    visualize.hpp       TODO stub: raylib score view (DAW / piano modes), synced to play
    miniaudio_impl.cpp  the one TU that compiles MINIAUDIO_IMPLEMENTATION
    tsf_impl.cpp        the one TU that compiles TSF_IMPLEMENTATION (SF2 fallback synth)
    transcribe.{hpp,cpp} MusicXML (.mxl/.xml) → Calliope source (hoxml SAX + miniz unzip)
standard_library/
  prelude.cal           stdlib in Calliope: list commons + music transforms
                        (line/chord/transpose/invert/retrograde/times/…)
third_party/libs/
  fluidsynth/         SoundFont synth (playback)
  sfizz/              SFZ sampler (playback)
  tsf.h               TinySoundFont, header-only SF2 synth (playback)
  miniaudio.h         audio output / device layer
  raylib/             graphics — live notation / piano-roll view
  hoxml/              MusicXML *parser* (SAX) — used by backend/transcribe (import)
  miniz               zip (de)compression — used to unzip `.mxl` in transcribe
third_party/fonts/        UI font (Zed Mono Nerd Font)
third_party/soundfonts/   instrument samples for playback (sso/ auto-fetched, gitignored)
cmake/
  FetchSoundfonts.cmake   shallow-clones the SSO sample library on demand
```

**Soundfonts are fetched, not committed.** `cmake/FetchSoundfonts.cmake` (run at
configure time) shallow-clones the ~2.6 GB Sonatina Symphonic Orchestra (SFZ) from
`github.com/peastman/sso` into `third_party/soundfonts/sso` only if it is missing;
that directory is gitignored. Skip the download with
`-DCALLIOPE_FETCH_SOUNDFONTS=OFF`. The path is exposed to C++ as
`CALLIOPE_SOUNDFONT_DIR` for the (not-yet-built) synth backend.

## Building

```sh
cmake -S . -B build              # also fetches SSO soundfonts (~2.6 GB) if missing;
                                 #   add -DCALLIOPE_FETCH_SOUNDFONTS=OFF to skip
cmake --build build
./build/calliope file.cal        # emit Music IR of `main` (see docs/README.md)
./build/calliopei file.cal       # interpret a file (prints main's IR)
./build/calliopei                # no args: interactive REPL
./build/calliopev file.cal       # play + visualize (raylib; --mode piano, Tab toggles)
ctest --test-dir build           # or run ./build/calliope_tests directly
```

## Tests

`tests/` builds a **separate** `calliope_tests` binary (never the main driver as
a test). Tiny C-style harness — `tests/test.hpp`/`test.cpp`: counters +
`CHECK`/`CHECK_EQ_STR` macros, an `ast_sexpr` serializer for AST assertions; test
cases are plain free functions (`run_lexer_tests`, `run_parser_tests`) called
from `test_main.cpp`. No framework, no templates. Add a test area = new
`tests/test_*.cpp` + a `run_*` call in `test_main.cpp` + list it in
`CMakeLists.txt`. Reserved letters (`a`–`g`, `r`, `R`, `s`) are notation, not
identifiers — name test variables `subj`, `m`, `foo`, etc.

Vendored libs have per-platform prebuilts under
`third_party/libs/<lib>/linux/` (this machine is Linux).

## Conventions & expectations for changes

- **Spec before code.** This project is design-led right now. If a change
  touches language behavior, update `language_spec.md` in the same edit and
  reference the relevant `O#` open question.
- **The `Music` IR is the contract.** Keep notation literals and combinators as
  *desugaring* into the IR; don't special-case them downstream.
- **Lexing rule:** pitch literal = lowercase `[a-g]` + optional accidental
  (`is`/`es`) + octave (`'`/`,`) + duration digits. This class is **reserved** —
  identifiers can't match it, so a pitch vs. a name is decided lexically (no
  sigil, no scope/type lookup). Adjacency of pitch literals = sequential
  composition. Juxtaposition with a non-pitch head = function application — the
  grammar stays context-free. A word is a pitch/rest **only when the whole
  maximal word is exactly one** — a trailing identifier char (a letter or `_`)
  means the entire word is an ordinary name, never a pitch + dangling tail
  (`fis_sharp`, `d3note`, `c_foo`, `g2x`, `r2d2`, `s_x` each lex as one
  identifier). A name may *start* with any of `a`–`g`/`r`/`s`; it just can't *be*
  a bare pitch (`c`, `cis`, `e4`).
- **Prefer exact rationals for durations** (spec O11) — avoids tuplet/tie drift.
- **Keep pitch spelling in the IR** (C# ≠ Db, spec O2/O8); collapse to semitones
  only at the playback boundary.
- This is **not** a git repository yet. Don't assume version control; offer to
  `git init` if the user wants history.

## C++ style (hard constraints)

Data-oriented "C with namespaces", not OOP. Target C++17.

- **No user-defined templates.** Don't write `template<...>` yourself. The STL
  (`std::vector`, `std::string`, `std::string_view`, …) is fine to *use* — those
  are standard and may be swapped for hand-rolled equivalents later, so keep
  usage shallow and replaceable (no deep template gymnastics, no fancy adaptors).
- **No OOP.** No inheritance, no `virtual`, no methods carrying behavior. Model
  variants with a tag enum + fields (e.g. the index-pooled AST: `NodeKind` +
  `Node` in a `std::vector<Node>`, referenced by `NodeId`), not class hierarchies.
- **Classes allowed but no `private`.** Prefer `struct`; all members public.
  State is plain data the free functions operate on.
- **C-style API.** Behavior lives in free functions in namespaces
  (`calliope::lex::tokenize`, `calliope::parse::parse_program`), taking structs
  by reference — not member functions.
- **`namespace` and `enum class` encouraged.** Those are fine modern features.

## Pointers

- Language design & open questions → `language_spec.md`
- Composer-intent → syntax mapping → `language_spec.md` §12
- User-facing reference (as implemented) → `docs/` (`syntax.md`, `builtins.md`,
  `stdlib.md`). Keep these in sync when language behavior changes.
