#include "interpreter.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

#include "diagnostic.h"

namespace
{

// The caret goes on the whole operation rather than on one operand: an operand
// only has the wrong type relative to what is being done with it.
[[noreturn]] void binaryTypeFault(const BinOpNode &node, const Value &left,
                                  const Value &right)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, node.span,
        "operator '" + node.op + "' cannot be applied to " +
            typeName(left.type) + " and " + typeName(right.type)});
}

[[noreturn]] void unaryTypeFault(const UnaryOpNode &node, const Value &operand)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, node.span,
        "operator '" + node.op + "' cannot be applied to " +
            typeName(operand.type)});
}

// ON TRAPPING OVERFLOW, AND ON WHY A BRANCH IS ALLOWED HERE.
//
// Item 1.5 puts an overflow check on every `+`, `-`, `*`, `/` and unary `-`.
// That is the hot path of every benchmark Phase 3 measures — not a narrow one —
// so the argument that admitted item 1.4's call-depth limit is *not available*
// and the case has to be made differently. 1.4's limit costs one comparison per
// **call**, so a loop benchmark pays nothing for it; this costs something on
// every arithmetic operation, and every benchmark pays it.
//
// It does not distort the ablation series, for a different reason: **the cost
// is uniform across every configuration, so it cancels in every delta.** The
// check is in configuration N, in each of `perf/iso-a`…`perf/iso-d`, in each of
// `perf/cum-a`…`perf/cum-d`, and in Phase 4's VM — which has to trap
// identically or item 4.4's differential testing would be comparing two
// languages. No ablation removes it, so it never appears as anyone's delta, and
// a term present in both endpoints of a subtraction subtracts out.
//
// What it does change is the *ratio*, and in the safe direction: a constant
// added to both sides of N→V and H→V moves each of them toward 1, so the
// headline speedup this project reports is understated by however much the
// check costs, never overstated. The one place the cancellation is not exact is
// wall-clock, where this branch competes for the same execution resources the
// interaction residual is about; instruction counts under cachegrind cancel
// exactly, one extra test per arithmetic operation in every configuration.
// Phase 5.3 is where that limit gets written down.
//
// And it is deliberately **not** a fifth ablation. Every ablation A–D preserves
// what the program computes and changes only how fast it is computed. Removing
// this one would make arithmetic wrap — a different language, and one whose
// results are undefined behaviour — so there is nothing honest to compare
// against.
//
// TWO CORRECTIONS AND ONE REVERSAL, and the history is kept because this block
// has now been rewritten twice in two days and a reader deserves to know why
// rather than suspect confusion.
//
// It originally named `perf/iso-e` and `perf/cum-e`. **Ablation E (item 3.5) was
// cut on 2026-08-30 and stays cut**, so the series is A–D over eight tags. That
// correction stands.
//
// It also said the check is in Phase 4's VM. That was removed on 2026-08-31
// because Phase 4 had been cut — and **restored the same day, because the cut
// was reversed and the VM is back in scope.** The sentence is true again as
// originally written.
//
// The argument itself never moved through any of it: the check is in every
// configuration that exists, so it cancels in every delta. Only the list of
// configurations changed, and only twice.
//
// The caret goes on the whole operation, matching what division by zero and a
// type fault already do: neither operand is individually wrong, the result of
// combining them is.
[[noreturn]] void binaryOverflowFault(const BinOpNode &node)
{
    throw RuntimeFault(Diagnostic{Severity::Error, node.span,
                                  "integer overflow in '" + node.op + "'"});
}

// Worded apart from the binary form because `-` names two different operators
// and a reader staring at `0 - x` needs to know which one trapped.
[[noreturn]] void unaryOverflowFault(const UnaryOpNode &node)
{
    throw RuntimeFault(Diagnostic{
        Severity::Error, node.span,
        "integer overflow in unary '" + node.op + "'"});
}

// Arithmetic and ordering are integer-only. See the type rules in value.h.
void requireIntegers(const BinOpNode &node, const Value &left, const Value &right)
{
    if (!left.isInt() || !right.isInt())
        binaryTypeFault(node, left, right);
}

