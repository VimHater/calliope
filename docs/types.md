# Types

Calliope is statically typed with **full type inference**: you almost never write
a type, the compiler works it out. Types catch mistakes before anything runs — a
`Pitch` is not an `Int`, a melody is not a number — and they drive how `^+`
(transpose) knows whether it is shifting a single pitch or a whole phrase.

This page describes the type system **as implemented**. For the operators and
builtin functions and their types, see [builtins.md](./builtins.md); for the
stdlib functions, [stdlib.md](./stdlib.md).

## Inference — you don't write types

The checker uses Hindley–Milner inference (Algorithm W). Every expression has a
type the compiler figures out on its own:

```
5            -- Int
True         -- Bool
c'           -- Pitch      (a single pitch literal)
c d e        -- Music      (a run of notes)
[1, 2, 3]    -- [Int]      (a list of Int)
P5           -- Interval
\x -> x      -- t0 -> t0   (a function; see "type variables" below)
```

The REPL's `:type <expr>` prints the inferred type without evaluating, and the
compiler prints the type of `main` with `--dump types`.

## The base types

| Type | Values | Notes |
|------|--------|-------|
| `Int` | `0`, `42`, `-3` | integers; `/` is integer division |
| `Bool` | `True`, `False` | |
| `Pitch` | `c'`, `fis,`, `g'8` | a spelled, tuned pitch |
| `Music` | `c d e`, `<c e g>`, `r` | a tree of notes/rests in time |
| `Interval` | `P5`, `m3`, `M7` | a distance between pitches |
| `Rational` | (durations) | exact fractions; e.g. `noteDur` returns one |
| `Str` | `"hello"` | string literal |
| `[t]` | `[1, 2, 3]`, `[]` | a list of some element type `t` |
| `t -> u` | `\x -> x + 1`, `double` | a function from `t` to `u` |

There is **no `Float`** — real numbers appear only at the synthesis boundary,
never in the language. Pitches keep their spelling (C♯ ≠ D♭); collapse to a number
only with `semitones`.

## Function types and currying

`->` is the function arrow, and it is right-associative, so a two-argument
function reads as a chain:

```
add :: Int -> Int -> Int          -- really  Int -> (Int -> Int)
```

That is why **partial application** works: `add 3` is a value of type `Int -> Int`
(a function still waiting for its second argument). Application itself is just
writing a function next to its argument (`add 3 4`), no parentheses.

## Type variables and polymorphism

A **type variable** — a lowercase name like `t`, written `t0`, `t1`, … when the
compiler prints it — stands for *any* type. A function whose type contains type
variables is **polymorphic** (works at many types):

```
map     :: (t0 -> t1) -> [t0] -> [t1]
length  :: [t0] -> Int
\x -> x :: t0 -> t0
cons    :: t0 -> [t0] -> [t0]
```

`map` works on a list of anything; `length` counts a list of anything. One program
can use such a function at several types at once:

```
main = length [1, 2, 3] + length [True, False]   -- length used at [Int] and [Bool]
```

> **The pitch rule applies to type variables too.** A type variable is an
> identifier, so it cannot be spelled like a pitch (`a`–`g`, `r`, `s`). Use `t`,
> `u`, `v`. The classic Haskell `class Transposable a` becomes `class Transposable
> t` in Calliope.

**When generalization happens.** Top-level bindings are generalized — made
polymorphic — in dependency order (so a helper is generalized before later code
uses it). `let`/`where`-local bindings are **monomorphic** (not generalized): a
local helper is fixed to one type. This is a current limitation.

## Type classes

A **type class** is a set of types that support some operation; a function can be
written once and dispatch to the right behaviour per type. Classes here are
**single-parameter** and dispatch on a method's **first argument**.

The builtin one is `Transposable` — the types you can transpose:

```
(^+) :: Transposable t0 => t0 -> Interval -> t0
```

Read `Transposable t0 =>` as "for any `t0` that is Transposable". Builtin instances
exist for `Pitch` and `Music`, so `^+` shifts a single pitch *or* a whole phrase,
and the result type equals what you shifted:

```
c' ^+ P5            -- Pitch
(c d e) ^+ P5       -- Music
transpose           -- :: Transposable t0 => Interval -> t0 -> t0
```

The other builtin class is **`Phrase`** — the things `:+:` / `:=:` compose
(instances `Pitch` and `Music`), which is why a bare pitch can sit beside a phrase
(`c' :+: (d e)`) and why `fn x = x :+: x` is `Phrase t0 => t0 -> Music`.

Using a method at a type with **no instance** is a type error. You can declare
your own classes and instances:

```
class Describable t where
  describe :: t -> Int

instance Describable Pitch where
  describe p = semitones p

instance Describable Bool where
  describe flag = if flag then 1 else 0
```

Now `describe` has type `Describable t0 => t0 -> Int` and works on `Pitch` and
`Bool` (and any other type you add an instance for). Operator methods are defined
infix: `instance Transposable Music where m ^+ i = mapPitches (\p -> p ^+ i) m`.

Not supported yet: superclasses, default method bodies, multi-parameter classes.

## Type signatures

You may write a signature on the line above a binding, with `::` ("has type"):

```
square :: Int -> Int
square x = x * x
```

Signatures are accepted on top-level and on `where`/`let`-local bindings, and the
standard library declares one for every function as documentation. **They are
currently informational only** — the checker infers types and does not yet read or
enforce signatures. So a wrong signature is not (yet) an error; the inferred type
is what counts.

## Reading an inferred type

- `t0`, `t1`, … are type variables, numbered in order of appearance.
- `->` is the function arrow (right-associative).
- `[t0]` is a list.
- `C t0 =>` is a class constraint (a qualified type).

```
foldr :: (t0 -> t1 -> t1) -> t1 -> [t0] -> t1
```

"`foldr` takes a function combining a `t0` and a `t1` into a `t1`, a starting `t1`,
and a list of `t0`, and returns a `t1`."

## Type errors

When inference fails, you get a message such as:

```
type error: type mismatch: Int vs Pitch        -- e.g. 1 + c'
type error: unbound name in type check: foo     -- foo isn't defined
type error: no instance for Describable Str      -- a method used at a type with no instance
```

Note: **type-error messages do not carry line/column numbers yet** (only parse
errors do). They report what conflicted, not where.

## What the type system does *not* have

- **No `Float`** (only at the synthesis boundary), no numeric type classes — `+`
  `-` `*` `/` are `Int`-only.
- **No laziness** — evaluation is eager, so e.g. a value defined in terms of
  itself (`h = h * 2`) type-checks but cannot produce a value.
- **No higher-kinded types, no multi-parameter classes, no superclasses.**
- **`let`/`where` bindings are monomorphic** (only top-level generalizes).
- **Signatures are not checked** (inference only) — see above.
- **`:+:` / `:=:` compose via the builtin `Phrase` class** (`Phrase t => Phrase u
  => t -> u -> Music`), instances `Pitch` and `Music`. A bare `Pitch` operand
  lifts to a one-note phrase, so `c' :+: d'` is `Music`; a non-phrase operand
  (`1 :+: 2`) is a `no instance for Phrase Int` error. Adjacency, `c d e` (a
  `Seq`), is still the idiomatic spelling.
