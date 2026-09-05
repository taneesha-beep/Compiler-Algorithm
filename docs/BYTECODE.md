# Algo — bytecode reference

> **Scope of this file.** Roadmap item **4.1** defines the chunk representation
> and the instruction set, and its acceptance criterion is that every opcode is
> documented here with its operand encoding and its stack effect. That is the
> [opcode table](#the-opcode-table) below; everything else on this page is the
> reasoning behind it.
>
> **Bytecode executes.** Item 4.2 is the compiler — **done, 2026-09-04**, in
> [`src/compiler.{h,cpp}`](../src/compiler.h) — and item 4.3 is the virtual
> machine that runs what it writes — **done, 2026-09-04**, in
> [`src/vm.{h,cpp}`](../src/vm.h), reached by `algo --engine=vm`. Item 4.4 is
> the differential test across the two engines — **done, 2026-09-04**, and it is
> what holds both to the same golden files — and item 4.5 is the disassembler,
> **done, 2026-09-05**, in [`src/disassembler.{h,cpp}`](../src/disassembler.h),
> reached by `algo --dump <file>`. This file describes the
> format those four are built on and nothing more; the format itself lives in
> [`src/chunk.h`](../src/chunk.h). **What 4.2 settled on top of it is a section
> of its own near the end**, and 4.3 and 4.5 should read it.
>
> **There is no performance claim on this page.** Item 4.1 runs no measurement,
> and `CLAUDE.md` rule 2 forbids writing a number that was not produced by a
> command. Where a cost is real but unmeasured it is written `<pending>`.
> **Item 5.1 filled in
> the lowered-comparison cost on 2026-09-05 — 57.00 instructions each; see below.** The
> one remaining `<pending>` on this page, the cost of the operand encoding's extra byte,
> is deliberate: no ablation prices it, so there is nothing to measure it against.

---

## The obligation that shapes everything here

Item **4.4** registers every golden case twice, once per engine. Item 4.3's
acceptance is that `--engine=vm` produces output *identical* to `--engine=tree`
on every program in the repository. So the VM does not merely have to compute
the same answers — it has to print the same bytes on stdout, render the same
diagnostic on stderr, and exit with the same code, including when a program
faults.

That single sentence decides three things this instruction set would otherwise
have got wrong, and each has its own section below: what the source-position
table has to carry, which opcodes trap, and how the language's six comparison
operators map onto four opcodes without changing a diagnostic.

---

## The chunk

One chunk per **program**, not one per function. Item 4.3's call frame is
`{ returnIP, slotBase }` — one return address, with no chunk to switch to — so
every function body is emitted into the same `code` vector and a call is a jump
within it.

| Member | Type | What it holds |
|---|---|---|
| `code` | `std::vector<std::uint8_t>` | the instruction stream; execution starts at offset **0** |
| `constants` | `std::vector<Value>` | the constant pool, indexed by `CONST`'s operand |
| `spans` | `std::vector<SpanEntry>` | one entry per instruction — see [The span table](#the-span-table-and-why-it-is-not-a-line-table) |
| `functions` | `std::vector<FunctionInfo>` | one entry per declared function, indexed by `CALL`'s operand |
| `programFrameSize` | `int` | slots in the program's own frame — `resolve()`'s return value |

`FunctionInfo` is `{ name, entry, arity, frameSize }`. `name` exists for the
disassembler; the VM never reads it. `arity` and `frameSize` come straight off
the resolved `FunctionNode` — item 1.3 wrote them and item 3.4 was the first
thing to read them.

**Layout is a convention of item 4.2, not a property of the format.** The
format requires only that execution begins at offset 0 and that every
`FunctionInfo::entry` names the first instruction of a body. The intended
layout is the program's own statements first, terminated by `HALT`, then the
function bodies in declaration order.

### Operand encoding

* An instruction is **one opcode byte**, optionally followed by **one two-byte
  operand**. Six of the nineteen opcodes take an operand; the other thirteen
  are a byte on their own. So every instruction is **1 or 3 bytes**, and
  `opCodeHasOperand` / `instructionLength` in `src/chunk.h` are what a decoder
  walks the stream with.
* An operand is an **unsigned 16-bit** value, **little-endian, low byte first**,
  written and read a byte at a time. Not a store through a `std::uint16_t *`:
  that would be host byte order and an unaligned access, and the same source
  would then produce different chunks on different machines — which would make
  the disassembly item 4.5 excerpts in the README a property of whoever built
  it. `tests/chunk_test.cpp` pins the two bytes.
* **Uniform width, deliberately.** A variable-width encoding — one byte for
  small indices, three for large — would be smaller and is what a production VM
  does. It is not done here because choosing it would be an optimisation, and an
  optimisation this project cannot justify is one it cannot measure: item 4.1
  runs no measurement, and the roadmap gives the encoding no ablation of its
  own. The cost of the extra byte is `<pending>` and there is no plan to
  measure it.
* **Jump operands are ABSOLUTE target offsets**, not signed relative
  displacements. One `JUMP` therefore serves both directions, which is what
  lets the roadmap's nineteen opcodes cover `while` — whose backward jump to
  the loop header has no opcode of its own. Backpatching becomes
  `patchOperand(jumpOffset, code.size())` with no arithmetic to get wrong, and
  the disassembler prints the operand unchanged. The trade is real and is
  stated rather than hidden: an absolute target caps a chunk's **total** code
  at 65535 bytes, where a signed relative displacement would instead cap the
  **distance** of any one jump at ±32767 and allow a larger chunk. Neither
  limit is near for any program in this repository, and the absolute form is
  the one whose correctness can be read off the disassembly.
* Every 16-bit operand — constant index, slot index, function index, jump
  target — is therefore capped at **65535**, and `code` may not exceed 65535
  bytes. Both limits are checked where the chunk is written, and exceeding
  either raises a `CompileError` (exit **65**), because a limit of the
  implementation is settled by the source text alone. The alternative — a
  silently truncated index — is a wrong answer with nothing to report it.

### Slots, the stack, and frames

Item 3.4 made the tree-walker read the frame slots the resolver has written
since item 1.3, so **the VM inherits the same numbering and does no name
resolution of its own.** `LOAD_LOCAL` and `STORE_LOCAL` take exactly the
indices already on `IdentifierNode::slot`, `AssignNode::slot` and
`Parameter::slot`.

Locals live **on the operand stack**, which is what the roadmap's `slotBase`
means: slot *s* of the running frame is `stack[slotBase + s]`. The resolver
numbers a function's parameters before it walks the body, so parameters occupy
slots `0 .. arity-1` and a call's arguments — pushed by the caller, in source
order — *are* the callee's first locals with nothing to copy.

`CALL` therefore sets `slotBase = stackTop - arity` and reserves
`frameSize - arity` further slots, each initialised to the integer `0`. That
matches the tree-walker exactly: `callFunction` sizes its frame vector from
`FunctionNode::frameSize`, and `Value`'s trivial default constructor
value-initialises the elements to `{Int, 0}`. Before the instruction at offset 0
runs, the VM reserves `programFrameSize` slots the same way, so the program's
own frame is `slotBase = 0`.

**Functions are not values.** There is no closure, no upvalue and nothing that
produces a function at runtime — the roadmap's *Out of scope* table excludes
all three. `CALL` names a function through an index settled at compile time; it
does not pop a callable. One consequence: the tree-walker's
`unknown function '<name>'` fault has no counterpart in the VM, because the
resolver has already rejected every call the function table could not answer.

---

## The span table, and why it is not a line table

The roadmap describes this as "a **line table** mapping instruction offsets back
to source lines". A line number is not enough, and the gap is not cosmetic.

`src/diagnostic.h` renders `path:line:col`, echoes the offending source line,
and underlines the offending text — a caret under its first character and a
tilde under each remaining one. That underline is `Span::len` characters long,
and `tests/diagnostic_test.cpp` pins those bytes for every fault the language
can raise. A table carrying only a line could reproduce neither the column nor
the tilde run, so item 4.3's acceptance would fail on every program that
faults — and it would fail after two items had been built on the format.

So the table carries the whole `Span`, and one further field:

```c++
struct SpanEntry {
    std::uint32_t offset;      // offset of the instruction's OPCODE byte
    Span span;                 // what a diagnostic for this instruction underlines
    const char *op;            // operator spelling for a message that quotes one
};
```

**`op` is there because the lowering below makes an opcode ambiguous.** An `LT`
is either a source `<` or the first half of a source `>=`, and four of the
interpreter's fault messages quote the operator as the programmer wrote it —
`operator '<op>' cannot be applied to …` in its binary and unary forms, and
`integer overflow in '<op>'` in its binary and unary forms. The instruction has
to carry that spelling because its opcode no longer determines it. It is a
`const char *` into a string literal or into the AST's `BinOpNode::op` /
`UnaryOpNode::op`, both of which outlive the chunk; nothing here owns it. Those
two fields are kept on the nodes for exactly this kind of reason — see
`src/ast.h`.

**The invariant.** Every emit appends exactly one entry, so the table is one
entry per instruction and sorted by offset without anyone maintaining either
property. `Chunk::spanAt(offset)` binary-searches for the greatest entry at or
before `offset`, so an operand byte resolves to its own instruction as well as
the opcode byte does. What it will *not* survive is being handed the
instruction pointer **after** the decode — that is the next instruction's
offset exactly. **The VM saves the opcode's own offset before decoding. That is
the contract**, and it is the one thing about this table item 4.3 can get
wrong without a test noticing.

The table is not run-length encoded. It is read only when a fault is raised,
never on the dispatch path, so compressing it buys nothing measurable and adds
a decoder to get wrong.

**Which span an instruction carries** is not free choice either. The rule is:
*the span an instruction carries is the span the tree-walker's fault for that
same operation carries.* Concretely — a binary arithmetic or comparison
instruction carries the whole `BinOpNode`'s span, `NEG` and `NOT` the whole
`UnaryOpNode`'s, `CALL` the `CallNode`'s, and `JUMP_IF_FALSE` the span of the
**condition** rather than of the enclosing `if` or `while`, because
`requireCondition` in `src/interpreter.cpp` puts the caret on the condition.
Getting this wrong moves a caret, which no golden case would catch until item
4.4 ran the same case through both engines.

---

## The six comparison operators, and the four opcodes

The six operators, their precedence and their associativity are the language's
and not this format's: [`docs/GRAMMAR.md`](GRAMMAR.md) holds the grammar and the
precedence table, and the lowering below preserves both exactly.

Item 1.1 gave the language six comparison operators and `src/ast.h`'s
`BinOpKind` has all six: `Less`, `LessEqual`, `Greater`, `GreaterEqual`,
`Equal`, `NotEqual`. The instruction set has `EQ`, `LT`, `GT` and `NOT`. Three
operators have no opcode.

**Decision: `<=`, `>=` and `!=` are lowered onto the other three plus `NOT`.**

| Source | Emitted |
|---|---|
| `a < b` | `LT` |
| `a > b` | `GT` |
| `a == b` | `EQ` |
| `a <= b` | `GT`, `NOT` |
| `a >= b` | `LT`, `NOT` |
| `a != b` | `EQ`, `NOT` |

The alternative was three more opcodes, `LE`, `GE` and `NE`. Lowering was
chosen because the roadmap states that nineteen opcodes suffice and this is the
claim that makes it true; because the three added opcodes would be three more
arms in the dispatch loop for no new capability; and because the identity is
exact on this language's values — integers are totally ordered, so
`a <= b` is `!(a > b)`, and equality is defined on two operands of one type, so
`a != b` is `!(a == b)`.

**Three things follow, and all three are consequences rather than details.**

*It costs an extra instruction per lowered comparison.* Every `<=`, `>=` and
`!=` executes a `NOT` the tree-walker never performed. That cost is real,
it lands in whichever direction it lands, and it belongs in the **H → V**
comparison item 5.1 reports rather than being discovered there: the VM is not
uniformly doing less work than the tree-walker, and this is one of the places
it is doing more. **Measured at item 5.1: 57.00 instructions per lowered comparison.** A probe pair
differing by one substitution — `i < 1000000` against `i <= 999999`, identical loop
header, identical 1,000,000 true arms, both printing `1000000` — retired 1,239,672,638
and 1,296,673,045 `Ir` under `--engine=vm`, a difference of 57,000,407 over exactly
1,000,000 lowered comparisons. That is one turn of the VM's dispatch loop, and it sits just
below the **67.46** `Ir` per instruction dispatched that `bench/loop10m.algo`'s committed row
gives independently — 6,071,652,034 `Ir` over 10,000,000 turns of a **nine**-instruction
body, counted off `./build/algo --dump bench/loop10m.algo` at offsets 0006 to
0026. **None of the four benchmark programs contains a lowered comparison** — all
four compare with `<` — so the cost is exactly zero in every cell of 5.1's table,
and the losses recorded there are not this. Workings in `results/README.md`.

*The type fault must still name the operator the programmer wrote.* `1 <= true`
faults in the tree-walker with `operator '<=' cannot be applied to integer and
boolean`. The VM raises it from a `GT` instruction, which would say `'>'` if it
derived the spelling from the opcode. It does not: it reads `SpanEntry::op`,
which item 4.2 sets to the source operator's text. This is the whole reason
that field exists.

*A lowered `NOT` cannot itself fault.* `NOT` raises `operator '!' cannot be
applied to <type>` when its operand is not a boolean, and the operand of a
lowered `NOT` is the boolean that `EQ`, `LT` or `GT` has just produced. So only
a `NOT` compiled from a source `!` can ever raise that message, and the
ambiguity in `op` never reaches a diagnostic on the lowered path. Item 4.2
should still set `op` on both instructions of a lowered pair, so that a
disassembly reads as one source construct.

---

## Trapping

Item 1.5 put a trapping 64-bit overflow check on five sites, and the comment
above `binaryOverflowFault` in `src/interpreter.cpp` says the check "is in
every configuration that exists" — **including this one**. It has to be: item
4.4 compares the two engines on the same programs, and an engine that wrapped
where the other trapped would not be running the same language.

So `ADD`, `SUB`, `MUL`, `DIV` and `NEG` are **trapping opcodes**, and the
faults below are part of their definition rather than an implementation detail
of item 4.3. All are `RuntimeFault` and exit **70**; the span table is what
tells each one where to point.

| Fault | Raised by | Message |
|---|---|---|
| type mismatch, binary | `ADD` `SUB` `MUL` `DIV` `EQ` `LT` `GT` | `operator '<op>' cannot be applied to <type> and <type>` |
| type mismatch, unary | `NEG` `NOT` | `operator '<op>' cannot be applied to <type>` |
| overflow, binary | `ADD` `SUB` `MUL` `DIV` | `integer overflow in '<op>'` |
| overflow, unary | `NEG` | `integer overflow in unary '<op>'` |
| division by zero | `DIV` | `division by zero` |
| non-boolean condition | `JUMP_IF_FALSE` | `a condition must be a boolean, not <type>` |
| call depth exceeded | `CALL` | `call depth exceeded` |

Four points of exactness, each of which is a way to get this subtly wrong:

* **`DIV` checks division by zero first**, then overflow. `/` overflows on
  exactly one pair of operands, `INT64_MIN / -1`, because the two's-complement
  range is asymmetric. `NEG` overflows on exactly one input, `-INT64_MIN`, for
  the same reason. Neither is reachable from a literal — the lexer makes a
  number token out of digits alone — so the most negative integer exists only
  as something a program computes. `docs/GRAMMAR.md` has the full rules.
* **The caret goes on the whole operation, not on the guilty operand**, for
  every binary fault: an operand is only wrong relative to what is being done
  with it. `JUMP_IF_FALSE` is the exception, and puts it on the condition.
* **`maxCallDepth` is 1000 and the VM keeps it**, along with its diagnostic.
  The tree-walker's predicate is `frames.size() > maxCallDepth`, on a stack
  whose first entry is the program's own frame — so it admits exactly
  `maxCallDepth` nested calls. The VM's frame stack has to be counted the same
  way, or `tests/deep_recursion.algo` and its failing counterpart move by one.
  `CALL` checks the depth *after* the arguments have been evaluated, which is
  where the preceding instructions leave them.
* **Arity is not checked at runtime.** The resolver rejects a call of the wrong
  arity as a `CompileError` (65) before either engine runs, and item 4.2
  consumes the resolved AST.

Item 4.3 adds one guard the tree-walker has no counterpart for: **operand-stack
overflow**. It is the VM's own resource, so it is 4.3's to define.

---

## The opcode table

Nineteen opcodes. **Operand** is the meaning of the two-byte operand, or *—* for
the thirteen that take none. **Stack** is the net effect on the operand stack:
`−2 +1` means two values are popped and one pushed.

| Opcode | Operand | Stack | Effect |
|---|---|---|---|
| `CONST` | constant index | `+1` | pushes `constants[i]` |
| `LOAD_LOCAL` | slot index | `+1` | pushes `stack[slotBase + s]` |
| `STORE_LOCAL` | slot index | `−1` | pops a value into `stack[slotBase + s]` |
| `POP` | — | `−1` | discards the top value. **Emitted nowhere** — see the note below |
| `ADD` | — | `−2 +1` | integer sum. Traps on a non-integer operand and on overflow |
| `SUB` | — | `−2 +1` | integer difference. Traps on a non-integer operand and on overflow |
| `MUL` | — | `−2 +1` | integer product. Traps on a non-integer operand and on overflow |
| `DIV` | — | `−2 +1` | integer quotient. Traps on a non-integer operand, on a zero divisor, and on `INT64_MIN / -1` |
| `NEG` | — | `−1 +1` | integer negation. Traps on a non-integer operand and on `-INT64_MIN` |
| `EQ` | — | `−2 +1` | pushes a boolean. Traps unless both operands have the same type |
| `LT` | — | `−2 +1` | pushes a boolean. Traps unless both operands are integers |
| `GT` | — | `−2 +1` | pushes a boolean. Traps unless both operands are integers |
| `NOT` | — | `−1 +1` | boolean negation. Traps on a non-boolean operand |
| `JUMP` | target offset | `0` | sets the instruction pointer to the operand |
| `JUMP_IF_FALSE` | target offset | `−1` | pops a boolean; jumps to the operand if it is false. Traps on a non-boolean |
| `CALL` | function index | `−arity +1` | see below |
| `RETURN` | — | see below | returns the top value to the caller |
| `PRINT` | — | `−1` | pops a value and writes it to **stdout** with a newline |
| `HALT` | — | `0` | ends the program |

**`CALL f`** — with the callee's `arity` arguments on top of the stack, in
source order: check the call-depth limit; set `slotBase = stackTop - arity`;
reserve `frameSize - arity` further slots holding the integer `0`; push
`{ returnIP, slotBase }` onto the frame stack; set the instruction pointer to
`functions[f].entry`. Net effect on the *caller's* stack once the call returns
is `−arity +1`.

**`RETURN`** — pops the result, discards the whole frame's slot and temporary
region back to `slotBase`, pushes the result there, pops the frame stack, and
restores the instruction pointer to `returnIP`. The result is the value the
caller sees, so the caller's stack is one deeper than before its arguments were
pushed. Item 4.2 emits `CONST 0` followed by `RETURN` at the end of every
function body: a body that runs off its end hands back the integer `0`, and so
does a bare `return`. That is what `callFunction` does today, and the argument
for `0` rather than a unit value — the language has two value types and the
roadmap adds no third — is in `src/interpreter.cpp` beside it.

**`PRINT` writes to stdout**, in the tree-walker's exact format: an integer in
decimal, a boolean as `true` or `false`, followed by a newline. Diagnostics and
`--trace` narration go to stderr — `CLAUDE.md`'s *Output discipline* — and
nothing in this instruction set writes there.

**Booleans are constants.** There is no `TRUE` or `FALSE` opcode, so `true` and
`false` reach the stack through `CONST` and the pool holds `Value`s of both
types. The pool is not deduplicated: two occurrences of `1` get two entries, so
an index names the occurrence it was made for and a disassembly reads against
its source.

### On `POP`, which nothing emits

`POP` is in the roadmap's nineteen and is defined above, and item 4.2 will emit
it nowhere. **The language has no expression statements.** `parseStatement` in
`src/parser.cpp` admits `print`, `if`, `while`, `return`, a block, an
assignment, and (at the top level) a function declaration — and nothing else,
so a bare `f(1)` is not a statement and every expression's value is consumed by
the construct that contains it. `JUMP_IF_FALSE` pops its own condition, which
is the other place a stack-discipline VM usually needs `POP`; this language has
no `&&` or `||` to make peeking worthwhile.

It is kept rather than dropped for two reasons. An instruction set is a format,
and a format with a hole where an opcode was is worse than one with the opcode
defined; and item 4.5's disassembler must have something to print for any byte
it meets. The consequence is stated plainly here so that nobody has to
rediscover it: item 4.3 will implement a `POP` arm that item 4.4's differential
testing never reaches, and that is a known coverage gap rather than an
oversight.

---

## What item 4.2 settled

Item 4.2 built the compiler — `src/compiler.{h,cpp}`, landed 2026-09-04. It
discharged the obligation listed for it below, and in doing so it took five
decisions this page had left open. They are recorded here rather than in the
roadmap because **4.3 and 4.5 are told to read this file**, and all five are
things those two items would otherwise have to guess at or rediscover.

**The tree must outlive the chunk, and the failure is quiet.** The section on
the span table says `SpanEntry::op` is a `const char *` into the AST and that
nothing here owns it. What it did not say is what happens when the AST is
dropped first, and 4.2's first acceptance driver did exactly that: it lexed,
parsed, resolved, compiled, and returned the chunk, leaving the tree behind.
Nothing crashed and nothing read as null — **the pointers stayed non-null and
read as empty strings**, so a type fault would have rendered
`operator '' cannot be applied to integer and boolean` and item 4.4 would have
met it as a diagnostic mismatch with no obvious cause. So: **whatever runs,
prints or tests a chunk must keep the tree it was compiled from alive for as
long as the chunk exists.** In `src/main.cpp` that is free, the AST being a
local of the same scope; the shape that breaks it silently is the natural
refactor — a `compileFile(path)` helper that returns a `Chunk` and lets the
tree go. Item 4.5 inherits this too: a disassembler prints `SpanEntry::op`.

**`CONST 0; RETURN` is emitted at the end of every body unconditionally**,
including bodies that already end in a `return`. The format requires it only for
a body that runs off its end, but emitting it always is what makes *every
function region ends in `RETURN`* an invariant rather than a case analysis, so
no body can fall through into the next one. Where it is unreachable it costs
four bytes. Item 4.5 will disassemble a trailing `CONST 0; RETURN` after an
explicit `return` in most functions, and that is correct output, not a bug.

**Three instructions carry a span that no diagnostic will ever render**, and
they carry one because every instruction has a span entry and a disassembly
reads better with a line number than without. `HALT` carries the last top-level
statement's span — a zero span for a program with no statements at all. The
unconditional `JUMP` out of an `if`'s then-branch, and a `while`'s backward
`JUMP`, carry the enclosing statement's span. **Only `JUMP_IF_FALSE` has a
rule**, and it is the one stated above: the *condition's* span.

**A `BlockNode` emits no instruction of its own.** The resolver flattened scopes
at item 1.3 — a slot is unique within a whole function body, not within the
block that declared it — so there is nothing to push and nothing to pop, and a
block is invisible in the disassembly.

**The unpatched-jump placeholder is `maxOperand`, not 0.** `emitJump` writes a
placeholder and `patchJumpToHere` fills it in later. 0 is the obvious
placeholder and it is the wrong one: **0 is a legitimate jump target** — a
`while` written as the program's first statement has its header at offset 0 —
so an unpatched jump written as 0 is indistinguishable from a correct one, and
the check that every jump target lands on an instruction boundary passes.
65535 cannot be a boundary in any chunk this format admits, `code` being capped
at 65535 bytes. The mutant that removes the patch for an `if` with no `else` is
caught for exactly this reason and would otherwise have survived.

**What 4.2 did *not* add.** `src/main.cpp` still takes only `--trace`. There is
no `--engine` and no `--dump`: 4.2 adds a back end and runs nothing, so the
acceptance criterion is met by a unit binary, `tests/compiler_test.cpp`, which
compiles every program under `tests/`, `examples/` and `bench/` and checks each
chunk structurally. Wiring a flag belongs to 4.3 and 4.5 respectively.

---

## What item 4.3 settled

Item 4.3 built the machine — `src/vm.{h,cpp}`, landed 2026-09-04, reached by
`algo --engine=vm`. It discharged the obligation listed for it below, and took
four decisions this page had left open. **4.4 and 4.5 should read these**; the
first two are things they would otherwise have to guess at.

**The operand-stack guard is `operand stack exhausted`, a `RuntimeFault`
(exit 70), at a cap of 2^20 slots.** This page left all three to 4.3 as "the
VM's own resource". The classification follows the rule in `CLAUDE.md`'s
*Output discipline* — a fault settled by the source text alone is a
`CompileError`, one that depends on what the program computed is a
`RuntimeFault` — and exhausting the stack is reached by recursing, which is
data-dependent, exactly like `call depth exceeded`. The wording is deliberately
not "overflow": four existing messages are `integer overflow in …`, and reusing
the word would make two unrelated faults read alike. The cap is a bound rather
than an OOM: slots and temporaries share one stack, so the depth it bounds is
the sum over live frames of `frameSize` plus live temporaries. `maxCallDepth`
already bounds the *number* of live frames at 1000, but a frame is as wide as
its function declares — up to `maxOperand` slots — so 1000 frames of maximum
width would be 65 million slots and neither guard implies the other. For a
program that recurses with very wide frames this one fires first, which is
correct: the resource actually exhausted is the operand stack, and reporting
the call depth would name the wrong cause.

**The fault messages are duplicated from the tree-walker, word for word, and
that is the decision rather than a shortcut.** Extracting them into a header
both engines include would make the two agree *by construction*, and agreeing
by construction is not something item 4.4 could then check — the duplication is
what 4.4 has to find a difference in. The helpers could not have been shared in
any case: the tree-walker's take a `BinOpNode` for their span and operator text,
and the VM has a `SpanEntry` instead. What *is* shared is `maxCallDepth`, which
`src/vm.cpp` includes from `src/interpreter.h` rather than restating — a
constant is not a rendered string, and the two engines must not drift on it.

**`--engine` defaults to `tree`, and `--dump` was not added.** Every golden case
invokes `algo <file>` with no flag, so the default decides what the whole suite
exercises; pointing it at the VM before 4.4 exists would re-aim 29 cases at an
engine nothing had checked. An unrecognised value is exit 64, like any other bad
command line. `--dump` remains item 4.5's.

**Locals living on the operand stack has one sharp edge, and it is `LOAD_LOCAL`.**
`stack[slotBase + s]` is a reference *into* the vector the same instruction is about to
grow, so pushing it directly dangles on exactly the pushes that reallocate. The value is
copied out first. Nothing in the format says this — it falls out of the roadmap's choice to
put slots and temporaries on one stack — and it is written here because the natural spelling
of the arm is the wrong one and the failure is intermittent rather than loud.

**Two engine-bug paths exist that no chunk reaches.** A byte outside the nineteen raises
`unknown opcode` as a `RuntimeFault`, the counterpart of the tree-walker's
`unknown node type`, so a corrupt chunk reports itself instead of falling through into
undefined behaviour; and the dispatch loop asserts that the instruction pointer is inside
`code`, which a chunk ending in neither `HALT` nor `RETURN` would violate. Item 4.5 should
expect the same two shapes: a disassembler also has to print *something* for a byte that
names no opcode.

**Item 4.3's acceptance was met by a throwaway driver, not by a committed one.**
`--engine=vm` matched `--engine=tree` on stdout, stderr and exit code for all 35
programs under `tests/`, `examples/` and `bench/` — including the seven the front
end rejects with 65 and the five that fault at run time with 70. The driver lived
under `build-vmcheck/` and was deleted, because **item 4.4 is precisely the item
that makes this permanent**, and a committed script here would be re-shaped
rather than reused. `tests/vm_test.cpp` holds instead only what 4.4 cannot reach:
the `POP` arm, the operand-stack guard, the unknown-opcode arm, and the span
contract.

---

## What item 4.5 settled

Item 4.5 added `src/disassembler.{h,cpp}` and the `--dump` flag. It reads this
file and adds nothing to the format: no opcode, no encoding, no table. Five
decisions it took that the format left open, recorded here so that a later
session reading a disassembly does not re-derive them.

**`--dump` goes to STDOUT, where `--trace` goes to stderr, and the two are not
inconsistent.** `--trace` narrates a run that is happening anyway, so it has to
stay off the stream the program is writing to — `CLAUDE.md`'s *Output
discipline*. `--dump` replaces the run: nothing executes, so there is no
program output to keep clear of, the disassembly is the thing the tool was
asked to produce, and `algo --dump f.algo > excerpt.txt` is how an excerpt is
captured. Checked with two files rather than a pipe; stderr is empty.

**`--dump` does not run the program, and is orthogonal to `--engine`.** A chunk
is not an engine's property — it is what `--engine=vm` would have executed — so
`--engine` is accepted alongside and has nothing to select between. `--dump`
takes no value, so `--dump=vm` falls into the unknown-option arm and exits 64,
which is `--engine`'s convention.

**Every byte prints. The walk never throws and never skips.** `POP` prints like
any other opcode — it is in `opCodeName`, and that is the whole reason the
section above keeps it. A byte outside the nineteen prints as
`<unknown opcode 0x7f>` and advances one byte, a byte that is no opcode having
no length. An operand-bearing opcode with fewer than two bytes behind it prints
as `<truncated: CONST needs 3 bytes, 1 available>` and ends the walk. Neither
can occur in a chunk item 4.2 wrote; a debugging tool that dies on the corrupt
input you reached for it to inspect is the wrong tool.

**The line column comes from an EXACT span-table hit, not from `spanAt`'s
answer.** `spanAt` returns the greatest entry at or before an offset, which is
right for a fault and wrong here: a byte with no entry of its own would
silently borrow the previous instruction's line. The disassembler prints the
line only when `entry->offset == offset`, and a dash otherwise.

**The operand is read through `Chunk::readOperand`, a byte at a time**, the
same little-endian decode `emitOperand` writes — so the disassembly is the same
on any host, which is what the encoding note above exists for. A jump's operand
is printed unchanged, being already the absolute target; the one thing derived
from it is the `(backward)` marker, which is what makes a `while` legible as a
loop.

`tests/disassembler_test.cpp` is the eighth unit binary. Two chunks built by
hand and, beside each, the disassembly written out by hand — every offset,
mnemonic, operand, annotation and column — so the expectation is stated a
second time and independently. The second chunk is the one nothing else can
reach: `POP`, a byte that is no opcode, and a truncated instruction. Five
mutants under the binary-hash guard, hashing `build/disassembler_test` because
that is what links the code under test; all five detected, and `chunk_test`
stayed green under each, so it is the new test doing the catching.

---

## What the rest of Phase 4 owes this file

* ~~**4.2**~~ — **done, 2026-09-04.** Sets `SpanEntry::span` per the rule above
  and `SpanEntry::op` on every arithmetic, comparison and unary instruction
  (**both halves of a lowered pair**); emits `CONST 0; RETURN` at the end of
  every function body; backpatches jumps through `patchOperand`. See the section
  above for the five decisions it took on top of these.
* ~~**4.3**~~ — **done, 2026-09-04.** Saves the opcode's own offset before
  decoding, so `spanAt` resolves the right instruction; keeps `maxCallDepth`
  — included from `src/interpreter.h` rather than restated — and counts the
  program's frame as the first; guards the operand stack at 2^20 slots. See the
  section above for the four decisions it took on top of these.
* ~~**4.4**~~ — **done, 2026-09-04.** Every claim on this page is now checked by
  CI on every push and under both compilers: each of the 29 `tests/*.algo`
  golden cases is registered twice, `<case>_tree` and `<case>_vm`, and the two
  engines are held to the *same* `.expected` / `.expected_err` /
  `.expected_code` files — so stdout, stderr and the exit code are compared,
  not merely answers. 65 tests where there were 36. Three things it settled on
  top of this file: the tree registration passes **no** engine flag (the goldens
  were recorded against the bare invocation, so that is what stays under test);
  **no golden file was written or regenerated**, because the shared expectation
  is what makes this differential rather than two independent suites; and the
  span-table rules above — the operator spelling on both halves of a lowered
  pair, `JUMP_IF_FALSE` carrying the condition's span — are now pinned by the
  five faulting cases running under the VM, not only by `tests/vm_test.cpp`.
* ~~**4.5**~~ — **done, 2026-09-05.** Reads `opCodeName`, `opCodeHasOperand`,
  `instructionLength`, the constant pool, the function table and the span
  table; prints a jump's operand unchanged, the absolute target needing no
  arithmetic. Adds nothing to the format. See the section above for the five
  decisions it took on top of it — the stream, the orthogonality to `--engine`,
  what it does with `POP` and with a byte that is no opcode, the exact
  span-table hit, and the byte-at-a-time operand read.

**THIS LIST IS NOW CLOSED.** 4.1 wrote this file; 4.2, 4.3, 4.4 and 4.5 have
each struck their bullet through. Phase 4 owes this file nothing further, and
item 6.3's job on it is to confirm and cross-link, not to write.
