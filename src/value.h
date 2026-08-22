#pragma once

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
//   * The integer arm is one field. Item 1.5 widens it to `int64_t` and touches
//     nothing else, which is what keeps 1.5 isolable as its own configuration.
//
// The live union member is the one `type` names; reading the other is undefined
// behaviour, so every read below goes through `isInt` / `isBool` first.
struct Value
{
    ValueType type;
    union
    {
        int integer;  // live when type == ValueType::Int
        bool boolean; // live when type == ValueType::Bool
    };

    bool isInt() const { return type == ValueType::Int; }
    bool isBool() const { return type == ValueType::Bool; }

    // Named constructors rather than aggregate initialisation: `Value{t, x}`
    // writes whichever union member is declared first no matter which type the
    // tag beside it names, and that is precisely the mistake the tag exists to
    // prevent.
    static Value fromInt(int v)
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
// std::string so that this header needs no includes at all.
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
