# Algo — language reference

> **Scope of this file.** Roadmap item 1.5 requires the **overflow semantics**
> to be written down, so that is what is here. The EBNF and the precedence
> table belong to the same file and are item **6.3**'s to add; this file was
> created early rather than duplicating the rules into the README and moving
> them later.

## Integers

An integer is a signed **64-bit two's-complement** value. There is exactly one
numeric type; the language has no floats, no unsigned type and no width
suffixes.

| | |
|---|---|
| Largest integer | `9223372036854775807` |
| Smallest integer | `-9223372036854775808` |

A *literal* is a run of digits. `-` is never part of one — it is the unary
minus operator applied to the literal after it. That has a consequence worth
stating plainly, because it looks like a bug the first time it is met:

**The smallest integer cannot be written down.** `-9223372036854775808` is
unary minus applied to the literal `9223372036854775808`, and that literal is
one past the largest integer, so it is rejected before the negation is ever
applied. The value exists and programs can hold it — it just has to be computed:

```
least = 0 - 9223372036854775807
least = least - 1
```

A literal outside the range is a **compile-time error, exit 65**, even though
the digits are converted while the tree is being walked. It is a property of
the token's text and of nothing the program computes, which is what puts it on
the compile-time side.

```
print 9223372036854775808
```

```
overflow.algo:1:7: error: integer literal out of range: 9223372036854775808
print 9223372036854775808
      ^~~~~~~~~~~~~~~~~~~
```

## Overflow traps

Every arithmetic operator **traps** when its result will not fit. Nothing wraps
silently, and there is no unchecked mode.

An overflow is a **runtime fault, exit 70** — the opposite classification from
the out-of-range literal above, and for the opposite reason: whether `x + y`
fits in 64 bits depends on what the program computed, and cannot be settled
from the source text. The two are the errors most likely to be confused with
one another, and they exit differently.

Five sites can trap:

| Operation | Traps when |
|---|---|
| `a + b` | the sum does not fit |
| `a - b` | the difference does not fit |
| `a * b` | the product does not fit |
| `a / b` | **only** for `-9223372036854775808 / -1` |
| `-a` | **only** for `-(-9223372036854775808)` |

The last two are the same fact reached by two operators: the range is
asymmetric, so the smallest integer has no positive counterpart. Every other
division and every other negation is safe — including `9223372036854775807 / -1`,
which is fine precisely because the range is lopsided the other way.

Division has two failure modes and **a zero divisor is reported first**:

```
x = 5 / 0
```
gives `division by zero`, never an overflow. The two conditions are mutually
exclusive (`b == 0` versus `b == -1`), so the order is a matter of which
message a reader meets, not of which programs are accepted.

The caret covers **the whole operation**, not one operand — the same convention
division by zero and a type mismatch already follow. Neither operand is wrong
by itself; combining them is.

```
total = 9223372036854775807
print total + 1
```

```
overflow.algo:2:7: error: integer overflow in '+'
print total + 1
      ^~~~~~~~~
```

```
least = 0 - 9223372036854775807
least = least - 1
print least / -1
```

```
overflow.algo:3:7: error: integer overflow in '/'
print least / -1
      ^~~~~~~~~~
```

Unary minus is worded apart from the binary operator, because `-` names two of
them and a reader needs to know which one trapped: `integer overflow in unary '-'`.

## Why it traps rather than wrapping

Signed overflow is undefined behaviour in C++. This language exists to be
measured, and a benchmark whose arithmetic silently wraps is reporting a number
produced by a program whose meaning the standard does not define — the result
would not survive the first question asked about it.

Wrapping *deliberately*, on unsigned arithmetic, was the alternative and is
worse here: it makes a wrong answer indistinguishable from a right one at
exactly the point where a reader has to decide whether to trust it. A trap is
loud, and loud is the property this project needs.

The check is `__builtin_add_overflow` and its siblings, which both compute and
test in one step, and are available on GCC and Clang alike.

**On the cost, since this project's subject is cost.** The check sits on every
arithmetic operation, which is the hot path of every benchmark in Phase 3 — so,
unlike the call-depth limit, it cannot be defended on the grounds that a
benchmark avoids it. It is defensible on a different ground: the cost is
**identical in every configuration** — the naive tree-walker, all ten ablation
configurations, and the bytecode VM, which has to trap the same way or the
differential testing in item 4.4 would be comparing two different languages. No
ablation removes it, so it never appears as any ablation's delta, and a term
present on both sides of a subtraction cancels. It does move the final ratio
slightly toward 1, which understates the reported speedup rather than
overstating it.

Removing the check is deliberately **not** one of the ablations. Every ablation
preserves what a program computes and changes only how fast; this one would
change the answers.

## Verifying this

The overflow rules are covered by `tests/int64_range.algo` (the values that
need more than 32 bits, and the near misses that must *not* trap),
`tests/error_int_overflow.algo`, `tests/error_overflow.algo`, and by unit
checks in `tests/expression_test.cpp` and `tests/diagnostic_test.cpp`.

The suite also runs clean under UndefinedBehaviorSanitizer, which is what says
the traps fire *before* the undefined operation rather than after it:

```bash
cmake -S . -B build-ubsan -DCMAKE_CXX_STANDARD=20 -DALGO_SANITIZE=ON && cmake --build build-ubsan && ctest --test-dir build-ubsan
```
