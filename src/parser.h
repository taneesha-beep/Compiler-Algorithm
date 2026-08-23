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
    //   <block>
    //
    // ON THE ABSENCE OF PARENTHESES. The condition of an `if` or a `while` is
    // not parenthesised, and the braces are mandatory. The language has no
    // grouping parentheses at all — `(1 + 2) * 3` does not parse and item 1.2
    // does not add it, because grouping is not a feature the roadmap lists.
    // Writing `if (x < 1) { }` would therefore have introduced a `(` that works
    // in exactly one position and nowhere else, which reads like an oversight
    // rather than a rule. Mandatory braces also settle the dangling `else`
    // outright: an `else` can only attach to an `if` whose block has closed.
    Node parseStatement();
    Node parseBlock();
    Node parseIf();
    Node parseWhile();

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
    Node parsePrimary();    // literals and names
};
