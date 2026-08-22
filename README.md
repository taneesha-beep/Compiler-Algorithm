# Algo Compiler

[![CI](https://github.com/taneesha-beep/Compiler-Algorithm/actions/workflows/ci.yml/badge.svg)](https://github.com/taneesha-beep/Compiler-Algorithm/actions/workflows/ci.yml)

A compiler for a simple algorithmic language, built from scratch in C++20 as a college mini project. Takes a `.algo` source file through all four classical compiler stages: lexical analysis, parsing, semantic analysis, and interpretation.

---

## 👩‍💻 My Contributions

Originated as a four-stage team coursework compiler, preserved at tag v0-coursework. Everything after that commit — the resolver, functions and control flow, the bytecode compiler and VM, the benchmark harness — is mine (diff).

---

## What It Does

Takes source code like this:

```
x = 5 + 3
y = x * 2
z = y - 4
print z
```

And runs it through four stages to produce:

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
  Token: [+]
  Token: [3]
  Token: [y]
  Token: [=]
  Token: [x]
  Token: [*]
  Token: [2]
  Token: [z]
  Token: [=]
  Token: [y]
  Token: [-]
  Token: [4]
  Token: [print]
  Token: [z]
=== Stage 2: Parsing ===
  Parsed 4 statement(s) successfully.
=== Stage 3: Semantic Analysis ===
  No semantic errors found.
=== Stage 4: Output ===
12
```

---

## Compiler Stages

**Stage 1 — Lexer**
Reads raw source text character by character. Groups characters into tokens (numbers, identifiers, operators, keywords). Silently discards whitespace. Appends a sentinel `END_OF_FILE` token to simplify parser bounds checking.

**Stage 2 — Recursive Descent Parser**
Converts the token stream into an Abstract Syntax Tree. Grammar rules are encoded directly as mutually recursive functions — `parseExpr()` calls `parseTerm()` which calls `parsePrimary()` — enforcing operator precedence through the call hierarchy rather than a lookup table.

**Stage 3 — Semantic Analysis**
Walks the AST before execution to catch use-before-assignment errors. Reports the offending variable name with a clear error message rather than a cryptic runtime crash.

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

| Error                        | Example input         | Message                                        |
| ---------------------------- | --------------------- | ---------------------------------------------- |
| Unknown character            | `x = 5 @ 3`           | `Unknown character: @`                         |
| Use before assignment        | `print z` (no prior assign) | `Semantic Error: Variable 'z' used before assignment` |
| Division by zero             | `x = 5 / 0`           | `Runtime Error: Division by zero`              |
| Missing argument             | `./algo`              | `Insufficient arguments`                       |
| File not found               | `./algo missing.algo` | `Could not open file: missing.algo`            |

---

## Testing & CI

Five golden-file test cases live in `tests/`, each a `.algo` input paired with its expected stdout:

| Case               | Covers                                      |
| ------------------ | -------------------------------------------- |
| `basic`             | End-to-end run through all four stages        |
| `precedence`        | `*`/`/` binding tighter than `+`/`-`          |
| `error_div_zero`    | Runtime error: division by zero               |
| `error_overflow`    | Runtime error: integer literal overflow       |
| `error_undef`       | Semantic error: variable used before assignment |

`CMakeLists.txt` globs every `tests/*.algo` file and registers it as a CTest test via `enable_testing()` / `add_test`, so new cases are picked up automatically — just add a matching `.algo` / `.expected` pair.

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
./build/algo path/to/program.algo
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
├── CMakeLists.txt                       # Build config + CTest registration for tests/*.algo
├── README.md
├── src/
│   ├── main.cpp                         # Driver — wires the four stages together
│   ├── token.h                          # Token type definitions
│   ├── lexer.h / lexer.cpp              # Stage 1: source text → token stream
│   ├── ast.h                            # AST node definitions
│   ├── parser.h / parser.cpp            # Stage 2: token stream → AST
│   ├── semantic.h / semantic.cpp        # Stage 3: use-before-assignment checks
│   └── interpreter.h / interpreter.cpp  # Stage 4: tree-walk evaluation
└── tests/
    ├── *.algo / *.expected              # Golden-file test cases (input + expected output)
    └── run_case.cmake                   # CTest driver script that runs a case and diffs output
```
