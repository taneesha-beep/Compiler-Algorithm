#pragma once

#include <iosfwd>

#include "chunk.h"

// ============================================================
// STAGE 5b: THE DISASSEMBLER — a chunk, printed
// ============================================================
//
// Item 4.5. Reads `opCodeName`, `opCodeHasOperand`, `instructionLength`, the
// constant pool, the function table and the span table; a jump operand is
// already the absolute target, so there is nothing to compute. It executes
// nothing and measures nothing.
//
// ON THE STREAM. The caller passes one, and `src/main.cpp` passes `std::cout`.
// `--trace` narration goes to stderr because it is the compiler talking about
// itself *while the program runs*, and stdout has to stay the program's own
// output. `--dump` is the opposite case: it replaces the run, so there is no
// program output to keep clear of, and the disassembly is the thing the user
// asked the tool to produce. It is also what makes `algo --dump f.algo > x`
// the way to capture an excerpt.
//
// ON PRINTING EVERY BYTE. The walk never throws and never skips. A byte
// outside the nineteen opcodes prints as `<unknown opcode 0x..>` and advances
// one byte; an operand-bearing opcode with fewer than two bytes behind it
// prints as `<truncated: ...>` and ends the walk. Neither can occur in a chunk
// item 4.2 wrote — this is a debugging tool, and a debugging tool that dies on
// the corrupt input you reached for it to inspect is the wrong tool.
void disassemble(const Chunk &chunk, std::ostream &out);
