#include "lexer.h"

#include <cctype>
#include <stdexcept>

std::vector<Token> lex(const std::string &source)
{
    std::vector<Token> tokens;
    std::size_t i = 0;

    while (i < source.size())
    {
        char c = source[i];

        // Skip whitespace and newlines
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
        {
            i++;
            continue;
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            std::string num;
            while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i])))
            {
                num += source[i++];
            }
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        // Identifiers and keywords (print)
        if (std::isalpha(static_cast<unsigned char>(c)))
        {
            std::string word;
            while (i < source.size() && std::isalnum(static_cast<unsigned char>(source[i])))
            {
                word += source[i++];
            }
            if (word == "print")
            {
                tokens.push_back({TokenType::PRINT, word});
            }
            else
            {
                tokens.push_back({TokenType::IDENTIFIER, word});
            }
            continue;
        }

        // Operators
        switch (c)
        {
        case '=':
            tokens.push_back({TokenType::EQUALS, "="});
            break;
        case '+':
            tokens.push_back({TokenType::PLUS, "+"});
            break;
        case '-':
            tokens.push_back({TokenType::MINUS, "-"});
            break;
        case '*':
            tokens.push_back({TokenType::MULTIPLY, "*"});
            break;
        case '/':
            tokens.push_back({TokenType::DIVIDE, "/"});
            break;
        default:
            throw std::runtime_error(std::string("Unknown character: ") + c);
        }
        i++;
    }

    tokens.push_back({TokenType::END_OF_FILE, ""});
    return tokens;
}
