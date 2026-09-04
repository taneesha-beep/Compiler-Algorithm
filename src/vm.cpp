#include "vm.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "diagnostic.h"
#include "interpreter.h" // maxCallDepth

namespace
{

// ON WHY THESE ARE COPIES OF `src/interpreter.cpp`'s.
//
// The tree-walker's helpers take AST nodes — a `BinOpNode` for its span and its
// operator text. This engine has neither: it has a `SpanEntry`, which carries
// the span the compiler attached to the instruction and the operator's source
// spelling beside it. So the signatures cannot be shared even if the text were.
//
// The text is duplicated on purpose as well. Item 4.4's differential testing is
// what checks that the two engines render the same diagnostic, and a shared
// message helper would make them agree by construction, leaving 4.4 nothing to
// find. `tests/diagnostic_test.cpp` pins these bytes for the tree-walker and is
// the specification both are written against.
//
// Note the two overflow wordings differ. `-` names two operators, so a reader
// staring at `0 - x` needs to know which one trapped.

const char *operatorTextOf(const SpanEntry &entry)
{
    // Null means the compiler emitted an instruction that can fault without a
    // spelling; empty means the AST was dropped before the chunk was run (see
    // the note in `src/vm.h`). Neither is reachable from `src/main.cpp`, and
    // both would otherwise render `operator '' cannot be applied to ...`.
    assert(entry.op != nullptr && "a faulting instruction carries its operator");
    return entry.op != nullptr ? entry.op : "";
}

// The caret goes on the whole operation rather than on one operand: an operand
// only has the wrong type relative to what is being done with it.
[[noreturn]] void binaryTypeFault(const SpanEntry &at, const Value &left,
                                  const Value &right)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, at.span,
        std::string("operator '") + operatorTextOf(at) + "' cannot be applied to " +
            typeName(left.type) + " and " + typeName(right.type)});
}

[[noreturn]] void unaryTypeFault(const SpanEntry &at, const Value &operand)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, at.span,
        std::string("operator '") + operatorTextOf(at) + "' cannot be applied to " +
            typeName(operand.type)});
}

[[noreturn]] void binaryOverflowFault(const SpanEntry &at)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, at.span,
        std::string("integer overflow in '") + operatorTextOf(at) + "'"});
}

[[noreturn]] void unaryOverflowFault(const SpanEntry &at)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, at.span,
        std::string("integer overflow in unary '") + operatorTextOf(at) + "'"});
}

// Arithmetic and ordering are integer-only. See the type rules in value.h.
void requireIntegers(const SpanEntry &at, const Value &left, const Value &right)
{
    if (!left.isInt() || !right.isInt())
        binaryTypeFault(at, left, right);
}

// Equality is the one binary group that accepts booleans, and only against
// booleans: `1 == true` compares two different types and is a fault rather
// than `false`.
void requireSameType(const SpanEntry &at, const Value &left, const Value &right)
{
    if (left.type != right.type)
        binaryTypeFault(at, left, right);
}

// A condition is a boolean and nothing else. `JUMP_IF_FALSE` is the one
// instruction whose span is the *condition's* rather than the enclosing
// statement's, which is what puts this caret in the same place the
// tree-walker's `requireCondition` puts it.
void requireCondition(const SpanEntry &at, const Value &value)
{
    if (!value.isBool())
        throw RuntimeFault(Diagnostic{
            Severity::Error, at.span,
            std::string("a condition must be a boolean, not ") +
                typeName(value.type)});
}

} // namespace

// Every chunk item 4.2 writes has one span entry per instruction, so this
// cannot be null for an offset the dispatch loop reached. The fallback is here
// so that a chunk built by hand — `tests/vm_test.cpp` builds three — faults
// rather than dereferencing null if it forgets one.
const SpanEntry &VM::entryAt(std::size_t at) const
{
    static const SpanEntry none{};
    const SpanEntry *entry = chunk->spanAt(at);
    assert(entry != nullptr && "every instruction has a span entry");
    return entry != nullptr ? *entry : none;
}

// The operand-stack guard. One comparison per push, and it stays on the hot
// path for the same reason item 1.5's overflow check does: it is part of what
// configuration V *is*, so item 5.1 measures V with it and there is no
// configuration it has to cancel out of.
void VM::push(const Value &value, std::size_t at)
{
    if (stack.size() >= maxStackSlots)
        throw RuntimeFault(Diagnostic{Severity::Error, entryAt(at).span,
                                      "operand stack exhausted"});
    stack.push_back(value);
}

