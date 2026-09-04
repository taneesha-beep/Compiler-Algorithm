# Algo — bytecode reference

> **Scope of this file.** Roadmap item **4.1** defines the chunk representation
> and the instruction set, and its acceptance criterion is that every opcode is
> documented here with its operand encoding and its stack effect. That is the
> [opcode table](#the-opcode-table) below; everything else on this page is the
> reasoning behind it.
>
> **Nothing executes bytecode yet.** Item 4.2 is the compiler, 4.3 the virtual
> machine, 4.4 the differential test across the two engines, 4.5 the
> disassembler. This file describes the format those four are built on and
> nothing more. The format lives in [`src/chunk.h`](../src/chunk.h).
>
> **There is no performance claim on this page.** Item 4.1 runs no measurement,
> and `CLAUDE.md` rule 2 forbids writing a number that was not produced by a
> command. Where a cost is real but unmeasured it is written `<pending>`, and
> item 5.1 is where it is filled in.

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
  operand**. Five of the nineteen opcodes take an operand; the other fourteen
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
it is doing more. The size of it is `<pending>`.

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
the fourteen that take none. **Stack** is the net effect on the operand stack:
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

## What the rest of Phase 4 owes this file

* **4.2** — sets `SpanEntry::span` per the rule above and `SpanEntry::op` on
  every arithmetic, comparison and unary instruction; emits `CONST 0; RETURN` at
  the end of every function body; backpatches jumps through `patchOperand`.
* **4.3** — saves the opcode's own offset before decoding, so `spanAt` resolves
  the right instruction; keeps `maxCallDepth` and counts the program's frame as
  the first; guards operand-stack overflow.
* **4.4** — is what turns every claim on this page from an assertion into
  something CI checks.
* **4.5** — reads `opCodeName`, `opCodeHasOperand`, the constant pool and the
  function table; a jump operand is already the absolute target, so there is
  nothing to compute.
