# Calliope documentation

Calliope is a functional DSL for composing music programmatically. You write
transformations of musical phrases — invert, retrograde, transpose, sequence,
layer — in a Haskell-like language; the program evaluates to a `Music` value that
a backend renders.

```
-- a subject in parallel with its inversion, answered up a fifth
development subj = subj `par` (invert subj ^+ P5)
main = development (c' d' e' g')
```

These docs describe the language **as currently implemented**. The forward-looking
design (open questions, planned backends) lives in
[`../language_spec.md`](../language_spec.md); project orientation for contributors
is in [`../CLAUDE.md`](../CLAUDE.md).

## Contents

- **[functional-primer.md](./functional-primer.md)** — **new to functional
  programming? start here.** Lambda calculus, functional vs imperative, why
  functional suits music, and the Calliope/Haskell syntax you need to read the code.
- **[syntax.md](./syntax.md)** — lexical rules, pitch/notation literals, bindings,
  expressions, types, type classes, multi-line layout, directives.
- **[types.md](./types.md)** — the type system: inference, the base types, type
  variables and polymorphism, type classes, signatures, and type errors.
- **[builtins.md](./builtins.md)** — the thin C++ core: operators and primitive
  functions (arithmetic, comparison, list axioms, pitch projections, Music tree
  axioms, transposition).
- **[axioms.md](./axioms.md)** — the complete axiom reference: every irreducible
  C++ primitive (programming + music) with its type, and the C++/stdlib boundary.
- **[stdlib.md](./stdlib.md)** — the standard library written in Calliope
  (`standard_library/prelude.cal`): list helpers and music transforms.

## Running

```sh
cmake -S . -B build && cmake --build build
```

### Compiler — `calliope`

```sh
./build/calliope score.mxl -o score.cal    # transcribe MusicXML (.mxl/.xml) -> Calliope
./build/calliope file.cal                  # render a WAV (the default: file.cal -> file.wav)
./build/calliope -o song.mid file.cal      # compile to a MIDI file (backend from extension)
./build/calliope -o song.ir file.cal       # write the Music IR text to a file
./build/calliope --emit midi file.cal      # force MIDI; output name derived (file.mid)
./build/calliope -o song.wav --soundfont inst.sfz file.cal   # ...or pick the instrument
./build/calliope --debug file.cal          # print the Music IR to stdout (no file)
./build/calliope --dump types file.cal     # debug: also dump tokens / ast / types
```

```
usage: calliope [options] <file.cal>
  -o <file>       write output to <file>; backend chosen by extension
                  (.mid/.midi = MIDI, .wav = audio, .ir = Music IR text)
  --emit <fmt>    force the backend: ir | midi | wav  (overrides the extension)
  --soundfont <f> .sfz instrument for the audio (wav) backend
                  (default: the bundled SSO Grand Piano)
  --debug         print the evaluated Music IR to stdout (no file emitted)
  --dump <what>   tokens | ast | types   (repeatable)
```

`calliope` behavior depends on the **input and output extension**. A **MusicXML
input** (`.mxl` zip, or `.xml`/`.musicxml`) is *transcribed* to Calliope source —
notes, chords, rests, parts and `<voice>`s (each a parallel `:=:` line), ties (`~`),
tuplets, articulations (staccato/accent/…), ornaments (trill/mordent/turn), dynamics
(forte/piano/…), plus `meter`/`tempo` (pitches are spelled exactly, so no `inKey` is
added; the key is a comment) — to `-o <file>.cal`, or stdout. Element tags it still
doesn't transcribe (slurs, repeats, lyrics, `<backup>` timing, …) are listed on
stderr so you know what was dropped.
Otherwise `calliope` is the **compiler** of a `.cal` program — it writes
files; live playback is `calliopei`'s job. With no `-o` and no `--emit` it renders a
**WAV** (`file.cal` → `file.wav`).
The backend is otherwise chosen by the `-o` extension (or forced with `--emit`);
`--emit midi` / `--emit wav` without `-o` derive the output name from the input.
Backends implemented so far: **wav** (offline render — the sfizz SFZ sampler → a
stereo WAV; defaults to the bundled SSO Grand Piano, or pick another instrument with
`--soundfont <file.sfz>`), **midi** (a format-0 Standard MIDI File), and **ir**
(Music IR text). `--debug` prints the Music IR to stdout and emits nothing. `main`
must be `Music` (a bare `Pitch` is lifted to one note) for the MIDI and audio
backends. The MusicXML backend is still to come.

