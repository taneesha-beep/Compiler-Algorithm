#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <memory>
#include <stdexcept>
#include <functional>
#include <map>
#include <set>
#include <cctype>

// ============================================================
// STAGE 1: LEXER
// ============================================================

enum class TokenType
{
    NUMBER,
    IDENTIFIER,
    EQUALS,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    PRINT,
    END_OF_FILE
};

struct Token
{
    TokenType type;
    std::string value;
};

std::vector<Token> lex(const std::string &source)
{
    std::vector<Token> tokens;
    std::size_t i = 0;

    while (i < source.size())
    {
        char c = source[i];

        // Skip whitespace and newlines
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
        {
            i++;
            continue;
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            std::string num;
            while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i])))
            {
                num += source[i++];
            }
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        // Identifiers and keywords (print)
        if (std::isalpha(static_cast<unsigned char>(c)))
        {
            std::string word;
            while (i < source.size() && std::isalnum(static_cast<unsigned char>(source[i])))
            {
                word += source[i++];
            }
            if (word == "print")
            {
                tokens.push_back({TokenType::PRINT, word});
            }
            else
            {
                tokens.push_back({TokenType::IDENTIFIER, word});
            }
            continue;
        }

        // Operators
        switch (c)
        {
        case '=':
            tokens.push_back({TokenType::EQUALS, "="});
            break;
        case '+':
            tokens.push_back({TokenType::PLUS, "+"});
            break;
        case '-':
            tokens.push_back({TokenType::MINUS, "-"});
            break;
        case '*':
            tokens.push_back({TokenType::MULTIPLY, "*"});
            break;
        case '/':
            tokens.push_back({TokenType::DIVIDE, "/"});
            break;
        default:
            throw std::runtime_error(std::string("Unknown character: ") + c);
        }
        i++;
    }

    tokens.push_back({TokenType::END_OF_FILE, ""});
    return tokens;
}

// ============================================================
// STAGE 2: PARSER — builds an AST (Abstract Syntax Tree)
// ============================================================

// Every kind of node in the tree
enum class NodeType
{
    Assign,
    BinOp,
    Number,
    Identifier,
    Print
};

// Every node in the tree is one of these types
struct ASTNode
{
    NodeType type;
    std::string value; // holds number value or variable name or operator
    std::shared_ptr<ASTNode> left;
    std::shared_ptr<ASTNode> right;
};

using Node = std::shared_ptr<ASTNode>;

Node makeNode(NodeType type, std::string value = "",
              Node left = nullptr, Node right = nullptr)
{
    return std::make_shared<ASTNode>(ASTNode{type, value, left, right});
}

// Parser class
class Parser
{
    std::vector<Token> tokens;
    std::size_t pos = 0;

    Token current() { return tokens[pos]; }
    Token consume() { return tokens[pos++]; }

    Token expect(TokenType type, const std::string &errMsg)
    {
        if (current().type != type)
            throw std::runtime_error(errMsg);
        return consume();
    }

public:
    Parser(std::vector<Token> tokens) : tokens(tokens) {}

    // Parse a full program (list of statements)
    std::vector<Node> parse()
    {
        std::vector<Node> statements;
        while (current().type != TokenType::END_OF_FILE)
        {
            statements.push_back(parseStatement());
        }
        return statements;
    }

    // A statement is either:
    //   print <expr>
    //   identifier = <expr>
    Node parseStatement()
    {
        if (current().type == TokenType::PRINT)
        {
            consume(); // eat 'print'
            Node expr = parseExpr();
            return makeNode(NodeType::Print, "", expr);
        }

        if (current().type == TokenType::IDENTIFIER)
        {
            std::string varName = consume().value;
            expect(TokenType::EQUALS, "Expected '=' after variable name");
            Node expr = parseExpr();
            return makeNode(NodeType::Assign, varName, expr);
        }

        throw std::runtime_error("Unknown statement starting with: " + current().value);
    }

    // Expression: handles + - * /
    // Simple precedence: * and / before + and -
    Node parseExpr()
    {
        Node left = parseTerm();

        while (current().type == TokenType::PLUS ||
               current().type == TokenType::MINUS)
        {
            std::string op = consume().value;
            Node right = parseTerm();
            left = makeNode(NodeType::BinOp, op, left, right);
        }

        return left;
    }

    Node parseTerm()
    {
        Node left = parsePrimary();

        while (current().type == TokenType::MULTIPLY ||
               current().type == TokenType::DIVIDE)
        {
            std::string op = consume().value;
            Node right = parsePrimary();
            left = makeNode(NodeType::BinOp, op, left, right);
        }

        return left;
    }

