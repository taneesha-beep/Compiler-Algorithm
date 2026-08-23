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
//
// ------------------------------------------------------------------
//
// ON THE FRAME BOUNDARY, added by item 1.4, and the one rule above that this
// one overrides. A slot is an index *within a function frame*, so "visible in
// an enclosing scope" has to stop at the edge of the frame: a name in the
// program's own frame and a name in a function's frame are numbered from two
// different counters, and one index cannot mean both. A function body
// therefore sees its parameters and its own locals, and nothing else — not a
// top-level variable, not another function's local.
//
// That is a scoping decision and not merely an implementation one, so it is
// worth stating why it is the right one here. Functions are not values and
// there are no closures (see the roadmap's Out of scope table), so a body
// reading a top-level name would be reading a *global*, which is a third kind
// of storage beside the frame and the slot — one the roadmap never describes
// and item 3.4 has no vector for. Everything a function needs, it is passed.
//
// The lookup below is what enforces it: it searches the current frame's scopes
// and stops. `visibleInAnEnclosingFrame` exists only to tell a reader who
// wrote `x` inside a function, meaning the `x` at the top of the file, what
// actually happened — a diagnostic, not a fallback.
struct Resolver
{
    // Names to slots, for one scope. A function's parameter list is the
    // outermost scope of its frame, and the program's own statement list is
    // the outermost scope of the program's frame; every block pushes another.
    using Scope = std::map<std::string, int>;

    // ON THE FRAME STACK. A slot is numbered within a *function* frame, and
    // until item 1.4 the only frame that existed was the program's own, so
    // this was a single counter and a single scope stack. A function body is a
    // frame of its own — its parameters and locals are numbered from zero
    // again — so the counter, the scopes it numbers into, and the record of
    // what has gone out of scope all move into a Frame, and the resolver
    // carries a stack of them.
    struct Frame
    {
        int nextSlot = 0;

        // Innermost scope last.
        std::vector<Scope> scopes;

        // Names declared in a scope of this frame that has since closed. A use
        // of one of those is a different mistake from a use of a name that was
        // never assigned at all — the variable is right there in the source,
        // two lines up — so the two get different messages. This is the whole
        // of the extra state that distinction costs, and it costs it at
        // compile time, where nothing is measured.
        std::set<std::string> retired;
    };

    // Innermost frame last. The program's own frame is frames.front(), so
    // `frames.size() == 1` is exactly "at the top level", which is what tells
    // a `return` outside any function from one inside one.
    std::vector<Frame> frames;

    // What is known about a declared function: how many parameters it takes,
    // which is all a call site has to agree with. Collected in a pass of its
    // own before anything is resolved, so that a function may call one
    // declared further down the file — and so that a function may call itself,
    // which is the whole point of the item this belongs to.
    //
    // A separate namespace from the variables above, deliberately: functions
    // are not values, so a name in this map and a name in a scope are two
    // different things that happen to be spelled alike. `fn f(f)` is legal.
    std::map<std::string, std::size_t> functionArity;

    Frame &frame() { return frames.back(); }
    const Frame &frame() const { return frames.back(); }

    void pushFrame() { frames.emplace_back(); }
    void popFrame() { frames.pop_back(); }

    void pushScope() { frame().scopes.emplace_back(); }

    void popScope()
    {
        for (const auto &binding : frame().scopes.back())
            frame().retired.insert(binding.first);
        frame().scopes.pop_back();
    }

    // The slot of the innermost visible binding of `name`, or null if it has
    // none. Searched innermost-first, which is what makes a block-local the one
    // that is found while its block is open — and only within the current
    // frame, which is what stops a function body from reaching a top-level
    // variable.
    const int *lookup(const std::string &name) const
    {
        const Frame &current = frame();
        for (auto scope = current.scopes.rbegin(); scope != current.scopes.rend();
             ++scope)
        {
            auto binding = scope->find(name);
            if (binding != scope->end())
                return &binding->second;
        }
        return nullptr;
    }

    // Whether some frame *outside* the current one can see `name`. Used only
    // to word a diagnostic: a name that would have resolved at the top level
    // and does not resolve inside a function has been stopped by the frame
    // boundary, and saying so is more use than reporting it as never assigned.
    bool visibleInAnEnclosingFrame(const std::string &name) const
    {
        for (std::size_t depth = 0; depth + 1 < frames.size(); depth++)
            for (const Scope &scope : frames[depth].scopes)
                if (scope.find(name) != scope.end())
                    return true;
        return false;
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
        const int slot = frame().nextSlot++;
        frame().scopes.back().emplace(name, slot);
        return slot;
    }

    // A declaration proper, which until item 1.4 the language had none of. A
    // parameter does not bind to a name that is already there the way an
    // assignment does — two parameters of one name are a mistake, not a
    // reassignment — so this refuses to reuse a slot and reports instead.
    int declareParameter(const Parameter &parameter)
    {
        Scope &scope = frame().scopes.back();
        if (scope.find(parameter.name) != scope.end())
            throw CompileError(Diagnostic{
                Severity::Error, parameter.span,
                "duplicate parameter '" + parameter.name + "'"});
        const int slot = frame().nextSlot++;
        scope.emplace(parameter.name, slot);
        return slot;
    }

