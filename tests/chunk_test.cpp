// Unit checks for the chunk format — roadmap item 4.1.
//
// Nothing executes bytecode until item 4.3, so there is no behaviour a golden
// case could compare and no `.algo` file that reaches this code. What there is
// is a *format*, and three parts of it are real logic that the rest of Phase 4
// is built on top of: the little-endian operand encoding, the backpatch that
// overwrites it, and the binary search over the span table. Each has a silent
// failure mode — a byte-swapped jump target, a half-patched operand, an
// off-by-one that attributes a fault to the neighbouring instruction — and each
// would surface at item 4.3 as a wrong answer or a caret under the wrong text,
// with two items already resting on the format. So the round-trip is checked
// here, where the diagnosis is one file wide.
//
// Same shape as the other unit tests: link algo_core, print failures to stderr,
// exit non-zero. No third-party framework.

#include <cstdio>
#include <string>
#include <vector>

#include "chunk.h"
#include "token.h"
#include "value.h"

namespace
{

int checks = 0;
int failures = 0;

void check(bool condition, const std::string &what)
{
    checks++;
    if (condition)
        return;
    failures++;
    std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

void checkEqual(const std::string &what, long long actual, long long expected)
{
    checks++;
    if (actual == expected)
        return;
    failures++;
    std::fprintf(stderr, "FAIL %s: expected %lld, got %lld\n",
                 what.c_str(), expected, actual);
}

// The nineteen, in the order `src/chunk.h` declares them, each with the width
// `docs/BYTECODE.md` says it has.
//
// ON WHY THE SECOND COLUMN IS WRITTEN OUT BY HAND. It is the whole point of
// the table. A check that asked `opCodeHasOperand` which form to emit and then
// compared the result against `instructionLength` would be comparing that
// function with itself, and a mutant that moved an opcode from one group to
// the other would satisfy it — which is exactly what happened when this test
// was first mutation-checked: dropping `CALL` from `opCodeHasOperand` survived.
// These booleans are a second, independent statement of the same fact, so the
// two have to be wrong in the same way to agree.
struct OpCodeSpec
{
    OpCode op;
    bool hasOperand;
};

const OpCodeSpec allOpCodes[] = {
    {OpCode::CONST, true},
    {OpCode::LOAD_LOCAL, true},
    {OpCode::STORE_LOCAL, true},
    {OpCode::POP, false},
    {OpCode::ADD, false},
    {OpCode::SUB, false},
    {OpCode::MUL, false},
    {OpCode::DIV, false},
    {OpCode::NEG, false},
    {OpCode::EQ, false},
    {OpCode::LT, false},
    {OpCode::GT, false},
    {OpCode::NOT, false},
    {OpCode::JUMP, true},
    {OpCode::JUMP_IF_FALSE, true},
    {OpCode::CALL, true},
    {OpCode::RETURN, false},
    {OpCode::PRINT, false},
    {OpCode::HALT, false}};

constexpr int opCodeCount = static_cast<int>(sizeof(allOpCodes) / sizeof(allOpCodes[0]));

Span at(int line, int col, int len) { return Span{line, col, len}; }

// An operand is written low byte first, and by hand rather than through a
// `std::uint16_t *`, so the bytes must be these on every machine that builds
// this repository.
void operandsAreLittleEndian()
{
    Chunk chunk;
    const std::size_t offset = chunk.emitOperand(OpCode::CONST, 0x1234, at(1, 1, 1));

    checkEqual("the instruction starts at offset 0", static_cast<long long>(offset), 0);
    checkEqual("it occupies three bytes", static_cast<long long>(chunk.code.size()), 3);
    checkEqual("byte 0 is the opcode",
               chunk.code[0], static_cast<long long>(static_cast<std::uint8_t>(OpCode::CONST)));
    checkEqual("byte 1 is the LOW byte of the operand", chunk.code[1], 0x34);
    checkEqual("byte 2 is the HIGH byte of the operand", chunk.code[2], 0x12);
    checkEqual("and readOperand puts them back together",
               chunk.readOperand(offset), 0x1234);
}

// Every value an operand can hold survives the round trip, including the two
// that a byte-swap leaves looking correct (0x0000 and 0xFFFF) and the two that
// a one-byte write leaves looking correct (anything under 0x0100).
void everyOperandValueRoundTrips()
{
    const std::uint16_t values[] = {0, 1, 0x00FF, 0x0100, 0x0101, 0xFF00, 0xFFFE, 0xFFFF};

    Chunk chunk;
    std::vector<std::size_t> offsets;
    for (std::uint16_t value : values)
        offsets.push_back(chunk.emitOperand(OpCode::JUMP, value, at(1, 1, 1)));

    for (std::size_t i = 0; i < offsets.size(); i++)
        checkEqual("operand " + std::to_string(values[i]) + " round trips",
                   chunk.readOperand(offsets[i]), values[i]);
}

// Backpatching is what item 4.2 does to a forward jump once its target is
// known. It must overwrite BOTH operand bytes and touch neither neighbour.
void patchingOverwritesTheWholeOperandAndNothingElse()
{
    Chunk chunk;
    chunk.emit(OpCode::HALT, at(1, 1, 1));
    const std::size_t jump = chunk.emitOperand(OpCode::JUMP_IF_FALSE, 0xFFFF, at(2, 1, 1));
    chunk.emit(OpCode::HALT, at(3, 1, 1));

    chunk.patchOperand(jump, 0x0102);

    checkEqual("the patched operand reads back", chunk.readOperand(jump), 0x0102);
    checkEqual("its low byte was written", chunk.code[jump + 1], 0x02);
    checkEqual("its high byte was written", chunk.code[jump + 2], 0x01);
    checkEqual("the opcode byte is untouched", chunk.code[jump],
               static_cast<long long>(static_cast<std::uint8_t>(OpCode::JUMP_IF_FALSE)));
    checkEqual("the instruction before is untouched", chunk.code[jump - 1],
               static_cast<long long>(static_cast<std::uint8_t>(OpCode::HALT)));
    checkEqual("the instruction after is untouched", chunk.code[jump + 3],
               static_cast<long long>(static_cast<std::uint8_t>(OpCode::HALT)));
    checkEqual("patching appended no span entry",
               static_cast<long long>(chunk.spans.size()), 3);
}

// `opCodeHasOperand` and `instructionLength` are what a decoder walks the code
// with — item 4.3's fetch and item 4.5's disassembler both. If either disagreed
// with what `emit` actually wrote, a decoder would fall out of step with the
// instruction stream and read an operand byte as an opcode.
void theDeclaredWidthIsTheWidthEmitted()
{
    for (int i = 0; i < opCodeCount; i++)
    {
        const OpCode op = allOpCodes[i].op;
        const bool hasOperand = allOpCodes[i].hasOperand;
        const std::string name = opCodeName(op);

        check(opCodeHasOperand(op) == hasOperand,
              name + " is classified as documented");
        checkEqual(name + " has its documented width",
                   static_cast<long long>(instructionLength(op)), hasOperand ? 3 : 1);

        Chunk chunk;
        if (hasOperand)
            chunk.emitOperand(op, 7, at(1, 1, 1));
        else
            chunk.emit(op, at(1, 1, 1));

        checkEqual(name + " emits that many bytes",
                   static_cast<long long>(chunk.code.size()), hasOperand ? 3 : 1);
    }
}

// A reordering of the enum that did not move `opCodeName`'s table with it would
// silently mislabel every disassembly. Distinct, non-empty names are the cheap
// half of that guard; the widths above are the other half.
void everyOpCodeHasItsOwnName()
{
    checkEqual("there are nineteen opcodes", opCodeCount, 19);

    for (int i = 0; i < opCodeCount; i++)
    {
        const std::string name = opCodeName(allOpCodes[i].op);
        check(!name.empty() && name != "?",
              "opcode " + std::to_string(i) + " has a name");
        for (int j = i + 1; j < opCodeCount; j++)
            check(name != opCodeName(allOpCodes[j].op),
                  "opcode " + name + " does not share its name with opcode " +
                      std::to_string(j));
    }
}

// The table is one entry per instruction, in emission order, and every byte of
// an instruction — its opcode byte and both operand bytes — resolves to that
// instruction's own entry. The last check is the one that matters at item 4.3:
// a lookup that resolved an operand byte to the *next* entry would put the
// caret under the next line of the program.
void everyByteOfAnInstructionFindsItsOwnSpan()
{
    Chunk chunk;
    const std::size_t first = chunk.emitOperand(OpCode::CONST, 0x0201, at(4, 9, 3), "+");
    const std::size_t second = chunk.emit(OpCode::ADD, at(4, 5, 7), "+");
    const std::size_t third = chunk.emitOperand(OpCode::JUMP, 0, at(9, 1, 5));

    checkEqual("three instructions, three entries",
               static_cast<long long>(chunk.spans.size()), 3);
    checkEqual("offsets are the emitted ones", static_cast<long long>(first), 0);
    checkEqual("offsets are the emitted ones", static_cast<long long>(second), 3);
    checkEqual("offsets are the emitted ones", static_cast<long long>(third), 4);

    check(chunk.spanAt(0) == &chunk.spans[0], "the first opcode byte finds entry 0");
    check(chunk.spanAt(1) == &chunk.spans[0], "its low operand byte finds entry 0");
    check(chunk.spanAt(2) == &chunk.spans[0], "its high operand byte finds entry 0");
    check(chunk.spanAt(3) == &chunk.spans[1], "the one-byte instruction finds entry 1");
    check(chunk.spanAt(4) == &chunk.spans[2], "the third instruction finds entry 2");
    check(chunk.spanAt(5) == &chunk.spans[2], "and so do both of its operand bytes");
    check(chunk.spanAt(6) == &chunk.spans[2], "and so do both of its operand bytes");

    // The whole Span, not a line number: `src/diagnostic.h` underlines `len`
    // characters starting at `col`, and `tests/diagnostic_test.cpp` pins those
    // bytes. This is the check that a line table would fail.
    const SpanEntry *entry = chunk.spanAt(3);
    checkEqual("the entry keeps its line", entry->span.line, 4);
    checkEqual("the entry keeps its column", entry->span.col, 5);
    checkEqual("the entry keeps its length", entry->span.len, 7);
    check(entry->op != nullptr && std::string(entry->op) == "+",
          "and the operator spelling a fault message would quote");
    check(chunk.spanAt(4)->op == nullptr,
          "an instruction whose faults quote no operator carries none");
}

void anEmptyChunkResolvesToNothing()
{
    Chunk chunk;
    check(chunk.spanAt(0) == nullptr, "an empty chunk has no span for offset 0");
}

// The pool is appended to and not deduplicated, so an index names the
// occurrence it was made for. Item 4.5 prints `CONST 3 ; 7` by following it.
void constantsAreAppendedInOrder()
{
    Chunk chunk;
    const std::uint16_t zero = chunk.addConstant(Value::fromInt(0), at(1, 1, 1));
    const std::uint16_t one = chunk.addConstant(Value::fromInt(7), at(1, 1, 1));
    const std::uint16_t two = chunk.addConstant(Value::fromBool(true), at(1, 1, 1));
    const std::uint16_t three = chunk.addConstant(Value::fromInt(7), at(1, 1, 1));

    checkEqual("the first constant is index 0", zero, 0);
    checkEqual("the second is index 1", one, 1);
    checkEqual("the third is index 2", two, 2);
    checkEqual("a repeated value is NOT deduplicated", three, 3);
    checkEqual("the pool holds them all", static_cast<long long>(chunk.constants.size()), 4);
    check(chunk.constants[1].isInt() && chunk.constants[1].integer == 7,
          "and index 1 is the value it was made for");
    check(chunk.constants[2].isBool() && chunk.constants[2].boolean,
          "booleans go in the pool too, there being no TRUE opcode");
}

} // namespace

int main()
{
    operandsAreLittleEndian();
    everyOperandValueRoundTrips();
    patchingOverwritesTheWholeOperandAndNothingElse();
    theDeclaredWidthIsTheWidthEmitted();
    everyOpCodeHasItsOwnName();
    everyByteOfAnInstructionFindsItsOwnSpan();
    anEmptyChunkResolvesToNothing();
    constantsAreAppendedInOrder();

    if (failures != 0)
    {
        std::fprintf(stderr, "chunk_test: %d of %d checks failed\n", failures, checks);
        return 1;
    }

    std::printf("chunk_test: %d checks passed\n", checks);
    return 0;
}
