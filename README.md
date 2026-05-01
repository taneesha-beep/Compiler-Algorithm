# Algo Compiler

A compiler for a simple algorithmic language, built in C++ as a college mini project. It takes a `.algo` source file and runs it through all four classic compiler stages: lexing, parsing, semantic analysis, and interpretation.

---

## Project Structure

```
Compiler-Algo/
├── CMakeLists.txt
├── README.md
└── src/
    └── main.cpp
```

---

## Prerequisites

- macOS with Xcode Command Line Tools
- CMake 3.20+

Install both if you haven't already:

```bash
xcode-select --install
brew install cmake
```

---

## Building

```bash
mkdir build
cd build
cmake ..
make
```

This produces an executable called `algo` inside the `build/` folder.

---

## Usage

Write a `.algo` source file, then pass it to the compiler:

```bash
./build/algo path/to/your/program.algo
```

---

## The Language

Your `.algo` programs support:

| Feature | Syntax |
|---|---|
| Assign a number | `x = 5` |
| Arithmetic | `y = x + 3 * 2` |
| Use a variable | `z = x + y` |
| Print a value | `print z` |

Supported operators: `+`, `-`, `*`, `/`

Operator precedence works correctly — `*` and `/` are evaluated before `+` and `-`.

### Example Program

Create a file called `program.algo`:

```
x = 5 + 3
y = x * 2
z = y - 4
print z
```

Run it:

```bash
./build/algo program.algo
```

Expected output:

```
=== Source Code ===
x = 5 + 3
y = x * 2
z = y - 4
print z

=== Stage 1: Lexing ===
  Token: [x]
  Token: [=]
  ...

=== Stage 2: Parsing ===
  Parsed 4 statement(s) successfully.

=== Stage 3: Semantic Analysis ===
  No semantic errors found.

=== Stage 4: Output ===
12
```

---

## Compiler Stages

**Stage 1 — Lexer:** Reads the raw source text and breaks it into tokens (numbers, identifiers, operators, keywords).

**Stage 2 — Parser:** Takes the token list and builds an Abstract Syntax Tree (AST) that represents the structure and meaning of the code. Handles operator precedence correctly.

**Stage 3 — Semantic Analysis:** Validates that all variables are assigned before they are used. Throws a clear error if not.

**Stage 4 — Interpreter:** Walks the AST, evaluates all expressions, and executes the program — printing output for every `print` statement.

---

## Error Handling

The compiler gives clear error messages for common mistakes:

| Error | Example | Message |
|---|---|---|
| Unknown character | `x = 5 @ 3` | `Unknown character: @` |
| Variable used before assignment | `print z` before `z = ...` | `Semantic Error: Variable 'z' used before assignment` |
| Division by zero | `x = 5 / 0` | `Runtime Error: Division by zero` |
| Wrong number of arguments | `./algo` | `Insufficient arguments` |
| File not found | `./algo missing.algo` | `Could not open file: missing.algo` |

---

## Rebuilding After Changes

If you edit `main.cpp`, just run:

```bash
cd build
make
```

No need to re-run `cmake` unless you change `CMakeLists.txt`.

---

## Tech Stack

- **Language:** C++20
- **Build System:** CMake
- **Compiler:** AppleClang (macOS) / GCC or Clang (Linux)
