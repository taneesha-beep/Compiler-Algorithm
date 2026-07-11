#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <memory>
#include <stdexcept>
#include <functional>
#include <map>
#include <set>
using namespace std;

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
    string value;
};

vector<Token> lex(const string &source)
{
    vector<Token> tokens;
    size_t i = 0;

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
        if (isdigit(c))
        {
            string num;
            while (i < source.size() && isdigit(source[i]))
            {
                num += source[i++];
            }
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        // Identifiers and keywords (print)
        if (isalpha(c))
        {
            string word;
            while (i < source.size() && isalnum(source[i]))
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
            throw runtime_error(string("Unknown character: ") + c);
        }
        i++;
    }

    tokens.push_back({TokenType::END_OF_FILE, ""});
    return tokens;
}

// ============================================================
// STAGE 2: PARSER — builds an AST (Abstract Syntax Tree)
// ============================================================

// Every node in the tree is one of these types
struct ASTNode
{
    string type;  // "assign", "binop", "number", "identifier", "print"
    string value; // holds number value or variable name or operator
    shared_ptr<ASTNode> left;
    shared_ptr<ASTNode> right;
};

using Node = shared_ptr<ASTNode>;

Node makeNode(string type, string value = "",
              Node left = nullptr, Node right = nullptr)
{
    return make_shared<ASTNode>(ASTNode{type, value, left, right});
}

// Parser class
class Parser
{
    vector<Token> tokens;
    size_t pos = 0;

    Token current() { return tokens[pos]; }
    Token consume() { return tokens[pos++]; }

    Token expect(TokenType type, const string &errMsg)
    {
        if (current().type != type)
            throw runtime_error(errMsg);
        return consume();
    }

public:
    Parser(vector<Token> tokens) : tokens(tokens) {}

    // Parse a full program (list of statements)
    vector<Node> parse()
    {
        vector<Node> statements;
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
            return makeNode("print", "", expr);
        }

        if (current().type == TokenType::IDENTIFIER)
        {
            string varName = consume().value;
            expect(TokenType::EQUALS, "Expected '=' after variable name");
            Node expr = parseExpr();
            return makeNode("assign", varName, expr);
        }

        throw runtime_error("Unknown statement starting with: " + current().value);
    }

    // Expression: handles + - * /
    // Simple precedence: * and / before + and -
    Node parseExpr()
    {
        Node left = parseTerm();

        while (current().type == TokenType::PLUS ||
               current().type == TokenType::MINUS)
        {
            string op = consume().value;
            Node right = parseTerm();
            left = makeNode("binop", op, left, right);
        }

        return left;
    }

    Node parseTerm()
    {
        Node left = parsePrimary();

        while (current().type == TokenType::MULTIPLY ||
               current().type == TokenType::DIVIDE)
        {
            string op = consume().value;
            Node right = parsePrimary();
            left = makeNode("binop", op, left, right);
        }

        return left;
    }

    Node parsePrimary()
    {
        if (current().type == TokenType::NUMBER)
        {
            return makeNode("number", consume().value);
        }
        if (current().type == TokenType::IDENTIFIER)
        {
            return makeNode("identifier", consume().value);
        }
        throw runtime_error("Expected number or variable, got: " + current().value);
    }
};

// ============================================================
// STAGE 3: SEMANTIC ANALYSIS
// ============================================================

// We just check: are all variables used in expressions
// actually assigned before use?

void semanticCheck(const vector<Node> &statements)
{
    set<string> declared;

    for (auto &stmt : statements)
    {
        if (stmt->type == "assign")
        {
            // Check right side before declaring left
            function<void(Node)> checkExpr = [&](Node node)
            {
                if (!node)
                    return;
                if (node->type == "identifier")
                {
                    if (declared.find(node->value) == declared.end())
                    {
                        throw runtime_error(
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
        else if (stmt->type == "print")
        {
            function<void(Node)> checkExpr = [&](Node node)
            {
                if (!node)
                    return;
                if (node->type == "identifier")
                {
                    if (declared.find(node->value) == declared.end())
                    {
                        throw runtime_error(
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

map<string, int> variables; // stores variable values at runtime

int evaluate(Node node)
{
    if (node->type == "number")
    {
        return stoi(node->value);
    }
    if (node->type == "identifier")
    {
        if (variables.find(node->value) == variables.end())
            throw runtime_error("Runtime Error: Undefined variable '" + node->value + "'");
        return variables[node->value];
    }
    if (node->type == "binop")
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
                throw runtime_error("Runtime Error: Division by zero");
            return left / right;
        }
    }
    throw runtime_error("Unknown node type: " + node->type);
}

void execute(const vector<Node> &statements)
{
    for (auto &stmt : statements)
    {
        if (stmt->type == "assign")
        {
            variables[stmt->value] = evaluate(stmt->left);
        }
        else if (stmt->type == "print")
        {
            cout << evaluate(stmt->left) << endl;
        }
    }
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char *argv[])
{

    string programName = argv[0];
    size_t lastSlash = programName.find_last_of("/\\");
    if (lastSlash != string::npos)
        programName = programName.substr(lastSlash + 1);

    try
    {
        if (argc == 2)
        {
            // Read input file
            fstream inputFile(argv[1], ios::in);
            if (!inputFile.is_open())
                throw runtime_error("Could not open file: " + string(argv[1]));

            stringstream ss;
            ss << inputFile.rdbuf();
            string source = ss.str();

            cout << "=== Source Code ===" << endl;
            cout << source << endl;

            // Stage 1: Lex
            cout << "=== Stage 1: Lexing ===" << endl;
            vector<Token> tokens = lex(source);
            for (auto &t : tokens)
            {
                if (t.type != TokenType::END_OF_FILE)
                    cout << "  Token: [" << t.value << "]" << endl;
            }

            // Stage 2: Parse
            cout << "=== Stage 2: Parsing ===" << endl;
            Parser parser(tokens);
            vector<Node> ast = parser.parse();
            cout << "  Parsed " << ast.size() << " statement(s) successfully." << endl;

            // Stage 3: Semantic Analysis
            cout << "=== Stage 3: Semantic Analysis ===" << endl;
            semanticCheck(ast);
            cout << "  No semantic errors found." << endl;

            // Stage 4: Execute
            cout << "=== Stage 4: Output ===" << endl;
            execute(ast);
        }
        else if (argc > 2)
        {
            throw runtime_error("Too many arguments. Max 1 input file.");
        }
        else
        {
            throw runtime_error("Insufficient arguments.\nUsage: " +
                                programName + " <input_file>");
        }
    }
    catch (runtime_error &e)
    {
        cout << "Error: " << e.what() << endl;
    }

    return 0;
}