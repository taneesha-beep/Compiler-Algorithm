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

// Item 1.2 split ASTNode into one struct per node type, so a child is reached
// through the type that owns it — `assign->value`, `binary->left` — rather than
// through a shared `left`/`right` pair. This returns the concrete node, or null
// with a counted failure, so that a wrong node type is reported rather than
// dereferenced.
template <typename T>
const T *expectNode(const Node &node, const std::string &what)
{
    const T *typed = node ? tryAs<T>(node) : nullptr;
    check(typed != nullptr, what);
    return typed;
}

// A name for a node in a failure message. Took the place of the `value` string
// every node used to carry, which the split removed from the nodes that had no
// use for it.
std::string nodeLabel(const Node &node)
{
    if (const BinOpNode *binary = tryAs<BinOpNode>(node))
        return "BinOp " + binary->op;
    if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(node))
        return "UnaryOp " + unary->op;
    if (const NumberNode *number = tryAs<NumberNode>(node))
        return "Number " + number->text;
    if (const IdentifierNode *identifier = tryAs<IdentifierNode>(node))
        return "Identifier " + identifier->name;
    if (const BooleanNode *boolean = tryAs<BooleanNode>(node))
        return boolean->value ? "Boolean true" : "Boolean false";
    if (const AssignNode *assign = tryAs<AssignNode>(node))
        return "Assign " + assign->name;
    if (tryAs<PrintNode>(node))
        return "Print";
    if (tryAs<BlockNode>(node))
        return "Block";
    if (tryAs<IfNode>(node))
        return "If";
    if (tryAs<WhileNode>(node))
        return "While";
    return "node of an unrecognised type";
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

    const AssignNode *assign =
        expectNode<AssignNode>(ast[0], "the statement is an assignment");
    if (!assign)
        return;
    checkSpan("Assign covers the whole statement", assign->span, 1, 1, 13);

    // The BinOp enclosing the `*` token is the outer `1 + 2 * 3`: cols 5..13
    const BinOpNode *outer =
        expectNode<BinOpNode>(assign->value, "the assigned expression is a BinOp");
    if (!outer)
        return;
    check(outer->op == "+", "the assigned expression is a + BinOp");
    checkSpan("BinOp 1 + 2 * 3 — acceptance criterion", outer->span, 1, 5, 9);

    // The inner `2 * 3` merges its own operands: cols 9..13
    const BinOpNode *inner =
        expectNode<BinOpNode>(outer->right, "its right child is a BinOp");
    if (!inner)
        return;
    check(inner->op == "*", "its right child is a * BinOp");
    checkSpan("BinOp 2 * 3", inner->span, 1, 9, 5);

    // Leaves carry the span of the token they were built from
    checkSpan("Number 1", outer->left->span, 1, 5, 1);
    checkSpan("Number 2", inner->left->span, 1, 9, 1);
    checkSpan("Number 3", inner->right->span, 1, 13, 1);
}

// Visits every node in the tree. The split means there is no longer one pair of
// child pointers to follow, so every node type that owns children is named here
// — and a node type added later without a case here would silently stop being
// covered, which is why the last line fails rather than returning quietly.
void everyNodeHasASpan(const Node &node)
{
    if (!node)
        return;
    check(node->span.line >= 1 && node->span.col >= 1,
          nodeLabel(node) + " carries a real span");

    if (const AssignNode *assign = tryAs<AssignNode>(node))
        return everyNodeHasASpan(assign->value);
    if (const PrintNode *print = tryAs<PrintNode>(node))
        return everyNodeHasASpan(print->value);
    if (const BinOpNode *binary = tryAs<BinOpNode>(node))
    {
        everyNodeHasASpan(binary->left);
        return everyNodeHasASpan(binary->right);
    }
    if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(node))
        return everyNodeHasASpan(unary->operand);
    if (const BlockNode *block = tryAs<BlockNode>(node))
    {
        for (const Node &inner : block->statements)
            everyNodeHasASpan(inner);
        return;
    }
    if (const IfNode *conditional = tryAs<IfNode>(node))
    {
        everyNodeHasASpan(conditional->condition);
        everyNodeHasASpan(conditional->thenBranch);
        return everyNodeHasASpan(conditional->elseBranch);
    }
    if (const WhileNode *loop = tryAs<WhileNode>(node))
    {
        everyNodeHasASpan(loop->condition);
        return everyNodeHasASpan(loop->body);
    }
    if (tryAs<NumberNode>(node) || tryAs<BooleanNode>(node) ||
        tryAs<IdentifierNode>(node))
        return; // leaves

    check(false, nodeLabel(node) + " is a node type this walk does not know");
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
    const AssignNode *assign =
        expectNode<AssignNode>(ast[1], "line 3 holds an assignment");
    if (assign)
        checkSpan("its BinOp x + 2 covers cols 7..11", assign->value->span, 3, 7, 5);
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

    const AssignNode *assign =
        expectNode<AssignNode>(ast[0], "the statement is an assignment");
    if (!assign)
        return;
    const BinOpNode *binop =
        expectNode<BinOpNode>(assign->value, "the assigned expression is a BinOp");
    if (!binop)
        return;
    checkSpan("Number 1 on line 1", binop->left->span, 1, 5, 1);
    checkSpan("Number 2 on line 2", binop->right->span, 2, 1, 1);
    checkSpan("BinOp does not span the line break", binop->span, 1, 5, 1);
}