// Equality is the one binary group that accepts booleans, and it accepts them
// only against booleans: there is no conversion, so `1 == true` compares two
// different types and is a fault rather than `false`.
void requireSameType(const BinOpNode &node, const Value &left, const Value &right)
{
    if (left.type != right.type)
        binaryTypeFault(node, left, right);
}

// A condition is a boolean and nothing else: an integer is not truthy. The
// caret goes on the condition itself, which is the text that has to change.
void requireCondition(const Node &condition, const Value &value)
{
    if (!value.isBool())
        throw RuntimeFault(Diagnostic{
            Severity::Error, condition->span,
            std::string("a condition must be a boolean, not ") +
                typeName(value.type)});
}

} // namespace

Value Interpreter::evaluate(const Node &node)
{
    // ON THE DISPATCH. A chain of tag comparisons, one per node type, exactly
    // as it was before the node split. `tryAs` performs the same comparison the
    // chain performed before and hands back the concrete node when it matches,
    // so reaching a field costs what it cost then. Turning this into a switch
    // would be a dispatch change that no planned ablation accounts for, and
    // Phase 3 would have no row to attribute it to.
    //
    // Item 3.3 makes that worth restating, because this file now holds two
    // `switch` statements and neither of them is this chain. Both are inside
    // the arms below and both dispatch on *which operator* a node applies,
    // which is ablation C. This chain dispatches on *which kind of node* it
    // is, which is nobody's ablation. Turning it into a switch as well would
    // fold an unaccounted change into C's number.
    if (const NumberNode *number = tryAs<NumberNode>(node))
    {
        // ON WHAT USED TO BE HERE. Until item 3.2 this arm called
        // `std::stoll(number->text)`, re-parsing the same digits on every
        // visit, and raised the out-of-range diagnostic from inside the walk.
        // That re-parse was ablation B, and both halves of it now live in
        // `integerValueOf` in `src/parser.cpp`, which runs once per literal in
        // the source. The node still carries its digits; nothing on this path
        // reads them.
        //
        // What this bought is a row, not a claim: see
        // `results/measurements.csv` under `perf/iso-b` and `perf/cum-b`,
        // which — unlike A's two tags — are two different commits, because the
        // isolated and cumulative series diverge here.
        return Value::fromInt(number->value);
    }
    if (const BooleanNode *boolean = tryAs<BooleanNode>(node))
    {
        return Value::fromBool(boolean->value);
    }
    if (const IdentifierNode *identifier = tryAs<IdentifierNode>(node))
    {
        // ON WHY AN UNWRITTEN SLOT CANNOT REACH THIS. The hazard was reachable
        // between items 1.2 and 1.3: the flat semantic check of the day held
        // one set of names and did not know that a block might not run, so a
        // variable first assigned inside an `if` or a `while` body passed it
        // and was still absent here when the body had not executed. Item 1.3's
        // resolver closed that.
        //
        // For any program the resolver accepted, this slot was written at
        // resolution and the frame standing here is wide enough to hold it.
        // The resolver accepts a use only if some assignment declared the name
        // earlier in the source *and* in a scope enclosing the use — and a
        // statement in an enclosing scope that stands earlier in that scope's
        // statement list has already executed whenever a statement after it is
        // executing. A closing scope does not hand its slot back, so nothing
        // else can have taken it and the assignment's write is still there.
        //
        // Item 1.4 narrowed what has to be argued rather than widening it. A
        // name inside a function body resolves against that function's frame
        // and no other — the resolver's lookup stops at the frame boundary —
        // so the frame this indexes is the one the resolver numbered the name
        // into, and the argument above applies within it unchanged.
        //
        // ON WHAT REPLACED THE `undefined variable` FAULT, which stood here
        // until item 3.4 and was every bit as unreachable as the paragraphs
        // above say. It survived that long for two reasons, and 3.4 spends the
        // first: the `find` against a string-keyed ordered map *was* ablation
        // D, so deleting it early would have performed half of that ablation
        // with nothing recording what it bought. The second reason — a fault
        // beats undefined behaviour if a later item ever widens what the
        // resolver admits — outlives the first, and is answered by an `assert`
        // rather than by a branch.
        //
        // **The assert is live exactly where it is free.** The default build
        // sets no `CMAKE_BUILD_TYPE`, so `NDEBUG` is not defined in it: that
        // is the build `ctest` runs, the build CI compiles under both GCC and
        // Clang, and the build `-DALGO_SANITIZE=ON` adds UBSan to. The
        // benchmarks are `RelWithDebInfo`, which does define `NDEBUG`, so the
        // configuration that takes the rows compiles this away entirely. The
        // invariant is therefore checked on every variable access of all 33
        // tests on both compilers, and ablation D pays nothing for it.
        //
        // **A surviving bounds test was rejected, and this is the reason.** An
        // `at()` or an explicit range check would be a comparison and a
        // conditional branch on the hot path, present in D and absent in N —
        // roughly one per variable access, which on `bench/vars.algo` is 23
        // per loop iteration. D would then remove the map lookup *and* add a
        // check, while this repository recorded only the removal. That is
        // item 3.1's rule — remove exactly the named cost and nothing else —
        // and it is the same call 3.1 made choosing `const Node &` over a raw
        // pointer and 3.2 made keeping `NumberNode::text`.
        //
        // **And it is strictly stronger than the lookup it replaces**, in the
        // build where it lives. `find` could only see a name missing from the
        // map. This sees an `unresolvedSlot` the resolver failed to write, and
        // a slot past the end of the frame — a frame the resolver numbered or
        // sized wrongly, which the map could not have noticed at all, and
        // which is precisely the failure mode a slot-indexed environment
        // newly has.
        Environment &environment = frames.back();
        assert(identifier->slot >= 0 &&
               static_cast<std::size_t>(identifier->slot) < environment.size());
        return environment[static_cast<std::size_t>(identifier->slot)];
    }
    if (const BinOpNode *binary = tryAs<BinOpNode>(node))
    {
        Value left = evaluate(binary->left);
        Value right = evaluate(binary->right);

        // ON WHAT USED TO BE HERE. Until item 3.3 this arm held a chain of
        // `op == "+"`, `op == "-"`, … comparisons against the node's text, ten
        // of them, walked from the top on every binary operation executed — so
        // what an operator cost depended on where it sat in the chain, `+`
        // being first and `!=` tenth. That chain was ablation C. The parser
        // settles the operator once now, at parse time, and this dispatches on
        // the enumerator it wrote: see `binOpKindOf` in `src/parser.cpp`. The
        // comparison operators item 1.1 added were in the same chain and move
        // in the same commit, because an operator set half on an enum and half
        // on strings would have made C's number mean half of what it says.
        //
        // The node still carries its text and nothing on this path reads it.
        // It stays because dropping it would change the node's size, which no
        // ablation accounts for, and because the four diagnostics below quote
        // it; `src/ast.h` carries that argument in full.
        //
        // ON WHAT DELIBERATELY DID NOT BECOME A SWITCH. The node-type chain
        // this arm is one link of is still an if-chain, for the reason given
        // where it starts: turning *that* into a switch would be a dispatch
        // change no ablation accounts for, so Phase 3 would have no row to
        // attribute it to. Only the operator chain is ablation C.
        //
        // Falling off the end of this switch continues down the node-type
        // chain and ends at `unknown node type`, which is exactly where an
        // unmatched string comparison landed before.
        switch (binary->opKind)
        {
        case BinOpKind::Add:
        {
            requireIntegers(*binary, left, right);
            std::int64_t result;
            if (__builtin_add_overflow(left.integer, right.integer, &result))
                binaryOverflowFault(*binary);
            return Value::fromInt(result);
        }
        case BinOpKind::Subtract:
        {
            requireIntegers(*binary, left, right);
            std::int64_t result;
            if (__builtin_sub_overflow(left.integer, right.integer, &result))
                binaryOverflowFault(*binary);
            return Value::fromInt(result);
        }
        case BinOpKind::Multiply:
        {
            requireIntegers(*binary, left, right);
            std::int64_t result;
            if (__builtin_mul_overflow(left.integer, right.integer, &result))
                binaryOverflowFault(*binary);
            return Value::fromInt(result);
        }
        case BinOpKind::Divide:
        {
            requireIntegers(*binary, left, right);
            // The caret covers the whole division, not just the divisor: the
            // divisor is only zero in the context of what it divides.
            if (right.integer == 0)
                throw RuntimeFault(Diagnostic{Severity::Error, binary->span,
                                              "division by zero"});
            // The one division that overflows, and the reason there is no
            // `__builtin_div_overflow` to call: it is a single pair of
            // operands rather than a range. The two's-complement range is
            // asymmetric, so negating the most negative value lands one past
            // the maximum — and `INT64_MIN / -1` *is* that negation. Left
            // unchecked it is undefined behaviour, and on x86-64 it faults the
            // process outright rather than wrapping, which is a crash with no
            // diagnostic. Checked in this order so a zero divisor, which a
            // reader will actually hit, is reported as division by zero.
            if (left.integer == std::numeric_limits<std::int64_t>::min() &&
                right.integer == -1)
                binaryOverflowFault(*binary);
            return Value::fromInt(left.integer / right.integer);
        }
        case BinOpKind::Less:
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer < right.integer);
        }
        case BinOpKind::LessEqual:
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer <= right.integer);
        }
        case BinOpKind::Greater:
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer > right.integer);
        }
        case BinOpKind::GreaterEqual:
        {
            requireIntegers(*binary, left, right);
            return Value::fromBool(left.integer >= right.integer);
        }
        case BinOpKind::Equal:
        {
            requireSameType(*binary, left, right);
            return Value::fromBool(valuesEqual(left, right));
        }
        case BinOpKind::NotEqual:
        {
            requireSameType(*binary, left, right);
            return Value::fromBool(!valuesEqual(left, right));
        }
        }
    }
    if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(node))
    {
        Value operand = evaluate(unary->operand);

        // Two comparisons rather than ten, and no benchmark program applies a
        // unary operator inside its loop — so ablation C barely moves a count
        // here. It is in C anyway because C is defined against operator
        // dispatch by string comparison, and a chain left behind would have
        // been work of exactly the named kind with no row to attribute it to.
        switch (unary->opKind)
        {
        case UnaryOpKind::Negate:
        {
            if (!operand.isInt())
                unaryTypeFault(*unary, operand);
            // `-INT64_MIN` is the whole of it — see the note on `/` above for
            // why the range's asymmetry makes exactly one value unnegatable.
            // Written as a subtraction from zero so that the check is the same
            // builtin the binary operators use rather than a second mechanism
            // spelled by hand.
            std::int64_t result;
            if (__builtin_sub_overflow(static_cast<std::int64_t>(0),
                                       operand.integer, &result))
                unaryOverflowFault(*unary);
            return Value::fromInt(result);
        }
        case UnaryOpKind::Not:
        {
            if (!operand.isBool())
                unaryTypeFault(*unary, operand);
            return Value::fromBool(!operand.boolean);
        }
        }
    }
    if (const CallNode *call = tryAs<CallNode>(node))
    {
        return callFunction(*call);
    }
    throw RuntimeFault(Diagnostic{Severity::Error, node->span,
                                  "unknown node type"});
}

