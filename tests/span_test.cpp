// Unit checks for source spans — roadmap item 0.2.
//
// The golden-file cases next to this file compare the interpreter's stdout;
// they cannot see a span, which is an in-memory value. This binary links
// algo_core and inspects tokens and nodes directly. A failed check prints to
// stderr and the process exits non-zero, which is all CTest reads — there is
// no third-party test framework here, and the project keeps it that way.
//
// ON THE ACCEPTANCE VALUES. The roadmap's 0.2 criteria say that in
// `x = 1 + 2 * 3` the `*` token sits at col 15 and the enclosing BinOp spans
// cols 5-17. Both are miscounted, and they contradict each other. Counting
// 1-based from the first character:
//
//     x = 1 + 2 * 3
//     ^   ^   ^ ^ ^
//     1   5   9 | 13
//               11
//
// `*` is at col 11, and the BinOp enclosing it — the outer `1 + 2 * 3` — runs
// cols 5..13. Col 15 for `*` would require four leading spaces, and with that
// indent the BinOp would run 9..17, not 5..17. These checks assert the counted
// values; the lexer was not bent to reproduce col 15.

#include <cstdio>
#include <string>
#include <vector>

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"

namespace
{

int checks = 0;
int failures = 0;

void check(bool condition, const std::string &what)
{
    checks++;
    if (condition)
        return;
    failures++;
    std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

void checkSpan(const std::string &what, const Span &actual,
               int line, int col, int len)
{
    checks++;
    if (actual.line == line && actual.col == col && actual.len == len)
        return;
    failures++;
    std::fprintf(stderr,
                 "FAIL %s: expected line %d col %d len %d, got line %d col %d len %d\n",
                 what.c_str(), line, col, len,
                 actual.line, actual.col, actual.len);
}

// The roadmap's acceptance case, with the columns counted rather than quoted.
void acceptanceCase()
{
    //                          1234567890123
    const std::string source = "x = 1 + 2 * 3";

    std::vector<Token> tokens = lex(source);
    check(tokens.size() == 8, "source lexes to seven tokens plus EOF");
    if (tokens.size() != 8)
        return;

    checkSpan("token x", tokens[0].span, 1, 1, 1);
    checkSpan("token =", tokens[1].span, 1, 3, 1);
    checkSpan("token 1", tokens[2].span, 1, 5, 1);
    checkSpan("token +", tokens[3].span, 1, 7, 1);
    checkSpan("token 2", tokens[4].span, 1, 9, 1);
    check(tokens[5].type == TokenType::MULTIPLY, "sixth token is *");
    checkSpan("token * — acceptance criterion", tokens[5].span, 1, 11, 1);
    checkSpan("token 3", tokens[6].span, 1, 13, 1);
    checkSpan("token EOF sits past the last character", tokens[7].span, 1, 14, 0);

    Parser parser(tokens);
    std::vector<Node> ast = parser.parse();
    check(ast.size() == 1, "source parses to one statement");
    if (ast.size() != 1)
        return;

    Node assign = ast[0];
    check(assign->type == NodeType::Assign, "the statement is an assignment");
    checkSpan("Assign covers the whole statement", assign->span, 1, 1, 13);

    // The BinOp enclosing the `*` token is the outer `1 + 2 * 3`: cols 5..13
    Node outer = assign->left;
    check(outer->type == NodeType::BinOp && outer->value == "+",
          "the assigned expression is a + BinOp");
    checkSpan("BinOp 1 + 2 * 3 — acceptance criterion", outer->span, 1, 5, 9);

    // The inner `2 * 3` merges its own operands: cols 9..13
    Node inner = outer->right;
    check(inner->type == NodeType::BinOp && inner->value == "*",
          "its right child is a * BinOp");
    checkSpan("BinOp 2 * 3", inner->span, 1, 9, 5);

    // Leaves carry the span of the token they were built from
    checkSpan("Number 1", outer->left->span, 1, 5, 1);
    checkSpan("Number 2", inner->left->span, 1, 9, 1);
    checkSpan("Number 3", inner->right->span, 1, 13, 1);
}

void everyNodeHasASpan(const Node &node)
{
    if (!node)
        return;
    check(node->span.line >= 1 && node->span.col >= 1,
          "node '" + node->value + "' carries a real span");
    everyNodeHasASpan(node->left);
    everyNodeHasASpan(node->right);
}

// Acceptance: every constructed Token and ASTNode carries a non-default span.
void nothingCarriesADefaultSpan()
{
    const std::string source =
        "x = 10\n"
        "y = x * 2 + 1\n"
        "print y / 3 - x\n";

    std::vector<Token> tokens = lex(source);
    for (std::size_t i = 0; i < tokens.size(); i++)
    {
        check(tokens[i].span.line >= 1 && tokens[i].span.col >= 1,
              "token " + std::to_string(i) + " carries a real span");
    }
    for (const Token &token : tokens)
    {
        check(token.span.len == static_cast<int>(token.value.size()),
              "token '" + token.value + "' is as long as its text");
    }

    Parser parser(tokens);
    std::vector<Node> ast = parser.parse();
    check(ast.size() == 3, "three statements parse");
    for (const Node &statement : ast)
        everyNodeHasASpan(statement);
}

void linesAndColumnsAdvance()
{
    const std::string source =
        "x = 1\n"       // line 1
        "\n"            // line 2, blank
        "  y = x + 2\n" // line 3, indented two spaces
        "print\ty\n";   // line 4, tab between keyword and name

    std::vector<Token> tokens = lex(source);
    check(tokens.size() == 11, "four lines lex to ten tokens plus EOF");
    if (tokens.size() != 11)
        return;

    checkSpan("line 1: x", tokens[0].span, 1, 1, 1);
    checkSpan("line 1: 1", tokens[2].span, 1, 5, 1);

    // A blank line still counts, and the indent pushes the column out
    checkSpan("line 3: y after a two-space indent", tokens[3].span, 3, 3, 1);
    checkSpan("line 3: x", tokens[5].span, 3, 7, 1);
    checkSpan("line 3: 2", tokens[7].span, 3, 11, 1);

    // A tab counts as one column, so a column is a character offset
    checkSpan("line 4: print", tokens[8].span, 4, 1, 5);
    checkSpan("line 4: y after a tab", tokens[9].span, 4, 7, 1);

    // EOF lands at the start of the line after the final newline
    checkSpan("EOF after a trailing newline", tokens[10].span, 5, 1, 0);

    Parser parser(tokens);
    std::vector<Node> ast = parser.parse();
    check(ast.size() == 3, "three statements parse");
    if (ast.size() != 3)
        return;

    checkSpan("Assign on line 3 covers cols 3..11", ast[1]->span, 3, 3, 9);
    checkSpan("its BinOp x + 2 covers cols 7..11", ast[1]->left->span, 3, 7, 5);
}

// A Span describes one line, so a merge across a line break cannot widen.
// mergeSpans keeps the left operand's extent rather than inventing a length.
void mergeAcrossLinesKeepsTheLeftOperand()
{
    const std::string source =
        "x = 1 +\n"
        "2\n";

    Parser parser(lex(source));
    std::vector<Node> ast = parser.parse();
    check(ast.size() == 1, "the expression parses across the newline");
    if (ast.size() != 1)
        return;

    Node binop = ast[0]->left;
    check(binop->type == NodeType::BinOp, "the assigned expression is a BinOp");
    checkSpan("Number 1 on line 1", binop->left->span, 1, 5, 1);
    checkSpan("Number 2 on line 2", binop->right->span, 2, 1, 1);
    checkSpan("BinOp does not span the line break", binop->span, 1, 5, 1);
}

} // namespace

int main()
{
    acceptanceCase();
    nothingCarriesADefaultSpan();
    linesAndColumnsAdvance();
    mergeAcrossLinesKeepsTheLeftOperand();

    if (failures != 0)
    {
        std::fprintf(stderr, "span_test: %d of %d checks failed\n", failures, checks);
        return 1;
    }

    std::printf("span_test: %d checks passed\n", checks);
    return 0;
}
