#pragma once

#include <map>
#include <string>
#include <vector>

#include "ast.h"

// ============================================================
// STAGE 4: INTERPRETER / CODE GENERATION
// Evaluates the AST and prints results
// ============================================================

class Interpreter
{
    std::map<std::string, int> variables; // stores variable values at runtime

public:
    int evaluate(Node node);
    void execute(const std::vector<Node> &statements);
};
