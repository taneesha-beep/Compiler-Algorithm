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
        Token keyword = consume(); // eat 'print'
        Node expr = parseExpr();
        // The statement covers the keyword and everything printed
        return makeNode(NodeType::Print, "",
                        mergeSpans(keyword.span, expr->span), expr);
    }

    if (current().type == TokenType::IDENTIFIER)
    {
        Token name = consume();
        expect(TokenType::EQUALS, "Expected '=' after variable name");
        Node expr = parseExpr();
        // The statement covers the variable name through the assigned value
        return makeNode(NodeType::Assign, name.value,
                        mergeSpans(name.span, expr->span), expr);
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
        // A BinOp runs from the first character of its left operand to the
        // last character of its right — not merely the operator token
        left = makeNode(NodeType::BinOp, op,
                        mergeSpans(left->span, right->span), left, right);
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
        left = makeNode(NodeType::BinOp, op,
                        mergeSpans(left->span, right->span), left, right);
    }

    return left;
}

Node Parser::parsePrimary()
{
    if (current().type == TokenType::NUMBER)
    {
        Token number = consume();
        return makeNode(NodeType::Number, number.value, number.span);
    }
    if (current().type == TokenType::IDENTIFIER)
    {
        Token name = consume();
        return makeNode(NodeType::Identifier, name.value, name.span);
    }
    throw std::runtime_error("Expected number or variable, got: " + current().value);
}
