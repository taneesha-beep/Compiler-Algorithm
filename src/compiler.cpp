#include "compiler.h"

#include <cassert>
#include <cstddef>
#include <map>
#include <string>

#include "diagnostic.h"
#include "value.h"

namespace
{

// The walk. It holds the chunk under construction and the name-to-index map
// that stands behind `CALL`; everything else is a parameter.
class Compiler
{
public:
    Chunk run(const std::vector<Node> &statements, int programFrameSize);

private:
    Chunk chunk;

    // Function name to its index in `chunk.functions`. Filled by the first
    // pass, read by every `CALL`. Names are unique: the resolver rejects a
    // duplicate function declaration as a `CompileError` before this runs.
    std::map<std::string, std::uint16_t> functionIndex;

    void declareFunctions(const std::vector<Node> &statements);
    void compileFunctionBody(const FunctionNode &function, std::uint16_t index);

    void compileStatement(const Node &statement);
    void compileExpression(const Node &expression);

    // ---- emit helpers --------------------------------------------------

    // A jump whose target is not known yet: emitted with a placeholder operand
    // and returned so that `patchJumpToHere` can fill it in.
    //
    // ON THE PLACEHOLDER BEING `maxOperand` AND NOT 0. An unpatched jump is a
    // bug in this file, and the only thing that can catch one is the structural
    // check in `tests/compiler_test.cpp` — which requires every jump target to
    // be an instruction boundary. 0 IS an instruction boundary in every chunk,
    // and legitimately so: a `while` written as the program's first statement
    // has its header at offset 0. So a placeholder of 0 would be indisting-
    // uishable from a real target and an unpatched jump would pass. 65535
    // cannot be a boundary in any chunk the format admits, because `code` may
    // not exceed 65535 bytes and so offset 65535 is at best one past the end.
    std::size_t emitJump(OpCode op, Span span);
    void patchJumpToHere(std::size_t jumpOffset, Span span);

    // A jump whose target is already known — the backward jump to a `while`
    // header, which is the one place an absolute operand can be written
    // straight out. This is what item 4.1's decision to make jump targets
    // absolute buys: no displacement arithmetic in either direction.
    void emitJumpTo(OpCode op, std::size_t target, Span span);

