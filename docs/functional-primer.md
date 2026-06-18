# A functional programming primer (for Calliope contributors)

This page is for contributors who have written imperative code (Python, Java, C,
JavaScript, …) but have never used a functional language. Calliope is functional,
so the codebase and the language it implements will feel unfamiliar at first.
After this page you should be able to read the standard library and the examples.

Everything here uses **Calliope's actual syntax**, not full Haskell — only the
parts that exist in the language today.

---

## 1. The core idea: functions and values, nothing else

Imperative programming is a list of **commands** that change **state**:

```python
# imperative: do this, then that, mutating variables
total = 0
for n in [1, 2, 3]:
    total = total + n      # total is reassigned each step
print(total)
```

Functional programming has no commands and no changing state. A program is just
**expressions that produce values**, and **functions that turn values into other
values**. You never say "do X then Y"; you say "the answer *is* this expression".

```
-- functional: the answer IS this expression
sum [1, 2, 3]      -- evaluates to 6
```

There is no "then". There is no variable being overwritten. `total = 0` in a
functional language is not an assignment you can change later — it is a permanent
**definition**, like `π = 3.14159` in maths. This single difference (no mutation)
drives everything else.

### Lambda calculus — the theory underneath

In the 1930s Alonzo Church showed you can compute *anything* with just three
things:

