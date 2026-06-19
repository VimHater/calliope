# Syntax

## The reserved pitch class

The defining lexical rule: **any token that matches the pitch grammar is always a
pitch**, decided at lex time before any type information exists. A pitch literal is

```
[a-g] (is | es)* ( ' + | , + )? [0-9]* \.*
```

a letter `a`–`g`, then zero or more accidentals, then octave marks, then an
optional duration. Because this class is reserved, **identifiers may not be
spelled like a pitch**. So `c`, `f`, `cis`, `e4` cannot be variable names, and the
letters `r`, `R`, `s` are reserved as rests. Every other name is free: `subject`,
`motif`, `phrase`, `x`, `func`.

This applies to **type variables** too (they are identifiers). The Haskell-style
`class Transposable a` must use a non-pitch variable in Calliope — the convention
is `t`:

```
class Transposable t where (^+) :: t -> Interval -> t
```

## Pitch literals

| Part | Syntax | Notes |
|------|--------|-------|
| letter | `a` `b` `c` … `g` | a bare letter is octave 3, so `c'` is C4 (middle C) |
| sharp | `is` | repeatable: `cisis` = C double-sharp |
| flat | `es` | repeatable: `aeses` = A double-flat |
| octave up | `'` | each `'` raises one octave: `c''` = C5 |
| octave down | `,` | each `,` lowers one octave: `c,` = C2 |
| duration | digits | LilyPond style: `4` = quarter, `8` = eighth, `2` = half, `1` = whole |
| dotted | `.` | `c'4.` = dotted quarter (3/8); `..` = double dotted |

Spelling is preserved (C♯ ≠ D♭) — collapse to a pitch number only at the playback
boundary with `semitones`. A pitch literal with no duration defaults to a quarter
note. Durations are exact rationals (whole-note fractions).

```
cis'      -- C#4, quarter
ees,2     -- Eb2, half note
g'8.      -- G4, dotted eighth
```

## Notation: notes, rests, chords, sequences

- A **single pitch literal evaluates to a `Pitch`** value (`c'` :: `Pitch`).
- **Adjacent** pitch literals **sequence** into `Music`: `c d e` builds a `Seq`
  (it is sugar for `c :+: d :+: e`). Each pitch is lifted to a note.
- A **chord** `<c' e' g'>` sounds its notes together (a `Par`). Each note keeps
  its own duration, so `<c'2 e'2 g'2>` is a half-note triad.
- A **rest** is `r` (or `R`, or the spacer `s`); it is `Music`. Rests take a
  duration like notes: `r2` is a half rest, `r4.` a dotted-quarter rest.
- A **tie** `~` joins two **same-pitch** notes into one of summed duration:
  `c'4 ~ c'8` is `C4` held `3/8`. (Binary only — no chaining yet; tying two
  different pitches is an error.)