A program is a set of bindings; `main` is the entry point and is expected to have
type `Music` (any value type works while experimenting). To use the standard
library, a file must load it explicitly with a directive:

```
#load "prelude"
```

(`#load "prelude"` resolves to the bundled standard library; other names are read
as file paths. Each loaded file is parsed as its own unit, so error line numbers
stay relative to the file they occur in.) The `ir`, `midi`, and `wav` backends are
wired up (see the options above); the MusicXML backend still reports "not
implemented". The audio backend is built only where the vendored sfizz prebuilts
exist (Linux) — elsewhere `--emit wav` reports the backend as unavailable.

### Interpreter — `calliopei`

The interpreter and audio player. Running a file **plays `main` live** on the
default audio device (`--debug` prints its Music tree instead); with no arguments
it starts a REPL. A file must `#load "prelude"` to use the stdlib; **the REPL
preloads the prelude**, so it is always in scope there.

```sh
./build/calliopei file.cal       # run a file = play `main` live (no file written)
./build/calliopei --debug file.cal # ...or print the Music tree of `main` instead

./build/calliopei                # no args: start the REPL (prelude preloaded)
λ> double x = x * 2              # a definition is remembered for the session
λ> double 5
  10 :: Int
λ> transpose P5 (c' e' g')      # a Music result shows just its type
  :: Music
λ> :debug transpose P5 (c' e' g')   # ...:debug prints the full IR tree
  ((G4:1/4 :+: B4:1/4) :+: D5:1/4) :: Music
λ> :play c' e' g'                # evaluate, then sound it live
λ> :quit
```

A line that parses as a **definition** (`x = …`, `foo x = …`, a signature, a
`class`/`instance`) — or a `#load "file"` directive — is added to the session,
silently, like ghci, and stays in scope; if it fails to compile it is reported and
not kept. Any other line is **evaluated** as an expression. A `Music` result prints
just its type (the IR tree is verbose — see `:debug`); a scalar result (`Int`,
`Bool`, `Pitch`, a list, …) prints its value and type. The expression is evaluated
directly (not pasted into a `main = …` binding), so it never collides with a `main`
you defined in the session. `:type <expr>` shows a type without evaluating;
**`:debug <expr>`** prints the full evaluated value (the Music IR); **`:play
<expr>`** evaluates a `Music` expression and plays it on the audio device; `:quit`
(or `:q`) exits. (No line editing/history yet, and multi-line input is not
implemented, so each definition must fit on one line.)

### Examples

Runnable programs live in [`../examples/`](../examples), split into two folders.

**[`examples/programming/`](../examples/programming)** — the language as a general
functional language: `functional-basics.cal` (higher-order functions, currying,
lambdas, pipe, `where`/`let`, recursion — start here), `arithmetic.cal`,
`factorial.cal`, `gcd.cal`, `list-math.cal` (`map`/`filter`/`foldr`),
`not-operator.cal` (booleans), `recursion.cal` (numeric, mutual, list, and
music-building recursion), and `constraints.cal` (type-class constraints — the `=>`
in a signature, used and inferred).

**[`examples/music/`](../examples/music)** — making music: `hello.cal` (a C-major
scale), `reverse-notes.cal`, `transforms.cal`, `arpeggio-and-chord.cal`, the
headline `development.cal`, `comparing-music.cal`, `pipeline.cal`, `instruments.cal`,
`custom-instrument.cal`, `tempo-and-velocity.cal`, `meter.cal` (time signatures + `|`
barlines), `dynamics.cal` (`pp`…`ff`), `articulation.cal` (staccato/legato/accent +
grace notes and ornaments), and `key.cal` (key signatures that respell bare letters
— `f` → F♯ in D major — plus diatonic transposition).
