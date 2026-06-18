# CLAUDE.md

Guidance for Claude Code (and humans) working in this repo.

## What Calliope is

A **functional DSL for composing music programmatically**. The aim is an
*expressive* language that maps how a composer thinks — transformations of
phrases (invert, retrograde, sequence, transpose), not note-by-note data entry.

```haskell
-- the intent we are designing for:
development :: Phrase -> Phrase
development subject = subject `par` (invert subject + P5)
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

## Status (2026-06-18)

Frontend first cut exists; nothing executes music yet.

- `src/compiler/compiler.cpp` — driver: reads a `.cal` file (or a built-in
  sample), prints the token stream and the parsed AST.
- `src/compiler/core/` — `token.hpp`, `lexer.{hpp,cpp}` (pitch-class-reserving
  lexer), `ast.{hpp,cpp}` (index-pooled tree + S-expr printer),
  `parser.{hpp,cpp}` (recursive descent + precedence climbing).
- `CMakeLists.txt` — builds the `calliope` executable (C++17, `-Wall -Wextra`,
  clean).
- `third_party/` — vendored dependencies already in place (see below).

Done: lex → parse → AST for bindings, type sigs, `#` directives, pitch-run
sequencing, application, infix/backtick operators, lists, chords, lambda,
`let`/`if`, `where` (column-aligned, desugars to `let`).
Not yet built: octave-resolution pass, typecheck, desugar to Music IR, eval,
score IR, backends. Next natural step is the octave-resolution pass over
`RawPitch` (currently pitches are kept as raw lexeme text in `PitchLit.tok`).

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
- Still open: O8 pitch spelling · O9 backend priority · O11 time model
  (leaning exact rationals → ticks at the score-IR boundary) · O12 preprocessor
  system design.

## Repo layout

```
src/compiler/
  compiler.cpp        driver: tokenize + parse a .cal file, dump tokens & AST
  core/
    token.hpp         TokenKind + Token
    lexer.{hpp,cpp}   tokenize() — reserves the pitch lexical class
    ast.{hpp,cpp}     index-pooled AST (NodeKind/Node/NodeId) + printer
    parser.{hpp,cpp}  parse_program() — recursive descent + precedence climbing
third_party/libs/
  fluidsynth/         SoundFont synth (playback)
  sfizz/              SFZ sampler (playback)
  tsf.h               TinySoundFont, header-only SF2 synth (playback)
  miniaudio.h         audio output / device layer
  raylib/             graphics — live notation / piano-roll view
  hoxml/              MusicXML *parser* (note: we need a writer for output)
  miniz               zip (de)compression (e.g. .mxl, compressed SF2)
third_party/fonts/        UI font (Zed Mono Nerd Font)
third_party/soundfonts/   instrument samples for playback
```

## Building

```sh
cmake -S . -B build
cmake --build build
./build/calliope [file.cal]      # no arg => runs a built-in sample
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
