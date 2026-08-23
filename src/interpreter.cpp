#include "interpreter.h"

#include <iostream>
#include <stdexcept>

#include "diagnostic.h"

namespace
{

// The caret goes on the whole operation rather than on one operand: an operand
// only has the wrong type relative to what is being done with it.
[[noreturn]] void binaryTypeFault(const BinOpNode &node, const Value &left,
                                  const Value &right)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, node.span,
        "operator '" + node.op + "' cannot be applied to " +
            typeName(left.type) + " and " + typeName(right.type)});
}

[[noreturn]] void unaryTypeFault(const UnaryOpNode &node, const Value &operand)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, node.span,
        "operator '" + node.op + "' cannot be applied to " +
            typeName(operand.type)});
}

// Arithmetic and ordering are integer-only. See the type rules in value.h.
void requireIntegers(const BinOpNode &node, const Value &left, const Value &right)
{
    if (!left.isInt() || !right.isInt())
        binaryTypeFault(node, left, right);
}

// Equality is the one binary group that accepts booleans, and it accepts them
// only against booleans: there is no conversion, so `1 == true` compares two
// different types and is a fault rather than `false`.
void requireSameType(const BinOpNode &node, const Value &left, const Value &right)
{
    if (left.type != right.type)
        binaryTypeFault(node, left, right);
}

// A condition is a boolean and nothing else: an integer is not truthy. The
// caret goes on the condition itself, which is the text that has to change.
void requireCondition(const Node &condition, const Value &value)
{
    if (!value.isBool())
        throw RuntimeFault(Diagnostic{
            Severity::Error, condition->span,
            std::string("a condition must be a boolean, not ") +
                typeName(value.type)});
}

} // namespace

Value Interpreter::evaluate(Node node)
{
    // ON THE DISPATCH. A chain of tag comparisons, one per node type, exactly
    // as it was before the node split. `tryAs` performs the same comparison the
    // chain performed before and hands back the concrete node when it matches,
    // so reaching a field costs what it cost then. Turning this into a switch
    // would be a dispatch change that no planned ablation accounts for, and
    // Phase 3 would have no row to attribute it to.
    if (const NumberNode *number = tryAs<NumberNode>(node))
    {
        // ON THE CLASSIFICATION. A literal too wide for the value type is a
        // property of the token's text, not of anything the program computes,
        // so it is a *compile-time* error (65) even though it is detected here,
        // while the tree is being walked. Item 3.2 moves the detection to parse
        // time; classing it as a runtime fault today would make 3.2 flip the
        // observable exit code 70 -> 65 in the middle of the ablation series,
        // which is exactly the contamination this project exists to avoid. The
        // exit code is fixed now so that 3.2 changes only when the check runs.
        try
        {
            return Value::fromInt(std::stoi(number->text));
        }
        catch (const std::out_of_range &)
        {
            throw CompileError(Diagnostic{
                Severity::Error, number->span,
                "integer literal out of range: " + number->text});
        }
        catch (const std::invalid_argument &)
        {
            throw CompileError(Diagnostic{
                Severity::Error, number->span,
                "invalid integer literal: " + number->text});
        }
    }
    if (const BooleanNode *boolean = tryAs<BooleanNode>(node))
    {
        return Value::fromBool(boolean->value);
    }
    if (const IdentifierNode *identifier = tryAs<IdentifierNode>(node))
    {
        // Reachable since item 1.2. semanticCheck holds one flat set of names
        // and does not know that a block may not run, so a variable first
        // assigned inside an `if` or a `while` body passes the check and is
        // still absent here when the body did not execute. Item 1.3's resolver
        // is what turns that back into a compile-time error.
        if (variables.find(identifier->name) == variables.end())
            throw RuntimeFault(Diagnostic{
                Severity::Error, identifier->span,
                "undefined variable '" + identifier->name + "'"});
        return variables[identifier->name];
    }
    if (const BinOpNode *binary = tryAs<BinOpNode>(node))
    {
        Value left = evaluate(binary->left);
        Value right = evaluate(binary->right);

        // ON THE STRING COMPARISONS. Dispatching an operator by comparing its
        // text is one of the baseline's four unforced inefficiencies, and
        // ablation C is what removes it. The comparison operators added by item
        // 1.1 join the same chain on purpose: if they dispatched on an enum
        // while `+` dispatched on a string, ablation C would be measuring half
        // an operator set and its number would not mean what it says.
        const std::string &op = binary->op;

        if (op == "+")
        {
            requireIntegers(*binary, left, right);
            return Value::fromInt(left.integer + right.integer);
        }
        if (op == "-")
        {
            requireIntegers(*binary, left, right);
            return Value::fromInt(left.integer - right.integer);
        }
        if (op == "*")
        {
            requireIntegers(*binary, left, right);
            return Value::fromInt(left.integer * right.integer);
        }
        if (op == "/")
        {
            requireIntegers(*binary, left, right);
            // The caret covers the whole division, not just the divisor: the
            // divisor is only zero in the context of what it divides.
            if (right.integer == 0)
                throw RuntimeFault(Diagnostic{Severity::Error, binary->span,
                                              "division by zero"});
            return Value::fromInt(left.integer / right.integer);
        }
        if (op == "<")
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer < right.integer);
        }
        if (op == "<=")
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer <= right.integer);
        }
        if (op == ">")
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer > right.integer);
        }
        if (op == ">=")
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer >= right.integer);
        }
        if (op == "==")
        {
            requireSameType(*binary, left, right);
            return Value::fromBool(valuesEqual(left, right));
        }
        if (op == "!=")
        {
            requireSameType(*binary, left, right);
            return Value::fromBool(!valuesEqual(left, right));
        }
    }
    if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(node))
    {
        Value operand = evaluate(unary->operand);
        const std::string &op = unary->op;

        if (op == "-")
        {
            if (!operand.isInt())
                unaryTypeFault(*unary, operand);
            return Value::fromInt(-operand.integer);
        }
        if (op == "!")
        {
            if (!operand.isBool())
                unaryTypeFault(*unary, operand);
            return Value::fromBool(!operand.boolean);
        }
    }
    throw RuntimeFault(Diagnostic{Severity::Error, node->span,
                                  "unknown node type"});
}

