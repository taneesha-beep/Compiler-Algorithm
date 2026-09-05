#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include "chunk.h"
#include "disassembler.h"

// Item 4.5. Two hand-built chunks and, beside each, the disassembly written
// out BY HAND — every offset, mnemonic, operand, resolved annotation and
// column. Nothing here asks the disassembler what to expect: the expected text
// is stated a second time and independently, which is the rule item 4.1's one
// surviving mutant bought (a check that derives its expectation from the
// function under test cannot fail).
//
// The second chunk is the one differential testing can never reach. It holds
// an opcode item 4.2 emits nowhere (`POP`, in the first chunk), a byte that is
// no opcode at all, and an operand-bearing opcode with no room for its
// operand. All three have to print rather than throw or be skipped.

namespace
{

int failures = 0;

void check(const char *name, const std::string &actual, const std::string &expected)
{
    if (actual == expected)
        return;
    failures++;
    std::cerr << "FAIL: " << name << "\n--- expected ---\n"
              << expected << "--- actual ---\n"
              << actual << "----------------\n";
}

std::string dumpOf(const Chunk &chunk)
{
    std::ostringstream out;
    disassemble(chunk, out);
    return out.str();
}

// A chunk shaped like a `while` with a call in it — a backward jump, a forward
// jump, both constant types, a call into the function table, and a function
// body after the HALT. Not the output of `compile`: written here so that the
// expected text below can be checked against it by reading, not by running.
Chunk wellFormedChunk()
{
    Chunk chunk;
    chunk.programFrameSize = 2;
    chunk.functions.push_back(FunctionInfo{"f", 24, 1, 2});

    const Span one{1, 1, 1};
    const Span two{2, 5, 3};
    const Span three{3, 9, 1};

    chunk.addConstant(Value::fromInt(7), one);      // #0
    chunk.addConstant(Value::fromBool(true), one);  // #1

    chunk.emitOperand(OpCode::CONST, 0, one);           // 0000
    chunk.emitOperand(OpCode::STORE_LOCAL, 0, one);     // 0003
    chunk.emitOperand(OpCode::LOAD_LOCAL, 0, two);      // 0006
    chunk.emitOperand(OpCode::CONST, 1, two);           // 0009
    chunk.emit(OpCode::LT, two, "<");                   // 0012
    chunk.emitOperand(OpCode::JUMP_IF_FALSE, 23, two);  // 0013
    chunk.emit(OpCode::POP, two);                       // 0016
    chunk.emitOperand(OpCode::CALL, 0, two);            // 0017
    chunk.emitOperand(OpCode::JUMP, 6, two);            // 0020
    chunk.emit(OpCode::HALT, one);                      // 0023
    chunk.emitOperand(OpCode::LOAD_LOCAL, 0, three);    // 0024, f's body
    chunk.emit(OpCode::RETURN, three);                  // 0027
    return chunk;
}

// A chunk no compiler could produce: a byte outside the nineteen, and a CONST
// with one byte of the chunk left behind it.
Chunk corruptChunk()
{
    Chunk chunk;
    chunk.code.push_back(0x7F);
    chunk.emit(OpCode::HALT, Span{9, 1, 4});
    chunk.code.push_back(static_cast<std::uint8_t>(OpCode::CONST));
    return chunk;
}

} // namespace

int main()
{
    check("well-formed chunk", dumpOf(wellFormedChunk()),
          "== chunk ==\n"
          "  code: 28 bytes  constants: 2  functions: 1  program frame: 2 slots\n"
          "\n"
          "== constants ==\n"
          "  #0  7\n"
          "  #1  true\n"
          "\n"
          "== functions ==\n"
          "  #0  f  arity 1  frame 2  entry 0024\n"
          "\n"
          "== code ==\n"
          "  0000  line   1  CONST             0  ; 7\n"
          "  0003  line   1  STORE_LOCAL       0\n"
          "  0006  line   2  LOAD_LOCAL        0\n"
          "  0009  line   2  CONST             1  ; true\n"
          "  0012  line   2  LT\n"
          "  0013  line   2  JUMP_IF_FALSE    23  ; -> 0023\n"
          "  0016  line   2  POP\n"
          "  0017  line   2  CALL              0  ; f/1\n"
          "  0020  line   2  JUMP              6  ; -> 0006  (backward)\n"
          "  0023  line   1  HALT\n"
          "  f:\n"
          "  0024  line   3  LOAD_LOCAL        0\n"
          "  0027  line   3  RETURN\n");

    check("corrupt chunk", dumpOf(corruptChunk()),
          "== chunk ==\n"
          "  code: 3 bytes  constants: 0  functions: 0  program frame: 0 slots\n"
          "\n"
          "== constants ==\n"
          "  (none)\n"
          "\n"
          "== functions ==\n"
          "  (none)\n"
          "\n"
          "== code ==\n"
          "  0000  line   -  <unknown opcode 0x7f>\n"
          "  0001  line   9  HALT\n"
          "  0002  line   -  <truncated: CONST needs 3 bytes, 1 available>\n");

    if (failures == 0)
        std::cout << "disassembler_test: all checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
