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
- `src/compiler/calliope.cpp` — `calliope` compiler CLI: `-o <file>` picks the
  backend by extension (`.mid`/`.midi` → MIDI, `.wav` → audio, `.ir` → Music IR
  text; `.mp3`/`.mp4` recognized too); `--emit ir|midi|wav` forces it; with neither
  the IR prints to stdout. `--emit midi`/`--emit wav` with no `-o` derives the name
  (`song.cal` → `song.mid`/`song.wav`). `--soundfont <file.sfz>` picks the audio
  instrument (defaults to the bundled SSO Grand Piano); `mp3`/`mp4`/`musicxml` are
  recognized but still error ("not implemented"). `--dump tokens|ast|types` for debug.
- `src/compiler/backend/score.{hpp,cpp}` — **shared timing seam** (`calliope::backend
  ::flatten`): walks the Music tree (Seq concatenates, Par overlays, Rest advances)
  into a flat list of absolute-timed `TimedNote`s in **exact Rational seconds** (each
  Control's tempo baked in, so per-region / polytempo works), with pitch collapsed to
  a MIDI key (`semitones + 12`, clamped) and a per-note velocity. A minimal stand-in
  for the score IR; both `midi` and `audio` source notes from it.
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
  Linux); other binaries report it unavailable. Offline render only — live playback
  is a later follow-up.
- `src/compiler/calliopei.cpp` — `calliopei`: the interpreter. `calliopei file.cal`
  runs a file (prints `main`'s Music IR); `calliopei` with no args starts a REPL.
  The REPL keeps a **session**: a line that parses as a definition (`x = …`, a
  signature, `class`/`instance`) **or a `#` directive** (`#load "…"`) is remembered
  and rejected if it doesn't compile; other lines are evaluated as expressions with
  prelude + session in scope (value + type on one line; `:type`, `:quit`). Uses
  plain `std::getline` — **readline (line editing + history) is a TODO**, to be done
  with a cross-platform library (replxx / linenoise + Win32). `#load` paths resolve
  cross-platform (`/` and `\\`) via `driver::directory_of` / `LoadOptions.base_dir`
  — relative to the running file for files, to the cwd in the REPL.
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
  `foldr` `foldr1` `flip` `append`; music — `note`s/`line`/`chord`/`par`,
  `transpose`, `mapPitches`, `invert` (spelled melodic inversion about the first
  pitch), `retrograde`, `times`, `triplet`, `onInstrument`, `reflectPitch`,
  `firstPitch`. The
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
along one axis: **instrument**, **`tempo` (bpm)**, or **`velocity` (0..127)** (key /
dynamics-by-name will follow). `Instrument` is a typed
enum of builtin nullary constructors (`Cello`, `Flute`, … like the `Interval`
constructors — `core/instrument.{hpp,cpp}` is the single name↔GM↔.sfz table); the
stdlib `onInstrument :: Instrument -> Music -> Music` (over the `withInstrument`
axiom) builds the node. **Custom instruments** outside the enum: `sfz :: Str ->
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
(`tempo :: Int -> Music -> Music`) and velocity (`velocity :: Int -> Music ->
Music`)** — `flatten` resolves tempo into absolute seconds (polytempo across `par`
voices works) and stamps each note's velocity; both backends honor them. Not yet
built: octave-resolution pass (absolute works inline; `#relative` needs it), a richer
score IR + live-playback / MusicXML backends, more prelude (intervals as a monoid,
scales/keys, chords/harmony — and the remaining `Modify` axes: key/dynamics).
Bootstrap: step 1 ✓ →
step 2 ✓ → step 3 (in progress).

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
  term-level combinators (`inKey`, `tempo`, …) producing `Control` nodes. Key is
  metadata; letters stay literal (`fis` even in D major). See spec §5.1–5.3.
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
    driver.{hpp,cpp}    compile/compile_expr/type_of — the full pipeline as one API
  backend/
    score.{hpp,cpp}     Music IR → flat absolute-timed notes (flatten → [TimedNote],
                        exact Rational time) — shared timing seam, score-IR seed
    midi.{hpp,cpp}      Music IR → format-0 Standard MIDI File (write_midi)
    audio.{hpp,cpp}     Music IR → stereo WAV via sfizz (+ tsf SF2 fallback) + miniaudio
    miniaudio_impl.cpp  the one TU that compiles MINIAUDIO_IMPLEMENTATION
    tsf_impl.cpp        the one TU that compiles TSF_IMPLEMENTATION (SF2 fallback synth)
standard_library/
  prelude.cal           stdlib in Calliope: list commons + music transforms
                        (line/chord/transpose/invert/retrograde/times/…)
third_party/libs/
  fluidsynth/         SoundFont synth (playback)
  sfizz/              SFZ sampler (playback)
  tsf.h               TinySoundFont, header-only SF2 synth (playback)
  miniaudio.h         audio output / device layer
  raylib/             graphics — live notation / piano-roll view
  hoxml/              MusicXML *parser* (note: we need a writer for output)
  miniz               zip (de)compression (e.g. .mxl, compressed SF2)
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
  grammar stays context-free.
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
