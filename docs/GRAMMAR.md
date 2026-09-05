# Algo — language reference

> **Scope of this file.** The whole of the language: its **grammar**, its
> **precedence and associativity**, its **integer and overflow semantics**, and the
> **errors it can raise** with the exit code each one carries.
> Item 1.5 wrote the overflow third; item 6.3 added the grammar and the
> precedence table. The instruction set the second back end compiles this
> language to is [`docs/BYTECODE.md`](BYTECODE.md); how the two back ends are
> measured, and what those measurements do not establish, is
> [`docs/MEASUREMENT.md`](MEASUREMENT.md).
>
> **Every listing on this page was run and its output copied from the result.**
> None is hand-written from the source or from this file's own rules, and all of
> them were re-run under both engines — `algo <file>` and `algo --engine=vm
> <file>` — after the virtual machine arrived.

## The grammar

Notation is EBNF: `{ x }` is zero or more, `[ x ]` is optional, `|` is
alternation, and a quoted run is a terminal.

```ebnf
program          = { function | statement } ;

function         = "fn" identifier "(" [ parameter-list ] ")" block ;
parameter-list   = identifier { "," identifier } ;

block            = "{" { statement } "}" ;

statement        = print-statement
                 | if-statement
                 | while-statement
                 | return-statement
                 | block
                 | assignment ;

print-statement  = "print" expression ;
if-statement     = "if" expression block [ "else" ( if-statement | block ) ] ;
while-statement  = "while" expression block ;
return-statement = "return" [ expression ] ;
assignment       = identifier "=" expression ;

expression       = equality ;
equality         = comparison { ( "==" | "!=" ) comparison } ;
comparison       = term { ( "<" | "<=" | ">" | ">=" ) term } ;
term             = factor { ( "+" | "-" ) factor } ;
factor           = unary { ( "*" | "/" ) unary } ;
unary            = ( "-" | "!" ) unary
                 | primary ;
primary          = number
                 | boolean
                 | identifier [ "(" [ argument-list ] ")" ] ;
argument-list    = expression { "," expression } ;

number           = digit { digit } ;
boolean          = "true" | "false" ;
identifier       = letter { letter | digit } ;

letter           = "A" … "Z" | "a" … "z" ;
digit            = "0" … "9" ;
```

That is the whole language. A program that uses only these rules:

```
i = 1
total = 0
while i <= 100 {
    total = total + i
    i = i + 1
}
print total
```

```
5050
```

### Seven things the rules above say that are easy to read past

**A function may only be declared at the top level.** `function` appears in
`program` and in no other rule — not in `block`, so not inside an `if`, a
`while`, or another function. The parser says so in as many words rather than
reporting a statement that cannot begin with `fn`.

**A call is not a rule of its own.** `primary` consumes an identifier and then
looks at one further token: a `(` makes it a call, anything else makes it a
variable read. There is no call whose callee is an expression, because such an
expression would have to evaluate to a function and functions are not values in
this language.

**`(` is legal in exactly one position — immediately after an identifier**,
where it opens a call's argument list or, after `fn`, a parameter list. There
are **no grouping parentheses**:

```
a = 1
b = 2
c = 3
print a * (b + c)
```

```
grouping.algo:4:11: error: expected an expression, found '('
print a * (b + c)
          ^
```

An expression is built out of precedence and left associativity instead:
`i * 6 / 3` is `(i * 6) / 3`. This is a property of the language rather than an
omission — adding grouping would be a new language feature, which the roadmap
forbids, and a program that seems to need one is a program to rewrite.

**A condition takes no parentheses, and adding them is the same error.** `if`
and `while` are followed by an expression and then a block, so the C habit is a
compile error at the `(`:

```
x = 1
while (x) {
  x = 0
}
```

```
cond.algo:2:7: error: expected an expression, found '('
while (x) {
      ^
```

**There is no statement separator.** No semicolons, and a newline is nothing but
whitespace. A statement ends where the next one begins, which the grammar
already says by writing `block` as `"{" { statement } "}"` with nothing between
the repetitions.

**There is no comment syntax.** `#` is an unknown character and `//` lexes as
two divides, so a `.algo` file cannot document itself. Explanation lives in the
commit message or in `docs/`.

**An identifier is letters and digits, and must start with a letter.** No
underscore, no leading digit, no other character. The eight reserved words —
`print`, `if`, `else`, `while`, `fn`, `return`, `true`, `false` — are not
available as names: the lexer consumes a whole alphanumeric run before
classifying it, so `printer` and `trueValue` are ordinary identifiers while
`print` never is.

### What the grammar does not decide

Parsing accepts programs the compiler still rejects, and every one of these is
a **compile-time error, exit 65**, decided in `src/resolver.cpp` after the tree
is built: a name read where nothing has assigned it, a name used outside the
block that assigns it, a call to an unknown function, a call whose argument
count does not match the declaration, a function name used as a value, a
variable used as a function, a duplicate parameter or function name, and
`return` outside a function body. They are *not* grammar rules, and writing
them into the EBNF would be writing a different grammar.

The language has **no type checker**, so a type mismatch is not caught here
either — it is a runtime fault, exit 70, which is the same classification the
overflow traps below get and for the same reason.

## Precedence and associativity

Six levels, loosest first. Each level in the EBNF above parses the one below it
and then loops, which is what makes the five binary levels **left-associative**;
`unary` recurses into itself instead, which is what makes prefix operators
right-associative and stackable.

| Level | Operators | Associativity |
|---|---|---|
| equality | `==` `!=` | left |
| comparison | `<` `<=` `>` `>=` | left |
| term | `+` `-` | left |
| factor | `*` `/` | left |
| unary | `-` `!` (prefix) | right |
| primary | literals, variables, calls | — |

