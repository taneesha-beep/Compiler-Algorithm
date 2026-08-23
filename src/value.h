#pragma once

#include <cstdint>

// ============================================================
// VALUES — the runtime representation of everything the
// language can compute
// ============================================================

// The language has exactly two value types. Nothing converts between them
// implicitly — see the note on the type rules at the bottom of this file.
enum class ValueType
{
    Int,
    Bool
};

// A tagged union, deliberately, rather than `std::variant<int, bool>`.
//
// Three properties are load-bearing, and each is something this project
// measures rather than merely prefers:
//
//   * It owns nothing. No heap, no reference count, no destructor. `evaluate`
//     returns one by value at every node of the tree, so a value that owned
//     anything would put allocation traffic into the hot path — and it would
//     put it there underneath ablation A, whose subject is the `shared_ptr` on
//     the *node*, not on the value. Those two costs must stay separable.
//   * Its layout is fixed by this file rather than by the standard library. CI
//     builds under GCC and Clang, so libstdc++ and libc++ both compile this hot
//     path; `std::variant`'s size, its index type, and how well its accessors
//     inline are implementation details that differ between the two. An
//     attribution resting on a representation that differs by standard library
//     is not an attribution.
//   * The integer arm is one field, which is what let item 1.5 widen it to
//     `std::int64_t` here rather than everywhere. Note what that did *not*
//     cover, since the plan predicted it would touch nothing else: `evaluate`
//     built its literals with `std::stoi`, which cannot produce a value this
//     arm is now wide enough to hold, so the arm and the literal parser had to
//     widen together or the language would have stayed 32-bit behind a 64-bit
//     value. See the note on `std::stoll` in `src/interpreter.cpp`.
//
// The live union member is the one `type` names; reading the other is undefined
// behaviour, so every read below goes through `isInt` / `isBool` first.
struct Value
{
    ValueType type;
    union
    {
        std::int64_t integer; // live when type == ValueType::Int
        bool boolean;         // live when type == ValueType::Bool
    };

    bool isInt() const { return type == ValueType::Int; }
    bool isBool() const { return type == ValueType::Bool; }

    // Named constructors rather than aggregate initialisation: `Value{t, x}`
    // writes whichever union member is declared first no matter which type the
    // tag beside it names, and that is precisely the mistake the tag exists to
    // prevent.
    static Value fromInt(std::int64_t v)
    {
        Value value;
        value.type = ValueType::Int;
        value.integer = v;
        return value;
    }

    static Value fromBool(bool v)
    {
        Value value;
        value.type = ValueType::Bool;
        value.boolean = v;
        return value;
    }
};

// Only ever asked of two values of the same type: the caller compares the tags
// first, because `1 == true` is a type error rather than `false`.
inline bool valuesEqual(const Value &left, const Value &right)
{
    return left.isInt() ? left.integer == right.integer
                        : left.boolean == right.boolean;
}

// The word for a type inside a diagnostic. Returns a literal rather than a
// std::string, so that the only thing this header includes is the fixed-width
// integer type its own arm is declared with.
inline const char *typeName(ValueType type)
{
    return type == ValueType::Int ? "integer" : "boolean";
}

// ON THE TYPE RULES. There is no implicit conversion in either direction: an
// integer is not truthy, and a boolean is not 0 or 1.
//
//     + - * /       integer, integer            -> integer
//     < <= > >=     integer, integer            -> boolean
//     == !=         two operands of one type    -> boolean
//     unary -       integer                     -> integer
//     unary !       boolean                     -> boolean
//
// Every other combination is a RuntimeFault (exit 70), not a CompileError (65).
// The language has no type checker and the roadmap does not add one — item 1.3
// is a resolver, which assigns frame slots and reports use-before-declaration,
// not types. Whether `x + 1` is well typed therefore depends on what the
// program computed, and that is the definition of a runtime fault here.
//
// Contrast the out-of-range integer literal, which stays a compile-time error:
// it is a property of the token's text and of nothing the program computes,
// which is why it keeps exit 65 even though `evaluate` is what detects it.
//
// ON THE RANGE, AND ON WHAT HAPPENS AT ITS EDGE. An integer is a signed 64-bit
// two's-complement value: -9223372036854775808 through 9223372036854775807.
// Every arithmetic operator **traps** when its result will not fit, raising a
// RuntimeFault (70) rather than producing a wrapped answer.
//
// It traps rather than wrapping because signed overflow is undefined behaviour
// in C++, and this language exists to be measured: a benchmark whose arithmetic
// silently wraps is reporting a number produced by a program whose meaning the
// standard does not define. Wrapping deliberately — on unsigned arithmetic —
// was the other option, and it is worse here, because it makes a wrong answer
// indistinguishable from a right one at the point a reader would have to trust
// it.
//
// The classification is a runtime fault for the same reason a type mismatch is:
// whether `x + y` overflows depends on what the program computed, not on the
// text of the operator. The literal is the opposite case and stays at 65. Both
// are pinned in `tests/diagnostic_test.cpp`.
//
// Five sites can trap, and the two nobody expects are the last two:
//
//     a + b     when the sum does not fit
//     a - b     when the difference does not fit
//     a * b     when the product does not fit
//     a / b     ONLY for -9223372036854775808 / -1, whose quotient is one past
//               the maximum. Division by zero is a separate fault, checked
//               first, and is the only other way `/` fails.
//     -a        ONLY for -(-9223372036854775808), for the same reason: the
//               range is asymmetric, so the most negative value has no positive
//               counterpart.
//
// Note that neither of those last two is reachable from a literal. The lexer
// makes a number token out of digits alone, so `-9223372036854775808` is unary
// minus applied to the literal `9223372036854775808` — which is itself out of
// range, and a compile error. The most negative integer exists only as
// something a program computes.
