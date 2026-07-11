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

struct Token
{
    TokenType type;
    std::string value;
};