Consequences worth stating, because with no grouping parentheses the table is
the only way to read a program:

* `a - b - c` is `(a - b) - c`, not `a - (b - c)`.
* `i * 6 / 3` is `(i * 6) / 3` — `*` and `/` share a level and go left to right.
* `- -5` is `5`, and `!!true` is `true`. There is no decrement operator, so
  `--5` has only the one reading.
* `-2 * 3` is `(-2) * 3`. Unary binds tighter than `*`, as in C. The binding is
  not observable in the answer — negation distributes through multiplication and
  division — so it is the shape of the tree that records it, and
  `tests/expression_test.cpp` is what checks it.
* `a < b == c < d` is `(a < b) == (c < d)`, since equality is looser than
  comparison. Both operands are booleans, and `==` accepts them.
* There is no `&&` and no `||`, so there is no short-circuit level and nothing
  below equality.

**The second back end preserves every line of this table.** Three of the six
comparison operators have no opcode of their own — `<=`, `>=` and `!=` are
lowered onto `GT NOT`, `LT NOT` and `EQ NOT` — which changes what the virtual
machine executes and not what the language means. The lowering, and what it
costs, are in [`docs/BYTECODE.md`](BYTECODE.md).

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

**Both engines trap identically** — message, caret and exit code. `ADD`, `SUB`,
`MUL`, `DIV` and `NEG` are trapping opcodes in the instruction set for exactly
this reason, and [`docs/BYTECODE.md`](BYTECODE.md) fixes each fault's wording
there rather than leaving the second back end to invent its own. The three
listings above were re-run under `--engine=vm` and reproduced byte for byte on
stdout, on stderr and in the exit code.

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

## Errors and exit codes

Every error is written to **stderr** as a diagnostic carrying the file, line and column it
came from, the source line echoed, and a caret under the offending span:

```
$ ./build/algo error_undef.algo
error_undef.algo:1:7: error: variable 'z' used before assignment
print z
      ^
```

The span comes from the stage that raised the error; the path and the source text come from
the driver, which is the only part of the program that has them. Exit codes are
sysexits-style: `0` success, `64` bad command line, `65` compile-time error, `66` unreadable
input file, `70` runtime fault.

**The class of the exception thrown decides between 65 and 70, so the throw site picks the
code and not the catch site. A fault settled by the source text alone is compile-time; one
that depends on what the program computed is a runtime fault.**

| Error                 | Example input               | Message                                        | Exit |
| --------------------- | --------------------------- | ---------------------------------------------- | ---- |
| Unknown character     | `x = 5 @ 3`                 | `unknown character '@'`                        | 65   |
| Syntax error          | `x 5`                       | `expected '=' after variable name`             | 65   |
| Use before assignment | `print z` (no prior assign) | `variable 'z' used before assignment`          | 65   |
| Literal out of range  | `print 9223372036854775808` | `integer literal out of range: 9223372036854775808` | 65 |
| Division by zero      | `x = 5 / 0`                 | `division by zero`                             | 70   |
| Arithmetic overflow   | `print 9223372036854775807 + 1` | `integer overflow in '+'`                  | 70   |
| Type mismatch         | `print true + 1`            | `operator '+' cannot be applied to boolean and integer` | 70 |
| Non-boolean condition | `while 1 { … }`             | `a condition must be a boolean, not integer`   | 70   |
| Unclosed block        | `if true { print 1`         | `expected '}' to close this block`             | 65   |
| Wrong arity           | `add(1, 2, 3)` for `fn add(a, b)` | `function 'add' expects 2 arguments, but 3 were given` | 65 |
| Unknown function      | `print nope(1)`             | `unknown function 'nope'`                      | 65   |
| Function as a value   | `print f` for `fn f()`      | `'f' is a function, not a value`               | 65   |
| Variable as a function| `x = 1` then `print x(2)`   | `'x' is a variable, not a function`            | 65   |
| Function not top level| `if true { fn f() { } }`    | `a function may only be declared at the top level` | 65 |
| `return` outside a function | `return 1`            | `'return' outside of a function`               | 65   |
| Duplicate parameter   | `fn f(a, a) { … }`          | `duplicate parameter 'a'`                      | 65   |
| Duplicate function    | two `fn f`                  | `duplicate function 'f'`                       | 65   |
| Frame boundary        | a top-level name read inside a function | `variable 'total' used inside a function, where only parameters and locals are in scope` | 65 |
| Call depth exceeded   | unbounded recursion         | `call depth exceeded`                          | 70   |
| Missing argument      | `./algo`                    | `no input file`                                | 64   |
| File not found        | `./algo missing.algo`       | `could not open file: missing.algo`            | 66   |

A literal too wide for the value type is classed **compile-time**: it is a property of the token's text, not of anything the program computes. It is detected at parse time, with the conversion, so it fires on every literal in the file — including one inside a function that is never called. A type mismatch is classed the opposite way for the same reason read in reverse: with no type checker in the language, whether an operand has the right type depends on what the program computed, so it is a runtime fault.

The two call errors fall on the same line for the same reason. An argument count is settled by the source text alone — the call site says how many, the declaration says how many — so a wrong arity is a **compile-time** error. How deep a chain of calls actually gets depends on what the program computed, so an exhausted call depth is a **runtime** fault. That limit exists because unbounded recursion would otherwise exhaust the C++ stack and kill the process on a signal, with no diagnostic and no exit code at all.

**Both engines render every one of these identically** — message, caret and exit code. The
virtual machine duplicates the tree-walker's fault messages rather than sharing them, so
that the two agree by test rather than by construction, and all 29 golden cases in `tests/`
run under both. `tests/diagnostic_test.cpp` pins every message, caret and code, and is the
specification for this section.

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
