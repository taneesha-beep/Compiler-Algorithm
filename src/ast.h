#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "token.h" // Span

// ============================================================
// STAGE 2: AST — node definitions shared by the parser, the
// resolver, and the interpreter
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
    While,
    Function,
    Call,
    Return
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
// destructor puts a vtable pointer in every node, and node size is something no
// ablation in the series accounts for. It was ablation E — arena-allocating the
// nodes, where per-node virtual dispatch on destruction is exactly the cost
// being removed — that would have priced it, and **E was cut on 2026-08-30**,
// so nothing measures node layout at all now. That makes the argument stronger
// rather than weaker: a size change here has no row to be attributed to. Item
// 3.2 grew `NumberNode` by eight bytes and could not avoid it; see the note
// there. Deleting through `shared_ptr<ASTNode>` is
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

// The value of a slot index before the resolver has assigned one. Every node
// that names a variable carries a slot; item 1.3's pass overwrites this on all
// of them, and one still holding it after the pass is a hole in that walk —
// which is what `tests/resolver_test.cpp` asserts against, since a slot is not
// visible in anything the program prints.
inline constexpr int unresolvedSlot = -1;

// A `span` is the first parameter of every constructor below, and none of them
// has a default, so no call site can build a node without saying where it came
// from.

struct NumberNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Number;

    // The digits as they were written, and the integer they denote. Item 3.2
    // — ablation B — added `value`: the parser converts the digits once, when
    // the node is built, where the interpreter used to call `std::stoll(text)`
    // on every evaluation of the node. See `integerValueOf` in
    // `src/parser.cpp`, which is where the conversion and its range check now
    // live.
    //
    // ON KEEPING `text`. Nothing on the hot path reads it any more, so it
    // could have gone, and dropping it would have shrunk the node by a whole
    // `std::string`. That would have made B two changes at once — the
    // re-parse *and* a change in node size — and node size is not something
    // any ablation in the series accounts for; it is what the cut ablation E
    // would have priced, and it is why this file has no virtual destructor.
    // The rule is item 3.1's: remove exactly the named cost and nothing else,
    // the same reason `evaluate` took `const Node &` rather than a raw
    // pointer. The digits are also still the text a diagnostic quotes, and
    // `tests/span_test.cpp` and `tests/expression_test.cpp` read them to
    // identify a node.
    std::string text;
    std::int64_t value;

    NumberNode(Span span, std::string text, std::int64_t value)
        : ASTNode(kind, span), text(std::move(text)), value(value) {}
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

    std::string name;

    // The variable's slot within its enclosing function frame, written by the
    // resolver in item 1.3. Nothing reads it yet, deliberately: the interpreter
    // still looks `name` up in a std::map keyed on the string, which is the
    // ordered-map lookup ablation D exists to remove. Item 3.4 is the commit
    // that starts reading this field, and the difference that makes is the
    // number ablation D reports. Writing the slot here and spending it there is
    // the whole arrangement — a 1.3 that also switched the environment over
    // would have performed the ablation with nothing measuring it.
    int slot = unresolvedSlot;

    IdentifierNode(Span span, std::string name)
        : ASTNode(kind, span), name(std::move(name)) {}
};

// Which operator a `UnaryOpNode` or a `BinOpNode` applies. Item 3.3 — ablation
// C — added both, and the parser settles which operator was written once, when
// the node is built. The interpreter used to compare the node's text against
// one string literal after another on every evaluation, so `+` cost one
// comparison and `!=` cost ten. See `binOpKindOf` and `unaryOpKindOf` in
// `src/parser.cpp`, which is where the mapping now lives, and the two switches
// in `Interpreter::evaluate`, which is what reads these.
//
// ON THE ORDER OF THE ENUMERATORS. They are in the order the string chain
// tested them, because what the chain cost an operation was a function of the
// operator's position in it — `+` was tested first and `!=` tenth — and item
// 3.3's per-operation model is fitted against those positions. Reordering them
// would not change what the program computes and would make that model
// unreadable against the code it describes.
//
// ON COVERING THE UNARY OPERATORS. The roadmap names only `BinOpKind`, but the
// cost ablation C is defined against is operator dispatch by string
// comparison, and the note on `UnaryOpNode` below has said since item 1.1 that
// its text comparison is C's subject too. Leaving `-` and `!` comparing text
// would have left a two-comparison chain on the hot path with no row to
// attribute it to, and C's number would mean "most of the operator dispatch"
// while the ledger recorded it as "the operator dispatch". None of the four
// benchmark programs applies a unary operator inside its loop, so this changes
// almost nothing in the measurement and settles what the number may be called.
enum class BinOpKind
{
    Add,
    Subtract,
    Multiply,
    Divide,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual
};

enum class UnaryOpKind
{
    Negate,
    Not
};

struct UnaryOpNode : ASTNode
{
    static constexpr NodeType kind = NodeType::UnaryOp;

    // The operator as it was written, and which operator that is. Item 3.3 —
    // ablation C — added `opKind`; until then the text was also what the
    // interpreter compared against `"-"` and `"!"` to decide which operator to
    // apply. `op` stays, for the reason given on `BinOpNode` below.
    std::string op;
    UnaryOpKind opKind;
    Node operand;

    UnaryOpNode(Span span, std::string op, UnaryOpKind opKind, Node operand)
        : ASTNode(kind, span), op(std::move(op)), opKind(opKind),
          operand(std::move(operand)) {}
};

struct BinOpNode : ASTNode
{
    static constexpr NodeType kind = NodeType::BinOp;

