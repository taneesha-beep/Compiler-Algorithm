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
    // ON THE ENVIRONMENT. One `std::vector<Value>` per call, indexed by the
    // frame slot the resolver assigned, held on a stack of them.
    //
    // It is *per call* because item 1.4 makes recursion expressible and one
    // flat frame cannot hold it: `fib(n)` calling `fib(n - 1)` would find a
    // single shared `n`, and the inner call's binding would destroy the outer
    // one's. Every call therefore gets its own environment and drops it on the
    // way out, which is what makes each activation's parameters its own.
    //
    // ⚠️ THIS IS THE ISOLATED ARM OF ABLATION D. This commit is `perf/iso-d`,
    // one commit on a branch off `v1-naive-treewalk` — configuration N — and
    // it is not on `main`. It carries D and **nothing else**: `evaluate` still
    // takes its `Node` by value (ablation A is not here), `NumberNode` still
    // re-parses its digits (B is not here), and both operator arms still walk
    // a chain of string comparisons (C is not here). D applied on top of A, B
    // and C is `perf/cum-d` on `main`, which is configuration **H**.
    //
    // What D replaced was a `std::map<std::string, Value>`, in which a
    // variable access was `find` against an ordered map: O(log n) comparisons
    // of `std::string`, where n is the number of variables the enclosing
    // function declares. The resolver has written a slot onto every
    // `IdentifierNode`, every `AssignNode` and every `Parameter` since item
    // 1.3, and a width onto every `FunctionNode`, and all four are read here —
    // because binding an argument is a write to a variable, and an ablation
    // that indexed only the reads would be half applied and its number would
    // mean half of what it says.
    //
    // The stack itself is common to both sides of the ablation — a per-call
    // vector needs exactly the same stack a per-call map did — so it changes
    // what D starts from without changing what D measures.
    using Environment = std::vector<Value>;
    std::vector<Environment> frames; // the program's own frame is frames[0]

    // Every function the program declares, by name. Functions are not values,
    // so this is a namespace of its own rather than anything a variable could
    // hold, and a call reaches it by the name the parser recorded.
    //
    // ON WHY THIS MAP DID NOT MOVE WITH THE ENVIRONMENT. It is the same
    // container looked up the same way, so converting it in the same commit
    // would have been the easy thing to do. It is not ablation D: the roadmap
    // defines D as variable access against `std::map<std::string, ...>` and
    // `CLAUDE.md` as *"the environment is a `std::map<std::string, Value>`
    // looked up by name"* — both name the frame, and the frame is what moved.
    // Converting this one alongside it would have folded a second change into
    // this tag with no row to attribute it to, so `perf/iso-d` would price two
    // removals where the ledger records one. Replacing it with an index is
    // Phase 4's work, not a Phase 3 ablation.
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
    // ON THE BY-VALUE Node. `evaluate` takes its `shared_ptr` by value, so
    // every node of every expression pays a reference-count increment and
    // decrement. That is not an oversight: it is one of the four unforced
    // inefficiencies the baseline is required to keep, and it is ablation A's
    // entire subject. Do not change it to `const Node &` before item 3.1.
    Value evaluate(Node node);

    // `frameSize` is how many slots the program's own frame needs, which is
    // what `resolve()` returns — see `src/resolver.h`. It is a parameter
    // rather than something recomputed here because the resolver is the only
    // thing that knows: it numbered those slots, and it does not reuse one
    // when a scope closes, so the width is not derivable from the statement
    // list without repeating the walk. A function's own width travels on its
    // node instead, in `FunctionNode::frameSize`. Ablation D added it.
    void execute(const std::vector<Node> &statements, int frameSize);
};
