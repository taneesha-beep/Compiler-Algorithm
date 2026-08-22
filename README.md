# Algo Compiler

[![CI](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml)

A compiler for a simple algorithmic language, built from scratch in C++20. Takes a `.algo` source file through all four classical compiler stages: lexical analysis, parsing, semantic analysis, and interpretation.

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
=== Stage 3: Semantic Analysis ===
  No semantic errors found.
=== Stage 4: Output ===
```

---

## Compiler Stages

**Stage 1 — Lexer**
Reads raw source text character by character. Groups characters into tokens (numbers, identifiers, operators, keywords). Discards whitespace, but tracks line and column across it, so every token records the source span it came from. Appends a sentinel `END_OF_FILE` token to simplify parser bounds checking.

**Stage 2 — Recursive Descent Parser**
Converts the token stream into an Abstract Syntax Tree. Grammar rules are encoded directly as mutually recursive functions — `parseExpr()` calls `parseTerm()` which calls `parsePrimary()` — enforcing operator precedence through the call hierarchy rather than a lookup table.

**Stage 3 — Semantic Analysis**
Walks the AST before execution to catch use-before-assignment errors. Points a caret at the *use*, not at the missing assignment — the offending node is the identifier itself.

**Stage 4 — Tree-Walk Interpreter**
Recursively evaluates the AST. Maintains a `std::map` as the variable environment. Executes `print` statements by evaluating the expression subtree and writing to stdout.

---

## Language Reference

| Feature       | Syntax            |
| ------------- | ----------------- |
| Assignment    | `x = 5`           |
| Arithmetic    | `y = x + 3 * 2`   |
| Print         | `print z`         |
| Operators     | `+` `-` `*` `/`   |

Operator precedence is correct — `*` and `/` bind tighter than `+` and `-`.

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
| Missing argument      | `./algo`                    | `no input file`                                | 64   |
| File not found        | `./algo missing.algo`       | `could not open file: missing.algo`            | 66   |

Exit codes are sysexits-style: `0` success, `64` bad command line, `65` compile-time error, `66` unreadable input file, `70` runtime fault.

A literal too wide for the value type is classed **compile-time**, not runtime, even though it is currently detected during evaluation — it is a property of the token's text, not of anything the program computes.

---

## Testing & CI

The suite is seven CTest cases: five golden-file cases and two unit tests.

Golden-file cases live in `tests/`, each a `.algo` input paired with the output it should produce:

| Case               | Covers                                      |
| ------------------ | -------------------------------------------- |
| `basic`             | End-to-end run through all four stages        |
| `precedence`        | `*`/`/` binding tighter than `+`/`-`          |
| `error_div_zero`    | Runtime error: division by zero               |
| `error_overflow`    | Runtime error: integer literal overflow       |
| `error_undef`       | Semantic error: variable used before assignment |

Four of the five carry all three golden files; `precedence` deliberately carries only `.expected`, so the optional-comparison path stays exercised.

A case compares up to three things. `<case>.expected` is stdout and is required. `<case>.expected_err` is stderr and `<case>.expected_code` is the process exit code; both are optional, and an absent file means *do not check* rather than *expect empty* — so a case that does not care what it exits with simply omits the file.

`CMakeLists.txt` globs every `tests/*.algo` file and registers it as a CTest test via `enable_testing()` / `add_test`, so new cases are picked up automatically — just add a matching `.algo` / `.expected` pair.

`tests/run_case.cmake` runs the interpreter **from `tests/` with a bare filename**, not with an absolute path. A diagnostic prints the path it was given verbatim, so an absolute one would write the checkout's own location into a golden file and fail on any other machine.

Golden-file cases compare streams, so they cannot assert a value that never reaches one. Unit tests cover those. The sources minus `main.cpp` build as an `algo_core` library, and a unit test is a plain binary that links it with its own `main()` — a failed check writes to stderr and the process exits non-zero, which is all CTest reads. There is no third-party test framework, and the project has no external dependencies. Unit tests are registered one explicit target each, since a glob over compiled sources would not re-run when a file is added.

| Unit test                   | Covers                                                        |
| --------------------------- | ------------------------------------------------------------- |
| `tests/span_test.cpp`       | The line, column and length the lexer and parser attach to every token and AST node |
| `tests/diagnostic_test.cpp` | The rendered form of a diagnostic — caret geometry, tab alignment, and which error class each site raises |

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
│   └── program.algo                     # Sample program
├── src/
│   ├── main.cpp                         # Driver — argument handling, --trace, and the catch site
│   ├── token.h                          # Token and source-span definitions
│   ├── diagnostic.h / diagnostic.cpp    # Diagnostic type, renderer, error classes, exit codes
│   ├── lexer.h / lexer.cpp              # Stage 1: source text → token stream
│   ├── ast.h                            # AST node definitions
│   ├── parser.h / parser.cpp            # Stage 2: token stream → AST
│   ├── semantic.h / semantic.cpp        # Stage 3: use-before-assignment checks
│   └── interpreter.h / interpreter.cpp  # Stage 4: tree-walk evaluation
└── tests/
    ├── *.algo                           # Golden-file case inputs
    ├── *.expected                       # Expected stdout (required)
    ├── *.expected_err                   # Expected stderr (optional)
    ├── *.expected_code                  # Expected exit code (optional)
    ├── span_test.cpp                    # Unit test: source spans on tokens and AST nodes
    ├── diagnostic_test.cpp              # Unit test: diagnostic rendering and error classification
    └── run_case.cmake                   # CTest driver script that runs a case and compares it
```
