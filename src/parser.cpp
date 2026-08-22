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

Node Parser::parseExpr() { return parseEquality(); }

// Each of the four binary levels below has the same shape: parse the tighter
// level, then loop while the next token is one of this level's operators. The
// loop is what makes every level left-associative — `1 - 2 - 3` is `(1-2)-3`.
Node Parser::parseEquality()
{
    Node left = parseComparison();

    while (current().type == TokenType::EQUAL_EQUAL ||
           current().type == TokenType::NOT_EQUAL)
    {
        std::string op = consume().value;
        Node right = parseComparison();
        left = makeNode(NodeType::BinOp, op,
                        mergeSpans(left->span, right->span), left, right);
    }

    return left;
}

Node Parser::parseComparison()
{
    Node left = parseTerm();

    while (current().type == TokenType::LESS ||
           current().type == TokenType::LESS_EQUAL ||
           current().type == TokenType::GREATER ||
           current().type == TokenType::GREATER_EQUAL)
    {
        std::string op = consume().value;
        Node right = parseTerm();
        left = makeNode(NodeType::BinOp, op,
                        mergeSpans(left->span, right->span), left, right);
    }

    return left;
}

Node Parser::parseTerm()
{
    Node left = parseFactor();

    while (current().type == TokenType::PLUS ||
           current().type == TokenType::MINUS)
    {
        std::string op = consume().value;
        Node right = parseFactor();
        // A BinOp runs from the first character of its left operand to the
        // last character of its right — not merely the operator token
        left = makeNode(NodeType::BinOp, op,
                        mergeSpans(left->span, right->span), left, right);
    }

    return left;
}

Node Parser::parseFactor()
{
    Node left = parseUnary();

    while (current().type == TokenType::MULTIPLY ||
           current().type == TokenType::DIVIDE)
    {
        std::string op = consume().value;
        Node right = parseUnary();
        left = makeNode(NodeType::BinOp, op,
                        mergeSpans(left->span, right->span), left, right);
    }

    return left;
}

// The one level that recurses into itself rather than looping, which is what
// makes a prefix operator right-associative and stackable: `- -5` negates twice
// and is 5, and `!!true` is true. There is no decrement operator in this
// language, so `--5` has only the one reading.
//
// Sitting below `factor` puts unary tighter than `*`, as in C: `-2 * 3` is
// `(-2) * 3`. That binding is not observable in the result — negation
// distributes through multiplication and division — so it is the shape of the
// tree that records it, and tests/expression_test.cpp is what checks it.
Node Parser::parseUnary()
{
    if (current().type == TokenType::MINUS || current().type == TokenType::NOT)
    {
        Token op = consume();
        Node operand = parseUnary();
        // The span covers the operator and everything it applies to
        return makeNode(NodeType::UnaryOp, op.value,
                        mergeSpans(op.span, operand->span), operand);
    }

    return parsePrimary();
}

Node Parser::parsePrimary()
{
    if (current().type == TokenType::NUMBER)
    {
        Token number = consume();
        return makeNode(NodeType::Number, number.value, number.span);
    }
    if (current().type == TokenType::BOOLEAN)
    {
        // Which of the two it is settles here, at parse time. The node keeps
        // its text for the same reason a Number does — `value` is uniformly the
        // source the node was built from — but nothing reads it again: the
        // interpreter dispatches on the node type.
        Token literal = consume();
        NodeType type =
            (literal.value == "true") ? NodeType::True : NodeType::False;
        return makeNode(type, literal.value, literal.span);
    }
    if (current().type == TokenType::IDENTIFIER)
    {
        Token name = consume();
        return makeNode(NodeType::Identifier, name.value, name.span);
    }
    throw CompileError(Diagnostic{
        Severity::Error, current().span,
        "expected an expression, found " + describe(current())});
}
