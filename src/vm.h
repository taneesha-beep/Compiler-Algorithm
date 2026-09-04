#pragma once

#include <cstddef>
#include <vector>

#include "chunk.h"
#include "value.h"

// ============================================================
// STAGE 7: THE BYTECODE VM — a third engine beside the
// tree-walker, executing the chunks item 4.2 writes
// ============================================================
//
// This is item 4.3, configuration **V**. An operand stack, a frame stack of
// `{ returnIP, slotBase }`, and a dispatch loop over the opcode enum. It shares
// the front end and the `Value` with the tree-walker and nothing else: nothing
// in `src/interpreter.cpp` was touched to add it, and nothing here is called
// from there.
//
// ON THE OBLIGATION THAT SHAPES IT. Item 4.4 registers every golden case
// against both engines, so this has to print the same bytes on stdout, render
// the same diagnostic on stderr and exit with the same code as the tree-walker
// on every program in the repository — not merely compute the same answers.
// That is why the fault messages below are duplicated from
// `src/interpreter.cpp` word for word rather than extracted into a header both
// engines share: extracting them would make the two agree *by construction*,
// and agreeing by construction is not something 4.4 could then check. The
// duplication is the thing under test.
//
// ON THE TREE OUTLIVING THE CHUNK. `SpanEntry::op` points into the AST's
// `BinOpNode::op` / `UnaryOpNode::op` and the chunk owns none of it, so
// whatever calls `run` must keep the tree the chunk was compiled from alive for
// as long as the chunk exists. The failure is quiet rather than loud — the
// pointers stay non-null and read as empty strings, so a type fault renders
// `operator '' cannot be applied to ...`. In `src/main.cpp` that is free, the
// AST being a local of the same scope; the shape that breaks it is a
// `runFile(path)` helper that compiles, drops the tree, and then executes.
// `src/compiler.h` and `docs/BYTECODE.md` both carry this warning; this is the
// item that inherits it.

// The operand-stack cap, and the one guard this engine has that the
// tree-walker has no counterpart for — `docs/BYTECODE.md` calls it "the VM's
// own resource, so it is 4.3's to define".
//
// ON THE VALUE. Slots and temporaries share one stack, so the depth this bounds
// is `sum over live frames of (frameSize + live temporaries)`. `maxCallDepth`
// already bounds the number of live frames at 1000, but a frame is as wide as
// its function declares — up to `maxOperand` slots — so the two guards bound
// different things and neither implies the other: 1000 frames of maximum width
// would be 65 million slots. 2^20 is far above anything in this repository
// (the deepest case here, `tests/deep_recursion.algo`, recurses 999 frames of a
// handful of slots each) and low enough to be a bound rather than an OOM.
//
// ON WHICH GUARD FIRES FIRST. For a program that recurses with very wide
// frames, this one does, and that is correct rather than unfortunate: the
// resource actually exhausted is the operand stack, and reporting the call
// depth instead would name the wrong cause.
inline constexpr std::size_t maxStackSlots = 1u << 20;

class VM
{
public:
    // Runs `chunk` from offset 0 to its `HALT`. Writes the program's output to
    // stdout and throws `RuntimeFault` (exit 70) on a fault, which is the same
    // contract `Interpreter::execute` has — `src/main.cpp` catches both in one
    // place.
    void run(const Chunk &chunk);

private:
    // One return address and one slot base. There is no chunk to switch to:
    // `docs/BYTECODE.md` puts every function body in the same `code` vector, so
    // a call is a jump within it.
    struct Frame
    {
        std::size_t returnIP = 0;
        std::size_t slotBase = 0;
    };

    const Chunk *chunk = nullptr;

    // Locals live ON the operand stack: slot `s` of the running frame is
    // `stack[frames.back().slotBase + s]`. The resolver numbers a function's
    // parameters before it walks the body, so a call's arguments — pushed by
    // the caller, in source order — *are* the callee's first locals and there
    // is nothing to copy.
    std::vector<Value> stack;

    // Its first entry is the program's own frame, which is what makes
    // `frames.size() > maxCallDepth` admit exactly `maxCallDepth` nested calls,
    // the same count `Interpreter::callFunction` admits. Counting it any other
    // way moves `tests/deep_recursion.algo` and `tests/error_call_depth.algo`
    // by one in opposite directions.
    std::vector<Frame> frames;

    void push(const Value &value, std::size_t at);
    const SpanEntry &entryAt(std::size_t at) const;
};
