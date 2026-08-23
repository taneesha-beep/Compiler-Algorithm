#include "resolver.h"

#include <map>
#include <set>
#include <string>

#include "diagnostic.h"

namespace
{

// ON THE SCOPING RULE. The language has no declaration keyword and the roadmap
// adds none, so an assignment is the only thing that can bring a variable into
// existence — and it is also the only thing that can update one. Which of the
// two a given assignment is depends on what is already visible where it stands:
//
//   * a name already visible, in the current scope or in any enclosing one, is
//     *assigned*: the assignment writes the variable that is already there;
//   * a name visible nowhere is *declared* in the current scope, and stops
//     being visible when that scope closes.
//
// The alternative — every assignment inside a block declaring a fresh
// block-local — was rejected because it takes the loop out of the language.
// `tests/while_sum.algo` is item 1.2's acceptance criterion:
//
//     while i <= 100 { total = total + i  i = i + 1 }
//
// Under that rule both assignments would declare new block-locals that die at
// the closing brace, so `total` would never accumulate and `i` would never
// reach 101. Phase 1 exists to make `fib(27)` expressible; a scoping rule that
// leaves the language unable to count to a hundred defeats the phase it is in.
//
// So what "an inner-block `x` is distinct from an outer `x`" means here is not
// that an inner declaration hides a live outer one — with assignment doubling
// as declaration, that is inexpressible, and the keyword it would need is a
// language feature no item lists. It means that a variable first assigned
// inside a block is a *different variable*, holding its own slot, from a
// same-named variable in any other scope, and that it is gone at the closing
// brace.
//
// Two consequences, both of them intended and both covered by a golden case:
// `{ x = 1 }` followed by `print x` is now a compile error where it used to
// print 1, and `while false { x = 1 }` followed by `print x` is a compile error
// where item 1.2 left it as a *runtime* fault — the interpreter reporting an
// undefined variable, because the flat check it replaced could not tell that a
// block might not run.
struct Resolver
{
    // Names to slots, for one scope. The program's own statement list is the
    // outermost scope; every block pushes another.
    using Scope = std::map<std::string, int>;

    // ON THE SINGLE FRAME. A slot is numbered within a *function* frame, and
    // the only frame that exists today is the program's own — functions arrive
    // with item 1.4, which is what turns this counter into one per frame with a
    // stack of them. There is no such stack here yet because there would be
    // nothing to put in it, and an unexercised one is untested code wearing the
    // shape of a design.
    int nextSlot = 0;

    // Innermost scope last.
    std::vector<Scope> scopes;

    // Names declared in a scope that has since closed. A use of one of those is
    // a different mistake from a use of a name that was never assigned at all —
    // the variable is right there in the source, two lines up — so the two get
    // different messages. This is the whole of the extra state that distinction
    // costs, and it costs it at compile time, where nothing is measured.
    std::set<std::string> retired;

    void pushScope() { scopes.emplace_back(); }

    void popScope()
    {
        for (const auto &binding : scopes.back())
            retired.insert(binding.first);
        scopes.pop_back();
    }

    // The slot of the innermost visible binding of `name`, or null if it has
    // none. Searched innermost-first, which is what makes a block-local the one
    // that is found while its block is open.
    const int *lookup(const std::string &name) const
    {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
        {
            auto binding = scope->find(name);
            if (binding != scope->end())
                return &binding->second;
        }
        return nullptr;
    }

    // Assignment as declaration: an already-visible name is bound to the slot
    // it already has, and a new one is created in the innermost scope and takes
    // the next slot in the frame.
    //
    // ON NOT REUSING SLOTS. A closing scope does not hand its slots back, so
    // the frame is as wide as the number of variables the program declares
    // rather than as its deepest scope. Reuse would save a few words in a
    // vector that nothing indexes yet, and would cost two things worth more
    // than that: two distinct variables would share a number, so "the inner `x`
    // is a different variable from the outer `x`" would stop being observable
    // in the very index this pass exists to assign; and item 3.4's environment,
    // sized once from the frame and never resized, would have live slots
    // aliasing dead ones.
    int declareOrBind(const std::string &name)
    {
        if (const int *existing = lookup(name))
            return *existing;
        const int slot = nextSlot++;
        scopes.back().emplace(name, slot);
        return slot;
    }

