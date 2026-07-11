#include "parser.h"

#include <stdexcept>

Token Parser::current() { return tokens[pos]; }
Token Parser::consume() { return tokens[pos++]; }

Token Parser::expect(TokenType type, const std::string &errMsg)
{
    if (current().type != type)
        throw std::runtime_error(errMsg);
    return consume();
}

Parser::Parser(std::vector<Token> tokens) : tokens(tokens) {}

std::vector<Node> Parser::parse()
{
    std::vector<Node> statements;
    while (current().type != TokenType::END_OF_FILE)
    {
        statements.push_back(parseStatement());
    }
    return statements;
}

Node Parser::parseStatement()
{
    if (current().type == TokenType::PRINT)
    {
        consume(); // eat 'print'
        Node expr = parseExpr();
        return makeNode(NodeType::Print, "", expr);
    }

    if (current().type == TokenType::IDENTIFIER)
    {
        std::string varName = consume().value;
        expect(TokenType::EQUALS, "Expected '=' after variable name");
        Node expr = parseExpr();
        return makeNode(NodeType::Assign, varName, expr);
    }

    throw std::runtime_error("Unknown statement starting with: " + current().value);
}

Node Parser::parseExpr()
{
    Node left = parseTerm();

    while (current().type == TokenType::PLUS ||
           current().type == TokenType::MINUS)
    {
        std::string op = consume().value;
        Node right = parseTerm();
        left = makeNode(NodeType::BinOp, op, left, right);
    }

    return left;
}

Node Parser::parseTerm()
{
    Node left = parsePrimary();

    while (current().type == TokenType::MULTIPLY ||
           current().type == TokenType::DIVIDE)
    {
        std::string op = consume().value;
        Node right = parsePrimary();
        left = makeNode(NodeType::BinOp, op, left, right);
    }

    return left;
}

Node Parser::parsePrimary()
{
    if (current().type == TokenType::NUMBER)
    {
        return makeNode(NodeType::Number, consume().value);
    }
    if (current().type == TokenType::IDENTIFIER)
    {
        return makeNode(NodeType::Identifier, consume().value);
    }
    throw std::runtime_error("Expected number or variable, got: " + current().value);
}
