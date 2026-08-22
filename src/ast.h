#pragma once

#include <memory>
#include <string>

#include "token.h" // Span

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
    Span span; // the source text this node was built from
};

using Node = std::shared_ptr<ASTNode>;

// `span` is required and comes before the children so that no call site can
// build a node without saying where it came from.
inline Node makeNode(NodeType type, std::string value, Span span,
                      Node left = nullptr, Node right = nullptr)
{
    return std::make_shared<ASTNode>(ASTNode{type, value, left, right, span});
}
