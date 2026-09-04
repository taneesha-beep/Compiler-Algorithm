// Item 4.2's test binary. It does two jobs, and they check different things.
//
// PART ONE is two small programs whose chunks are written out here BY HAND —
// every instruction, its offset, its operand, and the jump targets — and
// compared byte for byte against what `compile` produces. This is the rule
// item 4.1 learned the hard way and `CLAUDE.md` records: a test that derives
// its expectation from the function under test cannot fail. The offsets below
// were computed from the instruction widths in `docs/BYTECODE.md` and not from
// a run, so the compiler and this file have to be wrong in the same way to
// agree. Backpatching is what they exist for: a jump patched to the wrong
// instruction still lands on a boundary and would pass every structural check
// in part two.
//
// PART TWO is item 4.2's acceptance criterion — every program under `tests/`,
// `examples/` and `bench/` compiles to a chunk without error — plus the
// structural invariants that make "without error" mean something. Nothing
// executes bytecode until item 4.3, so "it did not throw" is very nearly the
// whole of what can be asserted about a chunk; a mis-backpatched jump would
// sail through that and surface at 4.3 as a wrong answer with no indication of
// where it came from. The invariants are checked instead: every jump target is
// inside `code` and on an instruction boundary, every constant, slot and
// function index is in range, the span table has exactly one entry per
// instruction and `spanAt` resolves every byte to its own instruction, and
// every region of code ends in the terminator its layout requires.
//
// It takes the repository root as argv[1] — `add_test` passes
// `${CMAKE_SOURCE_DIR}` — because the three directories it sweeps are inputs
// rather than fixtures it could carry.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "chunk.h"
#include "compiler.h"
#include "diagnostic.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"

