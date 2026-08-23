# Algo Compiler

[![CI](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml)

A compiler for a simple algorithmic language, built from scratch in C++20. Takes a `.algo` source file through four stages: lexical analysis, parsing, resolution, and interpretation.

*This repository began as a four-person college coursework project. That inherited state is
tagged [`v0-coursework`](https://github.com/taneesha-beep/algo-vm/releases/tag/v0-coursework);
everything since — [`v0-coursework...main`](https://github.com/taneesha-beep/algo-vm/compare/v0-coursework...main)
— is solo work. The `master` branch is preserved as the frozen team artifact.*

---

## What It Does

Takes source code like this:

```
x = 5 + 3
y = x * 2
z = y - 4
print z
```

And runs it through four stages. Standard output carries what the program printed, and nothing else:

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

Nothing reads those slots yet, deliberately. The interpreter goes on looking each name up by string in a `std::map`, and replacing that map with an array indexed by slot is a later, separately measured change — so the resolver's output waits for the commit that spends it.

**Stage 4 — Tree-Walk Interpreter**
Recursively evaluates the AST. Maintains one `std::map` as the variable environment **per call**, on a stack of them — recursion needs two live copies of a parameter at once, and one flat map has room for one. Executes `print` statements by evaluating the expression subtree and writing to stdout. Values are a tagged union of an integer and a boolean — there is no implicit conversion between them, so an integer is not truthy and a boolean is not 0 or 1.

A `return` unwinds as a flag reported up the statement walk rather than as an exception: `fib(27)` returns several hundred thousand times, and an exception per return would cost more than everything this project later sets out to optimise away. Recursion is bounded by a **call-depth limit** of 1000, which raises a diagnostic where the C++ stack would otherwise overflow and kill the process on a signal.

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
| Literal out of range  | `print 9999999999`          | `integer literal out of range: 9999999999`     | 65   |
| Division by zero      | `x = 5 / 0`                 | `division by zero`                             | 70   |
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

A literal too wide for the value type is classed **compile-time**, not runtime, even though it is currently detected during evaluation — it is a property of the token's text, not of anything the program computes. A type mismatch is classed the opposite way for the same reason read in reverse: with no type checker in the language, whether an operand has the right type depends on what the program computed, so it is a runtime fault.

The two call errors fall on the same line for the same reason. An argument count is settled by the source text alone — the call site says how many, the declaration says how many — so a wrong arity is a **compile-time** error. How deep a chain of calls actually gets depends on what the program computed, so an exhausted call depth is a **runtime** fault. That limit exists because unbounded recursion would otherwise exhaust the C++ stack and kill the process on a signal, with no diagnostic and no exit code at all.

---

## Testing & CI

The suite is thirty CTest cases: twenty-six golden-file cases and four unit tests.

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
| `error_overflow`         | Runtime error: integer literal overflow          |
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

`CMakeLists.txt` globs every `tests/*.algo` file and registers it as a CTest test via `enable_testing()` / `add_test`, so new cases are picked up automatically — just add a matching `.algo` / `.expected` pair.

`tests/run_case.cmake` runs the interpreter **from `tests/` with a bare filename**, not with an absolute path. A diagnostic prints the path it was given verbatim, so an absolute one would write the checkout's own location into a golden file and fail on any other machine.

Golden-file cases compare streams, so they cannot assert a value that never reaches one. Unit tests cover those. The sources minus `main.cpp` build as an `algo_core` library, and a unit test is a plain binary that links it with its own `main()` — a failed check writes to stderr and the process exits non-zero, which is all CTest reads. There is no third-party test framework, and the project has no external dependencies. Unit tests are registered one explicit target each, since a glob over compiled sources would not re-run when a file is added.

| Unit test                   | Covers                                                        |
| --------------------------- | ------------------------------------------------------------- |
| `tests/span_test.cpp`       | The line, column and length the lexer and parser attach to every token and AST node |
| `tests/diagnostic_test.cpp` | The rendered form of a diagnostic — caret geometry, tab alignment, and which error class each site raises |
| `tests/expression_test.cpp` | The value type, and the precedence facts no printed output can show |
| `tests/resolver_test.cpp`   | Scoping, the slot on every variable, the frame each function is numbered into, and the errors the resolver reports |

The last two exist because some claims cannot reach a stream. `-2 * 3` and `-(2 * 3)` agree on the answer — negation distributes through multiplication — so only an assertion on the tree can say which one the parser built. The same goes for the value type, which is an in-memory representation the interpreter never shows.

Run the suite locally:

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

CI (`.github/workflows/ci.yml`) builds and runs the full test suite on every push and pull request, across a matrix of GCC and Clang on Ubuntu.

---

## Tech Stack

- **Language:** C++20
- **Build System:** CMake 3.20+
- **Compiler:** AppleClang (macOS) / GCC or Clang (Linux)

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

Add `--trace` to see the stages narrate themselves on stderr:

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
├── examples/
│   ├── program.algo                     # Sample program
│   └── fib.algo                         # fib(27) — Phase 1's acceptance criterion
├── src/
│   ├── main.cpp                         # Driver — argument handling, --trace, and the catch site
│   ├── token.h                          # Token and source-span definitions
│   ├── diagnostic.h / diagnostic.cpp    # Diagnostic type, renderer, error classes, exit codes
│   ├── lexer.h / lexer.cpp              # Stage 1: source text → token stream
│   ├── ast.h                            # AST node definitions — one struct per node type
│   ├── value.h                          # The runtime value type: a tagged integer or boolean
│   ├── parser.h / parser.cpp            # Stage 2: token stream → AST
│   ├── resolver.h / resolver.cpp        # Stage 3: frames, scopes, calls, and a slot per variable
│   └── interpreter.h / interpreter.cpp  # Stage 4: tree-walk evaluation
└── tests/
    ├── *.algo                           # Golden-file case inputs
    ├── *.expected                       # Expected stdout (required)
    ├── *.expected_err                   # Expected stderr (optional)
    ├── *.expected_code                  # Expected exit code (optional)
    ├── span_test.cpp                    # Unit test: source spans on tokens and AST nodes
    ├── diagnostic_test.cpp              # Unit test: diagnostic rendering and error classification
    ├── expression_test.cpp              # Unit test: the value type and the precedence cascade
    ├── resolver_test.cpp                # Unit test: frames, scoping, and the slot on every variable
    └── run_case.cmake                   # CTest driver script that runs a case and compares it
```
