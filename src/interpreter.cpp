#include "interpreter.h"

#include <iostream>
#include <stdexcept>

int Interpreter::evaluate(Node node)
{
    if (node->type == NodeType::Number)
    {
        return std::stoi(node->value);
    }
    if (node->type == NodeType::Identifier)
    {
        if (variables.find(node->value) == variables.end())
            throw std::runtime_error("Runtime Error: Undefined variable '" + node->value + "'");
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
            if (right == 0)
                throw std::runtime_error("Runtime Error: Division by zero");
            return left / right;
        }
    }
    throw std::runtime_error("Unknown node type");
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