1. **variables** — `x`
2. **functions of one argument** — written `\x -> body` ("the function that takes
   `x` and returns `body`"). The `\` is a lambda (λ).
3. **application** — putting a function next to its argument: `func arg`.

That is the whole language. No loops, no assignment, no numbers even (those can be
built from functions too). This is **lambda calculus**, and every functional
language is lambda calculus plus convenience.

In Calliope you write a lambda exactly that way:

```
\x -> x + 1          -- the function that adds one
(\x -> x + 1) 41     -- apply it to 41  ->  42
```

"Running" a program is just **substitution**: replace the parameter with the
argument and simplify.

```
(\x -> x + 1) 41
=  41 + 1            -- substitute x := 41
=  42
```

That's it. Evaluation is repeated substitution until nothing's left to do.
Everything below is sugar on top of this.

---

## 2. Functional vs imperative, point by point

| | Imperative | Functional (Calliope) |
|--|-----------|------------------------|
| Building block | statement (a command) | expression (a value) |
| State | variables you mutate | values that never change |
| Repetition | loops (`for`, `while`) | recursion (a function calling itself) |
| Order | "do this, then that" | nesting of expressions |
| A function | may change the world | only maps input → output |
| `x = 5` | assignment (can change) | definition (permanent) |

### No loops — recursion instead

There is no `for` or `while`. To process a list you write a function that handles
one element and calls itself on the rest. Summing a list:

```
sum xs = case xs of
  []           -> 0                    -- empty: the sum is 0
  first : rest -> first + sum rest     -- else: first plus the sum of the rest
```

`case` is explained in §4. The point: where an imperative loop *mutates a counter*,
a functional program *describes the answer in terms of a smaller answer*.

### No mutation — new values instead of changed ones

You never change a list; you produce a new one. `reverse [1,2,3]` does not flip the
original list in place — it returns a brand-new `[3,2,1]` and the original still
exists. This sounds wasteful but is the source of functional code's safety: since
nothing is ever modified behind your back, a value means the same thing everywhere,
forever. You can pass `subject` to ten transforms and know none of them corrupted it.

### Referential transparency

Because functions only map input to output (no hidden state, no side effects),
calling `double 5` *always* gives `10`. You can replace any call with its result
without changing the program. This is called **referential transparency**, and it
makes code easy to reason about, test, and — for us — render deterministically.

---

## 3. Why functional fits music

Calliope is functional on purpose. Composing music *is* functional.

**A composer thinks in transformations, not commands.** "Take the subject, invert
it, answer it up a fifth." Those are functions from a phrase to a phrase:

```
development subject = subject `par` (invert subject ^+ P5)
```

"the development is the subject played together with its inversion,
shifted up a fifth." The code *is* the musical idea. Compare the imperative version
you'd write in C — loops over a note buffer, indices, a pitch-arithmetic helper,
off-by-one bugs — where the musical intent drowns in bookkeeping.

**A phrase is a value, and variations are new values.** When a composer varies a
theme, the original theme still exists; the variation is a *derived* thing. That is
exactly immutability. `invert subject` doesn't wreck `subject` — you can keep using
it. So you can build a whole piece by naming small phrases and combining them, with
no fear that one transform quietly mangled another.

**Transforms compose.** Functions chain. The pipe operator reads a chain left to
right, in the order the music happens:

```
subject |> invert |> transpose P5 |> times 2
```

"Take the subject, invert it, move it up a fifth, then play that twice."

**Music has an algebra.** Two fundamental ways to combine: in **sequence** (one
after another, `:+:`) and in **parallel** (at the same time, `:=:`). Those are
operators, and a whole piece is a tree built from them — exactly the kind of
recursive structure functional languages handle naturally (see the Music tree in
[builtins.md](./builtins.md)). A transform like `retrograde` (play backwards) is
just a function that walks that tree and rebuilds it.

**A program is a value the runtime plays.** `main` evaluates to a `Music` value;
the runtime renders it. No "output commands" scattered through the code — the music
*is* the result. Pure in, sound out.

---

## 4. Basic Calliope syntax (the Haskell-flavoured part)

Calliope's non-musical syntax is a small subset of Haskell. Here is everything you
need to read the prelude.

### Defining functions

```
double x   = x * 2          -- one parameter
add  x y   = x + y          -- two parameters
greeting   = c' e' g'       -- no parameters (just a value)
```

Left of `=`: the name and its parameters. Right of `=`: the result expression.

### Applying functions — juxtaposition, no parentheses

You call a function by writing it next to its arguments, separated by spaces:

```
double 5            -- 10        (NOT double(5))
add 3 4             -- 7         (NOT add(3, 4))
map double [1, 2]   -- [2, 4]
```

Parentheses are only for **grouping**, like in maths:

```
double (add 1 2)    -- double of 3  = 6
double add 1 2      -- WRONG: reads as (double add 1 2), four things in a row
```

This spacing-is-application rule is why a function call looks so bare. It is also
why **`f` cannot be a variable name** in Calliope: bare letters `a`–`g` are pitch
literals (notes), so `f` already means the note F. Use names like `func`, `fn`,
`subject`, `x` instead. (See [syntax.md](./syntax.md) §"reserved pitch class".)

### Partial application (currying)

Every multi-argument function is really a chain of one-argument functions. So you
can supply *some* arguments and get back a function waiting for the rest:

```
add 3               -- a function that adds 3 to whatever comes next
transpose P5        -- a function that raises any phrase a fifth
map double          -- a function that doubles every element of a list
```

This is why `transpose P5 phrase` works and why you can pass `double` to `map`.
It is everywhere in functional code; get comfortable with "a function missing its
last argument is still a useful value."

### `if` / `then` / `else` is an expression

It produces a value (both branches required):

```
abs x = if x < 0 then 0 - x else x
```

### `case` and patterns — branching on shape

`case` looks at a value and picks the first branch whose **pattern** matches,
binding names as it goes:

```
describe xs = case xs of
  []           -> 0                 -- matches the empty list
  first : rest -> first             -- matches a non-empty list; names its parts
```

Patterns you'll see:

- `_` — matches anything, binds nothing ("don't care")
- `x` — matches anything, binds it to `x`
- `0`, `True`, `False` — match that exact value
- `[]` — the empty list
- `first : rest` — a non-empty list, split into head (`first`) and tail (`rest`)

The `:` symbol both **splits** a list (in a pattern) and **builds** one (in an
expression): `1 : [2, 3]` is `[1, 2, 3]`.

### Recursion replaces loops

A function that calls itself on a smaller input, with a base case to stop. The
list functions all follow one shape: handle `[]`, otherwise peel the `first` and
recurse on the `rest`.

```
length xs = case xs of
  []       -> 0
  _ : rest -> 1 + length rest
```

`length [10,20,30]` becomes `1 + (1 + (1 + 0))` = 3. Every list function in the
prelude follows this same shape; read them alongside [stdlib.md](./stdlib.md).

### Lambdas — unnamed functions

When you need a small function on the spot:

```
map (\pitch -> reflectPitch pitch axis) phrase
```

`\pitch -> ...` is a one-off function used as an argument. Same `\x -> body` form
from the lambda calculus in §1.

### Operators and infix

Operators are just functions written between their arguments: `1 + 2`, `x : xs`,
`a :+: b`. Any ordinary function can be used infix with backticks — these two are
identical:

```
sequence a b       -- normal application
a `sequence` b      -- same thing, written between
```

That's why `subject `par` answer` reads naturally. The **pipe** `|>` feeds a value
into a function: `x |> func` is just `func x`, but lets you read a pipeline left to
right instead of inside out.

### Types and signatures

Calliope infers types automatically, but the standard library declares each
function's type as documentation, on the line above it:

```
map :: (t -> u) -> [t] -> [u]
map func xs = ...
```

Read `::` as "has type". `->` is the function arrow. `[t]` is "a list of `t`". A
lowercase letter like `t` is a **type variable** — it stands for *any* type, so
`map` works on lists of anything. So that signature reads: "`map` takes a function
from `t` to `u` and a list of `t`, and gives back a list of `u`." Base types are
`Int`, `Bool`, `Pitch`, `Music`, `Interval`. (Type variables can't be `a`–`g`
either — same pitch rule — so we use `t`, `u`, `v`.)

### Type classes (briefly)

A type class is a set of types that support some operation. `Transposable` is the
set of things you can transpose; both `Pitch` and `Music` belong to it, so `^+`
works on either. You'll mostly just *use* these; defining them is rarer. See
[builtins.md](./builtins.md).

---

## 5. What Calliope does **not** have (so you don't go looking)

- **No mutation, no loops, no `return`.** Recursion and expressions only.
- **No laziness.** Calliope is *eager* (arguments evaluated before a call), unlike
  real Haskell. So there are no infinite lists.
- **No `data` declarations or multi-equation function clauses yet** — use `case`.
- **No monads / `do` notation / IO actions.** A program is a pure value; the
  runtime renders it.

---

## Where to go next

- [syntax.md](./syntax.md) — the full language surface, precisely.
- [stdlib.md](./stdlib.md) — the library functions, with examples.
- [builtins.md](./builtins.md) — the primitive operations and the Music tree.
- `standard_library/prelude.cal` — read it now; it should make sense.
