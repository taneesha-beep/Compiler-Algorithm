# Algo

[![CI](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml/badge.svg)](https://github.com/taneesha-beep/algo-vm/actions/workflows/ci.yml)

**A small language with a C++20 front end and two execution engines — a tree-walk
interpreter and a bytecode virtual machine — built so that what the second one is worth can
be measured rather than asserted.**

No third-party dependencies, no test framework. Every performance number in this repository
traces to a row in [`results/measurements.csv`](results/measurements.csv).

<sub>This repository began as four-person college coursework, tagged
[`v0-coursework`](https://github.com/taneesha-beep/algo-vm/releases/tag/v0-coursework);
[everything since](https://github.com/taneesha-beep/algo-vm/compare/v0-coursework...main)
is solo work. The `master` branch is an earlier, thinner snapshot of the same era, kept
frozen — the tag, not that branch, is the fuller team artifact.</sub>

---

## The result

**Replacing the tree-walk interpreter with a bytecode VM made three of the four benchmark
programs retire *more* instructions, not fewer.**

Three configurations of the same language, measured under `valgrind --tool=cachegrind` in a
digest-pinned Linux container:

| | |
|---|---|
| **N** | the naive tree-walk interpreter, at tag `v1-naive-treewalk` |
| **H** | the same tree-walker, with four unforced inefficiencies removed |
| **V** | the bytecode compiler and virtual machine, `algo --engine=vm` |

Instructions retired. **H → V is the leading number** — the VM against a tree-walker that
has been given a fair chance. N → V is the same step with everything the four ablations
removed folded back into it:

| Program | What it loads | N → H | **H → V** | N → V *(includes the four ablations)* |
|---|---|---:|---:|---:|
| `bench/arith.algo` | expression-tree traversal | −70.80% | **+90.29%** | −44.44% |
| `bench/fib32.algo` | call and frame overhead | −47.29% | **−36.61%** | −66.59% |
| `bench/loop10m.algo` | dispatch | −71.95% | **+54.82%** | −56.57% |
| `bench/vars.algo` | variable access | −83.28% | **+87.92%** | −68.57% |

The VM wins only on the one program of the four that calls a function. The N → V column
looks like a 44–69% win everywhere, and that comparison is the one this project exists to
refuse: it credits the bytecode back end with four inefficiencies a tree-walker never had to
have in the first place.

**The loss comes with a win that is not in the instruction column.** On `bench/fib32.algo`
the VM retires **99.83%** fewer D1 cache misses — **20,593 against 11,901,112** — because a
call frame is a slot range on a stack the VM already owns rather than a heap block per call.
The cost sits in the branch columns instead: V mispredicts **2.6x to 19.1x** as often as H,
one indirect dispatch that every instruction funnels through against a tree-walker's spread
and individually predictable call sites.

The four ablations behind N → H are: passing the evaluated node by reference rather than by
value; converting integer literals once at parse time; dispatching operators on an enum
rather than a chain of string comparisons; and indexing each frame by slot rather than
looking names up in a `std::map<std::string, Value>`. Each is a commit and a tag of its own,
measured both in isolation against N and cumulatively, each with a per-visit cost model
checked across all four programs. The workings are in
[`results/README.md`](results/README.md).

### Four limits on reading H → V

Stated in full in [*Boundary of the claim*](docs/MEASUREMENT.md#boundary-of-the-claim), the
one place in this repository whose job is to say what the attribution does *not* establish:

- **It is an upper bound on what replacing the back end did**, not an attribution of what
  the architecture is worth. A fifth ablation for locality was cut, so locality sits inside
  this step unlabelled. An upper bound on a mixed result bounds the losses too.
- **N → V is never quoted on its own**, here or anywhere else in the repository.
- **The controlled *cache* comparison is V against its own `V-tree` control, not V against
  H** — V's command line gains an argument and its binary links three translation units H's
  does not. That control prices the boundary at a fixed **+4,053** instructions (+4,017 on
  `vars`): a constant, not a rate, which is why the instruction column carries H → V and the
  cache columns do not.
- **No IPC figure exists and none may be derived.** The `perf` columns are empty in all 68
  rows, and wall clock is narrative — it is not divided into the instruction count.

---

## Quick start

```bash
cmake -S . -B build && cmake --build build
./build/algo examples/fib.algo
```

| Flag | Effect |
|---|---|
| `--engine=tree` | the tree-walk interpreter (default) |
| `--engine=vm` | the bytecode virtual machine |
| `--dump` | print the compiled chunk to stdout and do not run |
| `--trace` | narrate the stages on stderr; stdout stays the program's own |

Requires CMake 3.20+ and a C++20 compiler. On macOS: `xcode-select --install && brew install cmake`.

---

## A diagnostic

Errors go to **stderr** with the path, line and column, the source line echoed, and a caret
under the offending span. Arithmetic **traps rather than wrapping**, so an overflow is a
diagnostic and not a wrong answer:

```
$ ./build/algo error_int_overflow.algo
error_int_overflow.algo:2:7: error: integer overflow in '+'
print total + 1
      ^~~~~~~~~
```

Exit 70 — a runtime fault, because whether `total + 1` overflows depends on what the program
computed. An out-of-range *literal* is exit 65, settled by the source text alone. **Both
engines render this identically**, and every case in `tests/` is checked against both.

---

## The bytecode

`--dump` compiles a program and prints its chunk instead of running it:

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
which is part of why nineteen opcodes are enough, and costs one extra instruction every time
it runs. And a jump operand is an **absolute target**, so the one `JUMP` at `0043` serves
the backward branch to a `while` header exactly as it serves the forward exit from an `if`.
The format is documented in [`docs/BYTECODE.md`](docs/BYTECODE.md).

**A 24-second terminal recording** puts both listings and the differential suite in one
clip: [`docs/demo.cast`](docs/demo.cast), played with `asciinema play docs/demo.cast` and
re-recorded by [`docs/demo.sh`](docs/demo.sh). It shows **no timings**, deliberately —
nothing on a developer's machine is a measurement here.

---

## The language

| Feature | Syntax |
|---|---|
| Assignment | `x = 5` |
| Arithmetic | `y = x + 3 * 2` |
| Comparison | `x < 10`, `n == 0`, `a != b` |
| Booleans | `true`, `false`, `!done` |
| Print | `print z` |
| Block | `{ x = 1 x = x + 1 }` |
| Selection | `if x < 5 { … } else if … { … } else { … }` |
| Iteration | `while i <= 100 { … }` |
| Function | `fn add(a, b) { return a + b }` |
| Call, return | `add(1, 2)` · `return expr` or bare `return` |
| Operators | `+` `-` `*` `/` `==` `!=` `<` `<=` `>` `>=` `!` unary `-` |

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

Precedence runs `equality → comparison → term → factor → unary → primary`, loosest first, so
`1 + 2 * 3 == 7` is `((1 + (2 * 3)) == 7)`. Prefix operators are right-associative and stack.

**Five things that surprise people, all of them deliberate:**

- **No grouping parentheses.** `(1 + 2) * 3` does not parse. A `(` follows a name and opens
  an argument or parameter list; it never groups an expression.
- **Conditions are not parenthesised** and braces are mandatory: `if x < 1 { … }`, never
  `if (x < 1) …`. `while (x) { }` is a compile error at the `(`.
- **No comment syntax.** `#` is an unknown character and `//` lexes as two divides.
- **No statement separator.** A statement ends where the next one begins.
- **Identifiers are letters and digits**, starting with a letter — no underscore.

Types do not mix, and there is no type checker: a mismatch is a runtime fault. An integer is
signed **64-bit** and every arithmetic operator traps rather than wrapping. Full rules,
grammar and precedence table: [`docs/GRAMMAR.md`](docs/GRAMMAR.md).

---

## Errors and exit codes

| Code | Meaning |
|---:|---|
| `0` | success |
| `64` | bad command line |
| `65` | compile-time error — lexical, syntactic, or a resolution error |
| `66` | unreadable input file |
| `70` | runtime fault |

**The split is the whole convention: an error settled by the source text alone is
compile-time; one that depends on what the program computed is a runtime fault.** So a
literal too wide for the value type is 65 even though it is only converted at parse time,
while a type mismatch is 70 — with no type checker, whether an operand has the right type
depends on what the program computed. A wrong argument count is 65; an exhausted call depth
is 70, for the same reason read twice.

Every message, caret and exit code is pinned by `tests/diagnostic_test.cpp`. The full table
of twenty-one errors is in
[`docs/GRAMMAR.md`](docs/GRAMMAR.md#errors-and-exit-codes).

---

## How it works

**1 · Lexer** — source text to tokens, tracking line and column across whitespace so every
token records the span it came from.

**2 · Parser** — recursive descent, one function per precedence level, so the level that
runs last binds loosest. Every node is its own struct with named fields.

**3 · Resolver** — walks the AST before execution with a stack of **frames**, one per
function body. Rejects out-of-scope names and wrong arities, and assigns every variable a
**slot index within its frame**. A frame is a boundary, not a counter: a function body sees
its parameters and its own locals and nothing else.

**4a · Tree-walk interpreter** (default) — evaluates the AST with one frame per call, a
`std::vector<Value>` indexed by slot. `return` unwinds as a flag rather than an exception,
because `fib(27)` returns several hundred thousand times. Recursion is bounded at 1000.

**4b · Bytecode compiler and VM** (`--engine=vm`) — a second back end over the same resolved
AST. `src/compiler.cpp` lowers it to a **chunk**: a flat byte array of nineteen opcodes, a
constant pool, a function table and a span table, with forward jumps backpatched.
`src/vm.cpp` runs it on an operand stack; locals live *on* that stack, so a call is a slot
range rather than a heap allocation.

The VM's fault messages are **duplicated from the tree-walker rather than shared**.
Extracting them into a common header would make the two engines agree by construction, and
agreeing by construction is not something a test can check.

---

## Testing

**Sixty-six CTest cases**: twenty-nine golden-file cases each registered **twice, once per
engine**, plus eight unit binaries linking the core library.

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -R _vm      # one engine
ctest --test-dir build -R _tree    # the other
```

Both engines are held to the **same** `.expected` files, so they must print the same bytes
on stdout, render the same diagnostic on stderr, and exit with the same code — not merely
compute the same answers. No golden file was regenerated when the second engine arrived, and
none should be: the shared expectation is what makes this a differential test rather than
two independent suites. The failure mode to watch for is not a red test but a **vacuous
pass** — register the second set, forget to pass the engine down, and twenty-nine green
tests run the tree-walker twice while looking perfect.

Golden cases compare streams, so unit tests cover what never reaches one: source spans,
diagnostic rendering, the value type and the precedence cascade, the resolver's slots and
frames, opcode widths, hand-written chunks, the VM arms differential testing cannot reach,
and the disassembler's listing geometry.

The suite also runs clean under UndefinedBehaviorSanitizer, which is what says the overflow
traps fire *before* the undefined operation. Off by default — a sanitizer in the default
build would instrument every number the benchmarks report:

```bash
cmake -S . -B build-ubsan -DALGO_SANITIZE=ON && cmake --build build-ubsan
ctest --test-dir build-ubsan --output-on-failure
```

CI builds and runs the full suite on every push across a matrix of GCC and Clang on Ubuntu.

---

## Measurement

Numbers come from `valgrind --tool=cachegrind`, which *simulates* a machine rather than
timing this one — so its counts are deterministic and reproduce exactly, which is what makes
them safe to commit. Valgrind does not meaningfully support arm64 macOS, so **there is no
measurement outside the Linux container**:

```bash
docker compose run --rm bench
docker compose run --rm bench bash scripts/bench.sh build-bench/algo bench/fib32.algo --config baseline
```

`scripts/bench.sh` measures one binary running one program and appends one row —
cachegrind's thirteen raw events, four derived totals, wall clock, and the provenance to
place the row later. It **refuses to run outside the container** and **refuses to measure an
unoptimised build**, because the worst outcome available here is not a missing number but a
plausible-looking wrong one. Both refusals were checked against builds that would otherwise
have produced a number.

`scripts/bench-ablations.sh` builds each configuration as a detached git worktree and hands
it to the driver, so the working tree never moves and no `#ifdef`-guarded variant is left
behind in the hot path. Worktrees are named after the **commit**, not the ref: the length of
the path a binary is invoked by moves cachegrind's cache columns by up to 9.6% of a
benchmark's D1 misses, with the binary image byte-identical.

Not all of a row is equally trustworthy, and [`results/README.md`](results/README.md) says
so: cachegrind counts may be committed, wall clock is narrative, and the `perf` columns are
empty here and expected to stay so.

---

## Repository layout

| Path | |
|---|---|
| `src/` | lexer, parser, resolver, tree-walk interpreter, bytecode compiler, VM, disassembler |
| `tests/` | golden `.algo` cases with their expected stdout/stderr/exit code, plus eight unit tests |
| `bench/` | the four benchmark programs — **not** globbed by CTest, so none inherits the suite's timeout |
| `scripts/` | the measurement driver and the configuration builder |
| `results/` | [`measurements.csv`](results/measurements.csv), the ledger every number traces to, and [its README](results/README.md) — schema, ablation workings, attribution table |
| `docs/` | [GRAMMAR](docs/GRAMMAR.md) · [BYTECODE](docs/BYTECODE.md) · [MEASUREMENT](docs/MEASUREMENT.md) · the demo clip |
| `examples/` | sample programs, including `fib.algo` |

Benchmarks build `RelWithDebInfo` inside the container; the default build stays unoptimised
on purpose, so `ctest` and the measurements never share a build directory.
