#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "diagnostic.h" // CompileError
#include "token.h"      // Span
#include "value.h"

// ============================================================
// STAGE 5: BYTECODE — the chunk, the instruction set, and the
// span table that keeps a fault's diagnostic alive once the
// AST is gone
// ============================================================
//
// This header is the *format*. Item 4.2 writes chunks through the emit helpers
// below, item 4.3 executes them, item 4.5 prints them. Nothing here executes
// anything, and nothing in the tree-walker includes this file: the two engines
// share the front end and the `Value`, and nothing else.
//
// ON THE ONE THING THAT IS NOT NEGOTIABLE. Item 4.4 registers every golden case
// against both engines, so the VM has to produce the same bytes on stdout, the
// same rendered diagnostic on stderr and the same exit code as the tree-walker,
// on every program in the repository. Everything that looks over-careful in
// this file is there because of that sentence — the span table, the operator
// spelling beside it, and the note on which span each instruction carries.

// The nineteen opcodes the roadmap names, in the order it names them.
//
// ON THEIR NUMERIC VALUES. Assigned by declaration order, and deliberately not
// promised to anyone: nothing writes a chunk to a file, so a chunk never
// outlives the process that built it and there is no compatibility obligation
// to a byte stream. Contrast `BinOpKind` in `src/ast.h`, whose order *is*
// load-bearing because item 3.3's per-operation model is fitted against the
// positions of the string chain it replaced. Nothing is fitted against these.
enum class OpCode : std::uint8_t
{
    // Constants, locals, and the stack
    CONST,
    LOAD_LOCAL,
    STORE_LOCAL,
    POP,

    // Arithmetic. All five trap; see `spanFor` below and `docs/BYTECODE.md`.
    ADD,
    SUB,
    MUL,
    DIV,
    NEG,

    // Comparison and logical negation. Three of the language's six comparison
    // operators have no opcode and are lowered onto these; see the note on
    // `SpanEntry::op`.
    EQ,
    LT,
    GT,
    NOT,

    // Control flow. Both take an ABSOLUTE target offset, so one JUMP serves
    // both the forward jump out of an `if` and the backward jump to a `while`
    // header.
    JUMP,
    JUMP_IF_FALSE,

    // Calls, output, and the end of the program
    CALL,
    RETURN,
    PRINT,
    HALT
};

// The mnemonic, for item 4.5's disassembler and for nothing else. Written out
// rather than derived, because a table indexed by the enumerator is the one
// form that a reordering of the enum cannot silently corrupt.
inline const char *opCodeName(OpCode op)
{
    switch (op)
    {
    case OpCode::CONST: return "CONST";
    case OpCode::LOAD_LOCAL: return "LOAD_LOCAL";
    case OpCode::STORE_LOCAL: return "STORE_LOCAL";
    case OpCode::POP: return "POP";
    case OpCode::ADD: return "ADD";
    case OpCode::SUB: return "SUB";
    case OpCode::MUL: return "MUL";
    case OpCode::DIV: return "DIV";
    case OpCode::NEG: return "NEG";
    case OpCode::EQ: return "EQ";
    case OpCode::LT: return "LT";
    case OpCode::GT: return "GT";
    case OpCode::NOT: return "NOT";
    case OpCode::JUMP: return "JUMP";
    case OpCode::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OpCode::CALL: return "CALL";
    case OpCode::RETURN: return "RETURN";
    case OpCode::PRINT: return "PRINT";
    case OpCode::HALT: return "HALT";
    }
    return "?";
}

// Whether the opcode is followed by a two-byte operand. Five of the nineteen
// are; the other fourteen are one byte on their own. A decoder — the VM's
// fetch, the disassembler's walk — needs this to know how far the next
// instruction is, so it lives beside the enum rather than in either of them.
inline bool opCodeHasOperand(OpCode op)
{
    switch (op)
    {
    case OpCode::CONST:
    case OpCode::LOAD_LOCAL:
    case OpCode::STORE_LOCAL:
    case OpCode::JUMP:
    case OpCode::JUMP_IF_FALSE:
    case OpCode::CALL:
        return true;
    default:
        return false;
    }
}

