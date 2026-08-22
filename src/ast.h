#pragma once

#include <memory>
#include <string>

#include "token.h" // Span

// ============================================================
// STAGE 2: AST — node definitions shared by parser, semantic
// analyzer, and interpreter
// ============================================================

// Every kind of node in the tree.
//
// `true` and `false` are two node types rather than one carrying a flag. The
// interpreter already dispatches on `type`, so this costs nothing at run time,
// whereas one Boolean node would have to re-read its text on every evaluation —
// a string comparison in the hot path that no planned ablation removes. Item
// 3.2 removes the one the Number node has; adding a second would leave a cost
// in the baseline that the attribution has no row for.
enum class NodeType
{
    Assign,
    BinOp,
    UnaryOp,
    Number,
    True,
    False,
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
// build a node without saying where it came from. A UnaryOp keeps its single
// operand in `left` and leaves `right` null; item 1.2 splits this struct into
// distinct node types, at which point the operand gets a name of its own.
inline Node makeNode(NodeType type, std::string value, Span span,
                      Node left = nullptr, Node right = nullptr)
{
    return std::make_shared<ASTNode>(ASTNode{type, value, left, right, span});
}
