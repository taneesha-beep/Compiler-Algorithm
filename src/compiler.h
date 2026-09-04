#pragma once

#include <vector>

#include "ast.h"
#include "chunk.h"

// ============================================================
// STAGE 6: THE BYTECODE COMPILER — a second back end beside the
// tree-walker, consuming the same resolved AST
// ============================================================
//
// This is item 4.2. It walks the tree the parser built and the resolver
// annotated, and writes a `Chunk` through the emit helpers in `src/chunk.h`.
// It executes nothing: item 4.3 is the machine that runs what this produces,
// and until then a chunk is written and then dropped.
//
// ON WHAT IT DOES NOT DO. There is no name resolution here. Item 1.3 numbered
// every variable reference with a frame slot and item 3.4 taught the
// tree-walker to read those numbers; this back end reads exactly the same
// fields, so `LOAD_LOCAL` and `STORE_LOCAL` carry `IdentifierNode::slot`,
// `AssignNode::slot` and `Parameter::slot` unchanged. There is no type
// checking either — the language has none, and every type fault is raised at
// run time by the instruction that meets the wrong value.
//
// ON THE LAYOUT IT CHOOSES. `docs/BYTECODE.md` calls the layout a convention
// of this item rather than a property of the format, and the convention is:
// the program's own statements first, terminated by `HALT`, then the function
// bodies in declaration order. Every function body ends with `CONST 0` and
// `RETURN`, so a body that runs off its end hands back the integer `0` exactly
// as `Interpreter::callFunction` does.
//
// ON THE ORDER OF THE TWO PASSES. `Interpreter::execute` walks the top-level
// statement list twice — once to record every `FunctionNode`, once to run
// everything else — which is what makes a call to a function declared later in
// the file legal. This compiler inherits that obligation: every `FunctionInfo`
// is appended to the chunk's function table *before* any code is emitted, so a
// `CALL` compiled before its callee's body already knows its index. The body's
// `entry` offset is filled in when the body is emitted, which is the second
// half of why a call needs no backpatching at all.
//
// ON WHAT THE CHUNK BORROWS. `SpanEntry::op` is a `const char *` into
// `BinOpNode::op` / `UnaryOpNode::op` — item 4.1's decision, taken because the
// lowering of `<=`, `>=` and `!=` leaves an opcode unable to say which operator
// a diagnostic must quote. Nothing in the chunk owns that text, so **the tree
// must outlive the chunk compiled from it.** Dropping the AST leaves every
// operator spelling dangling, and the symptom is mild enough to be missed: the
// pointers stay non-null and read as empty strings, so a fault would render
// `operator '' cannot be applied to ...`. Item 4.3 inherits this obligation.
//
// Throws `CompileError` (exit 65) if the program exceeds one of the format's
// 16-bit limits — see `maxOperand` in `src/chunk.h`. Those are limits of the
// implementation, settled by the source text alone, which is the rule that
// puts them at 65 rather than 70.

// `statements` is the resolved top level; `programFrameSize` is `resolve()`'s
// return value, the same number `Interpreter::execute` takes as a parameter.
Chunk compile(const std::vector<Node> &statements, int programFrameSize);
