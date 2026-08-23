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

    // A statement is one of:
    //   print <expr>
    //   identifier = <expr>
    //   if <expr> <block> ( else ( <if> | <block> ) )?
    //   while <expr> <block>
    //   return <expr>?
    //   <block>
    //
    // A function declaration is not in that list. It is only legal at the top
    // level, so `parse` handles it and `parseStatement` rejects it wherever it
    // is reached from — which is every position inside a block.
    //
    // ON THE ABSENCE OF PARENTHESES, revisited for item 1.4. The condition of
    // an `if` or a `while` is still not parenthesised, the braces are still
    // mandatory, and the language still has no grouping parentheses at all:
    // `(1 + 2) * 3` does not parse. Item 1.4 introduces `(` in two new
    // positions — a call's argument list and a function's parameter list — and
    // the reasoning recorded in item 1.2 survives that intact rather than
    // being overtaken by it. That reasoning was never "the language has no
    // `(`"; it was that a `(` which worked in exactly one position and nowhere
    // else would read as an oversight rather than as a rule. The rule the two
    // new positions obey is stateable in one line and holds everywhere: **a
    // `(` follows a name and delimits an argument or parameter list, and never
    // groups an expression.** `if (x < 1) { }` is still rejected, because
    // there is no name in front of the `(`.
    //
    // Adding grouping parentheses would be a language feature, and no roadmap
    // item lists one. The cost of not having them is real and bounded: an
    // expression's shape is fixed by the precedence cascade, so a program that
    // needs `(a + b) * c` has to name the sum first. Mandatory braces also
    // settle the dangling `else` outright: an `else` can only attach to an
    // `if` whose block has closed.
    Node parseStatement();
    Node parseBlock();
    Node parseIf();
    Node parseWhile();
    Node parseReturn();

    // A function declaration, reached only from `parse`. Item 1.4.
    Node parseFunction();

    // ONE FUNCTION PER PRECEDENCE LEVEL, loosest first:
    //
    //     equality -> comparison -> term -> factor -> unary -> primary
    //
    // Each level parses the level below it and then loops on its own
    // operators, so the level that runs last binds loosest. The three
    // functions that existed before item 1.1 have shifted down the cascade
    // rather than acquiring new meanings: what used to be `parseTerm` handled
    // `*` and `/` and is now `parseFactor`, and what used to be `parseExpr`
    // handled `+` and `-` and is now `parseTerm`. Naming them for the roadmap's
    // levels is the point — a `parseTerm` that means `factor` in the code and
    // `term` in the plan is a trap for every later item.
    //
    // `parseExpr` is the entry point, not a seventh level: it names the
    // loosest level in one place so that call sites do not have to.
    Node parseExpr();
    Node parseEquality();   // == !=
    Node parseComparison(); // < <= > >=
    Node parseTerm();       // + -
    Node parseFactor();     // * /
    Node parseUnary();      // prefix - !
    Node parsePrimary();    // literals, names, and calls
};