    // The three places a value has to fit in sixteen bits and the source is
    // what decides whether it does.
    std::uint16_t slotOperand(int slot, Span span);
    std::uint16_t offsetOperand(std::size_t offset, Span span);
    std::uint16_t constantFor(const Value &value, Span span);
};

// ON THE SPAN AN INSTRUCTION CARRIES. `docs/BYTECODE.md` fixes the rule: the
// span an instruction carries is the span the tree-walker's fault for that same
// operation carries. A binary arithmetic or comparison instruction therefore
// carries the whole `BinOpNode`'s span, `NEG` and `NOT` the whole
// `UnaryOpNode`'s, `CALL` the `CallNode`'s — and `JUMP_IF_FALSE` the span of
// the *condition*, not of the enclosing `if` or `while`, because
// `requireCondition` in `src/interpreter.cpp` puts the caret on the condition.
// Getting one of these wrong moves a caret, and nothing fails until item 4.4
// runs the same case through both engines.
//
// ON `SpanEntry::op`. It is set on every arithmetic, comparison and unary
// instruction, because the lowering below makes an opcode ambiguous about the
// operator a diagnostic must quote: an `LT` is either a source `<` or the first
// half of a source `>=`. It is a pointer into `BinOpNode::op` /
// `UnaryOpNode::op`, which outlive the chunk — the caller owns the tree — and
// nothing in the chunk owns it. Both halves of a lowered pair get the source
// operator's spelling, so that a disassembly reads as one source construct.

std::size_t Compiler::emitJump(OpCode op, Span span)
{
    return chunk.emitOperand(op, static_cast<std::uint16_t>(maxOperand), span);
}

void Compiler::patchJumpToHere(std::size_t jumpOffset, Span span)
{
    chunk.patchOperand(jumpOffset, offsetOperand(chunk.code.size(), span));
}

void Compiler::emitJumpTo(OpCode op, std::size_t target, Span span)
{
    chunk.emitOperand(op, offsetOperand(target, span), span);
}

std::uint16_t Compiler::slotOperand(int slot, Span span)
{
    // The resolver wrote this in item 1.3 and `tests/resolver_test.cpp` asserts
    // that it left no hole; a slot still holding `unresolvedSlot` here is a gap
    // in that walk rather than anything the source can express. The assert says
    // so, and the range check below catches it under `NDEBUG` too — a negative
    // slot cast to `std::uint16_t` would otherwise become 65535 and index some
    // other frame silently.
    assert(slot != unresolvedSlot);
    if (slot < 0 || static_cast<std::size_t>(slot) > maxOperand)
        throw CompileError(Diagnostic{Severity::Error, span,
                                      "too many local variables in one frame"});
    return static_cast<std::uint16_t>(slot);
}

std::uint16_t Compiler::offsetOperand(std::size_t offset, Span span)
{
    // `Chunk::writeByte` already refuses to grow `code` past this, so reaching
    // the throw here means the target was computed from something other than
    // the code vector. It is checked anyway: a truncated jump target is a wrong
    // answer with nothing to report it.
    if (offset > maxOperand)
        throw CompileError(Diagnostic{Severity::Error, span,
                                      "program too large for one chunk"});
    return static_cast<std::uint16_t>(offset);
}

std::uint16_t Compiler::constantFor(const Value &value, Span span)
{
    return chunk.addConstant(value, span);
}

// ---- the first pass ----------------------------------------------------

void Compiler::declareFunctions(const std::vector<Node> &statements)
{
    for (const Node &statement : statements)
    {
        const FunctionNode *function = tryAs<FunctionNode>(statement);
        if (function == nullptr)
            continue;

        if (chunk.functions.size() > maxOperand)
            throw CompileError(Diagnostic{Severity::Error, function->nameSpan,
                                          "too many functions in one program"});
        if (function->parameters.size() > maxOperand ||
            function->frameSize < 0 ||
            static_cast<std::size_t>(function->frameSize) > maxOperand)
            throw CompileError(Diagnostic{Severity::Error, function->nameSpan,
                                          "too many local variables in one frame"});

        // `entry` is a placeholder until the body is emitted. That is the whole
        // trick behind a call needing no backpatching: the call site names an
        // index into this table, and the table is what learns the offset.
        FunctionInfo info;
        info.name = function->name;
        info.entry = 0;
        info.arity = static_cast<std::uint16_t>(function->parameters.size());
        info.frameSize = static_cast<std::uint16_t>(function->frameSize);

        functionIndex[function->name] = static_cast<std::uint16_t>(chunk.functions.size());
        chunk.functions.push_back(std::move(info));
    }
}

// ---- expressions -------------------------------------------------------

void Compiler::compileExpression(const Node &expression)
{
    const Span span = expression->span;

    if (const NumberNode *number = tryAs<NumberNode>(expression))
    {
        // The integer item 3.2 had the parser convert once. Nothing re-parses
        // the digits here either.
        chunk.emitOperand(OpCode::CONST,
                          constantFor(Value::fromInt(number->value), span), span);
        return;
    }

    if (const BooleanNode *boolean = tryAs<BooleanNode>(expression))
    {
        // There is no TRUE or FALSE opcode; booleans reach the stack through
        // the constant pool like everything else.
        chunk.emitOperand(OpCode::CONST,
                          constantFor(Value::fromBool(boolean->value), span), span);
        return;
    }

    if (const IdentifierNode *identifier = tryAs<IdentifierNode>(expression))
    {
        chunk.emitOperand(OpCode::LOAD_LOCAL, slotOperand(identifier->slot, span), span);
        return;
    }

    if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(expression))
    {
        compileExpression(unary->operand);
        chunk.emit(unary->opKind == UnaryOpKind::Negate ? OpCode::NEG : OpCode::NOT,
                   span, unary->op.c_str());
        return;
    }

