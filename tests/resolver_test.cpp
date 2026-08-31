// Unit checks for the resolution pass — roadmap item 1.3.
//
// Most of what this pass does is invisible from a stream. A slot index is
// written onto a node, and since item 3.4 the interpreter indexes a frame by
// it — but no program can print one and no golden case can assert one, and a
// wrong slot shows up only as a wrong answer somewhere else; the acceptance
// criterion "every
// identifier node carries a valid slot index after the pass" is only reachable
// by walking the tree, which is what this binary does. Scoping is half visible
// — a program that used to run and now does not shows up as an exit code — but
// which slot two same-named variables ended up with does not, and that is the
// part that decides whether they were ever really two variables.
//
// The walk below is written out again rather than shared with the resolver's.
// A walk that skipped the body of a `while` would then skip it in both, and the
// two would agree with each other about a node neither had visited.
//
// A failed check prints to stderr and the process exits non-zero, which is all
// CTest reads — no third-party test framework, in keeping with the project
// having no external dependencies.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "ast.h"
#include "diagnostic.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "token.h"

namespace
{

int checks = 0;
int failures = 0;

void check(bool condition, const std::string &what)
{
    checks++;
    if (condition)
        return;
    failures++;
    std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

void checkText(const std::string &what, const std::string &actual,
               const std::string &expected)
{
    checks++;
    if (actual == expected)
        return;
    failures++;
    std::fprintf(stderr,
                 "FAIL %s\n--- expected ---\n%s--- actual ---\n%s--- end ---\n",
                 what.c_str(), expected.c_str(), actual.c_str());
}

// ============================================================
// Walking the tree for everything that names a variable
// ============================================================

// One mention of a variable in the source, in source order: the assignment
// target and the read are both here, because both have to carry a slot for the
// interpreter to index the environment on either side of an assignment — which
// it has done since item 3.4.
struct Reference
{
    std::string name;
    int slot;
    bool isTarget;
};

void collect(const Node &node, std::vector<Reference> &out)
{
    if (!node)
        return;
    if (const AssignNode *assign = tryAs<AssignNode>(node))
    {
        // The value first, so that `out` is in source order: `x = x + 1` reads
        // the old `x` before it writes the new one.
        collect(assign->value, out);
        out.push_back(Reference{assign->name, assign->slot, true});
        return;
    }
    if (const IdentifierNode *identifier = tryAs<IdentifierNode>(node))
    {
        out.push_back(Reference{identifier->name, identifier->slot, false});
        return;
    }
    if (const BinOpNode *binary = tryAs<BinOpNode>(node))
    {
        collect(binary->left, out);
        collect(binary->right, out);
        return;
    }
    if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(node))
    {
        collect(unary->operand, out);
        return;
    }
    if (const PrintNode *print = tryAs<PrintNode>(node))
    {
        collect(print->value, out);
        return;
    }
    if (const BlockNode *block = tryAs<BlockNode>(node))
    {
        for (const Node &inner : block->statements)
            collect(inner, out);
        return;
    }
    if (const IfNode *conditional = tryAs<IfNode>(node))
    {
        collect(conditional->condition, out);
        collect(conditional->thenBranch, out);
        collect(conditional->elseBranch, out);
        return;
    }
    if (const WhileNode *loop = tryAs<WhileNode>(node))
    {
        collect(loop->condition, out);
        collect(loop->body, out);
        return;
    }
    if (const CallNode *call = tryAs<CallNode>(node))
    {
        // The callee is a function name and not a variable, so it carries no
        // slot and is not a reference. The arguments are expressions in the
        // frame the call stands in, and are.
        for (const Node &argument : call->arguments)
            collect(argument, out);
        return;
    }
    if (const ReturnNode *returned = tryAs<ReturnNode>(node))
    {
        collect(returned->value, out); // null for the bare form
        return;
    }
    if (tryAs<FunctionNode>(node))
    {
        // Deliberately not descended into. A slot is an index within a frame,
        // and a function body is a frame of its own numbered from zero, so
        // mixing its slots into the caller's list would compare two different
        // numbering schemes as if they were one. `referencesInFrameOf` below
        // walks a function's own frame.
        return;
    }
    // A number or a boolean literal names nothing and has no children.
}

std::vector<Reference> referencesIn(const std::vector<Node> &statements)
{
    std::vector<Reference> references;
    for (const Node &statement : statements)
        collect(statement, references);
    return references;
}

// Everything numbered into one function's frame: its parameters, which are
// declarations rather than nodes, and then every variable its body mentions.
std::vector<Reference> referencesInFrameOf(const FunctionNode &function)
{
    std::vector<Reference> references;
    for (const Parameter &parameter : function.parameters)
        references.push_back(Reference{parameter.name, parameter.slot, true});
    collect(function.body, references);
    return references;
}

// The functions a program declares, in source order.
std::vector<const FunctionNode *> functionsIn(const std::vector<Node> &statements)
{
    std::vector<const FunctionNode *> functions;
    for (const Node &statement : statements)
        if (const FunctionNode *function = tryAs<FunctionNode>(statement))
            functions.push_back(function);
    return functions;
}

// ============================================================
// Running the front end
// ============================================================

// A program that is expected to resolve. Reports the failure and hands back an
// empty tree rather than letting the exception escape, so that one regression
// does not abort the binary and hide every check after it.
std::vector<Node> parseAndResolve(const std::string &source, int &frameSize)
{
    frameSize = -1;
    try
    {
        Parser parser(lex(source));
        std::vector<Node> ast = parser.parse();
        frameSize = resolve(ast);
        return ast;
    }
    catch (const DiagnosticError &e)
    {
        failures++;
        std::fprintf(stderr, "FAIL rejected by the front end: %s\n", e.what());
        return {};
    }
}

enum class Thrown
{
    Nothing,
    Compile,
    Runtime
};

// The interpreter is deliberately not run here: every case below is expected to
// be rejected before it would execute, and running it would be asserting where
// the error is caught rather than where it is raised.
Thrown resolveAndCatch(const std::string &source, Diagnostic &out)
{
    try
    {
        Parser parser(lex(source));
        std::vector<Node> ast = parser.parse();
        resolve(ast);
    }
    catch (const CompileError &e)
    {
        out = e.diagnostic();
        return Thrown::Compile;
    }
    catch (const RuntimeFault &e)
    {
        out = e.diagnostic();
        return Thrown::Runtime;
    }
    return Thrown::Nothing;
}

// ============================================================
// The acceptance criterion that no stream can show
// ============================================================

// "Every identifier node carries a valid slot index after the pass." The
// program below mentions a variable in every position the grammar has one:
// under a `print`, inside a condition, on both sides of an assignment, under a
// unary operator, in the branches of an `if`/`else`, and inside a `while` body
// and a bare block.
const char *kEveryPosition =
    "total = 0\n"                  // slot 0
    "flag = true\n"                // slot 1
    "i = 0\n"                      // slot 2
    "while i < 3 {\n"
    "    step = i * 2\n"           // slot 3 — local to the loop body
    "    if step > 2 {\n"
    "        total = total + step\n"
    "    } else {\n"
    "        total = total - step\n"
    "    }\n"
    "    i = i + 1\n"
    "}\n"
    "{\n"
    "    step = 99\n"              // slot 4 — a different variable, same name
    "    print step\n"
    "}\n"
    "print total\n"
    "print !flag\n"
    "print -total\n";

void everyVariableReferenceCarriesAValidSlot()
{
    Parser parser(lex(kEveryPosition));
    std::vector<Node> ast = parser.parse();

    // Before the pass, so that the check after it is known to be testing the
    // resolver and not the field's initialiser.
    bool allUnresolved = true;
    for (const Reference &reference : referencesIn(ast))
        allUnresolved = allUnresolved && reference.slot == unresolvedSlot;
    check(allUnresolved, "the parser leaves every slot unresolved");

    const int frameSize = resolve(ast);
    const std::vector<Reference> references = referencesIn(ast);

    // Counted off the source: 6 mentions of `step`, 7 of `total`, 5 of `i`
    // and 2 of `flag`. The number is asserted so that a walk which quietly
    // stopped descending — into an else-branch, say — fails here rather than
    // passing every check below on the nodes it did reach.
    check(references.size() == 20,
          "the walk reaches every variable mention in the program");

    bool allResolved = true;
    bool allInRange = true;
    for (const Reference &reference : references)
    {
        if (reference.slot == unresolvedSlot)
        {
            allResolved = false;
            std::fprintf(stderr, "  '%s' still carries no slot\n",
                         reference.name.c_str());
        }
        else if (reference.slot < 0 || reference.slot >= frameSize)
        {
            allInRange = false;
            std::fprintf(stderr, "  '%s' has slot %d, frame size %d\n",
                         reference.name.c_str(), reference.slot, frameSize);
        }
    }
    check(allResolved, "every variable reference carries a slot after the pass");
    check(allInRange, "every slot is an index into the frame");

    check(frameSize == 5,
          "the frame is as wide as the program's distinct variables");
}

// ============================================================
// Shadowing: which mentions are the same variable
// ============================================================

// Every mention of `name`, in source order.
std::vector<int> slotsOf(const std::vector<Node> &ast, const std::string &name)
{
    std::vector<int> slots;
    for (const Reference &reference : referencesIn(ast))
        if (reference.name == name)
            slots.push_back(reference.slot);
    return slots;
}

bool allEqual(const std::vector<int> &slots)
{
    for (std::size_t i = 1; i < slots.size(); i++)
        if (slots[i] != slots[0])
            return false;
    return true;
}

void anInnerBlockVariableIsDistinctFromAnOuterOne()
{
    int frameSize = 0;
    std::vector<Node> ast = parseAndResolve(kEveryPosition, frameSize);
    if (ast.empty())
        return;

    // `step` is first assigned inside the loop body and again inside the block
    // after it. Neither is visible where the other is, so they are two
    // variables that happen to share a name — which is the whole of what
    // shadowing can mean in a language where assignment is declaration.
    const std::vector<int> step = slotsOf(ast, "step");
    check(step.size() == 6, "every mention of 'step' is found");
    if (step.size() == 6)
    {
        // step[0..3]: the assignment in the loop body, then its read in the
        // `if` condition and one in each branch. step[4..5]: the assignment in
        // the trailing block and its read.
        check(step[0] == step[1] && step[1] == step[2] && step[2] == step[3],
              "the loop body's 'step' is one variable throughout the body");
        check(step[4] == step[5],
              "the trailing block's 'step' is one variable within that block");
        check(step[0] != step[4],
              "an inner-block 'step' is a different variable from another "
              "block's 'step'");
    }

    // The other half of the rule, and the half that keeps the language able to
    // loop: an assignment to a name that *is* visible writes that variable
    // rather than declaring a new one.
    const std::vector<int> total = slotsOf(ast, "total");
    check(total.size() == 7 && allEqual(total),
          "'total' is one variable, assigned from inside two nested blocks");
    const std::vector<int> counter = slotsOf(ast, "i");
    check(counter.size() == 5 && allEqual(counter),
          "the loop counter is one variable, assigned from inside the body");
}

// The shape item 1.2's acceptance criterion rests on. If a block-scoped
// assignment declared a fresh local, `total` and `i` inside the body would be
// new variables each time round and the loop would neither accumulate nor
// terminate — `tests/while_sum.algo` would hang rather than print 5050.
void aLoopBodyAssignsTheVariablesItSees()
{
    const std::string source =
        "i = 1\n"
        "total = 0\n"
        "while i <= 100 {\n"
        "    total = total + i\n"
        "    i = i + 1\n"
        "}\n"
        "print total\n";

    int frameSize = 0;
    std::vector<Node> ast = parseAndResolve(source, frameSize);
    if (ast.empty())
        return;

    check(frameSize == 2, "the summing loop declares exactly two variables");
    check(allEqual(slotsOf(ast, "total")),
          "the accumulator inside the body is the accumulator outside it");
    check(allEqual(slotsOf(ast, "i")),
          "the counter inside the body is the counter outside it");
}

// A slot is not handed back when its scope closes, so two variables that can
// never be live at once still get different numbers. Reuse would make "these
// are two variables" unobservable in the index this pass exists to assign, and
// would give the environment item 3.4 introduced live slots aliasing dead
// ones.
void aClosedScopeDoesNotHandItsSlotsBack()
{
    const std::string source =
        "{ a = 1  print a }\n"
        "{ b = 2  print b }\n"
        "{ c = 3  print c }\n";

    int frameSize = 0;
    std::vector<Node> ast = parseAndResolve(source, frameSize);
    if (ast.empty())
        return;

    check(frameSize == 3, "three sibling blocks declare three slots, not one");

    std::set<int> distinct;
    for (const Reference &reference : referencesIn(ast))
        distinct.insert(reference.slot);
    check(distinct.size() == 3, "no two of them share a slot");
}

// A name declared in a block that has closed, and then declared again outside
// it, is a second variable — the first one is not resurrected.
void aNameDeclaredAgainAfterItsBlockIsANewVariable()
{
    const std::string source =
        "{ x = 1  print x }\n"
        "x = 2\n"
        "print x\n";

    int frameSize = 0;
    std::vector<Node> ast = parseAndResolve(source, frameSize);
    if (ast.empty())
        return;

    const std::vector<int> x = slotsOf(ast, "x");
    check(frameSize == 2 && x.size() == 4,
          "both 'x' variables are declared and all four mentions found");
    if (x.size() == 4)
    {
        check(x[0] == x[1], "the block's 'x' is one variable");
        check(x[2] == x[3], "the top-level 'x' is one variable");
        check(x[0] != x[2], "and they are not the same variable");
    }
}

// ============================================================
// Frames: what item 1.4 made a stack of
// ============================================================

// Two functions and a top-level statement list. Each of the three is a frame,
// so each numbers its own slots from zero and no index means anything outside
// the frame that issued it.
const char *kThreeFrames =
    "fn scale(factor, value) {\n"   // slots 0 and 1 of scale's frame
    "    scaled = factor * value\n" // slot 2
    "    return scaled\n"
    "}\n"
    "fn count(limit) {\n"           // slot 0 of count's frame
    "    i = 0\n"                   // slot 1
    "    while i < limit {\n"
    "        i = i + 1\n"
    "    }\n"
    "    return i\n"
    "}\n"
    "base = 2\n"                    // slot 0 of the program's frame
    "print scale(base, 3)\n"
    "print count(base)\n";

void aFunctionBodyIsAFrameOfItsOwn()
{
    int frameSize = 0;
    std::vector<Node> ast = parseAndResolve(kThreeFrames, frameSize);
    if (ast.empty())
        return;

    check(frameSize == 1,
          "the program's own frame holds only the program's own variables");

    const std::vector<const FunctionNode *> functions = functionsIn(ast);
    check(functions.size() == 2, "both functions are found");
    if (functions.size() != 2)
        return;

    check(functions[0]->frameSize == 3,
          "two parameters and one local make a frame of three");
    check(functions[1]->frameSize == 2,
          "one parameter and one local make a frame of two");

    // The point of a frame: both functions and the program number from zero,
    // so slot 0 exists three times over and means something different each
    // time. Item 3.4 gave each frame its own vector, and this is why.
    for (const FunctionNode *function : functions)
    {
        std::set<int> slots;
        bool allInRange = true;
        for (const Reference &reference : referencesInFrameOf(*function))
        {
            slots.insert(reference.slot);
            if (reference.slot < 0 || reference.slot >= function->frameSize)
                allInRange = false;
        }
        check(allInRange, std::string("every slot in '") + function->name +
                              "' indexes that function's own frame");
        check(slots.find(0) != slots.end(),
              std::string("'") + function->name + "' numbers from zero");
    }

    // And the program's own frame is not widened by anything a function
    // declared: `scaled`, `i` and both parameter lists are numbered elsewhere.
    for (const Reference &reference : referencesIn(ast))
        check(reference.slot == 0,
              "the only variable in the program's frame is its own");
}

void parametersAreDeclaredAndCarrySlots()
{
    const std::string source =
        "fn f(a, b, c) {\n"
        "    return a + b + c\n"
        "}\n"
        "print f(1, 2, 3)\n";

    int frameSize = 0;
    std::vector<Node> ast = parseAndResolve(source, frameSize);
    if (ast.empty())
        return;

    const std::vector<const FunctionNode *> functions = functionsIn(ast);
    if (functions.size() != 1)
    {
        check(false, "the function is found");
        return;
    }
    const FunctionNode &f = *functions[0];

    check(f.parameters.size() == 3, "all three parameters are recorded");
    check(f.frameSize == 3, "three parameters and no locals make a frame of three");

    bool numberedInOrder = true;
    for (std::size_t i = 0; i < f.parameters.size(); i++)
        numberedInOrder =
            numberedInOrder && f.parameters[i].slot == static_cast<int>(i);
    check(numberedInOrder,
          "parameters take the first slots of the frame, in the order written");

    // The body's mentions have to agree with the parameter list, or an
    // argument would be bound into one slot and read out of another.
    std::vector<Reference> body;
    collect(f.body, body);
    check(body.size() == 3, "all three parameter reads are found in the body");
    bool agrees = true;
    for (const Reference &reference : body)
        for (const Parameter &parameter : f.parameters)
            if (reference.name == parameter.name && reference.slot != parameter.slot)
                agrees = false;
    check(agrees, "a parameter is read from the slot it was bound into");
}

// A parameter is the first construct in this language that is a declaration
// and nothing else, so it is the first that can be duplicated. Item 1.3
// recorded duplicate declaration as inexpressible, which was true of a
// language where assignment was the only way to introduce a name.
void aParameterIsADeclarationAndCanBeDuplicated()
{
    Diagnostic diag;
    check(resolveAndCatch("fn f(a, a) {\n    return a\n}\nprint f(1, 2)\n",
                          diag) == Thrown::Compile,
          "a duplicate parameter is rejected");
    check(resolveAndCatch("fn f(a) {\n    a = a + 1\n    return a\n}\n"
                          "print f(1)\n",
                          diag) == Thrown::Nothing,
          "but assigning to a parameter is not a redeclaration of it");
}

// ============================================================
// The errors the pass reports
// ============================================================

void resolutionErrorsAreCompileErrorsWithSpans()
{
    struct Case
    {
        const char *what;
        const char *source;
        const char *rendered;
    };

    const std::vector<Case> cases = {
        {"a name that was never assigned anywhere",
         "print z\n",
         "r.algo:1:7: error: variable 'z' used before assignment\n"
         "print z\n"
         "      ^\n"},
        // Distinct from the case above, and deliberately worded differently:
        // the assignment is right there in the source, so "used before
        // assignment" would send the reader looking for something that is
        // already written.
        {"a name whose only assignment is in a block that has closed",
         "{ x = 1 }\nprint x\n",
         "r.algo:2:7: error: variable 'x' used outside the block that assigns it\n"
         "print x\n"
         "      ^\n"},
        // The case the ledger records as having moved: with the flat check
        // item 1.2 left in place, this passed resolution and faulted at run
        // time as an undefined variable, exit 70. It is a compile error now.
        {"a name assigned only inside a loop body that may not run",
         "while false {\n    x = 1\n}\nprint x\n",
         "r.algo:4:7: error: variable 'x' used outside the block that assigns it\n"
         "print x\n"
         "      ^\n"},
        {"a name used inside its own first assignment",
         "x = x + 1\n",
         "r.algo:1:5: error: variable 'x' used before assignment\n"
         "x = x + 1\n"
         "    ^\n"},
        {"a name assigned in one block and used in the next",
         "{ a = 1 }\n{ print a }\n",
         "r.algo:2:9: error: variable 'a' used outside the block that assigns it\n"
         "{ print a }\n"
         "        ^\n"},
        {"a name used in a loop condition before the body assigns it",
         "while running {\n    running = false\n}\n",
         "r.algo:1:7: error: variable 'running' used before assignment\n"
         "while running {\n"
         "      ^~~~~~~\n"},
        {"a name assigned in a then-branch and used in the else-branch",
         "if true {\n    p = 1\n} else {\n    print p\n}\n",
         "r.algo:4:11: error: variable 'p' used outside the block that assigns it\n"
         "    print p\n"
         "          ^\n"},
    };

    for (const Case &c : cases)
    {
        Diagnostic diag;
        const Thrown thrown = resolveAndCatch(c.source, diag);
        check(thrown == Thrown::Compile,
              std::string("the resolver rejects it at compile time: ") + c.what);
        if (thrown != Thrown::Compile)
            continue;
        checkText(c.what, renderDiagnostic(diag, "r.algo", c.source), c.rendered);
    }
}

// The errors item 1.4 adds: everything about a call, a declaration, and the
// frame boundary. Item 1.3 deferred arity here because there was nothing to
// call; this is where that debt comes due.
void callAndFunctionErrorsAreCompileErrorsWithSpans()
{
    struct Case
    {
        const char *what;
        const char *source;
        const char *rendered;
    };

    const std::vector<Case> cases = {
        {"too many arguments",
         "fn add(a, b) {\n    return a + b\n}\nprint add(1, 2, 3)\n",
         "r.algo:4:7: error: function 'add' expects 2 arguments, but 3 were given\n"
         "print add(1, 2, 3)\n"
         "      ^~~~~~~~~~~~\n"},
        // The singular, which is the half of a count message that is usually
        // wrong. Both halves of it decline: one parameter, one argument.
        {"too few arguments, and both counts in the singular",
         "fn one(a) {\n    return a\n}\nprint one()\n",
         "r.algo:4:7: error: function 'one' expects 1 argument, but 0 were given\n"
         "print one()\n"
         "      ^~~~~\n"},
        {"one argument given where two are wanted",
         "fn add(a, b) {\n    return a + b\n}\nprint add(1)\n",
         "r.algo:4:7: error: function 'add' expects 2 arguments, but 1 was given\n"
         "print add(1)\n"
         "      ^~~~~~\n"},
        {"a call of a function that was never declared",
         "print nope(1)\n",
         "r.algo:1:7: error: unknown function 'nope'\n"
         "print nope(1)\n"
         "      ^~~~~~~\n"},
        // The two halves of "functions are not values", worded as a pair
        // because each says which of the two namespaces the name is in.
        {"a function name used as a value",
         "fn f() {\n    return 1\n}\nprint f\n",
         "r.algo:4:7: error: 'f' is a function, not a value\n"
         "print f\n"
         "      ^\n"},
        {"a variable name used as a function",
         "x = 1\nprint x(2)\n",
         "r.algo:2:7: error: 'x' is a variable, not a function\n"
         "print x(2)\n"
         "      ^~~~\n"},
        {"a return with a value outside any function",
         "return 1\n",
         "r.algo:1:1: error: 'return' outside of a function\n"
         "return 1\n"
         "^~~~~~~~\n"},
        {"a bare return outside any function",
         "return\n",
         "r.algo:1:1: error: 'return' outside of a function\n"
         "return\n"
         "^~~~~~\n"},
        {"two functions of one name",
         "fn f() {\n    return 1\n}\nfn f() {\n    return 2\n}\nprint f()\n",
         "r.algo:4:4: error: duplicate function 'f'\n"
         "fn f() {\n"
         "   ^\n"},
        {"a parameter written twice",
         "fn f(a, a) {\n    return a\n}\nprint f(1, 2)\n",
         "r.algo:1:9: error: duplicate parameter 'a'\n"
         "fn f(a, a) {\n"
         "        ^\n"},
        // The frame boundary, and the reason it gets a message of its own: the
        // assignment is right there at the top of the file, so reporting this
        // as "used before assignment" would send the reader looking for
        // something already written.
        {"a top-level variable read inside a function",
         "total = 5\nfn f() {\n    return total\n}\nprint f()\n",
         "r.algo:3:12: error: variable 'total' used inside a function, where only "
         "parameters and locals are in scope\n"
         "    return total\n"
         "           ^~~~~\n"},
        {"a function's parameter read from another function",
         "fn a(n) {\n    return n\n}\nfn b() {\n    return n\n}\nprint b()\n",
         "r.algo:5:12: error: variable 'n' used before assignment\n"
         "    return n\n"
         "           ^\n"},
    };

    for (const Case &c : cases)
    {
        Diagnostic diag;
        const Thrown thrown = resolveAndCatch(c.source, diag);
        check(thrown == Thrown::Compile,
              std::string("the resolver rejects it at compile time: ") + c.what);
        if (thrown != Thrown::Compile)
            continue;
        checkText(c.what, renderDiagnostic(diag, "r.algo", c.source), c.rendered);
    }
}

// The counterpart: programs whose names are all in scope must not be rejected.
// A resolver that popped one scope too many, or that failed to search outward,
// would still pass every check above.
void programsWhoseNamesAreInScopeAreAccepted()
{
    const std::vector<const char *> accepted = {
        "x = 1\nprint x\n",
        "x = 1\n{ print x }\n",
        "x = 1\n{ { { print x } } }\n",
        "x = 1\nx = x + 1\nprint x\n",
        "x = 1\n{ x = 2 }\nprint x\n",
        "{ x = 1 }\nx = 2\nprint x\n",
        "{ x = 1  print x }\n",
        "x = 0\nwhile x < 3 { x = x + 1 }\nprint x\n",
        "x = 1\nif x > 0 { print x } else { print x }\n",
        "x = 1\nif x > 0 { y = 2  print y }\n",
        // Item 1.4's own. Recursion needs a function to be visible inside its
        // own body; the forward reference and the mutual pair need every
        // function collected before any body is walked.
        "fn f(n) {\n  if n < 2 { return n }\n  return f(n - 1) + f(n - 2)\n}\n"
        "print f(5)\n",
        "fn a() {\n  return b()\n}\nfn b() {\n  return 1\n}\nprint a()\n",
        "fn even(n) {\n  if n == 0 { return true }\n  return odd(n - 1)\n}\n"
        "fn odd(n) {\n  if n == 0 { return false }\n  return even(n - 1)\n}\n"
        "print even(4)\n",
        "fn nothing() {\n  return\n}\nprint nothing()\n",
        "fn nothing() {\n}\nprint nothing()\n",
        // Two namespaces: the parameter and the function are both called `f`,
        // and inside the body `f` is the parameter while `f(…)` is the call.
        "fn f(f) {\n  return f + 1\n}\nprint f(1)\n",
        // A function's local and a top-level variable of one name are two
        // variables in two frames, and neither can see the other.
        "total = 1\nfn g() {\n  total = 2\n  return total\n}\nprint g()\n",
        // A block inside a function body scopes within that function's frame,
        // the same way a block at the top level scopes within the program's.
        "fn h(n) {\n  { inner = n * 2\n    return inner }\n}\nprint h(3)\n",
        // A call is an expression, so it may stand anywhere one may.
        "fn one() {\n  return 1\n}\nprint one() + one() * one()\n"
        "print -one()\nprint one() == 1\nx = one()\n"
        "if one() < 2 { print one() }\nwhile one() > 1 { print 0 }\n",
    };

    for (const char *source : accepted)
    {
        Diagnostic diag;
        check(resolveAndCatch(source, diag) == Thrown::Nothing,
              std::string("accepted: ") + source);
    }
}

} // namespace

int main()
{
    everyVariableReferenceCarriesAValidSlot();
    anInnerBlockVariableIsDistinctFromAnOuterOne();
    aLoopBodyAssignsTheVariablesItSees();
    aClosedScopeDoesNotHandItsSlotsBack();
    aNameDeclaredAgainAfterItsBlockIsANewVariable();
    aFunctionBodyIsAFrameOfItsOwn();
    parametersAreDeclaredAndCarrySlots();
    aParameterIsADeclarationAndCanBeDuplicated();
    resolutionErrorsAreCompileErrorsWithSpans();
    callAndFunctionErrorsAreCompileErrorsWithSpans();
    programsWhoseNamesAreInScopeAreAccepted();

    if (failures != 0)
    {
        std::fprintf(stderr, "resolver_test: %d of %d checks failed\n",
                     failures, checks);
        return 1;
    }

    std::printf("resolver_test: %d checks passed\n", checks);
    return 0;
}
