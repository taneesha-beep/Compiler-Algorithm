#pragma once

#include <map>
#include <string>
#include <vector>

#include "ast.h"
#include "value.h"

// ============================================================
// STAGE 4: INTERPRETER / CODE GENERATION
// Evaluates the AST and prints results
// ============================================================

// The deepest chain of calls the interpreter will enter before refusing. Item
// 1.4 requires a diagnostic rather than a stack overflow, and a diagnostic is
// only possible while there is still C++ stack left to raise one on.
//
// ON WHY THIS IS ALLOWED WHERE AN ITERATION CAP IS NOT. A `while` that never
// ends is guarded by a CTest timeout and deliberately not by the language, on
// the grounds that a counter would be a branch inside the hot path of every
// benchmark Phase 3 measures. This limit is not the same thing, on two counts.
//
// The cost is per *call*, not per node or per iteration: one comparison of a
// vector's size against a constant, on a path that already builds an
// environment and binds every argument into it. A program that calls nothing
// pays nothing, so the loop benchmarks are untouched and the call benchmark
// pays one integer compare per call against several map operations.
//
// And the external guard that covers a runaway loop does not cover this one. A
// non-terminating `while` runs forever, which a timeout can catch. Unbounded
// recursion exhausts the C++ stack, which is undefined behaviour — the process
// dies on a signal, with no diagnostic, no exit code of ours, and nothing for a
// golden file to compare. There is no way to hand that back to the caller from
// outside, so the limit has to be inside.
//
// ON THE VALUE. 1000, and it is chosen against a measurement rather than
// picked. A limit is only worth having if the deepest chain it admits still
// fits, so the deepest one was run against progressively smaller stacks:
//
//     (ulimit -s <kb>; ./build/algo deepest.algo)
//
// where `deepest.algo` recurses exactly `maxCallDepth` deep. It faults at
// 2048 KB and completes at 2560 KB, so a full chain needs between two and
// three megabytes of the eight this platform gives a process — enough headroom
// for a compiler whose frames are half again as large as this one's, and the
// build under test is unoptimised, which is the expensive case. Re-run that
// probe if a change makes a call's C++ frames bigger.
//
// It is also far enough below the 10,000-deep recursion item 1.4's acceptance
// criterion asks about that the criterion exercises this limit rather than the
// stack behind it. `tests/deep_recursion.algo` runs a chain one short of the
// limit, so the suite fails rather than the platform if either ever stops
// fitting inside the other.
inline constexpr int maxCallDepth = 1000;

// What a statement did: ran, or returned. Item 1.4's answer to how `return`
// unwinds, and the reasoning is in `src/interpreter.cpp` above
// `executeStatement` — the short form is that the two alternatives, a C++
// exception per return and a sentinel value, cost respectively a great deal on
// the hot path and a third case in the language's value type.
enum class Flow
{
    Normal,
    Return
};

class Interpreter
{
    // ON THE ENVIRONMENT. One ordered map keyed on `std::string` per call,
    // held on a stack of them, and every part of that is deliberate.
    //
    // It is *per call* because item 1.4 makes recursion expressible and one
    // flat map cannot hold it: `fib(n)` calling `fib(n - 1)` would find a
    // single shared `n`, and the inner call's binding would destroy the outer
    // one's. Every call therefore gets its own environment and drops it on the
    // way out, which is what makes each activation's parameters its own.
    //
    // It is still *an ordered map keyed on a string* because that is ablation
    // D's subject, and D has to stay the same experiment it was described as:
    // "string-keyed map -> slot-indexed vector". The resolver has already
    // written a frame slot onto every variable reference and a frame width
    // onto every function, and item 3.4 is the commit that spends them, by
    // making each of these frames a `std::vector` indexed by slot. Turning one
    // into a vector now would be performing that ablation with nothing
    // recording what it bought. See the note in `src/ast.h` on
    // `IdentifierNode::slot`.
    //
    // The stack itself is common to both sides of that ablation — a per-call
    // vector needs exactly the same stack a per-call map does — so it changes
    // what D starts from without changing what D measures.
    using Environment = std::map<std::string, Value>;
    std::vector<Environment> frames; // the program's own frame is frames[0]

    // Every function the program declares, by name. Functions are not values,
    // so this is a namespace of its own rather than anything a variable could
    // hold, and a call reaches it by the name the parser recorded — the same
    // string lookup a variable takes, and for the same reason: replacing it
    // with an index is Phase 4's work, not a Phase 1 optimisation.
    std::map<std::string, const FunctionNode *> functions;

    // Where a `return` leaves its value for the call that is waiting on it.
    // Written by the `return`, read once by `callFunction` immediately after
    // the body finishes, and stale between the two — which is safe because
    // nothing else looks at it, and a nested call that overwrites it has
    // already had its own value copied out.
    Value returnValue = Value::fromInt(0);

    Flow executeStatement(const Node &statement);
    Value callFunction(const CallNode &call);

public:
    // ON THE `const Node &`. It was a by-value `shared_ptr` until item 3.1 —
    // ablation A — which is the commit that changed it. Every node of every
    // expression used to pay a reference-count increment on the way in and a
    // decrement on the way out, and the reference those bought was never
    // needed: the tree is owned by the statement list `main` holds across
    // `execute`, no node is freed while it is being walked, and nothing here
    // copies the pointer or extends its lifetime. `evaluate` only ever hands
    // `node` to `tryAs` and reads `node->span`.
    //
    // A REFERENCE RATHER THAN A RAW `const ASTNode *`, deliberately. The
    // roadmap allows either, and an ablation must remove exactly one cost.
    // `tryAs` already takes a `const Node &`, so a reference leaves every call
    // site, every dispatch and every field access byte-for-byte what they were
    // and changes nothing but the reference count. A raw pointer would have
    // removed an indirection at each `tryAs` as well, and no ablation accounts
    // for that one.
    //
    // What this bought is a row, not a claim: see `results/measurements.csv`
    // under `perf/iso-a` and `perf/cum-a`, which are two tags on this commit
    // because the isolated and cumulative series coincide at A.
    //
    // The re-parsed literal went next, at item 3.2: `NumberNode` now carries
    // the integer its digits denote, converted once by the parser. Two of the
    // baseline's unforced inefficiencies are left untouched — the
    // string-compared operator (3.3) and the string-keyed frame (3.4) — and
    // `CLAUDE.md`'s *Do not "fix" these* still protects both.
    Value evaluate(const Node &node);
    void execute(const std::vector<Node> &statements);
};
