#include "semantic.h"

#include <functional>
#include <set>

#include "diagnostic.h"

void semanticCheck(const std::vector<Node> &statements)
{
    std::set<std::string> declared;

    std::function<void(Node)> checkExpr = [&](Node node)
    {
        if (!node)
            return;
        if (node->type == NodeType::Identifier)
        {
            if (declared.find(node->value) == declared.end())
            {
                // The caret belongs on the use, not the declaration that is
                // missing — the offending node is the identifier itself.
                throw CompileError(Diagnostic{
                    Severity::Error, node->span,
                    "variable '" + node->value + "' used before assignment"});
            }
        }
        checkExpr(node->left);
        checkExpr(node->right);
    };

    for (auto &stmt : statements)
    {
        if (stmt->type == NodeType::Assign)
        {
            checkExpr(stmt->left);        // check the expression
            declared.insert(stmt->value); // now declare variable
        }
        else if (stmt->type == NodeType::Print)
        {
            checkExpr(stmt->left);
        }
    }
}
