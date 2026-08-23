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
    // One flat environment, an ordered map keyed on std::string. Both halves of
    // that are deliberate and both are measured: the map is what ablation D
    // replaces with frame slots, and the flatness is what item 1.3 replaces
    // with a scope stack. A block does not push a scope here — see the note in
    // interpreter.cpp.
    std::map<std::string, Value> variables; // stores variable values at runtime

    void executeStatement(const Node &statement);

public:
    // ON THE BY-VALUE Node. `evaluate` takes its `shared_ptr` by value, so
    // every node of every expression pays a reference-count increment and
    // decrement. That is not an oversight: it is one of the four unforced
    // inefficiencies the baseline is required to keep, and it is ablation A's
    // entire subject. Do not change it to `const Node &` before item 3.1.
    Value evaluate(Node node);
    void execute(const std::vector<Node> &statements);
};