// The width of an encoded instruction, in bytes: 1 or 3.
inline std::size_t instructionLength(OpCode op)
{
    return opCodeHasOperand(op) ? 3 : 1;
}

// The largest value an operand can hold, and so the largest constant index,
// slot index, function index and jump target a chunk can express. Also the
// largest a chunk's `code` may grow: a jump target is an absolute offset, so a
// chunk whose code exceeded this would have offsets no jump could name.
inline constexpr std::size_t maxOperand = 0xFFFF;

// ON WHY THIS IS A SPAN TABLE AND NOT A LINE TABLE.
//
// The roadmap calls it "a line table mapping instruction offsets back to source
// lines". A line number is not enough, and the gap is not cosmetic:
// `src/diagnostic.h` renders `path:line:col`, echoes the source line, and
// underlines the offending text with a caret under its first character and a
// tilde under each remaining one. That underline is `Span::len` characters
// long, and `tests/diagnostic_test.cpp` pins those bytes. A table carrying only
// a line could reproduce neither the column nor the tilde run, so item 4.3's
// acceptance — identical output from both engines — would fail on every program
// that faults, and it would fail *after* two items had been built on the
// format. So the table carries the whole `Span`.
//
// ON `op`. Three of the language's six comparison operators are lowered onto
// the opcodes of the other three (see `docs/BYTECODE.md`), which means an
// opcode no longer determines the operator a diagnostic must quote: an `LT` is
// either a source `<` or the first half of a source `>=`. Four of the
// interpreter's fault messages quote that spelling — the two "operator '<op>'
// cannot be applied to …" forms and the two "integer overflow in …" forms — so
// the instruction has to carry it. It is the spelling as the programmer wrote
// it, which is exactly what `BinOpNode::op` and `UnaryOpNode::op` hold, and
// those two fields are kept on the nodes for the same reason (see `src/ast.h`).
//
// It is a `const char *` into a string literal or into the AST, both of which
// outlive the chunk. Nothing here owns it.
struct SpanEntry
{
    std::uint32_t offset = 0; // offset of the instruction's OPCODE byte
    Span span;                // what a diagnostic for this instruction underlines
    const char *op = nullptr; // operator spelling for a message that quotes one
};

// A function, resolved. Item 4.2 consumes the resolved AST, so a call site
// knows which function it names at compile time and CALL carries an index into
// this table rather than a name — which is what the note on `Interpreter`'s
// `functions` map means by "Phase 4's VM resolves a callee at compile time and
// gets it for nothing". There is no closure and no first-class function (the
// roadmap's Out of scope table), so nothing else can ever appear here.
//
// The table also decouples a call site from code layout: `entry` is filled in
// when the body is emitted, so a call compiled before its callee's body needs
// no backpatching. Backpatching remains what item 4.2 does for jumps.
struct FunctionInfo
{
    std::string name;             // for the disassembler; the VM never reads it
    std::uint32_t entry = 0;      // offset of the body's first instruction
    std::uint16_t arity = 0;      // parameters, which occupy slots 0..arity-1
    std::uint16_t frameSize = 0;  // total slots, from `FunctionNode::frameSize`
};

// One chunk per program, not one per function. Item 4.3's frame is
// `{ returnIP, slotBase }` — a single return address, with no chunk to switch
// to — so every function's body is emitted into this one `code` vector and a
// call is a jump within it.
struct Chunk
{
    std::vector<std::uint8_t> code;
    std::vector<Value> constants;

    // One entry per instruction, in emission order and therefore sorted by
    // offset. Not run-length encoded: the table is read only when a fault is
    // raised, never on the dispatch path, so there is nothing to buy by
    // compressing it and a decoder to get wrong if we did.
    std::vector<SpanEntry> spans;

    std::vector<FunctionInfo> functions;

    // How many slots the program's own frame needs — `resolve()`'s return
    // value, the same number `Interpreter::execute` takes as a parameter. The
    // VM reserves this many slots below the operand stack before running the
    // instruction at offset 0.
    int programFrameSize = 0;

    // ---- writing -------------------------------------------------------
    //
    // Every emit appends exactly one `SpanEntry`, which is what makes the table
    // one-per-instruction and sorted without anyone having to maintain either
    // property. Both return the offset of the opcode byte: item 4.2 keeps that
    // offset to backpatch a jump, and item 4.3 keeps it to look a fault up.

