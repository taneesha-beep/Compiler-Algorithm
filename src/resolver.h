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
// an assignment target, and since item 1.4 a parameter. Nothing reads those
// slots yet. The interpreter goes on looking each name up in a
// `std::map<std::string, Value>`, which is the ordered-map lookup ablation D
// exists to remove; item 3.4 is the commit that spends the slots, replacing
// that map with a vector indexed directly, and reports what the change bought.
// Writing them here and reading them there is deliberate — see the note in
// `src/ast.h` on `IdentifierNode::slot`.
//
// And it records how wide each frame is: `FunctionNode::frameSize` for a
// function's own, and this function's return value for the program's. Those
// are the sizes item 3.4 will give its vectors. Today only `--trace` and the
// unit tests read either.
int resolve(const std::vector<Node> &statements);
