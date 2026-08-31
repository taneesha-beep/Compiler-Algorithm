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
    // It is *a vector indexed by slot* since item 3.4 — ablation D, and the
    // last of the baseline's four unforced inefficiencies. Until then it was a
    // `std::map<std::string, Value>` and a variable access was `find` against
    // an ordered map: O(log n) comparisons of `std::string`, where n is the
    // number of variables the enclosing function declares. The resolver has
    // written a slot onto every `IdentifierNode`, every `AssignNode` and every
    // `Parameter` since item 1.3, and a width onto every `FunctionNode`, and
    // item 3.4 is the commit that spends all four — because binding an
    // argument is a write to a variable, and an ablation that indexed only the
    // reads would be half applied and its number would mean half of what it
    // says. See the note in `src/ast.h` on `IdentifierNode::slot`.
    //
    // The stack itself is common to both sides of that ablation — a per-call
    // vector needs exactly the same stack a per-call map did — so it changed
    // what D started from without changing what D measured.
    //
    // What this bought is a row, not a claim: see `results/measurements.csv`
    // under `perf/iso-d` and `perf/cum-d`, which are two different commits,
    // and the second of which is configuration **H**, the hardened
    // tree-walker Phase 4's VM is compared against.
    using Environment = std::vector<Value>;
    std::vector<Environment> frames; // the program's own frame is frames[0]

    // Every function the program declares, by name. Functions are not values,
    // so this is a namespace of its own rather than anything a variable could
    // hold, and a call reaches it by the name the parser recorded.
    //
    // ON WHY THIS MAP DID NOT MOVE AT ITEM 3.4. It is the same container the
    // environment was and it is looked up the same way, so converting it in
    // the same commit would have been the easy thing to do. It is not ablation
    // D. The roadmap defines D as *"variable access is `variables.find(...)`
    // against `std::map<std::string, int>`"* and `CLAUDE.md`'s protected
    // bullet as *"the environment is a `std::map<std::string, Value>` looked
    // up by name"* — both name the frame, and the frame is what moved.
    // Converting this one alongside it would have folded a second change into
    // D's tag with no row to attribute it to, so `perf/iso-d` and `perf/cum-d`
    // would price two removals and this ledger would record one. That is the
    // failure item 3.1 refused when it chose `const Node &` over a raw
    // pointer, and item 3.2 refused when it kept `NumberNode::text`.
    //
    // It is also a much smaller cost than the one D removes: a function is
    // looked up once per *call*, where a variable is looked up several times
    // per loop iteration, and the resolver has already assigned no index for
    // it to use. Giving calls an index is Phase 4's work — the VM resolves a
    // callee at compile time — and if it is ever wanted in the tree-walker it
    // is a change to make deliberately, with a measurement of its own.
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
    // the integer its digits denote, converted once by the parser. The
    // string-compared operator went at item 3.3: both operator arms `switch` on
    // an enumerator the parser writes onto the node. The string-keyed frame
    // went at item 3.4: each frame is a `std::vector<Value>` indexed by the
    // slot the resolver assigned. **All four of the baseline's unforced
    // inefficiencies are now spent**, `CLAUDE.md`'s *Do not "fix" these* list
    // has nothing live left on it, and `perf/cum-d` is configuration H.
    //
    // Item 3.3 is worth one more line here, because its row says something the
    // other two do not. It removed between 0.05% and 1.34% of instructions and
    // *added* 20 to 50 million conditional branches per program: GCC had already
    // collapsed the ten string comparisons into a length test and a one-byte
    // compare chain, and the enum switch compiles to a binary decision tree that
    // is dearer for `+` and `-` than the chain was. The dispatch this file
    // describes and the dispatch the processor runs were not the same dispatch.
    // `docs/MEASUREMENT.md`'s *Boundary of the claim* carries the disassembly.
    Value evaluate(const Node &node);

    // `frameSize` is how many slots the program's own frame needs, which is
    // what `resolve()` returns — see `src/resolver.h`. It is a parameter
    // rather than something recomputed here because the resolver is the only
    // thing that knows: it numbered those slots, and it does not reuse one
    // when a scope closes, so the width is not derivable from the statement
    // list without repeating the walk. A function's own width travels on its
    // node instead, in `FunctionNode::frameSize`. Item 3.4 added it.
    void execute(const std::vector<Node> &statements, int frameSize);
};
