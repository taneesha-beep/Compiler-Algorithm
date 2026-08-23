#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "token.h" // Span

// ============================================================
// STAGE 2: AST — node definitions shared by parser, semantic
// analyzer, and interpreter
// ============================================================

// Every kind of node in the tree. Each names exactly one struct below, through
// that struct's `kind` constant, which is what makes `tryAs` safe.
enum class NodeType
{
    Assign,
    BinOp,
    UnaryOp,
    Number,
    Boolean,
    Identifier,
    Print,
    Block,
    If,
    While
};

// ON THE SPLIT. Until item 1.2 there was one ASTNode struct with a `value`
// string and exactly two children, `left` and `right`. A block holds an
// arbitrary number of statements and an `if` holds three children, so that
// struct could only have grown a third and fourth pointer whose meaning changed
// with the node type — `right` being the else-branch here and the loop body
// there. The roadmap forbids that and it is right to: the bytecode compiler in
// Phase 4 walks this tree, and a walker that has to remember which child means
// what per node type is where that phase goes wrong.
//
// So each node type is its own struct with named fields. What they share — the
// tag and the span — lives in this base.
//
// ON THE MISSING VIRTUAL DESTRUCTOR. There is none, deliberately. A virtual
// destructor puts a vtable pointer in every node, and both the size of a node
// and the cost of destroying one are things Phase 3 measures: ablation E
// allocates nodes from an arena, where per-node virtual dispatch on destruction
// is exactly the cost being removed. Deleting through `shared_ptr<ASTNode>` is
// nevertheless safe, because a `shared_ptr` captures its deleter from the
// concrete type at construction and does not consult the static type when the
// count reaches zero. Every node here is built by `makeNode`, which calls
// `std::make_shared<T>`, so every node is destroyed as the type it was made as.
// Deleting a bare `ASTNode *` would be undefined, and nothing does.
struct ASTNode
{
    NodeType type;
    Span span; // the source text this node was built from

    ASTNode(NodeType type, Span span) : type(type), span(span) {}
};

using Node = std::shared_ptr<ASTNode>;

// A `span` is the first parameter of every constructor below, and none of them
// has a default, so no call site can build a node without saying where it came
// from.

struct NumberNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Number;

    // The digits as they were written. Re-parsed on every evaluation, which is
    // ablation B's subject; item 3.2 replaces this with the parsed integer.
    std::string text;

    NumberNode(Span span, std::string text)
        : ASTNode(kind, span), text(std::move(text)) {}
};

// `true` and `false` were two node types when item 1.1 added them, because the
// only payload a node could carry then was a string, and one Boolean node would
// have had to re-read its text on every evaluation. A node type can carry a
// bool now, so the reason has gone and the two collapse into one.
struct BooleanNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Boolean;

    bool value;

    BooleanNode(Span span, bool value) : ASTNode(kind, span), value(value) {}
};

struct IdentifierNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Identifier;

    // Item 1.3 adds the frame slot index alongside this name, and the
    // interpreter stops looking the name up at run time.
    std::string name;

    IdentifierNode(Span span, std::string name)
        : ASTNode(kind, span), name(std::move(name)) {}
};

struct UnaryOpNode : ASTNode
{
    static constexpr NodeType kind = NodeType::UnaryOp;

    // Held as text, and compared as text when the operator is applied. That is
    // ablation C's subject, so it stays text until item 3.3.
    std::string op;
    Node operand;

    UnaryOpNode(Span span, std::string op, Node operand)
        : ASTNode(kind, span), op(std::move(op)), operand(std::move(operand)) {}
};

struct BinOpNode : ASTNode
{
    static constexpr NodeType kind = NodeType::BinOp;

    std::string op;
    Node left;
    Node right;

    BinOpNode(Span span, std::string op, Node left, Node right)
        : ASTNode(kind, span), op(std::move(op)), left(std::move(left)),
          right(std::move(right)) {}
};

struct AssignNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Assign;

    std::string name;
    Node value;

    AssignNode(Span span, std::string name, Node value)
        : ASTNode(kind, span), name(std::move(name)), value(std::move(value)) {}
};

struct PrintNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Print;

    Node value;

    PrintNode(Span span, Node value)
        : ASTNode(kind, span), value(std::move(value)) {}
};

// The node the two-child struct could not represent, and the reason this split
// happens now rather than in Phase 4.
struct BlockNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Block;

    std::vector<Node> statements;

    BlockNode(Span span, std::vector<Node> statements)
        : ASTNode(kind, span), statements(std::move(statements)) {}
};

struct IfNode : ASTNode
{
    static constexpr NodeType kind = NodeType::If;

    Node condition;
    Node thenBranch;
    Node elseBranch; // null when the `if` has no `else`

    IfNode(Span span, Node condition, Node thenBranch, Node elseBranch)
        : ASTNode(kind, span), condition(std::move(condition)),
          thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}
};

struct WhileNode : ASTNode
{
    static constexpr NodeType kind = NodeType::While;

    Node condition;
    Node body;

    WhileNode(Span span, Node condition, Node body)
        : ASTNode(kind, span), condition(std::move(condition)),
          body(std::move(body)) {}
};

// Builds a node of type T. `make_shared` is what captures the concrete type for
// the deleter — see the note on the missing virtual destructor above.
template <typename T, typename... Args>
inline Node makeNode(Args &&...args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

// The concrete node behind a Node, or null if it is a different kind.
//
// The tag comparison this performs is the same one the dispatch chains already
// did before the split (`node->type == NodeType::BinOp`), and the cast that
// follows it compiles to nothing — single inheritance, no virtual functions, so
// the base sits at offset zero. Reaching a node's fields therefore costs
// exactly what it cost before item 1.2, which matters because Phase 3 measures
// this path. Pairing the two also means the fields cannot be reached without
// the tag having matched, so an unchecked downcast is not expressible here.
//
// `node` must not be null. That is the same precondition every `node->type`
// read carried before the split; the one child that is legitimately absent —
// an `if` with no `else` — is tested for at its use.
template <typename T>
inline const T *tryAs(const Node &node)
{
    return node->type == T::kind ? static_cast<const T *>(node.get()) : nullptr;
}
