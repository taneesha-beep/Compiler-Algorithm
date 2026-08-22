#include "parser.h"

#include "diagnostic.h"

namespace
{

// How a token reads inside a message. The end-of-file token has no text, so
// quoting its empty value would render as `got: ''`.
std::string describe(const Token &token)
{
    if (token.type == TokenType::END_OF_FILE)
        return "end of file";
    return "'" + token.value + "'";
}

} // namespace

Token Parser::current() { return tokens[pos]; }
Token Parser::consume() { return tokens[pos++]; }

Token Parser::expect(TokenType type, const std::string &errMsg)
{
    // The token that was found is what the caret should point at: it is the
    // text the reader has to change.
    if (current().type != type)
        throw CompileError(Diagnostic{Severity::Error, current().span, errMsg});
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
        expect(TokenType::EQUALS, "expected '=' after variable name");
        Node expr = parseExpr();
        // The statement covers the variable name through the assigned value
        return makeNode(NodeType::Assign, name.value,
                        mergeSpans(name.span, expr->span), expr);
    }

    throw CompileError(Diagnostic{
        Severity::Error, current().span,
        "expected a statement, found " + describe(current())});
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
    throw CompileError(Diagnostic{
        Severity::Error, current().span,
        "expected a number or a variable name, found " + describe(current())});
}
