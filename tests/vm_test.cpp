#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "chunk.h"
#include "diagnostic.h"
#include "value.h"
#include "vm.h"

// ============================================================
// tests/vm_test.cpp — item 4.3
// ============================================================
//
// Same shape as the other unit tests: link algo_core, print failures to stderr,
// exit non-zero. No third-party framework.
//
// ON WHAT THIS BINARY IS FOR, WHICH IS NARROWER THAN "THE VM".
//
// Item 4.4 registers every golden case against both engines, so almost
// everything this machine does is about to be checked against the tree-walker
// on 29 real programs — and a differential test over real programs is a far
// better check of a dispatch loop than any chunk written out here would be.
// What this binary holds is the part 4.4 CANNOT reach, plus the one trap
// `docs/BYTECODE.md` says a test would not notice:
//
//   * `POP`. The language has no expression statements, so item 4.2 emits it
//     nowhere and no `.algo` program can execute it. `docs/BYTECODE.md` records
//     that as a known coverage gap rather than an oversight, and the arm exists
//     because item 4.5 must print something for any byte it meets.
//   * The operand-stack guard. It is the VM's own resource and the tree-walker
//     has no counterpart, so there is nothing to differ from — and no program in
//     this repository can reach it, `maxStackSlots` being far above what 1000
//     frames of these programs occupy.
//   * The unknown-opcode arm, for the same reason: item 4.2 writes no such byte.
//   * That a fault resolves the span of the instruction that raised it and not
//     of the one after it. Item 4.4 would catch this on a faulting case, but
//     only because the caret moved; here it is stated directly, and the chunks
//     are built so that the wrong answer is the *next* instruction's span.
//
// ON WRITING THE EXPECTATIONS OUT BY HAND. Item 4.1's first mutation pass had
// one survivor because a check asked the code under test what to expect. So the
// spans, the messages and the printed bytes below are written literally, not
// read back out of the chunk or built from a helper the VM also uses.