// A call: evaluate the arguments where they are written, then run the body in
// an environment of its own.
//
// The arguments are evaluated *before* the new frame is pushed, because they
// are expressions in the caller's scope — `fib(n - 1)` reads the caller's `n`,
// and reading it after the callee's frame was in place would find the callee's
// or nothing at all. They are collected into a vector rather than bound one at
// a time for the same reason: binding the first would already have had to push
// the frame the second is not allowed to see.
Value Interpreter::callFunction(const CallNode &call)
{
    // ON WHY THIS CANNOT FIRE, which is the same argument the undefined
    // variable above rests on. The resolver collects every function before it
    // walks anything and rejects a call it cannot find, so a program that got
    // this far names a function that is in this map. The lookup stays because a
    // fault beats undefined behaviour if a later item widens what the resolver
    // admits, and because removing it would be an optimisation of a path no
    // ablation names.
    auto found = functions.find(call.callee);
    if (found == functions.end())
        throw RuntimeFault(Diagnostic{Severity::Error, call.span,
                                      "unknown function '" + call.callee + "'"});
    const FunctionNode &function = *found->second;

    std::vector<Value> arguments;
    arguments.reserve(call.arguments.size());
    for (const Node &argument : call.arguments)
        arguments.push_back(evaluate(argument));

    // The depth is the height of the frame stack, so there is no counter to
    // keep in step with it. `frames.size()` is 1 at the top level, so refusing
    // at `> maxCallDepth` admits exactly maxCallDepth nested calls. The caret
    // goes on the call that could not be entered.
    if (frames.size() > static_cast<std::size_t>(maxCallDepth))
        throw RuntimeFault(
            Diagnostic{Severity::Error, call.span, "call depth exceeded"});

    // Popped on the way out however this returns, including on the unwinding
    // of a fault raised inside the body: an interpreter that faulted with
    // frames still stacked would report the wrong depth to anything that ran
    // after it.
    struct FramePopper
    {
        std::vector<Environment> &frames;
        ~FramePopper() { frames.pop_back(); }
    };

    // Sized once, from the width the resolver wrote onto this function — which
    // is what `FunctionNode::frameSize` has existed for since item 1.3 and
    // what item 3.4 spends. The resolver does not hand a slot back when a
    // scope closes, so the width is the number of variables the body declares
    // rather than the depth of its deepest scope, and a slot stays valid for
    // the whole call; nothing here ever resizes this vector, which is what
    // makes that property load-bearing rather than incidental.
    //
    // `Value` has a trivial default constructor, so these elements are
    // value-initialised to `{Int, 0}`. That is deliberate and not merely
    // tolerated: reading a slot before its assignment would see the integer 0
    // rather than an indeterminate union member, which is undefined behaviour
    // the sanitizer build would be right to trap. No program can do it — the
    // paragraphs above the `IdentifierNode` arm are that argument — so this is
    // the belt beside that brace, not a semantic the language relies on.
    frames.emplace_back(static_cast<std::size_t>(function.frameSize));
    FramePopper popper{frames};

    // The resolver has already checked that these two counts agree, and it
    // numbered the parameters into the frame's outermost scope before walking
    // the body, so their slots are the first ones in the frame.
    //
    // **Binding an argument is a write to a variable**, which is why
    // `Parameter` carries a slot at all and why this line is one of the four
    // ablation D had to move. A version that indexed the two `evaluate` and
    // `executeStatement` sites and still bound arguments by name would have
    // left a string-keyed insertion on the call path of every recursive
    // benchmark — half the ablation, priced as the whole of it, with
    // `bench/fib32.algo` paying for the half that was left.
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        const int slot = function.parameters[i].slot;
        assert(slot >= 0 &&
               static_cast<std::size_t>(slot) < frames.back().size());
        frames.back()[static_cast<std::size_t>(slot)] = arguments[i];
    }

    // ON WHAT A FUNCTION WITHOUT A RETURN HANDS BACK: the integer 0, both for
    // the bare `return` and for a body that runs off its end.
    //
    // The language has exactly two value types and the roadmap adds no third,
    // so there is no unit or nil to hand back — inventing one would be a
    // language feature no item lists, and it would put a third case into the
    // value type that item 1.5 widens and Phase 4 pushes on a stack. The other
    // candidate, faulting when a call that produced no value is used, was
    // rejected because every call in this language is in an expression
    // position: there are no expression statements, so a function that
    // returned nothing could not be called at all, and the bare `return` item
    // 1.4 requires would be unusable. Zero makes it an early exit, which is
    // what it is for.
    if (executeStatement(function.body) == Flow::Return)
        return returnValue;
    return Value::fromInt(0);
}

