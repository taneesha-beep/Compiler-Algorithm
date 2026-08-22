// Unit checks for the expression grammar and the value type — roadmap item 1.1.
//
// Two things here cannot be reached from a golden-file case. The first is the
// *shape* of the tree: unary binds tighter than `*`, but negation distributes
// through multiplication and division, so `-2 * 3` and `-(2 * 3)` agree on the
// answer and disagree only on the tree. Nothing a program can print
// distinguishes them; only an assertion on the nodes does. The second is the
// value type itself, which is an in-memory representation the interpreter never
// shows. This binary links algo_core and inspects both directly. A failed check
// prints to stderr and the process exits non-zero, which is all CTest reads —
// no third-party test framework, in keeping with the project having no external
// dependencies.

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "ast.h"
#include "diagnostic.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "token.h"
#include "value.h"

namespace
{

int checks = 0;
int failures = 0;

void check(bool condition, const std::string &what)
{
    checks++;
    if (condition)
        return;
    failures++;
    std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

// ============================================================
// The value type
// ============================================================

// The representation is chosen, not incidental: item 1.5 widens the integer arm
// to int64_t and Phase 3 measures this exact type being returned from every
// node of the tree. These checks pin the properties that choice rests on.
void theValueTypeIsATaggedUnion()
{
    Value integer = Value::fromInt(7);
    check(integer.isInt(), "an integer value reports the Int type");
    check(!integer.isBool(), "an integer value is not a boolean");
    check(integer.integer == 7, "an integer value holds what it was built from");

    Value boolean = Value::fromBool(true);
    check(boolean.isBool(), "a boolean value reports the Bool type");
    check(!boolean.isInt(), "a boolean value is not an integer");
    check(boolean.boolean, "a boolean value holds what it was built from");

    check(Value::fromBool(false).isBool(), "false is still a boolean");
    check(!Value::fromBool(false).boolean, "false holds false");

    // The negative case matters because the union arms overlap in storage: a
    // zero integer and a false boolean have the same bits and must not compare
    // as the same value. Only the tag separates them, which is why every read
    // is guarded by it.
    check(Value::fromInt(0).type != Value::fromBool(false).type,
          "zero and false are distinguished by the tag, not by the bits");

    // No heap, no reference count, no destructor: `evaluate` returns one of
    // these at every node, so ownership here would be allocation traffic in the
    // hot path — underneath ablation A, whose subject is the shared_ptr on the
    // node rather than anything on the value.
    check(std::is_trivially_copyable<Value>::value,
          "a Value is trivially copyable — it owns nothing");
    check(sizeof(Value) <= 2 * sizeof(int),
          "a Value is a tag beside one arm, not a struct with both");

    check(std::string(typeName(ValueType::Int)) == "integer",
          "the Int arm is called 'integer' in a diagnostic");
    check(std::string(typeName(ValueType::Bool)) == "boolean",
          "the Bool arm is called 'boolean' in a diagnostic");
}

// ============================================================
// Running a program
// ============================================================

enum class Outcome
{
    Value,
    CompileError_,
    RuntimeFault_
};

// Runs `x = <expr>` and hands back what x ended up as. An assignment rather
// than a print, so that a case which wrongly succeeds does not scribble on this
// binary's own stdout.
Outcome evaluateExpression(const std::string &expression, Value &out)
{
    try
    {
        const std::string source = "x = " + expression + "\n";
        Parser parser(lex(source));
        std::vector<Node> ast = parser.parse();
        semanticCheck(ast);
        Interpreter interpreter;
        out = interpreter.evaluate(ast[0]->left);
    }
    catch (const CompileError &)
    {
        return Outcome::CompileError_;
    }
    catch (const RuntimeFault &)
    {
        return Outcome::RuntimeFault_;
    }
    return Outcome::Value;
}

void checkInt(const std::string &expression, int expected)
{
    Value value;
    Outcome outcome = evaluateExpression(expression, value);
    checks++;
    if (outcome == Outcome::Value && value.isInt() && value.integer == expected)
        return;
    failures++;
    if (outcome != Outcome::Value)
        std::fprintf(stderr, "FAIL %s: raised an error, expected %d\n",
                     expression.c_str(), expected);
    else if (!value.isInt())
        std::fprintf(stderr, "FAIL %s: got a boolean, expected the integer %d\n",
                     expression.c_str(), expected);
    else
        std::fprintf(stderr, "FAIL %s: got %d, expected %d\n",
                     expression.c_str(), value.integer, expected);
}

void checkBool(const std::string &expression, bool expected)
{
    Value value;
    Outcome outcome = evaluateExpression(expression, value);
    checks++;
    if (outcome == Outcome::Value && value.isBool() && value.boolean == expected)
        return;
    failures++;
    if (outcome != Outcome::Value)
        std::fprintf(stderr, "FAIL %s: raised an error, expected %s\n",
                     expression.c_str(), expected ? "true" : "false");
    else if (!value.isBool())
        std::fprintf(stderr, "FAIL %s: got an integer, expected the boolean %s\n",
                     expression.c_str(), expected ? "true" : "false");
    else
        std::fprintf(stderr, "FAIL %s: got %s, expected %s\n",
                     expression.c_str(), value.boolean ? "true" : "false",
                     expected ? "true" : "false");
}

void checkTypeFault(const std::string &expression)
{
    Value value;
    Outcome outcome = evaluateExpression(expression, value);
    check(outcome == Outcome::RuntimeFault_,
          expression + " is a runtime fault");
}

// ============================================================
// Precedence, by value
// ============================================================

void loosestLevelsBindLast()
{
    // The roadmap's acceptance criterion for item 1.1.
    checkBool("1 + 2 * 3 == 7", true);

    // equality is looser than comparison: `(1 < 2) == true`. The other reading,
    // `1 < (2 == true)`, compares an integer against a boolean and faults.
    checkBool("1 < 2 == true", true);
    checkBool("5 > 6 != true", true);

    // comparison is looser than term: `(1 + 2) < 4`. The other reading,
    // `1 + (2 < 4)`, adds an integer to a boolean and faults.
    checkBool("1 + 2 < 4", true);
    checkBool("10 - 4 >= 6", true);

    // term is looser than factor — the precedence the language already had.
    checkInt("2 + 3 * 4", 14);
    checkInt("2 * 3 + 4", 10);
    checkInt("12 / 3 - 1", 3);

    // Every comparison operator, so that none is wired to the wrong test.
    checkBool("1 == 1", true);
    checkBool("1 == 2", false);
    checkBool("1 != 2", true);
    checkBool("1 != 1", false);
    checkBool("1 < 2", true);
    checkBool("2 < 1", false);
    checkBool("2 <= 2", true);
    checkBool("3 <= 2", false);
    checkBool("3 > 2", true);
    checkBool("2 > 3", false);
    checkBool("3 >= 3", true);
    checkBool("2 >= 3", false);

    // Booleans compare against booleans.
    checkBool("true == true", true);
    checkBool("true == false", false);
    checkBool("true != false", true);

    // Binary levels are left-associative, which subtraction can see.
    checkInt("10 - 3 - 2", 5);
    checkInt("100 / 5 / 2", 10);
}

void unaryOperatorsStack()
{
    checkInt("-5", -5);
    checkInt("- -5", 5);
    // There is no decrement operator, so `--5` has only the one reading.
    checkInt("--5", 5);
    checkInt("---5", -5);
    checkBool("!true", false);
    checkBool("!!true", true);
    checkBool("!!!true", false);
    checkBool("!false", true);

    checkInt("-2 * 3", -6);
    checkInt("-2 + 3", 1);
    checkInt("3 - -2", 5);
    checkBool("-1 < 0", true);
}

// ============================================================
// Precedence, by tree shape
// ============================================================

// Unary sits below factor in the cascade, so it binds tighter than `*`. No
// program can print the difference — negation distributes through
// multiplication — so the assertion has to be on the nodes.
// Returns null rather than letting the exception escape. A front end that
// regressed into rejecting one of these would otherwise abort this binary at
// the first bad case, and the checks after it would never run — a suite that
// stops at the first failure hides the shape of the regression.
Node parseOneExpression(const std::string &expression)
{
    try
    {
        const std::string source = "x = " + expression + "\n";
        Parser parser(lex(source));
        return parser.parse()[0]->left;
    }
    catch (const DiagnosticError &e)
    {
        std::fprintf(stderr, "FAIL %s: rejected by the front end: %s\n",
                     expression.c_str(), e.what());
        return nullptr;
    }
}

// Guards every dereference of a parsed tree below, so that a null from the
// helper above is a counted failure rather than a crash.
bool parsed(const Node &node, const std::string &what)
{
    check(node != nullptr, what);
    return node != nullptr;
}

void unaryBindsTighterThanMultiplication()
{
    // `-2 * 3` must be `(-2) * 3`: a BinOp whose left child is the negation,
    // and not a negation wrapping the whole product.
    Node root = parseOneExpression("-2 * 3");
    if (!parsed(root, "-2 * 3 parses"))
        return;
    check(root->type == NodeType::BinOp && root->value == "*",
          "-2 * 3 is rooted at the multiplication, not at the negation");
    if (root->type != NodeType::BinOp)
        return;
    check(root->left->type == NodeType::UnaryOp && root->left->value == "-",
          "its left operand is the negation of 2");
    check(root->left->left->type == NodeType::Number &&
              root->left->left->value == "2",
          "the negation applies to 2 alone");
    check(root->right->type == NodeType::Number && root->right->value == "3",
          "its right operand is 3");
}

void aPrefixOperatorRecursesRatherThanLoops()
{
    // Right-associative, so the operators nest outward-in.
    Node root = parseOneExpression("- -5");
    if (!parsed(root, "- -5 parses"))
        return;
    check(root->type == NodeType::UnaryOp, "- -5 is rooted at a negation");
    if (root->type != NodeType::UnaryOp)
        return;
    check(root->left->type == NodeType::UnaryOp,
          "which applies to a second negation");
    check(root->left->left->type == NodeType::Number,
          "which applies to the literal");
}

void theCascadeNestsInTheStatedOrder()
{
    // equality -> comparison -> term -> factor -> unary -> primary, read off
    // one expression that exercises every boundary at once.
    Node equality = parseOneExpression("1 + 2 * -3 < 4 == true");
    if (!parsed(equality, "the whole cascade parses"))
        return;
    check(equality->type == NodeType::BinOp && equality->value == "==",
          "the loosest operator, ==, is at the root");
    if (equality->type != NodeType::BinOp)
        return;

    Node comparison = equality->left;
    check(comparison->type == NodeType::BinOp && comparison->value == "<",
          "comparison sits directly under equality");
    if (comparison->type != NodeType::BinOp)
        return;

    Node term = comparison->left;
    check(term->type == NodeType::BinOp && term->value == "+",
          "term sits directly under comparison");
    if (term->type != NodeType::BinOp)
        return;

    Node factor = term->right;
    check(factor->type == NodeType::BinOp && factor->value == "*",
          "factor sits directly under term");
    if (factor->type != NodeType::BinOp)
        return;

    check(factor->right->type == NodeType::UnaryOp,
          "unary sits directly under factor");
    check(factor->right->left->type == NodeType::Number,
          "primary sits directly under unary");
    check(equality->right->type == NodeType::True,
          "the right-hand side of == is the boolean literal");
}

// ============================================================
// Spans on the nodes item 1.1 adds
// ============================================================

void theNewTokensAndNodesCarryRealSpans()
{
    //                          1234567890123456
    const std::string source = "x = 1 <= 2 == true";
    std::vector<Token> tokens = lex(source);

    check(tokens.size() == 8, "the source lexes to seven tokens plus EOF");
    if (tokens.size() != 8)
        return;

    // A two-character operator is two columns wide. If it were recorded as one,
    // every caret drawn under it would be a character short.
    check(tokens[3].type == TokenType::LESS_EQUAL, "'<=' is one token");
    check(tokens[3].span.col == 7 && tokens[3].span.len == 2,
          "'<=' spans columns 7..8");
    check(tokens[5].type == TokenType::EQUAL_EQUAL, "'==' is one token");
    check(tokens[5].span.col == 12 && tokens[5].span.len == 2,
          "'==' spans columns 12..13");
    check(tokens[6].type == TokenType::BOOLEAN && tokens[6].span.len == 4,
          "'true' is one BOOLEAN token four columns wide");

    for (const Token &token : tokens)
        check(token.span.len == static_cast<int>(token.value.size()),
              "token '" + token.value + "' is as long as its text");

    // A unary node covers its operator and its operand together.
    Node negation = parseOneExpression("-42");
    if (parsed(negation, "-42 parses"))
        check(negation->span.col == 5 && negation->span.len == 3,
              "the negation in `x = -42` spans the operator and the literal");
}

// A word that merely begins with a keyword is one identifier. The lexer
// consumes the whole alphanumeric run before comparing, which is what makes
// this true — adding `true` and `false` to that path cannot break it, and this
// check is what says so.
void aNameThatStartsWithAKeywordIsAName()
{
    std::vector<Token> tokens = lex("trueValue = 1\nfalsehood = 2\nprinter = 3\n");
    check(tokens[0].type == TokenType::IDENTIFIER && tokens[0].value == "trueValue",
          "'trueValue' is one identifier, not `true` followed by `Value`");
    check(tokens[3].type == TokenType::IDENTIFIER && tokens[3].value == "falsehood",
          "'falsehood' is one identifier");
    check(tokens[6].type == TokenType::IDENTIFIER && tokens[6].value == "printer",
          "'printer' is one identifier");

    check(lex("true")[0].type == TokenType::BOOLEAN,
          "'true' on its own is still the literal");
    check(lex("false")[0].type == TokenType::BOOLEAN,
          "'false' on its own is still the literal");
}

// ============================================================
// The type rules
// ============================================================

// There is no implicit conversion in either direction: an integer is not
// truthy, and a boolean is not 0 or 1. The full table is in value.h.
void theTwoArmsDoNotMix()
{
    checkTypeFault("true + 1");
    checkTypeFault("1 - false");
    checkTypeFault("true * true");
    checkTypeFault("false / 2");

    checkTypeFault("true < false");
    checkTypeFault("1 <= true");
    checkTypeFault("false > 0");
    checkTypeFault("true >= true");

    // Equality accepts booleans, but only against booleans. `1 == true` is a
    // fault rather than `false`, because reporting `false` would be asserting
    // that the two are comparable and merely unequal.
    checkTypeFault("1 == true");
    checkTypeFault("false != 0");

    checkTypeFault("-true");
    checkTypeFault("!1");
    checkTypeFault("!0");

    // The same operators are fine on operands that do agree.
    checkInt("1 + 1", 2);
    checkBool("true == true", true);
    checkBool("!true", false);
    checkInt("-1", -1);
}

} // namespace

int main()
{
    theValueTypeIsATaggedUnion();
    loosestLevelsBindLast();
    unaryOperatorsStack();
    unaryBindsTighterThanMultiplication();
    aPrefixOperatorRecursesRatherThanLoops();
    theCascadeNestsInTheStatedOrder();
    theNewTokensAndNodesCarryRealSpans();
    aNameThatStartsWithAKeywordIsAName();
    theTwoArmsDoNotMix();

    if (failures != 0)
    {
        std::fprintf(stderr, "expression_test: %d of %d checks failed\n",
                     failures, checks);
        return 1;
    }

    std::printf("expression_test: %d checks passed\n", checks);
    return 0;
}