    std::size_t emit(OpCode op, Span span, const char *opText = nullptr)
    {
        const std::size_t offset = code.size();
        writeByte(static_cast<std::uint8_t>(op), span);
        spans.push_back(SpanEntry{static_cast<std::uint32_t>(offset), span, opText});
        return offset;
    }

    std::size_t emitOperand(OpCode op, std::uint16_t operand, Span span,
                            const char *opText = nullptr)
    {
        const std::size_t offset = code.size();
        writeByte(static_cast<std::uint8_t>(op), span);
        // Little-endian, low byte first, written a byte at a time. Not a store
        // through a `std::uint16_t *`: that would be host byte order and an
        // unaligned access, so the same source would produce different chunks
        // on different machines and the disassembly in the README would be a
        // property of whoever built it.
        writeByte(static_cast<std::uint8_t>(operand & 0xFF), span);
        writeByte(static_cast<std::uint8_t>((operand >> 8) & 0xFF), span);
        spans.push_back(SpanEntry{static_cast<std::uint32_t>(offset), span, opText});
        return offset;
    }

    // The operand of the instruction whose opcode byte is at `offset`.
    std::uint16_t readOperand(std::size_t offset) const
    {
        return static_cast<std::uint16_t>(code[offset + 1]) |
               static_cast<std::uint16_t>(static_cast<std::uint16_t>(code[offset + 2]) << 8);
    }

    // Backpatching: overwrite the operand of an already-emitted instruction,
    // which is how item 4.2 fills in a forward jump once its target is known.
    // Adds no span entry — the instruction already has one, and the span it
    // carries is the source construct's, which patching a target does not
    // change.
    void patchOperand(std::size_t offset, std::uint16_t operand)
    {
        code[offset + 1] = static_cast<std::uint8_t>(operand & 0xFF);
        code[offset + 2] = static_cast<std::uint8_t>((operand >> 8) & 0xFF);
    }

    // Appends a constant and returns its index. Does not deduplicate: two
    // occurrences of `1` get two pool entries. Deduplicating is a size
    // optimisation of a table that is read once per CONST, and it would make a
    // disassembly harder to read against its source, so it is left to be done
    // deliberately if it is ever wanted.
    //
    // `span` is only used to place the caret if the pool overflows.
    std::uint16_t addConstant(const Value &value, Span span)
    {
        if (constants.size() > maxOperand)
            throw CompileError(Diagnostic{Severity::Error, span,
                                          "too many constants in one program"});
        constants.push_back(value);
        return static_cast<std::uint16_t>(constants.size() - 1);
    }

    // ---- reading -------------------------------------------------------

    // The entry for the instruction *containing* `offset`, or null if the
    // chunk is empty. The search is for the greatest entry at or before
    // `offset`, so an operand byte's offset finds its own instruction as well
    // as the opcode byte's does — which matters because it makes the lookup
    // insensitive to whether item 4.3 saves the instruction pointer before or
    // part-way through decoding. What it will NOT survive is being handed the
    // instruction pointer *after* the decode: that is the next instruction's
    // offset exactly, and it names the next instruction. The VM saves the
    // opcode's own offset before decoding; that is the contract.
    const SpanEntry *spanAt(std::size_t offset) const
    {
        const auto after = std::upper_bound(
            spans.begin(), spans.end(), offset,
            [](std::size_t value, const SpanEntry &entry) { return value < entry.offset; });
        if (after == spans.begin())
            return nullptr;
        return &*(after - 1);
    }

private:
    // The one place `code` grows, so the size limit is checked once. A chunk
    // larger than this has offsets that no jump operand could name, and the
    // failure without the check is a silently truncated jump target — a wrong
    // answer with nothing to report it, which is the class of failure this
    // project exists to refuse. It is a `CompileError` (65) rather than a
    // runtime fault because it is settled by the source text alone, which is
    // the rule `CLAUDE.md`'s *Output discipline* states.
    void writeByte(std::uint8_t byte, Span span)
    {
        if (code.size() >= maxOperand)
            throw CompileError(Diagnostic{Severity::Error, span,
                                          "program too large for one chunk"});
        code.push_back(byte);
    }
};
