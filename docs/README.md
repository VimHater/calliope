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
- **[stdlib.md](./stdlib.md)** — the standard library written in Calliope
  (`standard_library/prelude.cal`): list helpers and music transforms.

## Running

```sh
cmake -S . -B build && cmake --build build
```

### Compiler — `calliope`

```sh
./build/calliope file.cal                  # print the Music tree of `main` to stdout
./build/calliope -o song.mid file.cal      # compile to a MIDI file (backend from extension)
./build/calliope -o song.ir file.cal       # write the Music IR text to a file
./build/calliope --emit midi file.cal      # force MIDI; output name derived (file.mid)
./build/calliope --dump types file.cal     # debug: also dump tokens / ast / types
```

```
usage: calliope [options] <file.cal>
  -o <file>       write output to <file>; backend chosen by extension
                  (.mid/.midi = MIDI, .ir = Music IR text)
  --emit <fmt>    force the backend: ir | midi  (overrides the extension)
  --soundfont <f> soundfont for audio backends (reserved; unused for now)
  --dump <what>   tokens | ast | types   (repeatable)
```

With no `-o` and no `--emit`, the Music IR is printed to stdout. The backend is
otherwise chosen by the `-o` extension (or forced with `--emit`); a `--emit midi`
without `-o` derives the output name from the input (`file.cal` → `file.mid`).
Backends implemented so far: **ir** (Music IR text) and **midi** (a format-0
Standard MIDI File). `main` must be `Music` (a bare `Pitch` is lifted to one note)
for the MIDI backend. The audio / MusicXML backends are still to come.

A program is a set of bindings; `main` is the entry point and is expected to have
type `Music` (any value type works while experimenting). To use the standard
library, a file must load it explicitly with a directive:

```
#load "prelude"
```

(`#load "prelude"` resolves to the bundled standard library; other names are read
as file paths. Each loaded file is parsed as its own unit, so error line numbers
stay relative to the file they occur in.) The `ir` and `midi` backends are wired
up (see the options above); the audio / MusicXML backends still report "not
implemented".

### Interpreter — `calliopei`

Runs a file, or starts a REPL when given no arguments. Both print the evaluated
Music tree. A file must `#load "prelude"` to use the stdlib; **the REPL preloads
the prelude**, so it is always in scope there.

```sh
./build/calliopei file.cal       # run a file: print the Music tree of `main`

./build/calliopei                # no args: start the REPL (prelude preloaded)
λ> double x = x * 2              # a definition is remembered for the session
λ> double 5
  10 :: Int
λ> transpose P5 (c' e' g')
  ((G4:1/4 :+: B4:1/4) :+: D5:1/4) :: Music
λ> :quit
```

A line that parses as a **definition** (`x = …`, `foo x = …`, a signature, a
`class`/`instance`) — or a `#load "file"` directive — is added to the session,
silently, like ghci, and stays in scope; if it fails to compile it is reported and
not kept. Any other line is **evaluated** as an expression: its value and its
inferred type are printed on one line. `:type <expr>` shows a type without
evaluating; `:quit` (or `:q`) exits. (No line editing/history yet, and multi-line
input is not implemented, so each definition must fit on one line.)

### Examples

Runnable programs live in [`../examples/`](../examples): `hello.cal`,
`reverse-notes.cal`, `transforms.cal`, `arpeggio-and-chord.cal`, the headline
`development.cal`, and the boolean/comparison tours `not-operator.cal` and
`comparing-music.cal`.