namespace
{

int failures = 0;

void fail(const std::string &what)
{
    std::cerr << "FAIL: " << what << "\n";
    ++failures;
}

template <typename T>
void expectEqual(const std::string &what, const T &actual, const T &expected)
{
    if (!(actual == expected))
    {
        std::ostringstream out;
        out << what << ": expected " << expected << ", got " << actual;
        fail(out.str());
    }
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The front end, exactly as `src/main.cpp` runs it up to the point where it
// hands off to an engine.
//
// ON THE `ast` OUT-PARAMETER, which is not a convenience. `SpanEntry::op` is a
// `const char *` into `BinOpNode::op` / `UnaryOpNode::op` — item 4.1's decision,
// and the note beside it says the AST outlives the chunk. So the tree cannot be
// a local of this function: dropping it leaves every operator spelling in the
// chunk dangling, and the first version of this file did exactly that. The
// symptom was mild — the pointers stayed non-null and read as empty strings —
// which is why check (5) below compares each spelling against the set of
// operators the language actually has rather than merely testing it for null.
// The obligation carries to item 4.3: whatever runs a chunk must keep the tree
// it was compiled from alive for as long as the chunk exists.
Chunk compileSource(const std::string &source, std::vector<Node> &ast)
{
    std::vector<Token> tokens = lex(source);
    Parser parser(tokens);
    ast = parser.parse();
    const int slots = resolve(ast);
    return compile(ast, slots);
}

// ============================================================
// PART ONE — two chunks written out by hand
// ============================================================

// One expected instruction. `operand` is ignored for the fourteen opcodes that
// take none.
struct Expected
{
    std::size_t offset;
    OpCode op;
    std::uint16_t operand;
};

void checkInstructions(const std::string &program, const Chunk &chunk,
                       const std::vector<Expected> &expected, std::size_t codeSize)
{
    expectEqual(program + ": code size", chunk.code.size(), codeSize);
    expectEqual(program + ": instruction count", chunk.spans.size(), expected.size());

    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const Expected &want = expected[i];
        if (want.offset + 1 > chunk.code.size())
        {
            fail(program + ": offset past the end of code");
            return;
        }
        const OpCode got = static_cast<OpCode>(chunk.code[want.offset]);
        if (got != want.op)
        {
            fail(program + ": at offset " + std::to_string(want.offset) + ", expected " +
                 opCodeName(want.op) + ", got " + opCodeName(got));
            continue;
        }
        if (opCodeHasOperand(want.op) && chunk.readOperand(want.offset) != want.operand)
            fail(program + ": " + opCodeName(want.op) + " at offset " +
                 std::to_string(want.offset) + ": expected operand " +
                 std::to_string(want.operand) + ", got " +
                 std::to_string(chunk.readOperand(want.offset)));
        if (i < chunk.spans.size() && chunk.spans[i].offset != want.offset)
            fail(program + ": span entry " + std::to_string(i) + " is at offset " +
                 std::to_string(chunk.spans[i].offset) + ", expected " +
                 std::to_string(want.offset));
    }
}

void checkSpanAt(const std::string &program, const Chunk &chunk, std::size_t offset,
                 int line, int col, int len)
{
    const SpanEntry *entry = chunk.spanAt(offset);
    if (entry == nullptr)
    {
        fail(program + ": no span entry at offset " + std::to_string(offset));
        return;
    }
    const std::string where = program + ": span at offset " + std::to_string(offset);
    expectEqual(where + " line", entry->span.line, line);
    expectEqual(where + " col", entry->span.col, col);
    expectEqual(where + " len", entry->span.len, len);
}

void checkOpText(const std::string &program, const Chunk &chunk, std::size_t offset,
                 const std::string &op)
{
    const SpanEntry *entry = chunk.spanAt(offset);
    if (entry == nullptr || entry->op == nullptr)
    {
        fail(program + ": no operator spelling at offset " + std::to_string(offset));
        return;
    }
    expectEqual(program + ": operator at offset " + std::to_string(offset),
                std::string(entry->op), op);
}

// PROGRAM ONE — an `if` with an `else`, which is the two-backpatch shape.
//
//   line 1:  x = 1
//   line 2:  if x < 2 {
//   line 3:  print 1
//   line 4:  } else {
//   line 5:  print 2
//   line 6:  }
//
// `x` is slot 0 and the program's frame is one slot wide. Laid out by hand from
// the widths in `docs/BYTECODE.md` — 3 bytes with an operand, 1 without:
//
//    0  CONST 0            the literal 1
//    3  STORE_LOCAL 0
//    6  LOAD_LOCAL 0
//    9  CONST 1            the literal 2
//   12  LT
//   13  JUMP_IF_FALSE 23   -> the else branch
//   16  CONST 2            the literal 1 again; the pool is not deduplicated
//   19  PRINT
//   20  JUMP 27            -> past the else
//   23  CONST 3            the literal 2 again
//   26  PRINT
//   27  HALT
//
// 28 bytes. The two patched operands, 23 and 27, are the whole point of the
// case: 23 is the first instruction of the else branch and 27 is the `HALT`.
void checkIfElseProgram()
{
    const std::string source = "x = 1\nif x < 2 {\nprint 1\n} else {\nprint 2\n}\n";
    std::vector<Node> ast;
    const Chunk chunk = compileSource(source, ast);

    const std::vector<Expected> expected = {
        {0, OpCode::CONST, 0},
        {3, OpCode::STORE_LOCAL, 0},
        {6, OpCode::LOAD_LOCAL, 0},
        {9, OpCode::CONST, 1},
        {12, OpCode::LT, 0},
        {13, OpCode::JUMP_IF_FALSE, 23},
        {16, OpCode::CONST, 2},
        {19, OpCode::PRINT, 0},
        {20, OpCode::JUMP, 27},
        {23, OpCode::CONST, 3},
        {26, OpCode::PRINT, 0},
        {27, OpCode::HALT, 0},
    };
    checkInstructions("if/else", chunk, expected, 28);

    expectEqual("if/else: program frame size", chunk.programFrameSize, 1);
    expectEqual("if/else: constant count", chunk.constants.size(), std::size_t{4});
    expectEqual("if/else: no functions", chunk.functions.size(), std::size_t{0});
    const std::int64_t constants[] = {1, 2, 1, 2};
    for (std::size_t i = 0; i < 4 && i < chunk.constants.size(); ++i)
    {
        expectEqual("if/else: constant " + std::to_string(i) + " is an integer",
                    chunk.constants[i].isInt(), true);
        expectEqual("if/else: constant " + std::to_string(i),
                    chunk.constants[i].integer, constants[i]);
    }

    // JUMP_IF_FALSE carries the CONDITION's span, not the `if`'s. On line 2,
    // `x < 2` starts at column 4 and runs to column 8, so five characters.
    checkSpanAt("if/else", chunk, 13, 2, 4, 5);
    // ... where the `if` statement itself starts at column 1 of the same line.
    // If the two were confused, the caret would move and no golden case would
    // notice until item 4.4.
    checkOpText("if/else", chunk, 12, "<");
}

// PROGRAM TWO — a `while`, a lowered `<=`, and a call to a function declared
// AFTER the call site.
//
//   line 1:  i = 0
//   line 2:  while i <= 2 {
//   line 3:  i = i + 1
//   line 4:  }
//   line 5:  print f(i)
//   line 6:  fn f(n) {
//   line 7:  return n
//   line 8:  }
//
//    0  CONST 0            the literal 0
//    3  STORE_LOCAL 0
//    6  LOAD_LOCAL 0       <- the loop header, and the backward jump's target
//    9  CONST 1            the literal 2
//   12  GT                 `<=` is lowered onto GT NOT ...
//   13  NOT                ... and both halves carry the spelling `<=`
//   14  JUMP_IF_FALSE 30   -> past the loop
//   17  LOAD_LOCAL 0
//   20  CONST 2            the literal 1
//   23  ADD
//   24  STORE_LOCAL 0
//   27  JUMP 6             -> the header, written out directly: it is known
//   30  LOAD_LOCAL 0       the argument, pushed by the caller
//   33  CALL 0
//   36  PRINT
//   37  HALT               <- the program section ends here
//   38  LOAD_LOCAL 0       <- f's body; `entry` is 38
//   41  RETURN
//   42  CONST 3            the literal 0 of the trailing `CONST 0; RETURN`,
//   45  RETURN             emitted whether or not the body ends in a return
//
// 46 bytes.
void checkWhileCallProgram()
{
    const std::string source =
        "i = 0\nwhile i <= 2 {\ni = i + 1\n}\nprint f(i)\nfn f(n) {\nreturn n\n}\n";
    std::vector<Node> ast;
    const Chunk chunk = compileSource(source, ast);

    const std::vector<Expected> expected = {
        {0, OpCode::CONST, 0},
        {3, OpCode::STORE_LOCAL, 0},
        {6, OpCode::LOAD_LOCAL, 0},
        {9, OpCode::CONST, 1},
        {12, OpCode::GT, 0},
        {13, OpCode::NOT, 0},
        {14, OpCode::JUMP_IF_FALSE, 30},
        {17, OpCode::LOAD_LOCAL, 0},
        {20, OpCode::CONST, 2},
        {23, OpCode::ADD, 0},
        {24, OpCode::STORE_LOCAL, 0},
        {27, OpCode::JUMP, 6},
        {30, OpCode::LOAD_LOCAL, 0},
        {33, OpCode::CALL, 0},
        {36, OpCode::PRINT, 0},
        {37, OpCode::HALT, 0},
        {38, OpCode::LOAD_LOCAL, 0},
        {41, OpCode::RETURN, 0},
        {42, OpCode::CONST, 3},
        {45, OpCode::RETURN, 0},
    };
    checkInstructions("while/call", chunk, expected, 46);

    expectEqual("while/call: program frame size", chunk.programFrameSize, 1);
    const std::int64_t constants[] = {0, 2, 1, 0};
    expectEqual("while/call: constant count", chunk.constants.size(), std::size_t{4});
    for (std::size_t i = 0; i < 4 && i < chunk.constants.size(); ++i)
    {
        expectEqual("while/call: constant " + std::to_string(i) + " is an integer",
                    chunk.constants[i].isInt(), true);
        expectEqual("while/call: constant " + std::to_string(i),
                    chunk.constants[i].integer, constants[i]);
    }

    // The function table. `entry` is 38 and the call at offset 33 names index 0
    // — a call compiled three instructions before the declaration it refers to,
    // which is the case the two-pass registration exists for.
    expectEqual("while/call: function count", chunk.functions.size(), std::size_t{1});
    if (chunk.functions.size() == 1)
    {
        expectEqual("while/call: function name", chunk.functions[0].name, std::string("f"));
        expectEqual("while/call: function entry", chunk.functions[0].entry,
                    std::uint32_t{38});
        expectEqual("while/call: function arity", chunk.functions[0].arity,
                    std::uint16_t{1});
        expectEqual("while/call: function frame size", chunk.functions[0].frameSize,
                    std::uint16_t{1});
    }

    // On line 2, `i <= 2` starts at column 7 and runs to column 12: six
    // characters, and that is the span JUMP_IF_FALSE carries.
    checkSpanAt("while/call", chunk, 14, 2, 7, 6);
    // Both halves of the lowered pair say `<=`, not `>`. This is the whole
    // reason SpanEntry carries an operator spelling at all: `1 <= true` must
    // fault naming the operator the programmer wrote.
    checkOpText("while/call", chunk, 12, "<=");
    checkOpText("while/call", chunk, 13, "<=");
    checkOpText("while/call", chunk, 23, "+");
}

// PROGRAM THREE — a two-parameter function, which is what pins the two orders
// that a one-argument call cannot distinguish: arguments are pushed in SOURCE
// order, and a binary operator evaluates its LEFT operand first. Both are
// observable — either operand of a call or of a `-` can raise a fault, and
// which one is reported is decided by whichever runs first — and both are the
// tree-walker's order, which item 4.4 will compare against.
//
//   line 1:  fn g(a, b) {
//   line 2:  return a - b
//   line 3:  }
//   line 4:  print g(1, 2)
//
// `a` is slot 0 and `b` is slot 1: the resolver numbers parameters before it
// walks the body, so a call's arguments ARE the callee's first locals.
//
//    0  CONST 0            the literal 1 — the FIRST argument, pushed first
//    3  CONST 1            the literal 2
//    6  CALL 0
//    9  PRINT
//   10  HALT
//   11  LOAD_LOCAL 0       <- g's body; `entry` is 11. `a` before `b`
//   14  LOAD_LOCAL 1
//   17  SUB
//   18  RETURN
//   19  CONST 2            the trailing `CONST 0; RETURN`, emitted even though
//   22  RETURN             the body already ended in one
//
// 23 bytes.
void checkTwoArgumentProgram()
{
    const std::string source = "fn g(a, b) {\nreturn a - b\n}\nprint g(1, 2)\n";
    std::vector<Node> ast;
    const Chunk chunk = compileSource(source, ast);

    const std::vector<Expected> expected = {
        {0, OpCode::CONST, 0},
        {3, OpCode::CONST, 1},
        {6, OpCode::CALL, 0},
        {9, OpCode::PRINT, 0},
        {10, OpCode::HALT, 0},
        {11, OpCode::LOAD_LOCAL, 0},
        {14, OpCode::LOAD_LOCAL, 1},
        {17, OpCode::SUB, 0},
        {18, OpCode::RETURN, 0},
        {19, OpCode::CONST, 2},
        {22, OpCode::RETURN, 0},
    };
    checkInstructions("two-argument", chunk, expected, 23);

    expectEqual("two-argument: program frame size", chunk.programFrameSize, 0);
    const std::int64_t constants[] = {1, 2, 0};
    expectEqual("two-argument: constant count", chunk.constants.size(), std::size_t{3});
    for (std::size_t i = 0; i < 3 && i < chunk.constants.size(); ++i)
    {
        expectEqual("two-argument: constant " + std::to_string(i) + " is an integer",
                    chunk.constants[i].isInt(), true);
        expectEqual("two-argument: constant " + std::to_string(i),
                    chunk.constants[i].integer, constants[i]);
    }

    expectEqual("two-argument: function count", chunk.functions.size(), std::size_t{1});
    if (chunk.functions.size() == 1)
    {
        expectEqual("two-argument: function name", chunk.functions[0].name,
                    std::string("g"));
        expectEqual("two-argument: function entry", chunk.functions[0].entry,
                    std::uint32_t{11});
        expectEqual("two-argument: function arity", chunk.functions[0].arity,
                    std::uint16_t{2});
        expectEqual("two-argument: function frame size", chunk.functions[0].frameSize,
                    std::uint16_t{2});
    }

    checkOpText("two-argument", chunk, 17, "-");
}

// ============================================================
// PART TWO — the sweep, and the structural invariants
// ============================================================

// The region of `code` a given offset belongs to, and how wide its frame is.
// The program's own statements run under `programFrameSize`; each function body
// runs under its own. A `LOAD_LOCAL` emitted against the wrong frame is in
// range for one and out of range for the other, which is what makes this worth
// tracking rather than checking every slot against the widest frame.
struct Region
{
    std::size_t begin;
    std::size_t end;
    int frameSize;
    OpCode terminator; // what the region's last instruction must be
    std::string name;
};

std::vector<Region> regionsOf(const Chunk &chunk)
{
    std::vector<std::pair<std::size_t, std::size_t>> bodies; // entry, index
    for (std::size_t i = 0; i < chunk.functions.size(); ++i)
        bodies.emplace_back(chunk.functions[i].entry, i);
    std::sort(bodies.begin(), bodies.end());

    std::vector<Region> regions;
    const std::size_t firstBody = bodies.empty() ? chunk.code.size() : bodies[0].first;
    regions.push_back(
        Region{0, firstBody, chunk.programFrameSize, OpCode::HALT, "the program"});
    for (std::size_t k = 0; k < bodies.size(); ++k)
    {
        const std::size_t end =
            (k + 1 < bodies.size()) ? bodies[k + 1].first : chunk.code.size();
        const FunctionInfo &info = chunk.functions[bodies[k].second];
        regions.push_back(
            Region{bodies[k].first, end, info.frameSize, OpCode::RETURN, "fn " + info.name});
    }
    return regions;
}

void checkChunk(const std::string &program, const Chunk &chunk)
{
    const std::string tag = program;

    if (chunk.code.empty())
    {
        fail(tag + ": empty code");
        return;
    }

    // (1) Decode the whole stream. Every byte must belong to exactly one
    // instruction, every opcode byte must name a real opcode, and the walk must
    // land exactly on the end — a trailing truncated instruction would put the
    // VM's fetch past the end of the vector.
    std::vector<std::size_t> instructions;
    std::set<std::size_t> boundaries;
    std::map<std::size_t, std::size_t> containing; // byte offset -> its instruction
    const std::uint8_t highest = static_cast<std::uint8_t>(OpCode::HALT);
    std::size_t at = 0;
    while (at < chunk.code.size())
    {
        if (chunk.code[at] > highest)
        {
            fail(tag + ": byte " + std::to_string(at) + " is not an opcode");
            return;
        }
        const OpCode op = static_cast<OpCode>(chunk.code[at]);
        const std::size_t width = instructionLength(op);
        if (at + width > chunk.code.size())
        {
            fail(tag + ": instruction at " + std::to_string(at) + " runs past the end");
            return;
        }
        instructions.push_back(at);
        boundaries.insert(at);
        for (std::size_t b = at; b < at + width; ++b)
            containing[b] = at;
        at += width;
    }

    // (2) One span entry per instruction, in emission order and therefore
    // sorted by offset — the invariant `Chunk::emit` is supposed to maintain
    // without anyone having to think about it.
    expectEqual(tag + ": span entries", chunk.spans.size(), instructions.size());
    for (std::size_t i = 0; i < instructions.size() && i < chunk.spans.size(); ++i)
        if (chunk.spans[i].offset != instructions[i])
            fail(tag + ": span entry " + std::to_string(i) + " names offset " +
                 std::to_string(chunk.spans[i].offset) + ", not " +
                 std::to_string(instructions[i]));

    // (3) `spanAt` resolves every byte — opcode and operand alike — to the
    // instruction that contains it. This is the contract item 4.3 depends on
    // for a diagnostic to land on the right source text.
    for (const auto &[byte, owner] : containing)
    {
        const SpanEntry *entry = chunk.spanAt(byte);
        if (entry == nullptr || entry->offset != owner)
        {
            fail(tag + ": spanAt(" + std::to_string(byte) + ") does not resolve to " +
                 std::to_string(owner));
            break;
        }
    }

    const std::vector<Region> regions = regionsOf(chunk);

    auto frameAt = [&regions](std::size_t offset) -> int {
        for (const Region &region : regions)
            if (offset >= region.begin && offset < region.end)
                return region.frameSize;
        return -1;
    };

    // (4) Every operand in range, and every jump landing on a boundary.
    std::size_t halts = 0;
    for (std::size_t i = 0; i < instructions.size(); ++i)
    {
        const std::size_t offset = instructions[i];
        const OpCode op = static_cast<OpCode>(chunk.code[offset]);
        const std::uint16_t operand =
            opCodeHasOperand(op) ? chunk.readOperand(offset) : 0;
        const std::string where =
            tag + ": " + opCodeName(op) + " at " + std::to_string(offset);

        switch (op)
        {
        case OpCode::CONST:
            if (operand >= chunk.constants.size())
                fail(where + ": constant index " + std::to_string(operand) +
                     " is out of range");
            break;
        case OpCode::LOAD_LOCAL:
        case OpCode::STORE_LOCAL:
            if (static_cast<int>(operand) >= frameAt(offset))
                fail(where + ": slot " + std::to_string(operand) +
                     " is outside a frame of " + std::to_string(frameAt(offset)));
            break;
        case OpCode::JUMP:
        case OpCode::JUMP_IF_FALSE:
            // Inside `code` AND on an instruction boundary. A target that is
            // merely inside the code would be a decode starting part-way
            // through an instruction, which is how a mis-backpatched jump would
            // otherwise reach item 4.3.
            if (boundaries.count(operand) == 0)
                fail(where + ": target " + std::to_string(operand) +
                     " is not an instruction boundary");
            break;
        case OpCode::CALL:
            if (operand >= chunk.functions.size())
                fail(where + ": function index " + std::to_string(operand) +
                     " is out of range");
            break;
        case OpCode::HALT:
            ++halts;
            break;
        default:
            break;
        }

        // (5) Every instruction that can quote an operator in a fault carries
        // its spelling. Nothing else tests this until item 4.4 renders a
        // diagnostic from one, and the lowering makes the opcode alone
        // insufficient to reconstruct it.
        switch (op)
        {
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::NEG:
        case OpCode::EQ:
        case OpCode::LT:
        case OpCode::GT:
        case OpCode::NOT:
            if (i >= chunk.spans.size() || chunk.spans[i].op == nullptr)
            {
                fail(where + ": no operator spelling");
            }
            else
            {
                // Written out by hand, and not derived from the node the
                // instruction came from: a spelling that is merely non-null
                // proves nothing, and a dangling one reads as an empty string
                // rather than as a crash.
                static const std::set<std::string> operators = {
                    "+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "!"};
                const std::string spelling(chunk.spans[i].op);
                if (operators.count(spelling) == 0)
                    fail(where + ": operator spelling \"" + spelling +
                         "\" is not one of the language's operators");
            }
            break;
        default:
            break;
        }
    }

    // (6) Exactly one `HALT`, and it is the last instruction of the program's
    // own section; every function body ends in `RETURN`. Together these say
    // that no region can run off its end into the next one, which is the shape
    // a missing terminator would take at item 4.3.
    expectEqual(tag + ": HALT count", halts, std::size_t{1});
    for (const Region &region : regions)
    {
        if (region.begin >= region.end)
        {
            fail(tag + ": " + region.name + " has no code");
            continue;
        }
        std::size_t last = region.begin;
        for (std::size_t offset : instructions)
            if (offset >= region.begin && offset < region.end)
                last = offset;
        const OpCode got = static_cast<OpCode>(chunk.code[last]);
        if (got != region.terminator)
            fail(tag + ": " + region.name + " ends in " + opCodeName(got) + ", not " +
                 opCodeName(region.terminator));
    }

    // (7) Every function's entry is an instruction boundary, and no function
    // has more parameters than slots.
    for (const FunctionInfo &info : chunk.functions)
    {
        if (boundaries.count(info.entry) == 0)
            fail(tag + ": fn " + info.name + " enters at " + std::to_string(info.entry) +
                 ", not an instruction boundary");
        if (info.arity > info.frameSize)
            fail(tag + ": fn " + info.name + " has more parameters than slots");
    }
}

// The golden case's expected exit code, or -1 when the case does not pin one.
// `tests/` holds negative cases the front end rejects — an out-of-range
// literal, an undefined name — and those never reach the compiler at all. That
// is legitimate, but it must not be a hole: a case is allowed to be rejected
// by the front end exactly when it pins exit code 65, and a case that pins 65
// must be rejected. Neither direction is a count that has to be maintained.
int expectedCodeOf(const std::filesystem::path &algo)
{
    std::filesystem::path codes = algo;
    codes.replace_extension(".expected_code");
    if (!std::filesystem::exists(codes))
        return -1;
    std::istringstream in(readFile(codes));
    int code = -1;
    in >> code;
    return code;
}

void sweep(const std::filesystem::path &directory, bool pinnedCodes, int &compiled,
           int &rejected)
{
    if (!std::filesystem::is_directory(directory))
    {
        fail("not a directory: " + directory.string());
        return;
    }

    std::vector<std::filesystem::path> programs;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".algo")
            programs.push_back(entry.path());
    std::sort(programs.begin(), programs.end());

    if (programs.empty())
    {
        fail("no .algo programs under " + directory.string());
        return;
    }

    for (const std::filesystem::path &program : programs)
    {
        const std::string name = program.filename().string();
        const int pinned = pinnedCodes ? expectedCodeOf(program) : -1;
        const std::string source = readFile(program);

        // The tree has to outlive the chunk — see `compileSource`.
        std::vector<Node> ast;
        Chunk chunk;
        try
        {
            chunk = compileSource(source, ast);
        }
        catch (const CompileError &e)
        {
            if (pinned == 65)
            {
                // A negative case, rejected before the compiler ran. Expected.
                ++rejected;
                continue;
            }
            fail(name + ": rejected with \"" + e.diagnostic().message +
                 "\" but does not pin exit code 65");
            continue;
        }
        catch (const std::exception &e)
        {
            fail(name + ": threw " + e.what());
            continue;
        }

        if (pinned == 65)
        {
            fail(name + ": pins exit code 65 but the front end accepted it");
            continue;
        }
        ++compiled;
        checkChunk(name, chunk);
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: compiler_test <repository root>\n";
        return 64;
    }
    const std::filesystem::path root(argv[1]);

    checkIfElseProgram();
    checkWhileCallProgram();
    checkTwoArgumentProgram();

    int compiled = 0;
    int rejected = 0;
    sweep(root / "tests", true, compiled, rejected);
    sweep(root / "examples", false, compiled, rejected);
    sweep(root / "bench", false, compiled, rejected);

    std::cout << compiled << " programs compiled to a chunk, " << rejected
              << " rejected by the front end before it\n";

    if (failures != 0)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    return 0;
}