    // All three variable errors point at the use rather than at the assignment
    // that is missing or out of reach: the identifier is the text that has to
    // change, and in the second and third cases there is no single assignment
    // to blame.
    [[noreturn]] void reportNotInScope(const IdentifierNode &identifier) const
    {
        const std::string &name = identifier.name;
        std::string message;

        if (frame().retired.find(name) != frame().retired.end())
            message = "variable '" + name + "' used outside the block that assigns it";
        else if (visibleInAnEnclosingFrame(name))
            message = "variable '" + name +
                      "' used inside a function, where only parameters and locals "
                      "are in scope";
        else if (functionArity.find(name) != functionArity.end())
            message = "'" + name + "' is a function, not a value";
        else
            message = "variable '" + name + "' used before assignment";

        throw CompileError(Diagnostic{Severity::Error, identifier.span, message});
    }

    // "expects 2 arguments" / "expects 1 argument".
    static std::string countOfArguments(std::size_t n)
    {
        return std::to_string(n) + (n == 1 ? " argument" : " arguments");
    }

    void resolveCall(const CallNode &call)
    {
        auto declared = functionArity.find(call.callee);
        if (declared == functionArity.end())
        {
            // A name that is a visible variable was more likely meant as one
            // than as a function that was never written, and saying which of
            // the two namespaces it is in is the whole of the answer.
            const std::string message =
                lookup(call.callee)
                    ? "'" + call.callee + "' is a variable, not a function"
                    : "unknown function '" + call.callee + "'";
            throw CompileError(Diagnostic{Severity::Error, call.span, message});
        }

        // ON ARITY, the third resolution error item 1.3 lists and defers. The
        // caret covers the whole call rather than the argument list alone,
        // following the same rule the type faults use: an argument count is
        // only wrong relative to what is being called, and the callee's name
        // is half of what the reader has to check.
        const std::size_t expected = declared->second;
        const std::size_t given = call.arguments.size();
        if (given != expected)
            throw CompileError(Diagnostic{
                Severity::Error, call.span,
                "function '" + call.callee + "' expects " +
                    countOfArguments(expected) + ", but " + std::to_string(given) +
                    (given == 1 ? " was given" : " were given")});

        for (const Node &argument : call.arguments)
            resolveExpression(argument);
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
        if (const CallNode *call = tryAs<CallNode>(node))
        {
            resolveCall(*call);
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
        if (const ReturnNode *returned = tryAs<ReturnNode>(statement))
        {
            // There is nothing at the top level for a `return` to return from
            // — the program is a statement list, not a function body — so this
            // is a resolution error rather than something the interpreter has
            // to have an answer for at run time.
            if (frames.size() == 1)
                throw CompileError(Diagnostic{Severity::Error, statement->span,
                                              "'return' outside of a function"});
            resolveExpression(returned->value); // null for the bare form
            return;
        }
        if (FunctionNode *function = tryAsMutable<FunctionNode>(statement))
        {
            // Only reachable from the top-level walk: the parser rejects a
            // function anywhere a statement may stand. `tryAsMutable` because
            // this pass writes a slot onto each parameter and the frame width
            // onto the function, which is the same write it makes on every
            // other variable reference.
            resolveFunction(*function);
            return;
        }
    }

    // A function body is resolved in a frame of its own, so its slots are
    // numbered from zero and its names cannot reach out. The parameters are
    // the frame's outermost scope; the body is a block, so resolving it pushes
    // a second scope inside that one — which is what makes `fn f(a) { a = 1 }`
    // an assignment to the parameter rather than a declaration of a new local.
    void resolveFunction(FunctionNode &function)
    {
        pushFrame();
        pushScope();

        for (Parameter &parameter : function.parameters)
            parameter.slot = declareParameter(parameter);

        resolveStatement(function.body);

        function.frameSize = frame().nextSlot;

        popScope();
        popFrame();
    }

    // Pass one: every function's name and arity, before any body is walked.
    // Without it a call could only name a function declared above it, which
    // rules out mutual recursion and — since a function is not in scope inside
    // its own body until its declaration is finished — direct recursion too.
    void collectFunctions(const std::vector<Node> &statements)
    {
        for (const Node &statement : statements)
        {
            const FunctionNode *function = tryAs<FunctionNode>(statement);
            if (!function)
                continue;

            // The other declaration item 1.3 could not express. Two functions
            // of one name are not a reassignment of anything: there is no
            // statement here, only a declaration, so the second is a mistake.
            if (functionArity.find(function->name) != functionArity.end())
                throw CompileError(Diagnostic{
                    Severity::Error, function->nameSpan,
                    "duplicate function '" + function->name + "'"});

            functionArity.emplace(function->name, function->parameters.size());
        }
    }
};

} // namespace

// ON DUPLICATE DECLARATION, which item 1.3 recorded as inexpressible and item
// 1.4 makes expressible twice over. That entry was right about what it
// described: assignment is declaration, so the second `x = …` in a scope where
// `x` is already visible is a reassignment, and no declaration keyword exists
// to make the two distinguishable. Item 1.4 introduces the language's first
// two constructs that are declarations and nothing else — a parameter name and
// a function name — and neither has a reassignment reading. Both are checked
// above, in `declareParameter` and `collectFunctions`.
//
// So the three errors item 1.3 lists are all real now: use before declaration,
// duplicate declaration, and arity mismatch.
int resolve(const std::vector<Node> &statements)
{
    Resolver resolver;

    // Pass one: what functions exist and how many parameters each takes.
    resolver.collectFunctions(statements);

    // Pass two, in source order so that diagnostics come out in it. The
    // program's own frame and its own scope, which nothing closes: it is not
    // delimited by braces, and a top-level name stays in scope to the end of
    // the file.
    resolver.pushFrame();
    resolver.pushScope();
    for (const Node &statement : statements)
        resolver.resolveStatement(statement);

    return resolver.frame().nextSlot;
}
