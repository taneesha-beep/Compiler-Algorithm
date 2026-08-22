#include "interpreter.h"

#include <iostream>
#include <stdexcept>

#include "diagnostic.h"

int Interpreter::evaluate(Node node)
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
            return std::stoi(node->value);
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
        int left = evaluate(node->left);
        int right = evaluate(node->right);
        if (node->value == "+")
            return left + right;
        if (node->value == "-")
            return left - right;
        if (node->value == "*")
            return left * right;
        if (node->value == "/")
        {
            // The caret covers the whole division, not just the divisor: the
            // divisor is only zero in the context of what it divides.
            if (right == 0)
                throw RuntimeFault(Diagnostic{Severity::Error, node->span,
                                              "division by zero"});
            return left / right;
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
            std::cout << evaluate(stmt->left) << std::endl;
        }
    }
}
