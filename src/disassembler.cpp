#include "disassembler.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace
{

std::string padRight(std::string text, std::size_t width)
{
    while (text.size() < width)
        text.push_back(' ');
    return text;
}

std::string padLeft(std::string text, std::size_t width)
{
    while (text.size() < width)
        text.insert(text.begin(), ' ');
    return text;
}

// Offsets are printed four digits wide and zero-padded, in decimal, so that a
// jump's annotated target reads back against the offset column literally. A
// chunk is capped at 65535 bytes (`maxOperand`), so five digits is the widest
// this can ever need and four covers every program in the repository.
std::string offsetLabel(std::size_t offset)
{
    std::string digits = std::to_string(offset);
    while (digits.size() < 4)
        digits.insert(digits.begin(), '0');
    return digits;
}

std::string hexByte(std::uint8_t byte)
{
    const char *digits = "0123456789abcdef";
    std::string text = "0x";
    text.push_back(digits[byte >> 4]);
    text.push_back(digits[byte & 0x0F]);
    return text;
}

// The tree-walker's and the VM's `PRINT` format, so a pool entry reads the way
// the value would if the program printed it.
std::string formatValue(const Value &value)
{
    if (value.isInt())
        return std::to_string(value.integer);
    if (value.isBool())
        return value.boolean ? "true" : "false";
    return "?";
}

std::string rstrip(std::string text)
{
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    return text;
}

// What the operand *means*, resolved: the constant it indexes, the function it
// names, the offset it jumps to. An out-of-range index is reported rather than
// dereferenced — see the note on printing every byte in the header.
std::string annotate(const Chunk &chunk, OpCode op, std::uint16_t operand, std::size_t offset)
{
    switch (op)
    {
    case OpCode::CONST:
        if (operand < chunk.constants.size())
            return "  ; " + formatValue(chunk.constants[operand]);
        return "  ; <constant " + std::to_string(operand) + " is not in the pool>";
    case OpCode::JUMP:
    case OpCode::JUMP_IF_FALSE:
        // The operand is the absolute target already. `(backward)` is the one
        // thing worth deriving: it is what makes a `while` legible as a loop.
        return "  ; -> " + offsetLabel(operand) +
               (operand <= offset ? "  (backward)" : "");
    case OpCode::CALL:
        if (operand < chunk.functions.size())
            return "  ; " + chunk.functions[operand].name + "/" +
                   std::to_string(chunk.functions[operand].arity);
        return "  ; <function " + std::to_string(operand) + " is not in the table>";
    default:
        // LOAD_LOCAL and STORE_LOCAL are the remaining operand-bearing
        // opcodes, and a slot index resolves to nothing: item 3.4's resolver
        // replaced the name with the slot, and the chunk never carried one.
        return "";
    }
}

} // namespace

void disassemble(const Chunk &chunk, std::ostream &out)
{
    out << "== chunk ==\n";
    out << "  code: " << chunk.code.size() << " bytes"
        << "  constants: " << chunk.constants.size()
        << "  functions: " << chunk.functions.size()
        << "  program frame: " << chunk.programFrameSize << " slots\n";

    out << "\n== constants ==\n";
    if (chunk.constants.empty())
        out << "  (none)\n";
    for (std::size_t i = 0; i < chunk.constants.size(); i++)
        out << "  #" << i << "  " << formatValue(chunk.constants[i]) << "\n";

    out << "\n== functions ==\n";
    if (chunk.functions.empty())
        out << "  (none)\n";
    for (std::size_t i = 0; i < chunk.functions.size(); i++)
    {
        const FunctionInfo &function = chunk.functions[i];
        out << "  #" << i << "  " << function.name << "  arity " << function.arity
            << "  frame " << function.frameSize << "  entry " << offsetLabel(function.entry)
            << "\n";
    }

    out << "\n== code ==\n";
    std::size_t offset = 0;
    while (offset < chunk.code.size())
    {
        for (const FunctionInfo &function : chunk.functions)
            if (function.entry == offset)
                out << "  " << function.name << ":\n";

        // The line comes from the span table, and only when an entry sits at
        // this exact offset. `spanAt` answers with the greatest entry at or
        // before the offset — right for a fault, wrong here, because a byte
        // the walk has no entry for would silently borrow the previous
        // instruction's line. A dash says "no entry" instead.
        const SpanEntry *entry = chunk.spanAt(offset);
        const std::string line = (entry != nullptr && entry->offset == offset)
                                     ? std::to_string(entry->span.line)
                                     : "-";
        const std::string prefix =
            "  " + offsetLabel(offset) + "  line " + padLeft(line, 3) + "  ";

        const std::uint8_t byte = chunk.code[offset];
        if (byte > static_cast<std::uint8_t>(OpCode::HALT))
        {
            out << prefix << "<unknown opcode " << hexByte(byte) << ">\n";
            offset += 1;
            continue;
        }

        const OpCode op = static_cast<OpCode>(byte);
        const std::size_t width = instructionLength(op);
        if (offset + width > chunk.code.size())
        {
            out << prefix << "<truncated: " << opCodeName(op) << " needs " << width
                << " bytes, " << (chunk.code.size() - offset) << " available>\n";
            return;
        }

        std::string text = prefix + padRight(opCodeName(op), 14);
        if (opCodeHasOperand(op))
        {
            const std::uint16_t operand = chunk.readOperand(offset);
            text += padLeft(std::to_string(operand), 5);
            text += annotate(chunk, op, operand, offset);
        }
        out << rstrip(text) << "\n";
        offset += width;
    }
}
