#pragma once

#include <vector>

#include "ast.h"

// ============================================================
// STAGE 3: RESOLUTION — scopes, and a frame slot per variable
// ============================================================

// Walks the tree carrying a scope stack, and does two things with it.
//
// It rejects a name that is not in scope where it is used, pointing the caret
// at the use, and throws `CompileError` like the rest of the front end — so a
// resolution error is a compile-time error and exits 65, on the same path as a
// lexical or syntactic one.
//
// And it assigns every variable a slot index within its enclosing function
// frame, writing that integer onto each node that names the variable. Nothing
// reads those slots yet. The interpreter goes on looking each name up in its
// `std::map<std::string, Value>`, which is the ordered-map lookup ablation D
// exists to remove; item 3.4 is the commit that spends the slots, replacing
// that map with a vector indexed directly, and reports what the change bought.
// Writing them here and reading them there is deliberate — see the note in
// `src/ast.h` on `IdentifierNode::slot`.
//
// Returns the number of slots the program's frame needs, which is the size
// item 3.4 will give that vector. Today only `--trace` and the unit tests read
// it.
int resolve(const std::vector<Node> &statements);