    // Both resolution errors point at the use rather than at the assignment
    // that is missing or out of reach: the identifier is the text that has to
    // change, and in the second case there is no single assignment to blame.
    [[noreturn]] void reportNotInScope(const IdentifierNode &identifier) const
    {
        const std::string message =
            retired.find(identifier.name) != retired.end()
                ? "variable '" + identifier.name +
                      "' used outside the block that assigns it"
                : "variable '" + identifier.name + "' used before assignment";
        throw CompileError(Diagnostic{Severity::Error, identifier.span, message});
    }

    void resolveExpression(const Node &node)
    {
        if (!node)
            return;
        if (IdentifierNode *identifier = tryAsMutable<IdentifierNode>(node))
        {
            const int *slot = lookup(identifier->name);
            if (!slot)
                reportNotInScope(*identifier);
            identifier->slot = *slot;
            return;
        }
        if (const BinOpNode *binary = tryAs<BinOpNode>(node))
        {
            resolveExpression(binary->left);
            resolveExpression(binary->right);
            return;
        }
        if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(node))
        {
            resolveExpression(unary->operand);
            return;
        }
        // A number or a boolean literal names nothing and has no children.
    }

    void resolveStatement(const Node &statement)
    {
        if (AssignNode *assign = tryAsMutable<AssignNode>(statement))
        {
            // The value before the name, so that `x = x + 1` with nothing
            // having assigned `x` is a use before assignment. Declaring first
            // would make a variable visible inside its own first assignment,
            // which is the one place it cannot yet hold anything.
            resolveExpression(assign->value);
            assign->slot = declareOrBind(assign->name);
            return;
        }
        if (const PrintNode *print = tryAs<PrintNode>(statement))
        {
            resolveExpression(print->value);
            return;
        }
        if (const BlockNode *block = tryAs<BlockNode>(statement))
        {
            pushScope();
            for (const Node &inner : block->statements)
                resolveStatement(inner);
            popScope();
            return;
        }
        if (const IfNode *conditional = tryAs<IfNode>(statement))
        {
            // The condition stands outside the branch it selects, so it is
            // resolved in the scope the `if` itself sits in. The `if` node
            // introduces no scope of its own: both branches are blocks — or,
            // for `else if`, another `if` whose branches are — and a block is
            // what pushes a scope.
            resolveExpression(conditional->condition);
            resolveStatement(conditional->thenBranch);
            if (conditional->elseBranch)
                resolveStatement(conditional->elseBranch);
            return;
        }
        if (const WhileNode *loop = tryAs<WhileNode>(statement))
        {
            // The body is walked once, however many times it runs. A name the
            // body declares is therefore in scope for the rest of the body and
            // nowhere else, which is the same answer for every iteration.
            resolveExpression(loop->condition);
            resolveStatement(loop->body);
            return;
        }
    }
};

} // namespace

// ON ARITY. Item 1.3 lists a third error, a call of the wrong arity, and it is
// not here: there are no functions to call until item 1.4, so there is no
// program that could raise it. It is deferred rather than stubbed — a stub
// would be a branch nothing can reach, which no test could fail and no reader
// could trust. 1.4 adds calls and the check together.
//
// ON DUPLICATE DECLARATION, the second error 1.3 lists: it is inexpressible in
// this language and no code here looks for it. Assignment is declaration, so
// the second `x = …` in a scope where `x` is already visible is a
// reassignment — which is what an assignment statement is for — and not a
// redeclaration of anything. The construct that would make the two
// distinguishable is a declaration keyword, and adding one is a language
// feature no roadmap item lists.
int resolve(const std::vector<Node> &statements)
{
    Resolver resolver;

    // The program's own scope, which nothing closes: it is not delimited by
    // braces, and a top-level name stays in scope to the end of the file.
    resolver.pushScope();
    for (const Node &statement : statements)
        resolver.resolveStatement(statement);

    return resolver.nextSlot;
}