// ON THE ABSENT SCOPE. A block groups statements and nothing more: it does not
// push an environment, so a variable assigned inside one outlives it and an
// inner `x` is the same `x` as the outer one. Shadowing is item 1.3's, and the
// roadmap puts it there — 1.3's acceptance criterion is the one that reads "an
// inner-block `x` is distinct from an outer `x`", and its resolver is what
// carries the scope stack.
//
// Building a scope stack here would also spoil what comes after it. Ablation D
// measures replacing this environment — one ordered map keyed on std::string —
// with frame slots. Make it a *stack* of ordered maps now and ablation D's
// baseline quietly becomes something the roadmap never described, so its number
// would no longer answer the question it was written to answer.
void Interpreter::executeStatement(const Node &statement)
{
    if (const AssignNode *assign = tryAs<AssignNode>(statement))
    {
        variables[assign->name] = evaluate(assign->value);
        return;
    }
    if (const PrintNode *print = tryAs<PrintNode>(statement))
    {
        // Written straight to the stream rather than through a
        // value-to-string helper, so that printing allocates nothing.
        Value value = evaluate(print->value);
        if (value.isBool())
            std::cout << (value.boolean ? "true" : "false") << std::endl;
        else
            std::cout << value.integer << std::endl;
        return;
    }
    if (const BlockNode *block = tryAs<BlockNode>(statement))
    {
        for (const Node &inner : block->statements)
            executeStatement(inner);
        return;
    }
    if (const IfNode *conditional = tryAs<IfNode>(statement))
    {
        Value condition = evaluate(conditional->condition);
        requireCondition(conditional->condition, condition);
        if (condition.boolean)
            executeStatement(conditional->thenBranch);
        else if (conditional->elseBranch)
            executeStatement(conditional->elseBranch);
        return;
    }
    if (const WhileNode *loop = tryAs<WhileNode>(statement))
    {
        // The condition is re-evaluated every turn, and nothing counts the
        // turns: a loop that never ends never ends. An iteration cap would be a
        // branch inside the hot path of every benchmark this project measures,
        // and it would be a language semantic the roadmap does not list. The
        // guard against a runaway program belongs to whatever runs it — the
        // test suite sets a CTest timeout, which is where it costs nothing.
        while (true)
        {
            Value condition = evaluate(loop->condition);
            requireCondition(loop->condition, condition);
            if (!condition.boolean)
                break;
            executeStatement(loop->body);
        }
        return;
    }

    throw RuntimeFault(Diagnostic{Severity::Error, statement->span,
                                  "unknown statement type"});
}

// The program itself is a statement list rather than a block: it is not
// delimited by braces and introduces no scope of its own.
//
// `executeStatement` takes its Node by reference, unlike `evaluate`. The
// by-value pass in `evaluate` is ablation A's subject and has to stay; adding a
// second one here would put reference-count traffic into ablation A's
// measurement that item 3.1 was never going to remove, and inflate it.
void Interpreter::execute(const std::vector<Node> &statements)
{
    for (const Node &statement : statements)
        executeStatement(statement);
}
