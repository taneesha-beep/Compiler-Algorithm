#include "semantic.h"

#include <functional>
#include <set>

#include "diagnostic.h"

// One flat set of names, unchanged by item 1.2's blocks. It detects a name used
// before anything assigned it, and nothing else; scopes, shadowing and frame
// slots all arrive together with item 1.3's resolver, which replaces this file.
//
// The flatness has an observable consequence now that a block may not run. In
//
//     while false { x = 1 }
//     print x
//
// the assignment declares `x` here, so the check passes and the interpreter is
// the one that reports `x` as undefined — a runtime fault rather than a compile
// error. The resolver in 1.3 is what moves that back to compile time.
void semanticCheck(const std::vector<Node> &statements)
{
    std::set<std::string> declared;

    std::function<void(const Node &)> checkExpr = [&](const Node &node)
    {
        if (!node)
            return;
        if (const IdentifierNode *identifier = tryAs<IdentifierNode>(node))
        {
            if (declared.find(identifier->name) == declared.end())
            {
                // The caret belongs on the use, not the declaration that is
                // missing — the offending node is the identifier itself.
                throw CompileError(Diagnostic{
                    Severity::Error, identifier->span,
                    "variable '" + identifier->name + "' used before assignment"});
            }
            return;
        }
        if (const BinOpNode *binary = tryAs<BinOpNode>(node))
        {
            checkExpr(binary->left);
            checkExpr(binary->right);
            return;
        }
        if (const UnaryOpNode *unary = tryAs<UnaryOpNode>(node))
        {
            checkExpr(unary->operand);
            return;
        }
        // A number or a boolean literal names nothing and has no children.
    };

    std::function<void(const Node &)> checkStatement = [&](const Node &statement)
    {
        if (const AssignNode *assign = tryAs<AssignNode>(statement))
        {
            checkExpr(assign->value);            // check the expression
            declared.insert(assign->name);       // now declare variable
            return;
        }
        if (const PrintNode *print = tryAs<PrintNode>(statement))
        {
            checkExpr(print->value);
            return;
        }
        if (const BlockNode *block = tryAs<BlockNode>(statement))
        {
            for (const Node &inner : block->statements)
                checkStatement(inner);
            return;
        }
        if (const IfNode *conditional = tryAs<IfNode>(statement))
        {
            checkExpr(conditional->condition);
            checkStatement(conditional->thenBranch);
            if (conditional->elseBranch)
                checkStatement(conditional->elseBranch);
            return;
        }
        if (const WhileNode *loop = tryAs<WhileNode>(statement))
        {
            checkExpr(loop->condition);
            checkStatement(loop->body);
            return;
        }
    };

    for (const Node &statement : statements)
        checkStatement(statement);
}