// The node types item 1.2 added. A block spans its braces; an `if` and a
// `while` start at their keyword. All three usually run across several lines,
// and mergeSpans deliberately does not widen across a line break — it keeps the
// left operand's extent rather than inventing a length — so a multi-line
// statement's span stops at the end of its first line. That is the documented
// behaviour, and these checks are what say it is intended rather than a bug.
void theStatementNodesCarrySpans()
{
    const std::string source =
        "x = 1\n"                 // line 1
        "while x < 3 {\n"         // line 2
        "    x = x + 1\n"         // line 3
        "}\n"                     // line 4
        "if x == 3 { print x }\n" // line 5, entirely on one line
        "{ x = 0 }\n";            // line 6, a block on its own

    Parser parser(lex(source));
    std::vector<Node> ast = parser.parse();
    check(ast.size() == 4, "the source parses to four statements");
    if (ast.size() != 4)
        return;

    const WhileNode *loop = expectNode<WhileNode>(ast[1], "statement 2 is a while");
    if (loop)
    {
        // Starts at the keyword on line 2; the body closes on line 4, so the
        // merge cannot widen and the span stops at the end of line 2.
        checkSpan("while starts at its keyword", loop->span, 2, 1, 13);
        checkSpan("its condition x < 3 covers cols 7..11", loop->condition->span,
                  2, 7, 5);
        const BlockNode *body = expectNode<BlockNode>(loop->body, "its body is a block");
        if (body)
        {
            checkSpan("the body's brace opens on line 2", body->span, 2, 13, 1);
            check(body->statements.size() == 1,
                  "the body holds one statement");
        }
    }

    // A single-line `if` is the case where a span can cover the whole statement.
    const IfNode *conditional = expectNode<IfNode>(ast[2], "statement 3 is an if");
    if (conditional)
    {
        checkSpan("a one-line if spans keyword to closing brace",
                  conditional->span, 5, 1, 21);
        checkSpan("its condition x == 3 covers cols 4..9",
                  conditional->condition->span, 5, 4, 6);
        check(conditional->elseBranch == nullptr,
              "an if with no else leaves the else branch null");
        const BlockNode *thenBranch =
            expectNode<BlockNode>(conditional->thenBranch, "its then-branch is a block");
        if (thenBranch)
            checkSpan("the then-branch spans its braces", thenBranch->span, 5, 11, 11);
    }

    // A block standing on its own is a statement.
    const BlockNode *bare = expectNode<BlockNode>(ast[3], "statement 4 is a bare block");
    if (bare)
    {
        checkSpan("a bare block spans its braces", bare->span, 6, 1, 9);
        check(bare->statements.size() == 1, "it holds one statement");
    }

    // And every node underneath them still carries a real span.
    for (const Node &statement : ast)
        everyNodeHasASpan(statement);
}

// An `else` branch is a child the tree did not have before, and it is the one
// child that is legitimately absent. Both shapes are walked.
void anElseBranchIsAChildLikeAnyOther()
{
    Parser parser(lex("if true { print 1 } else { print 2 }\n"));
    std::vector<Node> ast = parser.parse();
    check(ast.size() == 1, "the if/else parses to one statement");
    if (ast.size() != 1)
        return;

    const IfNode *conditional = expectNode<IfNode>(ast[0], "the statement is an if");
    if (!conditional)
        return;
    check(conditional->elseBranch != nullptr, "its else branch is present");
    checkSpan("the if spans keyword to the closing brace of the else",
              conditional->span, 1, 1, 36);
    everyNodeHasASpan(ast[0]);

    // `else if` nests a second If inside the first rather than needing a node
    // type of its own.
    Parser chained(lex("if true { print 1 } else if false { print 2 } else { print 3 }\n"));
    std::vector<Node> chain = chained.parse();
    const IfNode *first = expectNode<IfNode>(chain[0], "the chain is an if");
    if (!first)
        return;
    const IfNode *second =
        expectNode<IfNode>(first->elseBranch, "its else branch is a second if");
    if (second)
        check(second->elseBranch != nullptr, "which carries the final else");
}

} // namespace

int main()
{
    acceptanceCase();
    nothingCarriesADefaultSpan();
    linesAndColumnsAdvance();
    mergeAcrossLinesKeepsTheLeftOperand();
    theStatementNodesCarrySpans();
    anElseBranchIsAChildLikeAnyOther();

    if (failures != 0)
    {
        std::fprintf(stderr, "span_test: %d of %d checks failed\n", failures, checks);
        return 1;
    }

    std::printf("span_test: %d checks passed\n", checks);
    return 0;
}
