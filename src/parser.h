#pragma once

#include <vector>

#include "ast.h"
#include "token.h"

// Parser class
class Parser
{
    std::vector<Token> tokens;
    std::size_t pos = 0;

    Token current();
    Token consume();
    Token expect(TokenType type, const std::string &errMsg);

public:
    Parser(std::vector<Token> tokens);

    // Parse a full program (list of statements)
    std::vector<Node> parse();

    // A statement is either:
    //   print <expr>
    //   identifier = <expr>
    Node parseStatement();

    // Expression: handles + - * /
    // Simple precedence: * and / before + and -
    Node parseExpr();
    Node parseTerm();
    Node parsePrimary();
};