- A **tuplet** fits notes against the beat: `tuplet n m music` plays `n` notes in
  the time of `m` (scaling every duration by `m/n`). A triplet is `tuplet 3 2`
  (or the prelude's `triplet`): `tuplet 3 2 (c c c)` makes three `1/6` notes.

```
c' d' e'          -- a three-note melody     :: Music
<c' e' g'>        -- a C-major triad         :: Music
c' r e'           -- note, rest, note        :: Music
c'4 ~ c'8         -- one C4 held 3/8         :: Music
tuplet 3 2 (c c c) -- an eighth-note triplet  :: Music
```

A run of notes does not cross line breaks (notation stays on one line); ordinary
expressions may span lines (see Layout).

## Bindings

```
name           = expr               -- value binding
name p1 p2     = expr               -- function of two parameters
```

A `where` clause attaches local bindings:

```
frere = line [jacques, dormez]
  where
    jacques = c'4 d' e' c'
    dormez  = e'4 f' g'2
```

Top-level bindings may appear in any order (forward references and mutual
recursion are fine). Each binding is generalized for polymorphism in dependency
order, so a function may be used at several types in one program.

## Expressions

| Form | Example |
|------|---------|
| application | `invert subject`, `map double xs` |
| lambda | `\p -> reflectPitch p axis` |
| `if`/`then`/`else` | `if null xs then 0 else 1` |
| `case`/`of` | `case xs of { [] -> 0; h : t -> 1 + length t }` |
| `let`/`in` | `let y = 5 in y * y` |
| list | `[1, 2, 3]`, `[]`, `1 : 2 : [3]` |
| parenthesised | `(invert subj ^+ P5)` |
| booleans | `True`, `False` |
| backtick infix | ``subj `par` answer`` (use any binary function infix) |
| pipe | `phrase \|> invert \|> transpose P5` (`x \|> f = f x`) |

Application binds tighter than every operator. `and`/`or` are short-circuiting
keyword operators; `not` is an ordinary function.

### Operators and precedence

Higher binds tighter. Right-associative unless noted.

| Prec | Operators | Assoc | Meaning |
|------|-----------|-------|---------|
| 9 | `.` † | right | function composition |
| 7 | `*` `/` | left | multiply, integer divide |
| 6 | `+` `-` | left | add, subtract |
| 6 | `^+` `^-` | left | transpose up / down (class `Transposable`) |
| 5 | `:+:` | right | sequential composition (`Music`) |
| 5 | `:` | right | list cons (`x : xs`) |
| 4 | `==` `/=` `<` `>` `<=` `>=` | left | comparison |
| 4 | `` `par` `` | left | any function used infix |
| 3 | `:=:` | right | parallel composition (`Music`) |
| 3 | `and` | right | logical and (short-circuit) |
| 2 | `or` | right | logical or (short-circuit) |
| 1 | `\|>` | left | pipe (`x \|> f = f x`) |
| 0 | `$` † | right | low-precedence application |

So `subj :+: motif ^+ P5` parses as `subj :+: (motif ^+ P5)`, and
`a :+: b :=: c` as `(a :+: b) :=: c`.

† `.` and `$` have reserved precedence but **no implementation yet** — using them
evaluates to an "unknown operator" error. Compose with explicit application or a
lambda for now.

## Pattern matching

`case <expr> of` with one alternative per line, column-aligned (offside rule).
The first matching alternative wins; a value matching none is a runtime error.

```
length xs = case xs of
  []           -> 0
  _ : rest     -> 1 + length rest

map func xs = case xs of
  []           -> []
  first : rest -> func first : map func rest
```

Supported patterns:

| Pattern | Matches |
|---------|---------|
| `_` | anything (binds nothing) |
| `x` | anything, binds it to `x` |
| `3` | that integer |
| `True` / `False` | that boolean |
| `[]` | the empty list |
| `h : t` | a non-empty list; `h` = head, `t` = tail (nestable) |
| `(p)` | grouping |

Cons `:` is also an expression operator, so `h : t` reads the same building a list
as it does destructuring one. Not yet supported: multi-equation function clauses
(`f [] = …`), `data` declarations / user constructors, list patterns `[a, b]`, and
Music patterns (`a :+: b` — use `isSeq`/`leftChild` for now).

## Types

Calliope has full Hindley–Milner inference; **signatures are optional**. The base
types are `Int`, `Bool`, `Pitch`, `Music`, `Interval`, `Rational`, `Str`, lists
`[a]`, and function types `a -> b`.

A type signature line is accepted syntactically:

```
subject :: Music
```

but is currently **informational only** — the checker infers types and does not
yet read signatures. Inferred types print in the driver's `== types ==` section.
Signatures may also precede `where`/`let`-local bindings (they are parsed and
discarded the same way). The standard library declares every function with a
signature above its definition as living documentation.

## Type classes

Single-parameter type classes, dispatched on a method's first argument:

```
class Describable t where
  describe :: t -> Int

instance Describable Pitch where
  describe p = semitones p

instance Describable Bool where
  describe flag = if flag then 1 else 0
```

A method gets a qualified type (`describe :: Describable t => t -> Int`). Using a
method at a type with no instance is a type error. Operator methods are defined
infix: `instance Transposable Music where m ^+ i = mapPitches (\p -> p ^+ i) m`.
`Transposable` is a builtin class with builtin instances for `Pitch` and `Music`.

Not yet supported: superclasses, default-method bodies, multi-parameter classes.

## Layout (multi-line expressions)

An expression continues onto the next line when that line is **indented past the
binding's name column** (the offside rule); a line at or left of it ends the
binding. In addition, an obviously-incomplete line always continues: after a
trailing operator, after `=`, inside `(` `[` and after `,`, and across
`if`/`then`/`else`, `->`, and `let … in`.

```
filter predicate xs =
  if null xs then []
  else if predicate (head xs)
       then cons (head xs) (filter predicate (tail xs))
       else filter predicate (tail xs)

total = 1 +
        2 +
        3
```

## Directives

Lines beginning with `#` are preprocessor directives:

```
#load "prelude"        -- bring the standard library into scope
#load "scales.cal"     -- load another file (its bindings join the program)
#relative c'
#absolute
```

**`#load` works**: `#load "prelude"` loads the bundled standard library; any other
name is read as a file path. Loaded files are parsed as their own units and merged
in, so they do not shift the program's error line numbers. A program must `#load
"prelude"` to use the stdlib (the REPL preloads it). `#relative` / `#absolute`
are parsed but **not yet implemented** — octaves are always absolute.

## Comments

```
-- line comment
{- block comment -}
```
