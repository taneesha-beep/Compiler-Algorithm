#pragma once

#include <map>
#include <string>
#include <vector>

#include "ast.h"
#include "value.h"

// ============================================================
// STAGE 4: INTERPRETER / CODE GENERATION
// Evaluates the AST and prints results
// ============================================================

class Interpreter
{
    // Ordered map keyed on std::string, deliberately: this is the environment
    // ablation D replaces with frame slots, so it stays as it is until then.
    std::map<std::string, Value> variables; // stores variable values at runtime

public:
    Value evaluate(Node node);
    void execute(const std::vector<Node> &statements);
};