// ON RUN-TIME SCOPE. A block still groups statements and nothing more: it does
// not push an environment, and scope inside a frame is entirely the resolver's
// business — by the time a program reaches this file, every name it can still
// mention refers to exactly one variable of its frame, so a flat frame answers
// every lookup within it the way a stack of scopes would.
//
// Item 3.4 made that sentence load-bearing rather than merely true. The
// resolver numbers a frame's variables from zero and **never reuses a number
// when a scope closes**, so the inner `x` of a block and the outer `x` are two
// slots, not one — which is exactly what lets a single flat vector stand in
// for the stack of scopes the resolver walked. Reuse would have saved a few
// words per frame and made a live slot alias a dead one; `declareOrBind` in
// `src/resolver.cpp` says so, and this is the code that depends on it.
//
// What item 1.4 changed is the *frame*, not the scope. Recursion makes one
// environment for the whole program wrong rather than merely slow: `fib(n)`
// calling `fib(n - 1)` needs two live `n`s, and one frame has room for one. So
// there is an environment per call, on a stack. Since item 3.4 that
// environment is a `std::vector<Value>` indexed by slot rather than an ordered
// map keyed on `std::string` — see the note on the environment in
// `interpreter.h` for what that ablation removed and what it deliberately did
// not touch.
//
// ON HOW `return` UNWINDS. It is a flag returned up the statement walk: every
// statement says whether it ran or returned, and a block, an `if` and a
// `while` stop as soon as one of them says it returned. The value travels in
// the `returnValue` member beside it.
//
// The two alternatives were rejected on cost, and the cost is what decides it
// here rather than taste. A **C++ exception** per return is the tidiest to
// write — `throw Return{value}` needs no propagation at all — and it is the
// one choice that would wreck Phase 3: `fib(27)` returns several hundred
// thousand times, and a thrown-and-caught exception costs on the order of a
// microsecond, so the mechanism would cost more than everything the five
// ablations remove put together, and no ablation would account for it. A
// **sentinel value** — a distinguished Value meaning "this was a return" —
// needs a third case in a two-case value type, which is a language change
// nothing asked for and which item 1.5 and Phase 4 would both have to carry.
//
// What the flag costs is one comparison per *statement executed inside a
// function*, not per node evaluated: an expression pays nothing, and the
// statement walk is the smaller of the two by a wide margin. In `fib` that is
// a handful of predictable branches per call, against a map construction and a
// parameter binding on the same path.
Flow Interpreter::executeStatement(const Node &statement)
{
    if (const AssignNode *assign = tryAs<AssignNode>(statement))
    {
        // The value first and the binding second, written as two statements so
        // that neither the order nor the frame the write lands in depends on
        // how the compiler sequences one expression: evaluating the value may
        // enter and leave any number of calls, and the frame it is stored into
        // is the one standing after all of them. Since item 3.4 the frame is
        // read back *after* the evaluation for the same reason — a reference
        // taken before it could be left dangling by a call that pushed frames.
        //
        // The write side of ablation D. Where this said
        // `frames.back()[assign->name] = value` it built or found a
        // red-black-tree node keyed on the name; it now stores into the slot
        // the resolver assigned. The assert is the one above the
        // `IdentifierNode` arm, for the reasons argued there.
        Value value = evaluate(assign->value);
        Environment &environment = frames.back();
        assert(assign->slot >= 0 &&
               static_cast<std::size_t>(assign->slot) < environment.size());
        environment[static_cast<std::size_t>(assign->slot)] = value;
        return Flow::Normal;
    }
    if (const PrintNode *print = tryAs<PrintNode>(statement))
    {
        // Written straight to the stream rather than through a
        // value-to-string helper, so that printing allocates nothing.
        Value value = evaluate(print->value);
        if (value.isBool())
            std::cout << (value.boolean ? "true" : "false") << std::endl;
        else
            std::cout << value.integer << std::endl;
        return Flow::Normal;
    }
    if (const BlockNode *block = tryAs<BlockNode>(statement))
    {
        for (const Node &inner : block->statements)
            if (executeStatement(inner) == Flow::Return)
                return Flow::Return;
        return Flow::Normal;
    }
    if (const IfNode *conditional = tryAs<IfNode>(statement))
    {
        Value condition = evaluate(conditional->condition);
        requireCondition(conditional->condition, condition);
        if (condition.boolean)
            return executeStatement(conditional->thenBranch);
        if (conditional->elseBranch)
            return executeStatement(conditional->elseBranch);
        return Flow::Normal;
    }
    if (const WhileNode *loop = tryAs<WhileNode>(statement))
    {
        // The condition is re-evaluated every turn, and nothing counts the
        // turns: a loop that never ends never ends. An iteration cap would be a
        // branch inside the hot path of every benchmark this project measures,
        // and it would be a language semantic the roadmap does not list. The
        // guard against a runaway program belongs to whatever runs it — the
        // test suite sets a CTest timeout, which is where it costs nothing.
        // Item 1.4's call-depth limit is not a counter-example; see the note on
        // it in `interpreter.h`.
        while (true)
        {
            Value condition = evaluate(loop->condition);
            requireCondition(loop->condition, condition);
            if (!condition.boolean)
                break;
            if (executeStatement(loop->body) == Flow::Return)
                return Flow::Return;
        }
        return Flow::Normal;
    }
    if (const ReturnNode *returned = tryAs<ReturnNode>(statement))
    {
        // A bare `return` carries no expression and yields 0 — see the note in
        // `callFunction` on why 0 and not a unit value or a fault.
        returnValue = returned->value ? evaluate(returned->value)
                                      : Value::fromInt(0);
        return Flow::Return;
    }

    throw RuntimeFault(Diagnostic{Severity::Error, statement->span,
                                  "unknown statement type"});
}

