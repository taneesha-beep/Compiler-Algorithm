#include "semantic.h"

#include <functional>
#include <set>
#include <stdexcept>

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
                throw std::runtime_error(
                    "Semantic Error: Variable '" + node->value +
                    "' used before assignment");
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
