#pragma once

#include <vector>

#include "ast.h"

// ============================================================
// STAGE 3: SEMANTIC ANALYSIS
// ============================================================

// We just check: are all variables used in expressions
// actually assigned before use?
void semanticCheck(const std::vector<Node> &statements);