void VM::run(const Chunk &program)
{
    chunk = &program;

    // The program's own frame, reserved before the instruction at offset 0
    // runs, so its `slotBase` is 0. Sized from `resolve()`'s return value, the
    // same number `Interpreter::execute` takes as a parameter, and filled with
    // the integer `0` — which is what `Value`'s trivial default constructor
    // leaves in the tree-walker's frame vector. No guard is needed here:
    // `programFrameSize` cannot exceed `maxOperand`, which is below the cap.
    stack.assign(static_cast<std::size_t>(program.programFrameSize),
                 Value::fromInt(0));
    frames.assign(1, Frame{0, 0});

    std::size_t ip = 0;
    for (;;)
    {
        assert(ip < program.code.size() && "a chunk ends in HALT or RETURN");

        // THE CONTRACT. `spanAt` finds the greatest entry at or before an
        // offset, so any byte inside an instruction resolves to that
        // instruction — but the instruction pointer *after* the decode below is
        // the next instruction's offset exactly, and a fault raised with it
        // would put the caret on the next source construct. So the opcode's own
        // offset is saved first, and every fault below is raised against `here`.
        // `docs/BYTECODE.md` calls this the one thing about the span table item
        // 4.3 can get wrong without a test noticing; it is `tests/vm_test.cpp`
        // and item 4.4 that notice.
        const std::size_t here = ip;
        const OpCode op = static_cast<OpCode>(program.code[here]);
        ip += instructionLength(op);

        switch (op)
        {
        case OpCode::CONST:
        {
            push(program.constants[program.readOperand(here)], here);
            break;
        }
        case OpCode::LOAD_LOCAL:
        {
            // Copied out before the push: `push` may reallocate the vector the
            // slot lives in, so passing `stack[...]` by reference would hand it
            // a dangling operand on exactly the pushes that grow the stack.
            const Value local =
                stack[frames.back().slotBase + program.readOperand(here)];
            push(local, here);
            break;
        }
        case OpCode::STORE_LOCAL:
        {
            const Value value = stack.back();
            stack.pop_back();
            stack[frames.back().slotBase + program.readOperand(here)] = value;
            break;
        }
        case OpCode::POP:
        {
            // Emitted nowhere. The language has no expression statements, so
            // every expression's value is consumed by the construct containing
            // it, and `JUMP_IF_FALSE` pops its own condition. This arm is a
            // known coverage gap that item 4.4's differential testing cannot
            // reach — `docs/BYTECODE.md` says so rather than leaving it to be
            // rediscovered — so `tests/vm_test.cpp` reaches it instead.
            stack.pop_back();
            break;
        }
        case OpCode::ADD:
        {
            const Value right = stack.back();
            stack.pop_back();
            const Value left = stack.back();
            stack.pop_back();
            const SpanEntry &at = entryAt(here);
            requireIntegers(at, left, right);
            std::int64_t result;
            if (__builtin_add_overflow(left.integer, right.integer, &result))
                binaryOverflowFault(at);
            stack.push_back(Value::fromInt(result));
            break;
        }
        case OpCode::SUB:
        {
            const Value right = stack.back();
            stack.pop_back();
            const Value left = stack.back();
            stack.pop_back();
            const SpanEntry &at = entryAt(here);
            requireIntegers(at, left, right);
            std::int64_t result;
            if (__builtin_sub_overflow(left.integer, right.integer, &result))
                binaryOverflowFault(at);
            stack.push_back(Value::fromInt(result));
            break;
        }
        case OpCode::MUL:
        {
            const Value right = stack.back();
            stack.pop_back();
            const Value left = stack.back();
            stack.pop_back();
            const SpanEntry &at = entryAt(here);
            requireIntegers(at, left, right);
            std::int64_t result;
            if (__builtin_mul_overflow(left.integer, right.integer, &result))
                binaryOverflowFault(at);
            stack.push_back(Value::fromInt(result));
            break;
        }
        case OpCode::DIV:
        {
            const Value right = stack.back();
            stack.pop_back();
            const Value left = stack.back();
            stack.pop_back();
            const SpanEntry &at = entryAt(here);
            requireIntegers(at, left, right);
            // Checked in this order so a zero divisor, which a reader will
            // actually hit, is reported as division by zero. The overflow below
            // is the single pair `INT64_MIN / -1`, whose quotient is one past
            // the maximum — the two's-complement range being asymmetric — and
            // left unchecked it is undefined behaviour rather than a wrap.
            if (right.integer == 0)
                throw RuntimeFault(
                    Diagnostic{Severity::Error, at.span, "division by zero"});
            if (left.integer == std::numeric_limits<std::int64_t>::min() &&
                right.integer == -1)
                binaryOverflowFault(at);
            stack.push_back(Value::fromInt(left.integer / right.integer));
            break;
        }
        case OpCode::NEG:
        {
            const Value operand = stack.back();
            stack.pop_back();
            const SpanEntry &at = entryAt(here);
            if (!operand.isInt())
                unaryTypeFault(at, operand);
            // `-INT64_MIN` is the whole of it, for the reason `/` gives above.
            // Written as a subtraction from zero so the check is the same
            // builtin the binary operators use rather than a second mechanism
            // spelled by hand.
            std::int64_t result;
            if (__builtin_sub_overflow(static_cast<std::int64_t>(0),
                                       operand.integer, &result))
                unaryOverflowFault(at);
            stack.push_back(Value::fromInt(result));
            break;
        }
        case OpCode::EQ:
        {
            const Value right = stack.back();
            stack.pop_back();
            const Value left = stack.back();
            stack.pop_back();
            requireSameType(entryAt(here), left, right);
            stack.push_back(Value::fromBool(valuesEqual(left, right)));
            break;
        }
        case OpCode::LT:
        {
            const Value right = stack.back();
            stack.pop_back();
            const Value left = stack.back();
            stack.pop_back();
            requireIntegers(entryAt(here), left, right);
            stack.push_back(Value::fromBool(left.integer < right.integer));
            break;
        }
        case OpCode::GT:
        {
            const Value right = stack.back();
            stack.pop_back();
            const Value left = stack.back();
            stack.pop_back();
            requireIntegers(entryAt(here), left, right);
            stack.push_back(Value::fromBool(left.integer > right.integer));
            break;
        }
        case OpCode::NOT:
        {
            const Value operand = stack.back();
            stack.pop_back();
            // Only a `NOT` compiled from a source `!` can reach the fault: the
            // operand of the `NOT` in a lowered `<=`, `>=` or `!=` is the
            // boolean `GT`, `LT` or `EQ` has just produced. So the ambiguity in
            // `SpanEntry::op` on the lowered path never reaches a diagnostic.
            if (!operand.isBool())
                unaryTypeFault(entryAt(here), operand);
            stack.push_back(Value::fromBool(!operand.boolean));
            break;
        }
        case OpCode::JUMP:
        {
            // An absolute target, not a signed displacement, so one opcode
            // serves both the forward jump out of an `if` and the backward jump
            // to a `while` header.
            ip = program.readOperand(here);
            break;
        }
        case OpCode::JUMP_IF_FALSE:
        {
            const Value condition = stack.back();
            stack.pop_back();
            requireCondition(entryAt(here), condition);
            if (!condition.boolean)
                ip = program.readOperand(here);
            break;
        }
        case OpCode::CALL:
        {
            const FunctionInfo &function = program.functions[program.readOperand(here)];

            // Checked *after* the arguments have been evaluated, which is where
            // the preceding instructions leave them, and against a frame stack
            // whose first entry is the program's own — so this admits exactly
            // `maxCallDepth` nested calls, the count
            // `Interpreter::callFunction` admits. The caret goes on the call
            // that could not be entered, which is the span `CALL` carries.
            if (frames.size() > static_cast<std::size_t>(maxCallDepth))
                throw RuntimeFault(Diagnostic{Severity::Error, entryAt(here).span,
                                              "call depth exceeded"});

            // The arguments are already the callee's slots `0 .. arity-1`,
            // pushed by the caller in source order. Only the locals above them
            // need reserving, each holding the integer `0` — the same value
            // `callFunction`'s frame vector is value-initialised to.
            const std::size_t slotBase = stack.size() - function.arity;
            for (std::uint16_t slot = function.arity; slot < function.frameSize; slot++)
                push(Value::fromInt(0), here);

            // `ip` already names the instruction after this `CALL`, which is
            // exactly the return address.
            frames.push_back(Frame{ip, slotBase});
            ip = function.entry;
            break;
        }
        case OpCode::RETURN:
        {
            const Value result = stack.back();
            stack.pop_back();
            const Frame frame = frames.back();
            frames.pop_back();
            // Discard the whole frame — slots and any temporaries — and put the
            // result where the arguments were, so the caller's stack is one
            // deeper than before it pushed them. No guard on this push: the
            // stack was at least `slotBase + 1` deep a line ago.
            stack.resize(frame.slotBase);
            stack.push_back(result);
            ip = frame.returnIP;
            break;
        }
        case OpCode::PRINT:
        {
            // Written straight to the stream rather than through a
            // value-to-string helper, so that printing allocates nothing — and
            // byte for byte what the tree-walker's `PrintNode` arm writes,
            // because item 4.4 compares the two on stdout.
            const Value value = stack.back();
            stack.pop_back();
            if (value.isBool())
                std::cout << (value.boolean ? "true" : "false") << std::endl;
            else
                std::cout << value.integer << std::endl;
            break;
        }
        case OpCode::HALT:
        {
            return;
        }
        default:
        {
            // Unreachable from a chunk this repository can produce, and here
            // for the reason the tree-walker's `unknown node type` is: a fault
            // beats undefined behaviour if a later item widens what a chunk may
            // hold. It is the engine talking about itself, so it carries the
            // instruction's own span.
            throw RuntimeFault(Diagnostic{Severity::Error, entryAt(here).span,
                                          "unknown opcode"});
        }
        }
    }
}