    if (const BinOpNode *binary = tryAs<BinOpNode>(expression))
    {
        // Left then right, which is the order `Interpreter::evaluate` uses. It
        // is observable: the two operands can each raise a fault, and the one
        // that is reported is whichever is evaluated first.
        compileExpression(binary->left);
        compileExpression(binary->right);

        const char *op = binary->op.c_str();

        // The lowering item 4.1 chose, and the claim that makes nineteen
        // opcodes enough: `<=`, `>=` and `!=` have no opcode and become the
        // opposite comparison followed by `NOT`. It costs one extra instruction
        // per lowered comparison, which item 5.1 reports rather than
        // discovers. Both halves carry the source operator's spelling, so the
        // type fault a `GT` raises for `1 <= true` still says `'<='`.
        switch (binary->opKind)
        {
        case BinOpKind::Add: chunk.emit(OpCode::ADD, span, op); break;
        case BinOpKind::Subtract: chunk.emit(OpCode::SUB, span, op); break;
        case BinOpKind::Multiply: chunk.emit(OpCode::MUL, span, op); break;
        case BinOpKind::Divide: chunk.emit(OpCode::DIV, span, op); break;
        case BinOpKind::Less: chunk.emit(OpCode::LT, span, op); break;
        case BinOpKind::Greater: chunk.emit(OpCode::GT, span, op); break;
        case BinOpKind::Equal: chunk.emit(OpCode::EQ, span, op); break;
        case BinOpKind::LessEqual:
            chunk.emit(OpCode::GT, span, op);
            chunk.emit(OpCode::NOT, span, op);
            break;
        case BinOpKind::GreaterEqual:
            chunk.emit(OpCode::LT, span, op);
            chunk.emit(OpCode::NOT, span, op);
            break;
        case BinOpKind::NotEqual:
            chunk.emit(OpCode::EQ, span, op);
            chunk.emit(OpCode::NOT, span, op);
            break;
        }
        return;
    }

    if (const CallNode *call = tryAs<CallNode>(expression))
    {
        // Arguments in source order, which is the order the tree-walker
        // evaluates them in and also the order the callee's slots expect: the
        // resolver numbers parameters before it walks the body, so the
        // arguments the caller pushes *are* the callee's first locals with
        // nothing to copy.
        for (const Node &argument : call->arguments)
            compileExpression(argument);

        const auto found = functionIndex.find(call->callee);
        // The resolver rejects a call of an unknown function, and one of the
        // wrong arity, as a `CompileError` before this back end sees the tree.
        // Reaching this is a hole in the first pass rather than a program the
        // user could have written.
        assert(found != functionIndex.end());
        if (found == functionIndex.end())
            throw CompileError(Diagnostic{Severity::Error, span,
                                          "unknown function '" + call->callee + "'"});

        chunk.emitOperand(OpCode::CALL, found->second, span);
        return;
    }

    // Every expression node type is handled above. A new one arriving here
    // without an arm would otherwise compile to nothing at all, which is a
    // wrong answer rather than a failure.
    assert(false && "unhandled expression node in the bytecode compiler");
    throw CompileError(Diagnostic{Severity::Error, span,
                                  "internal error: unhandled expression node"});
}

// ---- statements --------------------------------------------------------