// The program itself is a statement list rather than a block: it is not
// delimited by braces and introduces no scope of its own. It does have a frame
// — the one the resolver numbered its top-level variables into — which is why
// one is pushed here before anything runs.
//
// Functions are collected before any statement executes, so a call may name a
// function declared further down the file, and a function may call itself. The
// resolver settled the same question the same way in its own first pass; the
// two agree because both are answering "what functions does this program have",
// which is a property of the whole file and not of a position in it.
//
// `executeStatement` and `evaluate` both take their Node by reference. They did
// not always agree, and the reason is worth keeping rather than leaving to be
// re-derived: `executeStatement` took a reference from the start, while
// `evaluate` took its `shared_ptr` by value because that by-value pass was
// ablation A's entire subject. A second one here would have added
// reference-count traffic on the statement path that item 3.1 was never going
// to remove, inflating A's delta with a cost outside the hypothesis it tests.
// Item 3.1 has since removed the one in `evaluate`, so the asymmetry is gone.
void Interpreter::execute(const std::vector<Node> &statements, int frameSize)
{
    // Reserved once so that pushing a frame can never move the frames already
    // on the stack. Nothing here holds a reference across a call, but a
    // reallocation in the middle of one is the kind of bug that would appear
    // only at a particular depth, and the bound is known: the call-depth limit
    // is what makes it known. Since item 3.4 an `Environment` is a
    // `std::vector<Value>` rather than a `std::map`, so this block is smaller
    // than it was — the elements it reserves are three words each instead of
    // six — and the argument for reserving it is unchanged.
    frames.reserve(static_cast<std::size_t>(maxCallDepth) + 1);

    // The program's own frame, sized from `resolve()`'s return value the way a
    // function's is sized from `FunctionNode::frameSize`. `main.cpp` has
    // captured that number since item 1.3 and printed it under `--trace`;
    // item 3.4 is what spends it. A top-level program that declares nothing
    // gets an empty vector, which allocates nothing.
    frames.emplace_back(static_cast<std::size_t>(frameSize));

    for (const Node &statement : statements)
        if (const FunctionNode *function = tryAs<FunctionNode>(statement))
            functions.emplace(function->name, function);

    // A function declaration is not something to execute, so the second pass
    // steps over the ones the first pass recorded. The Flow a top-level
    // statement reports is discarded: the resolver rejects a `return` outside a
    // function, so nothing at this level can report anything but Normal.
    for (const Node &statement : statements)
        if (!tryAs<FunctionNode>(statement))
            executeStatement(statement);
}