namespace
{

int checks = 0;
int failures = 0;

void check(bool condition, const std::string &what)
{
    checks++;
    if (condition)
        return;
    std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    failures++;
}

void checkEqual(const std::string &what, const std::string &actual,
                const std::string &expected)
{
    checks++;
    if (actual == expected)
        return;
    std::fprintf(stderr, "  FAIL: %s\n    expected [%s]\n    actual   [%s]\n",
                 what.c_str(), expected.c_str(), actual.c_str());
    failures++;
}

void checkEqual(const std::string &what, long long actual, long long expected)
{
    checks++;
    if (actual == expected)
        return;
    std::fprintf(stderr, "  FAIL: %s: expected %lld, got %lld\n", what.c_str(),
                 actual, expected);
    failures++;
}

// Runs a chunk with stdout captured, so that a `PRINT` can be compared against
// the bytes it should have written. Restores the buffer however it leaves.
std::string runCapturingStdout(const Chunk &chunk)
{
    std::ostringstream captured;
    std::streambuf *saved = std::cout.rdbuf(captured.rdbuf());
    struct Restore
    {
        std::streambuf *saved;
        ~Restore() { std::cout.rdbuf(saved); }
    } restore{saved};

    VM vm;
    vm.run(chunk);
    std::cout.flush();
    return captured.str();
}

// ---- POP ---------------------------------------------------------------
//
// The one arm no `.algo` program in this repository executes. Three constants
// are pushed, the top one discarded, and the remaining two printed — so the
// check sees both that `POP` removed one value and that it removed the *top*
// one and left the rest in order.
void popDiscardsTheTopValueAndNothingElse()
{
    Chunk chunk;
    const Span anywhere{1, 1, 1};
    const std::uint16_t seven = chunk.addConstant(Value::fromInt(7), anywhere);
    const std::uint16_t eight = chunk.addConstant(Value::fromInt(8), anywhere);
    const std::uint16_t nine = chunk.addConstant(Value::fromInt(9), anywhere);

    chunk.emitOperand(OpCode::CONST, seven, anywhere);
    chunk.emitOperand(OpCode::CONST, eight, anywhere);
    chunk.emitOperand(OpCode::CONST, nine, anywhere);
    chunk.emit(OpCode::POP, anywhere);
    chunk.emit(OpCode::PRINT, anywhere);
    chunk.emit(OpCode::PRINT, anywhere);
    chunk.emit(OpCode::HALT, anywhere);

    checkEqual("POP discards the top value and leaves the rest in order",
               runCapturingStdout(chunk), "8\n7\n");
}

// ---- the operand-stack guard -------------------------------------------
//
// `CONST` then a backward `JUMP` to offset 0: a loop that pushes forever and
// pops nothing, which is the only way to exhaust the operand stack in a chunk
// this format admits. No `.algo` program can express it — the language has no
// expression statements, so nothing leaves a value on the stack across a
// statement boundary.
//
// The check is on all three things item 4.3 had to decide: the message, the
// class of the exception (which is what decides exit 70 rather than 65), and
// that the caret lands on the instruction that could not push.
void theOperandStackIsGuarded()
{
    Chunk chunk;
    const Span pushSite{12, 5, 3};
    const Span jumpSite{13, 1, 9};
    const std::uint16_t one = chunk.addConstant(Value::fromInt(1), pushSite);

    chunk.emitOperand(OpCode::CONST, one, pushSite);
    chunk.emitOperand(OpCode::JUMP, 0, jumpSite);

    bool faulted = false;
    try
    {
        VM vm;
        vm.run(chunk);
    }
    catch (const RuntimeFault &fault)
    {
        faulted = true;
        checkEqual("the operand-stack guard's message", fault.diagnostic().message,
                   "operand stack exhausted");
        // The instruction that could not push is the CONST at offset 0, whose
        // span is written out by hand above. The JUMP's span is the wrong
        // answer and is deliberately different in all three fields.
        checkEqual("the guard's caret line", fault.diagnostic().span.line, 12);
        checkEqual("the guard's caret column", fault.diagnostic().span.col, 5);
        checkEqual("the guard's caret length", fault.diagnostic().span.len, 3);
    }
    catch (const CompileError &)
    {
        // Named separately rather than folded into a catch-all: the class is
        // the decision, because it is what `src/main.cpp` turns into an exit
        // code. Exhausting the stack depends on what the program computed, so
        // it is a RuntimeFault (70) like `call depth exceeded`, not a
        // CompileError (65).
        check(false, "the operand-stack guard raises RuntimeFault, not CompileError");
        faulted = true;
    }
    check(faulted, "an unbounded push loop faults rather than running out of memory");
}

// ---- the span contract --------------------------------------------------
//
// `Chunk::spanAt` returns the greatest entry at or before an offset, so the
// instruction pointer *after* a decode resolves to the NEXT instruction. The VM
// has to save the opcode's own offset before decoding, and `docs/BYTECODE.md`
// calls that "the one thing about this table item 4.3 can get wrong without a
// test noticing".
//
// The chunk is arranged so that getting it wrong is visible twice over: `ADD`
// is one byte, so the pointer after its decode is exactly `PRINT`'s offset —
// and `PRINT` carries a different span and no operator spelling at all, so a
// VM reading the wrong entry would render the wrong caret *and*
// `operator '' cannot be applied to ...`.
void aFaultResolvesItsOwnInstructionAndNotTheNext()
{
    Chunk chunk;
    const Span addSite{7, 3, 5};
    const Span printSite{9, 1, 11};
    const std::uint16_t one = chunk.addConstant(Value::fromInt(1), addSite);
    const std::uint16_t yes = chunk.addConstant(Value::fromBool(true), addSite);

    chunk.emitOperand(OpCode::CONST, one, addSite);
    chunk.emitOperand(OpCode::CONST, yes, addSite);
    chunk.emit(OpCode::ADD, addSite, "+");
    chunk.emit(OpCode::PRINT, printSite);
    chunk.emit(OpCode::HALT, printSite);

    bool faulted = false;
    try
    {
        VM vm;
        vm.run(chunk);
    }
    catch (const RuntimeFault &fault)
    {
        faulted = true;
        // Byte for byte what `binaryTypeFault` in `src/interpreter.cpp` renders
        // for `1 + true`, written out here rather than borrowed from either
        // engine.
        checkEqual("a type fault names the operator the programmer wrote",
                   fault.diagnostic().message,
                   "operator '+' cannot be applied to integer and boolean");
        checkEqual("the fault's caret line", fault.diagnostic().span.line, 7);
        checkEqual("the fault's caret column", fault.diagnostic().span.col, 3);
        checkEqual("the fault's caret length", fault.diagnostic().span.len, 5);
    }
    check(faulted, "ADD on an integer and a boolean faults");
}

// ---- the unknown-opcode arm ---------------------------------------------
//
// Item 4.2 writes no byte outside the nineteen, so this is unreachable from any
// program. It is checked because the arm exists to make a corrupt chunk report
// itself rather than fall through into undefined behaviour, and an arm nothing
// ever enters is an arm nothing ever proves.
void anUnknownOpcodeFaultsRatherThanRunningOn()
{
    Chunk chunk;
    const Span anywhere{3, 2, 4};
    chunk.emit(OpCode::HALT, anywhere);
    chunk.code[0] = 200; // outside the nineteen; the span entry stays

    bool faulted = false;
    try
    {
        VM vm;
        vm.run(chunk);
    }
    catch (const RuntimeFault &fault)
    {
        faulted = true;
        checkEqual("an unknown opcode's message", fault.diagnostic().message,
                   "unknown opcode");
    }
    check(faulted, "a byte outside the nineteen opcodes faults");
}

} // namespace

int main()
{
    popDiscardsTheTopValueAndNothingElse();
    theOperandStackIsGuarded();
    aFaultResolvesItsOwnInstructionAndNotTheNext();
    anUnknownOpcodeFaultsRatherThanRunningOn();

    if (failures != 0)
    {
        std::fprintf(stderr, "vm_test: %d of %d checks failed\n", failures, checks);
        return 1;
    }

    std::printf("vm_test: %d checks passed\n", checks);
    return 0;
}
