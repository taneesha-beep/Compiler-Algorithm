#include "interpreter.h"

#include <iostream>
#include <stdexcept>

#include "diagnostic.h"

namespace
{

// The caret goes on the whole operation rather than on one operand: an operand
// only has the wrong type relative to what is being done with it.
[[noreturn]] void binaryTypeFault(const Node &node, const Value &left,
                                  const Value &right)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, node->span,
        "operator '" + node->value + "' cannot be applied to " +
            typeName(left.type) + " and " + typeName(right.type)});
}

[[noreturn]] void unaryTypeFault(const Node &node, const Value &operand)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, node->span,
        "operator '" + node->value + "' cannot be applied to " +
            typeName(operand.type)});
}

// Arithmetic and ordering are integer-only. See the type rules in value.h.
void requireIntegers(const Node &node, const Value &left, const Value &right)
{
    if (!left.isInt() || !right.isInt())
        binaryTypeFault(node, left, right);
}

// Equality is the one binary group that accepts booleans, and it accepts them
// only against booleans: there is no conversion, so `1 == true` compares two
// different types and is a fault rather than `false`.
void requireSameType(const Node &node, const Value &left, const Value &right)
{
    if (left.type != right.type)
        binaryTypeFault(node, left, right);
}

} // namespace

Value Interpreter::evaluate(Node node)
{
    if (node->type == NodeType::Number)
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
            return Value::fromInt(std::stoi(node->value));
        }
        catch (const std::out_of_range &)
        {
            throw CompileError(Diagnostic{
                Severity::Error, node->span,
                "integer literal out of range: " + node->value});
        }
        catch (const std::invalid_argument &)
        {
            throw CompileError(Diagnostic{
                Severity::Error, node->span,
                "invalid integer literal: " + node->value});
        }
    }
    if (node->type == NodeType::True)
    {
        return Value::fromBool(true);
    }
    if (node->type == NodeType::False)
    {
        return Value::fromBool(false);
    }
    if (node->type == NodeType::Identifier)
    {
        // Unreachable while semanticCheck runs first, which is why this is a
        // fault of the engine rather than of the source: the resolver let
        // through a name the environment does not hold.
        if (variables.find(node->value) == variables.end())
            throw RuntimeFault(Diagnostic{
                Severity::Error, node->span,
                "undefined variable '" + node->value + "'"});
        return variables[node->value];
    }
    if (node->type == NodeType::BinOp)
    {
        Value left = evaluate(node->left);
        Value right = evaluate(node->right);

        // ON THE STRING COMPARISONS. Dispatching an operator by comparing its
        // text is one of the baseline's four unforced inefficiencies, and
        // ablation C is what removes it. The comparison operators added by item
        // 1.1 join the same chain on purpose: if they dispatched on an enum
        // while `+` dispatched on a string, ablation C would be measuring half
        // an operator set and its number would not mean what it says.
        const std::string &op = node->value;

        if (op == "+")
        {
            requireIntegers(node, left, right);
            return Value::fromInt(left.integer + right.integer);
        }
        if (op == "-")
        {
            requireIntegers(node, left, right);
            return Value::fromInt(left.integer - right.integer);
        }
        if (op == "*")
        {
            requireIntegers(node, left, right);
            return Value::fromInt(left.integer * right.integer);
        }
        if (op == "/")
        {
            requireIntegers(node, left, right);
            // The caret covers the whole division, not just the divisor: the
            // divisor is only zero in the context of what it divides.
            if (right.integer == 0)
                throw RuntimeFault(Diagnostic{Severity::Error, node->span,
                                              "division by zero"});
            return Value::fromInt(left.integer / right.integer);
        }
        if (op == "<")
        {
            requireIntegers(node, left, right);
            return Value::fromBool(left.integer < right.integer);
        }
        if (op == "<=")
        {
            requireIntegers(node, left, right);
            return Value::fromBool(left.integer <= right.integer);
        }
        if (op == ">")
        {
            requireIntegers(node, left, right);
            return Value::fromBool(left.integer > right.integer);
        }
        if (op == ">=")
        {
            requireIntegers(node, left, right);
            return Value::fromBool(left.integer >= right.integer);
        }
        if (op == "==")
        {
            requireSameType(node, left, right);
            return Value::fromBool(valuesEqual(left, right));
        }
        if (op == "!=")
        {
            requireSameType(node, left, right);
            return Value::fromBool(!valuesEqual(left, right));
        }
    }
    if (node->type == NodeType::UnaryOp)
    {
        Value operand = evaluate(node->left);
        const std::string &op = node->value;

        if (op == "-")
        {
            if (!operand.isInt())
                unaryTypeFault(node, operand);
            return Value::fromInt(-operand.integer);
        }
        if (op == "!")
        {
            if (!operand.isBool())
                unaryTypeFault(node, operand);
            return Value::fromBool(!operand.boolean);
        }
    }
    throw RuntimeFault(Diagnostic{Severity::Error, node->span,
                                  "unknown node type"});
}

void Interpreter::execute(const std::vector<Node> &statements)
{
    for (auto &stmt : statements)
    {
        if (stmt->type == NodeType::Assign)
        {
            variables[stmt->value] = evaluate(stmt->left);
        }
        else if (stmt->type == NodeType::Print)
        {
            // Written straight to the stream rather than through a
            // value-to-string helper, so that printing allocates nothing.
            Value value = evaluate(stmt->left);
            if (value.isBool())
                std::cout << (value.boolean ? "true" : "false") << std::endl;
            else
                std::cout << value.integer << std::endl;
        }
    }
}
