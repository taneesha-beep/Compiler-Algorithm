#include "lexer.h"

#include <cctype>

#include "diagnostic.h"

std::vector<Token> lex(const std::string &source)
{
    std::vector<Token> tokens;
    std::size_t i = 0;

    // Position of source[i], 1-based. `col` advances with `i` and resets on a
    // newline, so every token can record where it started.
    int line = 1;
    int col = 1;

    while (i < source.size())
    {
        char c = source[i];

        // Newlines end a line; other whitespace just advances the column
        if (c == '\n')
        {
            i++;
            line++;
            col = 1;
            continue;
        }
        if (c == ' ' || c == '\r' || c == '\t')
        {
            i++;
            col++;
            continue;
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            int startCol = col;
            std::string num;
            while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i])))
            {
                num += source[i++];
                col++;
            }
            tokens.push_back({TokenType::NUMBER, num,
                              Span{line, startCol, static_cast<int>(num.size())}});
            continue;
        }

        // Identifiers and keywords (print, if, else, while, fn, return)
        if (std::isalpha(static_cast<unsigned char>(c)))
        {
            int startCol = col;
            std::string word;
            while (i < source.size() && std::isalnum(static_cast<unsigned char>(source[i])))
            {
                word += source[i++];
                col++;
            }
            // The whole alphanumeric run is consumed before anything is
            // compared, so a name that merely starts with a keyword — `trueValue`,
            // `printer` — is one IDENTIFIER and not a keyword followed by a name.
            Span span{line, startCol, static_cast<int>(word.size())};
            if (word == "print")
            {
                tokens.push_back({TokenType::PRINT, word, span});
            }
            else if (word == "if")
            {
                tokens.push_back({TokenType::IF, word, span});
            }
            else if (word == "else")
            {
                tokens.push_back({TokenType::ELSE, word, span});
            }
            else if (word == "while")
            {
                tokens.push_back({TokenType::WHILE, word, span});
            }
            else if (word == "fn")
            {
                tokens.push_back({TokenType::FN, word, span});
            }
            else if (word == "return")
            {
                tokens.push_back({TokenType::RETURN, word, span});
            }
            else if (word == "true" || word == "false")
            {
                tokens.push_back({TokenType::BOOLEAN, word, span});
            }
            else
            {
                tokens.push_back({TokenType::IDENTIFIER, word, span});
            }
            continue;
        }

        // Operators. Each two-character form is tested before the
        // one-character operator it starts with, so `==` never lexes as two
        // `=`. The length is carried out of the switch rather than assumed,
        // because it is both how far to advance and the span's `len` — and a
        // two-character token recorded as one character wide would draw every
        // caret under it a character short.
        const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        TokenType type;
        int length = 1;

        switch (c)
        {
        case '=':
            if (next == '=') { type = TokenType::EQUAL_EQUAL; length = 2; }
            else             { type = TokenType::EQUALS; }
            break;
        case '!':
            if (next == '=') { type = TokenType::NOT_EQUAL; length = 2; }
            else             { type = TokenType::NOT; }
            break;
        case '<':
            if (next == '=') { type = TokenType::LESS_EQUAL; length = 2; }
            else             { type = TokenType::LESS; }
            break;
        case '>':
            if (next == '=') { type = TokenType::GREATER_EQUAL; length = 2; }
            else             { type = TokenType::GREATER; }
            break;
        case '+':
            type = TokenType::PLUS;
            break;
        case '-':
            type = TokenType::MINUS;
            break;
        case '*':
            type = TokenType::MULTIPLY;
            break;
        case '/':
            type = TokenType::DIVIDE;
            break;
        case '{':
            type = TokenType::LEFT_BRACE;
            break;
        case '}':
            type = TokenType::RIGHT_BRACE;
            break;
        case '(':
            type = TokenType::LEFT_PAREN;
            break;
        case ')':
            type = TokenType::RIGHT_PAREN;
            break;
        case ',':
            type = TokenType::COMMA;
            break;
        default:
            throw CompileError(Diagnostic{
                Severity::Error, Span{line, col, 1},
                std::string("unknown character '") + c + "'"});
        }

        tokens.push_back({type, source.substr(i, length), Span{line, col, length}});
        i += length;
        col += length;
    }

    // Zero-width token sitting just past the last character
    tokens.push_back({TokenType::END_OF_FILE, "", Span{line, col, 0}});
    return tokens;
}