    Node parsePrimary()
    {
        if (current().type == TokenType::NUMBER)
        {
            return makeNode(NodeType::Number, consume().value);
        }
        if (current().type == TokenType::IDENTIFIER)
        {
            return makeNode(NodeType::Identifier, consume().value);
        }
        throw std::runtime_error("Expected number or variable, got: " + current().value);
    }
};

// ============================================================
// STAGE 3: SEMANTIC ANALYSIS
// ============================================================

// We just check: are all variables used in expressions
// actually assigned before use?

void semanticCheck(const std::vector<Node> &statements)
{
    std::set<std::string> declared;

    for (auto &stmt : statements)
    {
        if (stmt->type == NodeType::Assign)
        {
            // Check right side before declaring left
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
            checkExpr(stmt->left);        // check the expression
            declared.insert(stmt->value); // now declare variable
        }
        else if (stmt->type == NodeType::Print)
        {
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
            checkExpr(stmt->left);
        }
    }
}

// ============================================================
// STAGE 4: INTERPRETER / CODE GENERATION
// Evaluates the AST and prints results
// ============================================================

std::map<std::string, int> variables; // stores variable values at runtime

int evaluate(Node node)
{
    if (node->type == NodeType::Number)
    {
        return std::stoi(node->value);
    }
    if (node->type == NodeType::Identifier)
    {
        if (variables.find(node->value) == variables.end())
            throw std::runtime_error("Runtime Error: Undefined variable '" + node->value + "'");
        return variables[node->value];
    }
    if (node->type == NodeType::BinOp)
    {
        int left = evaluate(node->left);
        int right = evaluate(node->right);
        if (node->value == "+")
            return left + right;
        if (node->value == "-")
            return left - right;
        if (node->value == "*")
            return left * right;
        if (node->value == "/")
        {
            if (right == 0)
                throw std::runtime_error("Runtime Error: Division by zero");
            return left / right;
        }
    }
    throw std::runtime_error("Unknown node type");
}

void execute(const std::vector<Node> &statements)
{
    for (auto &stmt : statements)
    {
        if (stmt->type == NodeType::Assign)
        {
            variables[stmt->value] = evaluate(stmt->left);
        }
        else if (stmt->type == NodeType::Print)
        {
            std::cout << evaluate(stmt->left) << std::endl;
        }
    }
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char *argv[])
{
    if (argc > 2)
    {
        std::cout << "Error: Too many arguments. Max 1 input file." << std::endl;
        return 0;
    }
    if (argc < 2)
    {
        std::string programName = argv[0];
        std::size_t lastSlash = programName.find_last_of("/\\");
        if (lastSlash != std::string::npos)
            programName = programName.substr(lastSlash + 1);

        std::cout << "Error: Insufficient arguments.\nUsage: " +
                          programName + " <input_file>"
                  << std::endl;
        return 0;
    }

    try
    {
        // Read input file
        std::fstream inputFile(argv[1], std::ios::in);
        if (!inputFile.is_open())
            throw std::runtime_error("Could not open file: " + std::string(argv[1]));

        std::stringstream ss;
        ss << inputFile.rdbuf();
        std::string source = ss.str();

        std::cout << "=== Source Code ===" << std::endl;
        std::cout << source << std::endl;

        // Stage 1: Lex
        std::cout << "=== Stage 1: Lexing ===" << std::endl;
        std::vector<Token> tokens = lex(source);
        for (auto &t : tokens)
        {
            if (t.type != TokenType::END_OF_FILE)
                std::cout << "  Token: [" << t.value << "]" << std::endl;
        }

        // Stage 2: Parse
        std::cout << "=== Stage 2: Parsing ===" << std::endl;
        Parser parser(tokens);
        std::vector<Node> ast = parser.parse();
        std::cout << "  Parsed " << ast.size() << " statement(s) successfully." << std::endl;

        // Stage 3: Semantic Analysis
        std::cout << "=== Stage 3: Semantic Analysis ===" << std::endl;
        semanticCheck(ast);
        std::cout << "  No semantic errors found." << std::endl;

        // Stage 4: Execute
        std::cout << "=== Stage 4: Output ===" << std::endl;
        execute(ast);
    }
    catch (std::runtime_error &e)
    {
        std::cout << "Error: " << e.what() << std::endl;
    }

    return 0;
}
