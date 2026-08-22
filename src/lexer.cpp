#include "lexer.h"

#include <cctype>
#include <stdexcept>

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

        // Identifiers and keywords (print)
        if (std::isalpha(static_cast<unsigned char>(c)))
        {
            int startCol = col;
            std::string word;
            while (i < source.size() && std::isalnum(static_cast<unsigned char>(source[i])))
            {
                word += source[i++];
                col++;
            }
            Span span{line, startCol, static_cast<int>(word.size())};
            if (word == "print")
            {
                tokens.push_back({TokenType::PRINT, word, span});
            }
            else
            {
                tokens.push_back({TokenType::IDENTIFIER, word, span});
            }
            continue;
        }

        // Operators — all one character wide
        Span span{line, col, 1};
        switch (c)
        {
        case '=':
            tokens.push_back({TokenType::EQUALS, "=", span});
            break;
        case '+':
            tokens.push_back({TokenType::PLUS, "+", span});
            break;
        case '-':
            tokens.push_back({TokenType::MINUS, "-", span});
            break;
        case '*':
            tokens.push_back({TokenType::MULTIPLY, "*", span});
            break;
        case '/':
            tokens.push_back({TokenType::DIVIDE, "/", span});
            break;
        default:
            throw std::runtime_error(std::string("Unknown character: ") + c);
        }
        i++;
        col++;
    }

    // Zero-width token sitting just past the last character
    tokens.push_back({TokenType::END_OF_FILE, "", Span{line, col, 0}});
    return tokens;
}
