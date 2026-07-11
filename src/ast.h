#pragma once

#include <memory>
#include <string>

// ============================================================
// STAGE 2: AST — node definitions shared by parser, semantic
// analyzer, and interpreter
// ============================================================

// Every kind of node in the tree
enum class NodeType
{
    Assign,
    BinOp,
    Number,
    Identifier,
    Print
};

// Every node in the tree is one of these types
struct ASTNode
{
    NodeType type;
    std::string value; // holds number value or variable name or operator
    std::shared_ptr<ASTNode> left;
    std::shared_ptr<ASTNode> right;
};

using Node = std::shared_ptr<ASTNode>;

inline Node makeNode(NodeType type, std::string value = "",
                      Node left = nullptr, Node right = nullptr)
{
    return std::make_shared<ASTNode>(ASTNode{type, value, left, right});
}
