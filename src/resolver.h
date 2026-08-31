#pragma once

#include <vector>

#include "ast.h"

// ============================================================
// STAGE 3: RESOLUTION — scopes, and a frame slot per variable
// ============================================================

// Walks the tree carrying a stack of frames, each with a stack of scopes, and
// does three things with it.
//
// It rejects a name that is not in scope where it is used, pointing the caret
// at the use, and throws `CompileError` like the rest of the front end — so a
// resolution error is a compile-time error and exits 65, on the same path as a
// lexical or syntactic one. Since item 1.4 that also covers a call of a
// function that does not exist, a call of the wrong arity, a duplicate
// parameter or function name, and a `return` outside any function.
//
// It assigns every variable a slot index within its enclosing function frame,
// writing that integer onto each node that names the variable — an identifier,
// an assignment target, and since item 1.4 a parameter. **Since item 3.4 the
// interpreter reads all three.** Between items 1.3 and 3.4 nothing did: the
// interpreter went on looking each name up in a `std::map<std::string, Value>`,
// which is the ordered-map lookup ablation D exists to remove, and 3.4 is the
// commit that spent the slots by replacing that map with a vector indexed
// directly. Writing them here and reading them there was deliberate — a
// resolver that had also switched the environment over would have performed
// the ablation with nothing measuring it. See the note in `src/ast.h` on
// `IdentifierNode::slot`.
//
// And it records how wide each frame is: `FunctionNode::frameSize` for a
// function's own, and this function's return value for the program's. Those are
// the sizes the interpreter gives its frame vectors — `callFunction` reads the
// first and `execute` takes the second as a parameter, which is why `main.cpp`
// keeps this return value rather than only narrating it. `--trace` and the unit
// tests read them too.
int resolve(const std::vector<Node> &statements);
