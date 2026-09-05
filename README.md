# Algo

[![CI](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml)

**A small language with a C++20 front end and two execution engines — a tree-walk
interpreter and a bytecode virtual machine — built so that what the second one is worth can
be measured rather than asserted.**

*This repository began as four-person college coursework. That inherited state is tagged
[`v0-coursework`](https://github.com/taneesha-beep/algo-vm/releases/tag/v0-coursework);
everything since — [`v0-coursework...main`](https://github.com/taneesha-beep/algo-vm/compare/v0-coursework...main)
— is solo work. The `master` branch is an earlier and thinner snapshot of the same
coursework era, kept frozen; the tag, not that branch, is the fuller team artifact.*

---

## The measured result

**Replacing the tree-walk interpreter with a bytecode virtual machine made three of the four
benchmark programs retire *more* instructions, not fewer.** Against the **hardened**
tree-walker — the same engine with four unforced inefficiencies removed — the VM costs
**+90.29%** on `arith`, **+54.82%** on `loop10m` and **+87.92%** on `vars`. It returns
**−36.61%** on `fib32`, the one program of the four that calls a function.

Against the **naive** tree-walker instead, the same VM looks like a 44–69% win on every
program. That second comparison is the one this project exists to refuse: it credits the
bytecode back end with four inefficiencies a tree-walker never had to have in the first
place. It is shown below, and it is not shown without the first beside it.

Three configurations of the same language, all measured under `valgrind --tool=cachegrind`
in a digest-pinned Linux container. Every figure here traces to a row in
[`results/measurements.csv`](results/measurements.csv).

| | |
|---|---|
| **N** | the naive tree-walk interpreter, at tag `v1-naive-treewalk` |
| **H** | the same tree-walker with four unforced inefficiencies removed |
| **V** | the bytecode compiler and virtual machine, reached by `algo --engine=vm` |

Instructions retired, per benchmark program. **H → V is the leading number**; N → V is the
same step with everything the four ablations removed folded back into it:

| Program | What it loads | N → H | **H → V** | N → V *(includes the four ablations)* |
|---|---|---:|---:|---:|
| `bench/arith.algo` | expression-tree traversal | −70.80% | **+90.29%** | −44.44% |
| `bench/fib32.algo` | call and frame overhead | −47.29% | **−36.61%** | −66.59% |
| `bench/loop10m.algo` | dispatch | −71.95% | **+54.82%** | −56.57% |
| `bench/vars.algo` | variable access | −83.28% | **+87.92%** | −68.57% |

The four ablations behind the N → H column are: passing the evaluated node by reference
rather than by value; converting integer literals once at parse time instead of re-parsing
their digits at every evaluation; dispatching operators on an enum rather than on a chain of
string comparisons; and indexing each frame by slot rather than looking names up in a
`std::map<std::string, Value>`. Each is a commit and a tag of its own, measured both in
isolation against N and cumulatively along `main`, and each carries a per-visit cost model
checked across all four programs. The workings are in
[`results/README.md`](results/README.md).

**The loss comes with a win that is not in the instruction column at all.** On
`bench/fib32.algo` the VM retires **99.83%** fewer D1 cache misses — **20,593 against
11,901,112** — because a call frame is a slot range on a stack the VM already owns rather
than a heap block per call. The cost sits in the branch columns instead: V mispredicts
**2.6x to 19.1x** as often as H, one indirect dispatch that every instruction funnels
through against a tree-walker's spread-out and individually predictable call sites.

**Four limits on how far H → V may be read.** All four are stated in full in
[*Boundary of the claim*](docs/MEASUREMENT.md#boundary-of-the-claim), which is the one place
in this repository whose job is to say what the attribution does *not* establish:

- **It is an upper bound on what replacing the back end did, not an attribution of what the
  architecture is worth.** A fifth ablation for locality was cut, so locality sits inside
  this step unlabelled rather than priced beside it. An upper bound on a mixed result bounds
  the losses too, so this is not a hedge in the flattering direction.
- **N → V is never quoted on its own**, here or anywhere else in the repository.
- **The controlled *cache* comparison is V against its own `V-tree` control, not V against
  H.** V's command line gains an argument and its binary links four translation units H's
  does not, and both move the cache columns; the 20,593-against-11,901,112 figure above is
  stated across that control for exactly this reason. `V-tree` prices the boundary at a
  fixed **+4,053** instructions (**+4,017** on `vars`) — a constant, not a rate — which is
  why the instruction column carries H → V and the cache columns do not.
- **No IPC figure exists here and none may be derived.** The `perf` columns are empty in all
  68 rows of the ledger, and wall clock is narrative — it is not divided into the instruction
  count to manufacture one.

---

## A diagnostic

Errors go to **stderr**, carrying the path, line and column they came from, the source line
echoed, and a caret under the offending span. Arithmetic **traps rather than wrapping**, so
an overflow is a diagnostic and not a wrong answer:

```
$ ./build/algo error_int_overflow.algo
error_int_overflow.algo:2:7: error: integer overflow in '+'
print total + 1
      ^~~~~~~~~
```

Exit code 70 — a runtime fault, because whether `total + 1` overflows depends on what the
program computed. An out-of-range *literal* is exit 65 instead, settled by the source text
alone. Both engines render this identically, and every case in `tests/` is checked against
both. The full table is under [Error handling](#error-handling).

---

## The bytecode

`--dump` compiles a program and prints its chunk to stdout instead of running it:

```bash
./build/algo --dump tests/while_sum.algo
```

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
== chunk ==
  code: 51 bytes  constants: 4  functions: 0  program frame: 2 slots

== constants ==
  #0  1
  #1  0
  #2  100
  #3  1

== functions ==
  (none)

== code ==
  0000  line   1  CONST             0  ; 1
  0003  line   1  STORE_LOCAL       0
  0006  line   2  CONST             1  ; 0
  0009  line   2  STORE_LOCAL       1
  0012  line   3  LOAD_LOCAL        0
  0015  line   3  CONST             2  ; 100
  0018  line   3  GT
  0019  line   3  NOT
  0020  line   3  JUMP_IF_FALSE    46  ; -> 0046
  0023  line   4  LOAD_LOCAL        1
  0026  line   4  LOAD_LOCAL        0
  0029  line   4  ADD
  0030  line   4  STORE_LOCAL       1
  0033  line   5  LOAD_LOCAL        0
  0036  line   5  CONST             3  ; 1
  0039  line   5  ADD
  0040  line   5  STORE_LOCAL       0
  0043  line   3  JUMP             12  ; -> 0012  (backward)
  0046  line   7  LOAD_LOCAL        1
  0049  line   7  PRINT
  0050  line   7  HALT
```

Two things that listing shows. `<=` has **no opcode of its own** — it lowers onto `GT NOT`,
which is part of why nineteen opcodes are enough, and it costs one extra instruction every
time it runs. And a jump operand is an **absolute target**, not a signed displacement, so
the one `JUMP` at `0043` serves the backward branch to a `while` header exactly as it serves
the forward exit from an `if`. The format is documented in
[`docs/BYTECODE.md`](docs/BYTECODE.md).

Setup and build instructions are [below](#build--run).

---

## What It Does

Takes source code like this:

```
x = 5 + 3
y = x * 2
z = y - 4
print z
```

And runs it through the front end and then through whichever engine was asked for. Standard output carries what the program printed, and nothing else:

```
12
```

The language reaches as far as selection, iteration and recursion:

```
i = 1
total = 0
while i <= 100 {
    total = total + i
    i = i + 1
}
if total == 5050 {
    print total
} else {
    print 0
}
```

```
5050
```

```
fn fib(n) {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

print fib(27)
```

```
196418
```

That last one is `examples/fib.algo`, and it is the program the rest of this project is
built to measure.

The stages narrate themselves only when asked. `--trace` writes that commentary to **stderr**, so `algo prog.algo > out.txt` captures the program's output alone whether the flag is on or off:

```bash
./build/algo --trace tests/basic.algo
```

```
=== Source Code ===
x = 5 + 3
y = x * 2
z = y - 4
print z

=== Stage 1: Lexing ===
  Token: [x]
  Token: [=]
  Token: [5]
  ...
=== Stage 2: Parsing ===
  Parsed 4 statement(s) successfully.
=== Stage 3: Resolution ===
  Resolved 3 variable(s) into the program's frame.
  Resolved 0 function(s), each into a frame of its own.
=== Stage 4: Output ===
```

---

## Compiler Stages

**Stage 1 — Lexer**
Reads raw source text character by character. Groups characters into tokens (numbers, identifiers, operators, keywords). Discards whitespace, but tracks line and column across it, so every token records the source span it came from. Appends a sentinel `END_OF_FILE` token to simplify parser bounds checking.

**Stage 2 — Recursive Descent Parser**
Converts the token stream into an Abstract Syntax Tree. Grammar rules are encoded directly as mutually recursive functions, one per precedence level, enforcing operator precedence through the call hierarchy rather than a lookup table:

```
equality → comparison → term → factor → unary → primary
   ==        <  <=        +       *       -       1
   !=        >  >=        -       /       !       x
                                                  true
```

Each level parses the level below it and then loops on its own operators, so the level that runs last binds loosest. Every node the parser builds is its own struct with named fields — a `BinOpNode` has `left` and `right`, an `IfNode` has a condition and two branches, a `BlockNode` has a statement list — reached through a tag check that hands back the concrete type.

**Stage 3 — Resolver**
Walks the AST before execution carrying a stack of **frames**, one per function body, each with its own stack of scopes. It rejects a name that is not in scope where it is used, pointing the caret at the *use* rather than at the missing assignment — the offending node is the identifier itself. It checks every call against the functions that exist, by name and by argument count. And it assigns every variable a **slot index within its enclosing function frame**, writing that integer onto each node that names it — an identifier, an assignment target, or a parameter.

A frame is a boundary, not just a counter. Lookup searches the current frame and stops, so a function body sees its parameters and its own locals and nothing else — not a top-level variable, not another function's local. That falls out of what a slot *is*: an index numbered from zero within one frame, which cannot also name a position in another. Everything a function needs, it is passed.

Both back ends address variables by those slots. The tree-walk interpreter indexes a frame vector with them; the bytecode compiler writes them straight into `LOAD_LOCAL` and `STORE_LOCAL` operands. Replacing the original `std::map<std::string, Value>` environment with an array indexed by slot was a change measured on its own before it was kept — it is the fourth ablation in the table above.

**Stage 4a — Tree-Walk Interpreter** (`--engine=tree`, the default)
Recursively evaluates the AST. Maintains one **frame per call**, on a stack of them — recursion needs two live copies of a parameter at once, and one flat environment has room for one. A frame is a `std::vector<Value>` indexed by the slot the resolver assigned, sized from the count that function declared. Executes `print` statements by evaluating the expression subtree and writing to stdout. Values are a tagged union of a 64-bit integer and a boolean — there is no implicit conversion between them, so an integer is not truthy and a boolean is not 0 or 1.

A `return` unwinds as a flag reported up the statement walk rather than as an exception: `fib(27)` returns several hundred thousand times, and an exception per return would cost more than everything this project later sets out to optimise away. Recursion is bounded by a **call-depth limit** of 1000, which raises a diagnostic where the C++ stack would otherwise overflow and kill the process on a signal.

**Stage 4b — Bytecode Compiler and Virtual Machine** (`--engine=vm`)
A second back end over the same resolved AST. `src/compiler.cpp` lowers it to a **chunk** — a flat byte array of nineteen opcodes, a constant pool, a function table, and a span table — doing no name resolution and no type checking of its own, because Stage 3 has already done both. Forward jumps are emitted with a placeholder operand and backpatched once the target offset is known.

`src/vm.cpp` runs that chunk on an operand stack and a frame stack of `{returnIP, slotBase}`. Locals live *on* the operand stack, so slot `s` is `stack[slotBase + s]` and a call is a slot range on memory the VM already owns rather than a heap allocation. Faults are raised from the span table, which stores each instruction's source span **and the operator's spelling** — a span alone cannot say which operator a lowered `NOT` came from.

The fault messages in the VM are **duplicated from the tree-walker rather than shared**. Extracting them into a common header would make the two engines agree by construction, and agreeing by construction is not something the test suite could check. Keeping them apart is what makes the differential testing below a test.

`--dump` prints a chunk instead of running it; it does not execute, and is orthogonal to `--engine`.

---

## Language Reference

| Feature       | Syntax                                  |
| ------------- | --------------------------------------- |
| Assignment    | `x = 5`                                 |
| Arithmetic    | `y = x + 3 * 2`                         |
| Comparison    | `x < 10`, `n == 0`, `a != b`            |
| Booleans      | `true`, `false`, `!done`                |
| Unary minus   | `x = -5`                                |
| Print         | `print z`                               |
| Block         | `{ x = 1 x = x + 1 }`                   |
| Selection     | `if x < 5 { … } else if … { … } else { … }` |
| Iteration     | `while i <= 100 { … }`                  |
| Function      | `fn add(a, b) { return a + b }`          |
| Call          | `add(1, 2)`                             |
| Return        | `return expr`, or bare `return`          |
| Operators     | `+` `-` `*` `/` `==` `!=` `<` `<=` `>` `>=` `!` unary `-` |

Precedence runs `equality → comparison → term → factor → unary → primary`, loosest first, so `1 + 2 * 3 == 7` is `((1 + (2 * 3)) == 7)` and prints `true`. Prefix operators are right-associative and stack: `- -5` is `5` and `!!true` is `true`.

Conditions are **not parenthesised** and braces are **mandatory** — `if x < 1 { … }`, never `if (x < 1) …`. The language has **no grouping parentheses**: `(1 + 2) * 3` does not parse. The only `(` it has is the one that opens a call's argument list or a function's parameter list, and the rule covering both holds everywhere — **a `(` follows a name and delimits an argument or parameter list, and never groups an expression.** An expression's shape is fixed by the precedence cascade instead, so a program that wants `(a + b) * c` names the sum first. Mandatory braces also settle the dangling `else` outright.

Types do not mix. Arithmetic and ordering take two integers, `==` and `!=` take two operands of the same type, `!` takes a boolean and unary `-` an integer, and a condition must be a boolean. Anything else is a runtime fault, because the language has no type checker — whether `x + 1` is well typed depends on what the program computed.

An integer is a signed **64-bit** value, and arithmetic **traps rather than wrapping**: `+`, `-`, `*`, `/` and unary `-` each raise a runtime fault when the result will not fit. Signed overflow is undefined behaviour in C++, and this language exists to be measured — a benchmark whose arithmetic silently wraps is reporting a number produced by a program whose meaning the standard does not define. Two of the five trap sites are easy to miss: `/` and unary `-` overflow on exactly one input each, because the range is asymmetric and the smallest integer has no positive counterpart. That same asymmetry means the smallest integer **cannot be written as a literal** — `-9223372036854775808` is unary minus applied to a literal that is itself out of range — so a program that wants it computes it. The full rules are in [`docs/GRAMMAR.md`](docs/GRAMMAR.md).

The language has **no comment syntax**, so every listing below is source exactly as it can be run, with its output shown beneath it.

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

A block introduces a scope. There is no `var` or `let` — an assignment is what brings a variable into existence — so which of the two a given assignment does depends on what is visible where it stands:

```
total = 0
{
    total = total + 1
    label = 100
    print label
}
print total
```

`total` is visible inside the block, so the first assignment writes it; `label` is visible nowhere, so the second declares it there. The output is `100` then `1`.

A variable first assigned inside a block is a *different variable* from a same-named one in any other scope, and it is gone at the closing brace — so mentioning `label` after that block is a compile error, not a lookup that fails at run time:

```
$ ./build/algo out_of_scope.algo
out_of_scope.algo:5:7: error: variable 'label' used outside the block that assigns it
print label
      ^~~~~
```

The rule cuts the other way too, and has to: `while i <= 100 { total = total + i }` assigns the `total` and `i` it can already see, which is what lets a loop accumulate at all.

Functions are declared with `fn`, **at the top level only**, and are **not values** — there are no closures, a function cannot be passed to one, and a function name used as an expression is an error rather than a reference. Function names and variable names are two separate namespaces, so `fn f(f)` is legal and inside that body `f` is the parameter while `f(…)` is the call.

A function body is a frame of its own and sees only its parameters and its own locals:

```
total = 5

fn addToTotal(n) {
    return total + n
}

print addToTotal(1)
```

```
$ ./build/algo scope.algo
scope.algo:4:12: error: variable 'total' used inside a function, where only parameters and locals are in scope
    return total + n
           ^~~~~
```

Every function is visible before the file is read through, so a function may call one declared further down, and may call itself:

```
fn isEven(n) {
    if n == 0 {
        return true
    }
    return isOdd(n - 1)
}

fn isOdd(n) {
    if n == 0 {
        return false
    }
    return isEven(n - 1)
}

print isEven(10)
```

```
true
```

`return` takes an expression or stands bare. A bare `return`, and a body that runs off its end, both yield the integer `0` — the language has two value types and no unit, and faulting instead would make the bare form unusable, since every call in this language stands in an expression position:

```
fn describe(n) {
    if n < 0 {
        print 0 - 1
        return
    }
    print 1
}

print describe(0 - 5)
```

```
-1
0
```

---

## Error Handling

Every error is written to **stderr** as a diagnostic carrying the file, line and column it came from, the source line echoed, and a caret under the offending span:

```
$ ./build/algo error_undef.algo
error_undef.algo:1:7: error: variable 'z' used before assignment
print z
      ^
```

The span comes from the stage that raised the error; the path and source text come from the driver, which is the only part of the program that has them.

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

Exit codes are sysexits-style: `0` success, `64` bad command line, `65` compile-time error, `66` unreadable input file, `70` runtime fault.

A literal too wide for the value type is classed **compile-time**: it is a property of the token's text, not of anything the program computes. It is detected at parse time, with the conversion, so it fires on every literal in the file — including one inside a function that is never called. A type mismatch is classed the opposite way for the same reason read in reverse: with no type checker in the language, whether an operand has the right type depends on what the program computed, so it is a runtime fault.

The two call errors fall on the same line for the same reason. An argument count is settled by the source text alone — the call site says how many, the declaration says how many — so a wrong arity is a **compile-time** error. How deep a chain of calls actually gets depends on what the program computed, so an exhausted call depth is a **runtime** fault. That limit exists because unbounded recursion would otherwise exhaust the C++ stack and kill the process on a signal, with no diagnostic and no exit code at all.

---

## Testing & CI

The suite is **sixty-six CTest cases**. Twenty-nine golden-file cases are each registered **twice**, once per engine, plus eight unit tests that link the core library.

**Every golden-file case runs under both engines on every push**, as `<case>_tree` and `<case>_vm`. That is what turns *the VM preserves the language's semantics* from an assertion into something CI re-checks under both GCC and Clang: the two engines must print the same bytes on stdout, render the same diagnostic on stderr, and exit with the same code — not merely compute the same answers. `ctest -R _vm` selects one engine, `ctest -R _tree` the other. The failure mode to watch for is not a red test but a **vacuous pass**: register the second set, forget to pass the engine down, and twenty-nine green tests run the tree-walker twice while looking perfect. The only thing that disproves it is a deliberate mutation of `src/vm.cpp` that requires *both* halves — the `_vm` cases go red **and** the `_tree` cases stay green.

Golden-file cases live in `tests/`, each a `.algo` input paired with the output it should produce:

| Case                     | Covers                                          |
| ------------------------ | ----------------------------------------------- |
| `basic`                  | End-to-end run through all four stages           |
| `precedence`             | `*`/`/` binding tighter than `+`/`-`             |
| `precedence_equality`    | Equality binding looser than comparison          |
| `precedence_comparison`  | Comparison binding looser than `+`/`-`           |
| `precedence_unary`       | Prefix operators binding tighter than `*`, and stacking |
| `comparisons`            | All six comparison operators                     |
| `booleans`               | `true`/`false`, `!`, and printing a boolean      |
| `blocks`                 | Nested blocks, and statements running in order   |
| `scopes`                 | Two sibling blocks, each with its own local      |
| `if_else_chain`          | `if` / `else if` / `else` selecting correctly    |
| `while_sum`              | A `while` loop summing 1..100                    |
| `while_never_runs`       | A `while` testing before it runs its body        |
| `fib`                    | `fib(27) = 196418` — Phase 1's acceptance criterion |
| `functions`              | Parameters, nested calls, a call inside an expression, and a function whose parameters are not interchangeable |
| `return_early`           | A bare `return`, an early exit from a loop, and a body that runs off its end |
| `deep_recursion`         | A chain exactly as deep as the call-depth limit allows |
| `error_div_zero`         | Runtime error: division by zero                  |
| `error_overflow`         | Compile error: an integer literal too wide for the value type |
| `error_overflow_unreached` | The same, inside a function that is never called — literals are checked at parse time |
| `error_int_overflow`     | Runtime error: arithmetic overflow in `+`        |
| `int64_range`            | The signed 64-bit range, and the edges at which each of the five sites traps |
| `error_undef`            | Resolution error: a name never assigned anywhere |
| `error_out_of_scope`     | Resolution error: a name used after its block ended |
| `error_while_local`      | Resolution error: a name local to a loop body    |
| `error_type_mismatch`    | Runtime error: an operator applied across types  |
| `error_condition_type`   | Runtime error: a condition that is not a boolean |
| `error_arity`            | Resolution error: a call of the wrong arity      |
| `error_function_scope`   | Resolution error: a top-level name read inside a function |
| `error_call_depth`       | Runtime error: 10,000-deep recursion, refused rather than crashed |

All but one carry all three golden files; `precedence` deliberately carries only `.expected`, so the optional-comparison path stays exercised.

Almost every case carries a **ten-second CTest timeout**. `while` makes a non-terminating program expressible, and a case that hangs would otherwise stall CI until the job's own limit — a failure that reads as an infrastructure problem rather than as the bug it is. The guard sits here rather than in the interpreter on purpose: an iteration cap would be a branch inside the hot path of every benchmark, and a language semantic that was never asked for. The exception is `fib`, which is a workload rather than a check — it computes `fib(27)` under an unoptimised tree-walk interpreter — and carries a longer limit that is still only a hang guard, not an assertion about how long it should take.

The **call-depth limit** is the one guard that had to go inside the language instead, and the difference is worth stating: a runaway `while` runs forever, which something outside the process can catch, and this timeout does. Unbounded recursion exhausts the C++ stack, which kills the process on a signal with no diagnostic to compare and no exit code of ours — there is nothing outside to catch it with. Its cost is also per *call* rather than per loop turn, so a program that calls nothing pays nothing for it.

A case compares up to three things. `<case>.expected` is stdout and is required. `<case>.expected_err` is stderr and `<case>.expected_code` is the process exit code; both are optional, and an absent file means *do not check* rather than *expect empty* — so a case that does not care what it exits with simply omits the file.

`CMakeLists.txt` globs every `tests/*.algo` file and registers it twice via `enable_testing()` / `add_test`, so new cases are picked up automatically — a matching `.algo` / `.expected` pair arrives as two tests, not one. **No `.expected` file was regenerated when the second engine was added, and none should be**: the existing golden files are the *shared* expectation both engines are held to, which is what makes this a differential test rather than two independent suites.

`tests/run_case.cmake` runs the interpreter **from `tests/` with a bare filename**, not with an absolute path. A diagnostic prints the path it was given verbatim, so an absolute one would write the checkout's own location into a golden file and fail on any other machine. Its engine argument is optional, and the tree side passes **no flag at all** — that is byte for byte the invocation every golden file was recorded under.

Golden-file cases compare streams, so they cannot assert a value that never reaches one. Unit tests cover those. The sources minus `main.cpp` build as an `algo_core` library, and a unit test is a plain binary that links it with its own `main()` — a failed check writes to stderr and the process exits non-zero, which is all CTest reads. There is no third-party test framework, and the project has no external dependencies. Unit tests are registered one explicit target each, since a glob over compiled sources would not re-run when a file is added.

| Unit test                   | Covers                                                        |
| --------------------------- | ------------------------------------------------------------- |
| `tests/span_test.cpp`       | The line, column and length the lexer and parser attach to every token and AST node |
| `tests/diagnostic_test.cpp` | The rendered form of a diagnostic — caret geometry, tab alignment, and which error class each site raises |
| `tests/expression_test.cpp` | The value type, and the precedence facts no printed output can show |
| `tests/resolver_test.cpp`   | Scoping, the slot on every variable, the frame each function is numbered into, and the errors the resolver reports |
| `tests/chunk_test.cpp`      | The bytecode format — every opcode's operand encoding and instruction width, written out a second time by hand so the two have to be wrong in the same way to agree |
| `tests/compiler_test.cpp`   | Three chunks written out by hand, and structural invariants over the chunk of every `.algo` file in `tests/`, `examples/` and `bench/` |
| `tests/vm_test.cpp`         | What differential testing cannot reach — the `POP` arm nothing emits, the operand-stack guard no program can trigger, the unknown-opcode arm, and the span a fault reports |
| `tests/disassembler_test.cpp` | That every byte prints, including a corrupt one, and that the line column requires an exact span-table hit rather than borrowing the previous instruction's |

The last two exist because some claims cannot reach a stream. `-2 * 3` and `-(2 * 3)` agree on the answer — negation distributes through multiplication — so only an assertion on the tree can say which one the parser built. The same goes for the value type, which is an in-memory representation the interpreter never shows.

Run the suite locally:

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

The same suite runs under UndefinedBehaviorSanitizer, which is what says the overflow traps fire *before* the undefined operation rather than after it. Off by default, and deliberately so — a sanitizer in the default build would put its own instrumentation into every number the benchmarks report:

```bash
cmake -S . -B build-ubsan -DALGO_SANITIZE=ON && cmake --build build-ubsan
ctest --test-dir build-ubsan --output-on-failure
```

CI (`.github/workflows/ci.yml`) builds and runs the full test suite on every push and pull request, across a matrix of GCC and Clang on Ubuntu.

---

## Measurement

Performance numbers are produced by `valgrind --tool=cachegrind`, which *simulates* a machine rather than timing this one — so its counts are deterministic and reproduce exactly across runs, which is what makes them safe to commit and to compare across versions of the interpreter.

Valgrind does not meaningfully support arm64 macOS, so on Apple Silicon there is no measurement outside a Linux container. That is not a packaging convenience; it is the only place the instrument runs:

```bash
docker compose run --rm bench
```

That builds the interpreter at `-O2` into `build-bench/` and runs cachegrind over `examples/fib.algo`. `Dockerfile.bench` pins its Ubuntu image **by digest rather than by tag**, because the tag is rebuilt whenever a base package changes, and a number measured against a moving base is not reproducible.

### The measurement driver

`scripts/bench.sh` measures one binary running one program and appends one row to `results/measurements.csv`:

```bash
docker compose run --rm bench bash scripts/bench.sh build-bench/algo bench/fib32.algo --config baseline
```

The row carries cachegrind's thirteen raw events and the four totals derived from them, wall clock over ten runs, what the program printed, and the provenance needed to place the row later — commit, build type, architecture, kernel, compiler, valgrind version and image digest. **Every number anywhere in this repository traces to a row in that file.**

The script **refuses to run outside the container**, and **refuses to measure an unoptimised build**. Both refusals exist for the same reason: the worst outcome available to a project like this is not a missing number, it is a plausible-looking wrong one. There is no cachegrind on arm64 macOS, and the default build is unoptimised — a host run that appeared to work would produce a row identical in shape to a real one and wrong by the whole distance between an optimised build and an unoptimised one. Each refusal was checked against a build that would otherwise have produced a number, rather than assumed.

Not all of the row is equally trustworthy, and [`results/README.md`](results/README.md) says so explicitly. The cachegrind counts are simulated and deterministic, and they are what may be committed as a threshold. The wall-clock columns are narrative — they are taken inside a virtualised container on a machine whose CPU the guest cannot even name — and nothing gates on them. The `perf` columns are empty here and expected to stay that way: the packaged tools are built for a different kernel than Docker Desktop runs, which is recorded rather than worked around.

Validating the driver turned up one result worth stating: **the instruction count is invocation-independent and the cache counts are not.** Measuring the same binary on the same program, the instruction count barely moves however the process is launched, while the D1 miss count of the variable-access benchmark settles on one of two well-separated values — the environment block shifts the initial process layout, and a working set that straddles a cache line lands on one side of it or the other. Each invocation style then reproduces exactly; different styles do not agree with each other. That is precisely why measurements go through one driver rather than being typed at a prompt, and it is why the attribution this project is building rests on instruction counts. The figures are in [`results/README.md`](results/README.md), beside the rows they came from.

### Measuring several configurations

Comparing versions of the interpreter means building several of them, and the way not to do it is a set of `#ifdef`-guarded variants of the hot path — that leaves permanently unreadable code in a repository whose readability is part of the point. Each configuration is a commit instead, and `scripts/bench-ablations.sh` takes a list of refs, materialises each as a detached git worktree, builds it into a directory of its own, and hands it to the driver once per benchmark program:

```bash
scripts/bench-ablations.sh --dry-run v1-naive-treewalk perf/iso-a
```

The working tree never moves. Nothing is checked out over it and nothing is stashed; the only file a run changes is the ledger it appends to.

**This script runs on the host and the driver runs in the container, and that split is forced rather than chosen.** `git` is not installed in the measurement image, so creating a worktree is something only the host can do; valgrind does not work on arm64 macOS, so measuring is something only the container can do. Something has to cross that boundary, and the orchestrator is the only correct place for it — a driver that re-invoked docker could not be called from inside docker, and translating host paths into container paths is exactly the silently-wrong-binary failure its refusals exist to prevent. So the two halves refuse in opposite directions: the driver refuses to run outside the container, the orchestrator refuses to run inside it.

One detail looks arbitrary and is load-bearing. **Worktrees are named after the commit they hold, not the ref that asked for them**, because the length of the path a binary is invoked by moves cachegrind's cache columns — by 9.6% of the D1 miss count on one benchmark, for a fourteen-character difference, with the binary image held byte-identical. Naming worktrees after tags would have given two configurations different path lengths and therefore different miss counts: an ablation delta manufactured by a tag name, in a series whose whole purpose is attributing deltas to causes. A commit-derived name is the same length for every configuration, and the script refuses a series whose paths are not. The measurement is in [`results/README.md`](results/README.md).

### The benchmark programs

`bench/` holds four programs, each written to load a different part of the interpreter so that later comparisons have something to separate rather than one blended number:

| Program | What it loads | Prints |
|---|---|---|
| `fib32.algo` | call and frame overhead — recursion, a fresh environment per call | `2178309` |
| `loop10m.algo` | dispatch — ten million iterations over the smallest body that still terminates | `10000000` |
| `arith.algo` | expression-tree traversal and operand traffic — a wide tree of operators and integer literals | `24000000` |
| `vars.algo` | variable access — every leaf inside its loop is a variable read, and no integer literal is evaluated there at all | `136000000` |

Each program is sized to run between half a second and five seconds under the tree-walk interpreter, so that run-to-run noise stays small relative to what is being measured. **That sizing is a property of the `-O2` container build and of no other.** Under the unoptimised build the test suite uses, the same four programs take an order of magnitude longer and every one of them overshoots the range — which is why the benchmark build type is set explicitly rather than inherited.

The answers are not incidental. Each has a closed form checked against a computation performed outside the interpreter — `arith.algo` contributes exactly 12 per iteration and `vars.algo` exactly 136, the sum of 1 through 16 — because a benchmark that computes the wrong thing still burns instructions and would quietly corrupt every comparison drawn from it.

The language has no comment syntax, so none of these files can explain itself from the inside. Their design is recorded here and in the commit that added them.

**`bench/` is not `tests/`.** CTest globs `tests/*.algo` only, so no benchmark is registered as a test case and none inherits the ten-second timeout that guards the suite against a hanging `while` loop. A benchmark picked up as a test would fail that timeout on principle.

Two decisions behind those numbers are recorded in [`docs/MEASUREMENT.md`](docs/MEASUREMENT.md) along with the image digest, the CPU and the compiler version. Both were settled by measurement rather than assumption: cachegrind runs **native arm64** — emulating x86_64 works but makes the counts *less* portable, because valgrind then auto-detects a host-derived cache model instead of using its own fixed defaults. And benchmarks build at **`-O2`**, not the unoptimised default the test build uses: `-O0` charges the interpreter 6.26x the instructions a real build executes, which would inflate every later comparison by optimisation the compiler simply declined to attempt.

---

## Tech Stack

- **Language:** C++20
- **Engines:** a tree-walk interpreter and a bytecode VM over one shared front end
- **Build System:** CMake 3.20+
- **Compiler:** AppleClang (macOS) / GCC or Clang (Linux)
- **Measurement:** valgrind/cachegrind, inside a digest-pinned Ubuntu container
- **Dependencies:** none — no third-party libraries, and no test framework

---

## Build & Run

### Prerequisites

```bash
xcode-select --install   # macOS
brew install cmake
```

### Build

```bash
mkdir build
cd build
cmake ..
make
```

### Run

```bash
./build/algo examples/program.algo
```

Select the back end with `--engine`. It defaults to `tree`; an unrecognised value is exit 64:

```bash
./build/algo --engine=vm examples/program.algo
```

`--dump` prints the compiled chunk to stdout and does **not** run the program, so it is orthogonal to `--engine`:

```bash
./build/algo --dump tests/while_sum.algo
```

Add `--trace` to see the stages narrate themselves on stderr, which leaves stdout carrying the program's own output either way:

```bash
./build/algo --trace examples/program.algo
```

### Rebuild after changes

```bash
cd build && make
```

No need to re-run `cmake` unless `CMakeLists.txt` changes.

---

## Project Structure

```
algo/
├── CMakeLists.txt                       # algo_core library, algo driver, CTest registration
├── LICENSE
├── README.md
├── Dockerfile.bench                     # Measurement platform — Ubuntu pinned by digest, cachegrind
├── compose.yaml                         # The `bench` service; mounts the repo, runs cachegrind
├── bench/                               # Benchmark programs — NOT globbed by CTest
│   ├── fib32.algo                       # Call and frame overhead — recursive fib(32)
│   ├── loop10m.algo                     # Dispatch — ten million iterations, minimal body
│   ├── arith.algo                       # Expression-tree traversal and operand traffic
│   └── vars.algo                        # Variable access — every loop leaf is a variable read
├── scripts/
│   ├── bench.sh                         # The measurement driver — one binary, one program, one row
│   └── bench-ablations.sh               # The configuration builder — a list of refs, one worktree each
├── results/
│   ├── README.md                        # Schema, the ablation workings, and the attribution table
│   └── measurements.csv                 # The ledger — every committed number traces to a row here
├── docs/
│   ├── GRAMMAR.md                       # Language reference — grammar, precedence and overflow rules
│   ├── BYTECODE.md                      # Opcode reference — operand encoding and stack effect
│   └── MEASUREMENT.md                   # Image digest, CPU, compiler, and the boundary of the claim
├── examples/
│   ├── program.algo                     # Sample program
│   └── fib.algo                         # fib(27) — Phase 1's acceptance criterion
├── src/
│   ├── main.cpp                         # Driver — --engine, --dump, --trace, and the catch site
│   ├── token.h                          # Token and source-span definitions
│   ├── diagnostic.h / diagnostic.cpp    # Diagnostic type, renderer, error classes, exit codes
│   ├── lexer.h / lexer.cpp              # Stage 1: source text → token stream
│   ├── ast.h                            # AST node definitions — one struct per node type
│   ├── value.h                          # The runtime value type: a tagged 64-bit integer or boolean
│   ├── parser.h / parser.cpp            # Stage 2: token stream → AST
│   ├── resolver.h / resolver.cpp        # Stage 3: frames, scopes, calls, and a slot per variable
│   ├── interpreter.h / interpreter.cpp  # Stage 4a: tree-walk evaluation
│   ├── chunk.h                          # The bytecode format — opcodes, constants, span table
│   ├── compiler.h / compiler.cpp        # Stage 4b: resolved AST → chunk
│   ├── vm.h / vm.cpp                    # Stage 4b: the chunk's execution engine
│   └── disassembler.h / disassembler.cpp # `--dump` — prints a chunk, never throws, never skips a byte
└── tests/
    ├── *.algo                           # Golden-file case inputs — each registered under both engines
    ├── *.expected                       # Expected stdout (required)
    ├── *.expected_err                   # Expected stderr (optional)
    ├── *.expected_code                  # Expected exit code (optional)
    ├── span_test.cpp                    # Unit test: source spans on tokens and AST nodes
    ├── diagnostic_test.cpp              # Unit test: diagnostic rendering and error classification
    ├── expression_test.cpp              # Unit test: the value type and the precedence cascade
    ├── resolver_test.cpp                # Unit test: frames, scoping, and the slot on every variable
    ├── chunk_test.cpp                   # Unit test: opcode widths and operand encoding
    ├── compiler_test.cpp                # Unit test: hand-written chunks, and invariants over every program
    ├── vm_test.cpp                      # Unit test: the arms differential testing cannot reach
    ├── disassembler_test.cpp            # Unit test: listing geometry, and that every byte prints
    └── run_case.cmake                   # CTest driver script that runs a case and compares it
```
