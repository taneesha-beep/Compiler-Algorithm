#pragma once

#include <string>

// ============================================================
// STAGE 1: LEXER — token definitions
// ============================================================

enum class TokenType
{
    NUMBER,
    IDENTIFIER,
    EQUALS,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    PRINT,
    END_OF_FILE
};

// Where a run of characters came from: the 1-based line and column of its
// first character, and its length in characters. A tab counts as one column,
// so a column is a character offset, not a screen offset.
//
// A default-constructed Span has line 0, which means "no position". Nothing
// the lexer or parser builds carries one; the zero value exists only so the
// struct has a defined empty state.
//
// Spans live here rather than in a header of their own because a span is a
// lexical fact — a range of source characters. `ast.h` includes this header
// to put the same type on every AST node.
struct Span
{
    int line = 0;
    int col = 0;
    int len = 0;
};

// The span running from the first character of `first` to the last character
// of `last`. A binary expression uses this to cover both its operands.
//
// A Span describes a range on a single line. When `last` starts on a later
// line the merged span keeps `first`'s start and stops at the end of `first`:
// an under-approximation is truthful, whereas a length spanning a line break
// would be invented.
inline Span mergeSpans(const Span &first, const Span &last)
{
    if (first.line != last.line)
        return first;
    return Span{first.line, first.col, last.col + last.len - first.col};
}

struct Token
{
    TokenType type;
    std::string value;
    Span span;
};