    // The operator as it was written, and which operator that is — the same
    // pairing item 3.2 gave `NumberNode`, and added by item 3.3 for the same
    // kind of reason: the walk reads a field where it used to walk a chain of
    // string comparisons.
    //
    // ON KEEPING `op`. There is no option that leaves the node's size alone,
    // and node size is not something any ablation in this series accounts for
    // — it is what the cut ablation E would have priced, and it is why this
    // file has no virtual destructor. So the rule is item 3.2's: take the
    // smaller of the two available perturbations. Dropping the digits takes
    // this node from 80 bytes to 56 where adding the enumerator beside them
    // takes it to 88, and `UnaryOpNode` moves the same way, 64 to 40 against 64
    // to 72 — a change of 24 bytes against one of 8, measured with a `sizeof`
    // probe compiled against each variant under the toolchain that takes the
    // rows (GCC 13.3.0, libstdc++). Item 3.2 chose +8 over −24 on exactly this
    // arithmetic for `NumberNode::text`.
    //
    // Here there is a second reason, and it is the stronger one in practice.
    // Four diagnostics quote this text — the two "operator '<op>' cannot be
    // applied to …" messages, "integer overflow in '<op>'", and "integer
    // overflow in unary '<op>'" — and `tests/diagnostic_test.cpp` pins them
    // byte for byte. Reconstructing that text from the enumerator would be a
    // second mapping to keep in step with this one, in the one place where
    // being wrong is invisible until a user sees it.
    std::string op;
    BinOpKind opKind;
    Node left;
    Node right;

    BinOpNode(Span span, std::string op, BinOpKind opKind, Node left, Node right)
        : ASTNode(kind, span), op(std::move(op)), opKind(opKind),
          left(std::move(left)), right(std::move(right)) {}
};

struct AssignNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Assign;

    std::string name;
    Node value;

    // The slot this assignment writes — the same field as on IdentifierNode
    // and for the same reason. An assignment target is a variable reference
    // too, and item 3.4 has to index the environment to write a variable as
    // well as to read one; if only reads carried a slot, that ablation would be
    // half applied and its number would mean half of what it says.
    int slot = unresolvedSlot;

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

// One name in a function's parameter list. It carries its own span because a
// parameter is the first construct in this language that can be *declared*
// twice: `fn f(a, a)` is a duplicate declaration, and the caret has to point
// at the second `a` rather than at the whole list. Item 1.3 recorded duplicate
// declaration as inexpressible, which was true while assignment was the only
// way to introduce a name — the second `x = 1` in a scope is a reassignment.
// A parameter is a declaration and nothing else, so the error becomes real
// here.
struct Parameter
{
    std::string name;
    Span span;

    // The parameter's slot in the function's own frame, written by the
    // resolver. Read by nothing until item 3.4, exactly as `IdentifierNode`'s
    // is — binding an argument is a write to a variable, so ablation D has to
    // index it on the same terms as every other write.
    int slot = unresolvedSlot;
};

// A function declaration. Top-level only: the parser refuses one anywhere a
// statement may otherwise stand, so `body` is the only place a function's
// statements can be and no function can be declared inside another.
//
// Functions are **not values.** There is no node that produces one, no way to
// pass one, and no way to store one — see the Out of scope table in the
// roadmap, which excludes closures and first-class functions because upvalue
// capture roughly doubles the Phase 4 VM and no benchmark needs it. A
// consequence worth stating: function names and variable names live in two
// separate namespaces, so `fn f(f)` is legal and the `f` inside the body is
// the parameter, while `f(1)` is the function.
struct FunctionNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Function;

    std::string name;
    Span nameSpan; // just the name, for a diagnostic about the declaration
    std::vector<Parameter> parameters;
    Node body; // always a BlockNode

    // How many slots this function's frame needs, written by the resolver.
    // The counterpart of `resolve()`'s return value, which reports the same
    // number for the program's own frame. Item 3.4 is what sizes a per-call
    // environment with it; nothing reads it before then.
    int frameSize = 0;

    FunctionNode(Span span, std::string name, Span nameSpan,
                 std::vector<Parameter> parameters, Node body)
        : ASTNode(kind, span), name(std::move(name)), nameSpan(nameSpan),
          parameters(std::move(parameters)), body(std::move(body)) {}
};

// A call, which is an expression and the only thing a function name may
// appear in. The callee is a name rather than a sub-expression, because a
// sub-expression would have to evaluate to a function and functions are not
// values.
struct CallNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Call;

    std::string callee;
    std::vector<Node> arguments;

    CallNode(Span span, std::string callee, std::vector<Node> arguments)
        : ASTNode(kind, span), callee(std::move(callee)),
          arguments(std::move(arguments)) {}
};

// `return expr` or the bare `return`, whose `value` is null. What a bare
// return hands back is the integer 0 — see the note on it in
// `src/interpreter.cpp`.
struct ReturnNode : ASTNode
{
    static constexpr NodeType kind = NodeType::Return;

    Node value; // null for the bare form

    ReturnNode(Span span, Node value)
        : ASTNode(kind, span), value(std::move(value)) {}
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

// The same tag comparison, for the one pass that has to write to the tree. The
// resolver assigns a frame slot to every variable reference, so it needs a
// non-const view of the node it has just identified. Everything else — the
// interpreter, every test — only reads, and uses `tryAs`.
//
// The two are separate names rather than a const/non-const overload pair so
// that a write to the tree after parsing is greppable. That matters from here
// on: Phase 4 walks this tree with a second back end beside the interpreter,
// and exactly one walk over it is allowed to change it.
//
// Note that the constness of the `Node` says nothing about the node itself —
// `shared_ptr<ASTNode>::get()` hands back a non-const `ASTNode *` either way,
// so `tryAs` returns const by choice rather than by obligation, and this is the
// single place that choice is made differently.
template <typename T>
inline T *tryAsMutable(const Node &node)
{
    return node->type == T::kind ? static_cast<T *>(node.get()) : nullptr;
}
