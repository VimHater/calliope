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
./build/calliope path/to/file.cal          # print the Music tree of `main` to stdout
./build/calliope --emit midi file.cal      # other backends: not implemented yet
./build/calliope --dump types file.cal     # debug: also dump tokens / ast / types
```

```
usage: calliope [options] <file.cal>
  --emit <fmt>    ir (default) | midi | wav | mp3 | mp4 | musicxml
  --dump <what>   tokens | ast | types   (repeatable)
```

The compiler prepends the standard-library prelude, so every stdlib function is in
scope. A program is a set of bindings; `main` is the entry point and is expected to
have type `Music` (any value type works while experimenting). Today only `--emit
ir` is wired up — it prints the evaluated Music tree; the audio / MIDI / MusicXML
backends are stubs that report "not implemented".

### REPL — `calliope-repl`

```sh
./build/calliope-repl
λ> transpose P5 (c' e' g')
  ((G4:1/4 :+: B4:1/4) :+: D5:1/4)  :: Music
λ> :type invert
  :: Music -> Music
λ> :quit
```

Each line is evaluated as an expression with the prelude in scope. `:type <expr>`
shows a type without evaluating; `:quit` (or `:q`) exits. (Persistent session
bindings and multi-line input are not implemented yet.)

### Examples

Runnable programs live in [`../examples/`](../examples): `hello.cal`,
`reverse-notes.cal`, `transforms.cal`, `arpeggio-and-chord.cal`, and the headline
`development.cal`.