void Compiler::compileStatement(const Node &statement)
{
    const Span span = statement->span;

    if (const PrintNode *print = tryAs<PrintNode>(statement))
    {
        compileExpression(print->value);
        chunk.emit(OpCode::PRINT, span);
        return;
    }

    if (const AssignNode *assign = tryAs<AssignNode>(statement))
    {
        compileExpression(assign->value);
        chunk.emitOperand(OpCode::STORE_LOCAL, slotOperand(assign->slot, span), span);
        return;
    }

    if (const BlockNode *block = tryAs<BlockNode>(statement))
    {
        // A block introduces no frame and no instruction of its own. The
        // resolver flattened the scopes in item 1.3 — a slot is unique within
        // the whole function body, not within the block that declared it — so
        // there is nothing to push and nothing to pop.
        for (const Node &inner : block->statements)
            compileStatement(inner);
        return;
    }

    if (const IfNode *branch = tryAs<IfNode>(statement))
    {
        //     <condition>
        //     JUMP_IF_FALSE  -> else, or past the whole statement
        //     <then>
        //     JUMP           -> past the else            (only when there is one)
        //   else:
        //     <else>
        //   end:
        compileExpression(branch->condition);
        const std::size_t toElse =
            emitJump(OpCode::JUMP_IF_FALSE, branch->condition->span);
        compileStatement(branch->thenBranch);

        if (branch->elseBranch)
        {
            const std::size_t toEnd = emitJump(OpCode::JUMP, span);
            patchJumpToHere(toElse, span);
            compileStatement(branch->elseBranch);
            patchJumpToHere(toEnd, span);
        }
        else
        {
            patchJumpToHere(toElse, span);
        }
        return;
    }

    if (const WhileNode *loop = tryAs<WhileNode>(statement))
    {
        //   header:
        //     <condition>
        //     JUMP_IF_FALSE  -> end
        //     <body>
        //     JUMP           -> header
        //   end:
        //
        // The backward jump is the reason jump targets are absolute: `header`
        // is an offset already recorded, so it is written out directly and only
        // the forward jump is backpatched.
        const std::size_t header = chunk.code.size();
        compileExpression(loop->condition);
        const std::size_t toEnd = emitJump(OpCode::JUMP_IF_FALSE, loop->condition->span);
        compileStatement(loop->body);
        emitJumpTo(OpCode::JUMP, header, span);
        patchJumpToHere(toEnd, span);
        return;
    }

    if (const ReturnNode *ret = tryAs<ReturnNode>(statement))
    {
        // The bare form hands back the integer `0`, which is what
        // `Interpreter::callFunction` does and the argument for it is beside
        // that code. `RETURN` always has a value to pop.
        if (ret->value)
            compileExpression(ret->value);
        else
            chunk.emitOperand(OpCode::CONST, constantFor(Value::fromInt(0), span), span);
        chunk.emit(OpCode::RETURN, span);
        return;
    }

    if (tryAs<FunctionNode>(statement) != nullptr)
    {
        // Declarations are not executed. The first pass registered this one and
        // `run` emits its body after the program's `HALT`.
        return;
    }

    assert(false && "unhandled statement node in the bytecode compiler");
    throw CompileError(Diagnostic{Severity::Error, span,
                                  "internal error: unhandled statement node"});
}

// ---- function bodies ---------------------------------------------------

void Compiler::compileFunctionBody(const FunctionNode &function, std::uint16_t index)
{
    chunk.functions[index].entry =
        static_cast<std::uint32_t>(offsetOperand(chunk.code.size(), function.nameSpan));

    compileStatement(function.body);

    // `CONST 0; RETURN` at the end of every body, emitted whether or not the
    // body ends in a `return`. A body that runs off its end hands back `0` in
    // the tree-walker, so it must here; and emitting it unconditionally means
    // no body can fall through into the next one, which is a class of bug that
    // would first appear at item 4.3 as an answer from the wrong function.
    // Where it is unreachable it costs four bytes of a table nothing measures.
    chunk.emitOperand(OpCode::CONST,
                      constantFor(Value::fromInt(0), function.span), function.span);
    chunk.emit(OpCode::RETURN, function.span);
}

Chunk Compiler::run(const std::vector<Node> &statements, int programFrameSize)
{
    if (programFrameSize < 0 || static_cast<std::size_t>(programFrameSize) > maxOperand)
        throw CompileError(Diagnostic{Severity::Error, Span{},
                                      "too many local variables in one frame"});
    chunk.programFrameSize = programFrameSize;

    // Pass one: every function into the table, before a single byte of code, so
    // that a call compiled ahead of its callee's declaration already has an
    // index to name it by.
    declareFunctions(statements);

    // Pass two: the program's own statements, in order.
    for (const Node &statement : statements)
        compileStatement(statement);

    // `HALT` carries the last top-level statement's span, or a zero span for a
    // program with no statements at all. It cannot fault, so nothing renders
    // it; the span is there because every instruction has an entry and a
    // disassembly reads better with a line number than without one.
    chunk.emit(OpCode::HALT, statements.empty() ? Span{} : statements.back()->span);

    // Pass three: the bodies, in declaration order, after the `HALT`.
    std::uint16_t index = 0;
    for (const Node &statement : statements)
        if (const FunctionNode *function = tryAs<FunctionNode>(statement))
            compileFunctionBody(*function, index++);

    return std::move(chunk);
}

} // namespace

Chunk compile(const std::vector<Node> &statements, int programFrameSize)
{
    Compiler compiler;
    return compiler.run(statements, programFrameSize);
}
