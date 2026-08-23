// Unit checks for the diagnostic type and its renderer — roadmap item 0.3.
//
// The golden-file cases next to this file compare the interpreter's stdout, and
// a diagnostic no longer goes there: it goes to stderr, which nothing compares
// until item 0.4 adds `.expected_err`. Until then this binary is the only thing
// that reads the rendered text. It links algo_core, builds diagnostics both by
// hand and by running real source through the front end, and compares the
// rendered string exactly. A failed check prints to stderr and the process
// exits non-zero, which is all CTest reads — no third-party test framework.

#include <cstdio>
#include <string>
#include <vector>

#include "ast.h"
#include "diagnostic.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "token.h"

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

// Compares rendered text exactly. Both sides are printed between markers on
// failure, because the interesting differences here are trailing spaces and
// where a newline falls.
void checkText(const std::string &what, const std::string &actual,
               const std::string &expected)
{
    checks++;
    if (actual == expected)
        return;
    failures++;
    std::fprintf(stderr,
                 "FAIL %s\n--- expected ---\n%s--- actual ---\n%s--- end ---\n",
                 what.c_str(), expected.c_str(), actual.c_str());
}

Diagnostic errorAt(int line, int col, int len, const std::string &message)
{
    return Diagnostic{Severity::Error, Span{line, col, len}, message};
}

// The exact shape the roadmap specifies: `path:line:col: severity: message`,
// the source line echoed verbatim, then a caret under the span's first
// character and a tilde under each remaining one.
void rendersTheConventionalForm()
{
    const std::string source =
        "def next(counter)\n"
        "    counter = counter + 1\n"
        "\n"
        "    return couter + 1\n";

    // `couter` on line 4 starts at column 12 and is six characters long.
    const std::string rendered = renderDiagnostic(
        errorAt(4, 12, 6, "undefined variable 'couter'"), "fib.algo", source);

    checkText("the roadmap's rendered form", rendered,
              "fib.algo:4:12: error: undefined variable 'couter'\n"
              "    return couter + 1\n"
              "           ^~~~~~\n");
}

void caretGeometry()
{
    //                          1234567890123
    const std::string source = "x = 1 + 2 * 3\n";

    checkText("a one-character span is a bare caret",
              renderDiagnostic(errorAt(1, 11, 1, "here"), "a.algo", source),
              "a.algo:1:11: error: here\n"
              "x = 1 + 2 * 3\n"
              "          ^\n");

    checkText("a wide span carries tildes for its remaining characters",
              renderDiagnostic(errorAt(1, 5, 9, "here"), "a.algo", source),
              "a.algo:1:5: error: here\n"
              "x = 1 + 2 * 3\n"
              "    ^~~~~~~~~\n");

    // The end-of-file token has length 0 and still has to point somewhere.
    checkText("a zero-length span still gets one caret",
              renderDiagnostic(errorAt(1, 14, 0, "here"), "a.algo", source),
              "a.algo:1:14: error: here\n"
              "x = 1 + 2 * 3\n"
              "             ^\n");

    // A run wider than the text left on the line stops at the end of it
    // rather than trailing past the characters it points at.
    checkText("the caret run is clamped to the end of the line",
              renderDiagnostic(errorAt(1, 11, 99, "here"), "a.algo", source),
              "a.algo:1:11: error: here\n"
              "x = 1 + 2 * 3\n"
              "          ^~~\n");
}

// Item 0.2 defined a column as a character offset, so a tab is one column. The
// caret only stays under its character at every tab width if the padding is
// built from the same characters as the line.
void tabsAreCopiedIntoThePadding()
{
    const std::string source = "print\tzz\n";

    checkText("a tab in the line becomes a tab in the padding",
              renderDiagnostic(errorAt(1, 7, 2, "no"), "t.algo", source),
              "t.algo:1:7: error: no\n"
              "print\tzz\n"
              "     \t^~\n");
}

// The renderer is given a span and a source, and cannot assume they agree.
// Where it cannot find the line, it says what it knows and stops.
void anUnfindableLineIsOmittedRatherThanInvented()
{
    const std::string source = "x = 1\n";

    checkText("a line past the end of the source is not echoed",
              renderDiagnostic(errorAt(9, 3, 1, "nowhere"), "a.algo", source),
              "a.algo:9:3: error: nowhere\n");

    // Line 0 is the default-constructed Span: "no position".
    checkText("a positionless span falls back to the tool form",
              renderDiagnostic(errorAt(0, 0, 0, "no position"), "a.algo", source),
              "a.algo: error: no position\n");
}

void toolErrorsCarryTheProgramName()
{
    checkText("a tool error names the program where a location would go",
              renderToolError("algo", "could not open file: missing.algo"),
              "algo: error: could not open file: missing.algo\n");
}

// Everything above builds a Diagnostic by hand. This runs real source through
// the front end so that the spans under test are the ones item 0.2 attaches.
enum class Thrown
{
    Nothing,
    Compile,
    Runtime
};

Thrown runAndCatch(const std::string &source, Diagnostic &out)
{
    try
    {
        Parser parser(lex(source));
        std::vector<Node> ast = parser.parse();
        resolve(ast);
        Interpreter interpreter;
        interpreter.execute(ast);
    }
    catch (const CompileError &e)
    {
        out = e.diagnostic();
        return Thrown::Compile;
    }
    catch (const RuntimeFault &e)
    {
        out = e.diagnostic();
        return Thrown::Runtime;
    }
    return Thrown::Nothing;
}

void everyErrorSiteCarriesARealSpan()
{
    struct Case
    {
        const char *what;
        const char *source;
        Thrown thrown;
        const char *rendered;
    };

    // Assignments rather than prints, so that a case that wrongly succeeds
    // does not scribble on this binary's own stdout.
    const std::vector<Case> cases = {
        {"lexer: unknown character",
         "x = 5 @ 3\n",
         Thrown::Compile,
         "c.algo:1:7: error: unknown character '@'\n"
         "x = 5 @ 3\n"
         "      ^\n"},
        {"parser: missing '='",
         "x 5\n",
         Thrown::Compile,
         "c.algo:1:3: error: expected '=' after variable name\n"
         "x 5\n"
         "  ^\n"},
        {"parser: statement cannot start here",
         "= 5\n",
         Thrown::Compile,
         "c.algo:1:1: error: expected a statement, found '='\n"
         "= 5\n"
         "^\n"},
        // ON THE WORDING. Before item 1.1 this message named what could start
        // an operand: "expected a number or a variable name". An expression can
        // now also start with `true`, `false`, `-` or `!`, so that enumeration
        // stopped being true and was replaced rather than extended — item 1.4
        // adds a call, and a message listing every primary form ages badly.
        {"parser: operand missing at end of file",
         "x = 1 +",
         Thrown::Compile,
         "c.algo:1:8: error: expected an expression, found end of file\n"
         "x = 1 +\n"
         "       ^\n"},
        {"resolver: use before assignment points at the use",
         "y = zz + 1\n",
         Thrown::Compile,
         "c.algo:1:5: error: variable 'zz' used before assignment\n"
         "y = zz + 1\n"
         "    ^~\n"},
        {"interpreter: division by zero covers the whole division",
         "x = 5 / 0\n",
         Thrown::Runtime,
         "c.algo:1:5: error: division by zero\n"
         "x = 5 / 0\n"
         "    ^~~~~\n"},
        // ON THIS LITERAL. It was `9999999999` until item 1.5, chosen because
        // it did not fit in the `int` the value arm used to be. It fits in an
        // int64 comfortably, so on the day the arm widened this case stopped
        // testing anything and would have gone on passing as a plain number —
        // the failure mode a golden file cannot warn about. The literal is now
        // one past the maximum exactly, which pins the boundary rather than
        // merely clearing it, and the caret run grew with the token: it is as
        // long as the literal, so a nineteen-digit number needs nineteen
        // characters under it.
        {"literal out of range covers the literal",
         "x = 9223372036854775808\n",
         Thrown::Compile,
         "c.algo:1:5: error: integer literal out of range: 9223372036854775808\n"
         "x = 9223372036854775808\n"
         "    ^~~~~~~~~~~~~~~~~~~\n"},
        // Item 1.5's runtime fault, at all three of the operators whose caret
        // geometry differs. It goes on the whole operation, which is the same
        // convention division by zero and a type fault follow: neither operand
        // is individually wrong, combining them is.
        {"an overflowing addition covers the whole operation",
         "x = 9223372036854775807\nx = x + 1\n",
         Thrown::Runtime,
         "c.algo:2:5: error: integer overflow in '+'\n"
         "x = x + 1\n"
         "    ^~~~~\n"},
        // The most negative integer is not writable as a literal — the lexer
        // makes a number out of digits alone, so `-9223372036854775808` is
        // unary minus applied to an out-of-range literal — so both of the
        // asymmetry cases below have to compute it into a name first.
        {"the one division that overflows is not division by zero",
         "least = 0 - 9223372036854775807\nleast = least - 1\nx = least / -1\n",
         Thrown::Runtime,
         "c.algo:3:5: error: integer overflow in '/'\n"
         "x = least / -1\n"
         "    ^~~~~~~~~~\n"},
        // Worded apart from the binary form: `-` names two operators, and a
        // reader needs to know which one trapped.
        {"negating the most negative integer names the unary operator",
         "least = 0 - 9223372036854775807\nleast = least - 1\nx = -least\n",
         Thrown::Runtime,
         "c.algo:3:5: error: integer overflow in unary '-'\n"
         "x = -least\n"
         "    ^~~~~~\n"},
        // The error sites item 1.1 adds. A type fault points at the whole
        // operation, not at one operand: an operand only has the wrong type
        // relative to what is being done with it.
        // A two-character operator must span two characters. If `==` were
        // lexed as one token of length 1, or as two `=` tokens, the caret under
        // it here would be one character short and this text would not match.
        {"a caret under '==' covers both characters",
         "x == 5\n",
         Thrown::Compile,
         "c.algo:1:3: error: expected '=' after variable name\n"
         "x == 5\n"
         "  ^~\n"},
        {"a caret under '<=' covers both characters",
         "<= 5\n",
         Thrown::Compile,
         "c.algo:1:1: error: expected a statement, found '<='\n"
         "<= 5\n"
         "^~\n"},
        {"arithmetic on a boolean covers the whole operation",
         "x = true + 1\n",
         Thrown::Runtime,
         "c.algo:1:5: error: operator '+' cannot be applied to boolean and integer\n"
         "x = true + 1\n"
         "    ^~~~~~~~\n"},
        {"ordering two booleans covers the whole comparison",
         "x = true < false\n",
         Thrown::Runtime,
         "c.algo:1:5: error: operator '<' cannot be applied to boolean and boolean\n"
         "x = true < false\n"
         "    ^~~~~~~~~~~~\n"},
        {"equality across the two types is a fault, not false",
         "x = 1 == true\n",
         Thrown::Runtime,
         "c.algo:1:5: error: operator '==' cannot be applied to integer and boolean\n"
         "x = 1 == true\n"
         "    ^~~~~~~~~\n"},
        {"negating a boolean covers the operator and its operand",
         "x = -true\n",
         Thrown::Runtime,
         "c.algo:1:5: error: operator '-' cannot be applied to boolean\n"
         "x = -true\n"
         "    ^~~~~\n"},
        {"negating an integer logically covers the operator and its operand",
         "x = !1\n",
         Thrown::Runtime,
         "c.algo:1:5: error: operator '!' cannot be applied to integer\n"
         "x = !1\n"
         "    ^~\n"},
        // Item 1.2 gave the pass a recursive shape and item 1.3's resolver
        // kept it: statements nest inside blocks, and an `if` and a `while`
        // each hold a condition beside their body. Each of those is a place the
        // walk can fail to reach, and a name that is never checked is a
        // diagnostic that never fires. Every name below is undeclared in any
        // scope, so each case reaches the resolver's walk rather than its
        // scoping rule — `tests/resolver_test.cpp` covers the scoping.
        {"a name used inside a block is still checked",
         "if true { print zz }\n",
         Thrown::Compile,
         "c.algo:1:17: error: variable 'zz' used before assignment\n"
         "if true { print zz }\n"
         "                ^~\n"},
        {"a name used in a while condition is still checked",
         "while zz < 1 { print 1 }\n",
         Thrown::Compile,
         "c.algo:1:7: error: variable 'zz' used before assignment\n"
         "while zz < 1 { print 1 }\n"
         "      ^~\n"},
        {"a name used in an if condition is still checked",
         "if zz { print 1 }\n",
         Thrown::Compile,
         "c.algo:1:4: error: variable 'zz' used before assignment\n"
         "if zz { print 1 }\n"
         "   ^~\n"},
        {"a name used inside an else branch is still checked",
         "if false { print 1 } else { print zz }\n",
         Thrown::Compile,
         "c.algo:1:35: error: variable 'zz' used before assignment\n"
         "if false { print 1 } else { print zz }\n"
         "                                  ^~\n"},
        {"a name used inside a nested block is still checked",
         "{ { print zz } }\n",
         Thrown::Compile,
         "c.algo:1:11: error: variable 'zz' used before assignment\n"
         "{ { print zz } }\n"
         "          ^~\n"},
        {"a condition that is not a boolean names the type it got",
         "while 1 { print 2 }\n",
         Thrown::Runtime,
         "c.algo:1:7: error: a condition must be a boolean, not integer\n"
         "while 1 { print 2 }\n"
         "      ^\n"},
        // The source ends in a newline, so the end-of-file token sits at the
        // start of the empty line 2 — which the renderer echoes as the empty
        // line it is, rather than reaching back to the line that had text on
        // it. The caret has nothing to underline and still points somewhere.
        {"an unclosed block reports the brace that is missing",
         "if true { print 1\n",
         Thrown::Compile,
         "c.algo:2:1: error: expected '}' to close this block\n"
         "\n"
         "^\n"},
        // The error sites item 1.4 adds. The resolver's own — arity, an
        // unknown function, the frame boundary — are in
        // `tests/resolver_test.cpp` beside the rest of that pass; what is here
        // is the parser's, and the one new runtime fault.
        {"a function needs a name",
         "fn 5() {\n}\n",
         Thrown::Compile,
         "c.algo:1:4: error: expected a function name after 'fn'\n"
         "fn 5() {\n"
         "   ^\n"},
        {"a function needs a parameter list, even an empty one",
         "fn f {\n}\n",
         Thrown::Compile,
         "c.algo:1:6: error: expected '(' after the function name\n"
         "fn f {\n"
         "     ^\n"},
        {"an unclosed parameter list reports the paren that is missing",
         "fn f(a {\n}\n",
         Thrown::Compile,
         "c.algo:1:8: error: expected ')' to close the parameter list\n"
         "fn f(a {\n"
         "       ^\n"},
        {"a parameter list holds names, not punctuation",
         "fn f(,) {\n}\n",
         Thrown::Compile,
         "c.algo:1:6: error: expected a parameter name\n"
         "fn f(,) {\n"
         "     ^\n"},
        // Functions are top-level only, so this is reported where the `fn`
        // stands rather than as a statement that cannot begin with one.
        {"a function inside a block says where a function may go",
         "if true {\n    fn f() {\n    }\n}\n",
         Thrown::Compile,
         "c.algo:2:5: error: a function may only be declared at the top level\n"
         "    fn f() {\n"
         "    ^~\n"},
        {"an unclosed argument list reports the paren that is missing",
         "fn f(a) {\n}\nx = f(1\nprint 2\n",
         Thrown::Compile,
         "c.algo:4:1: error: expected ')' to close the argument list\n"
         "print 2\n"
         "^~~~~\n"},
        // The `(` of a call and of a parameter list is the only `(` this
        // language has. It never groups, so one standing where an expression
        // should start is not a grouped sub-expression, and the message that
        // was already there says so without having to know about calls.
        {"a parenthesis cannot group an expression",
         "x = (1 + 2) * 3\n",
         Thrown::Compile,
         "c.algo:1:5: error: expected an expression, found '('\n"
         "x = (1 + 2) * 3\n"
         "    ^\n"},
        // The one runtime fault item 1.4 adds. The caret goes on the call that
        // could not be entered, which is the innermost one.
        {"an exhausted call depth points at the call that overran it",
         "fn down(n) {\n    return down(n - 1)\n}\nx = down(1)\n",
         Thrown::Runtime,
         "c.algo:2:12: error: call depth exceeded\n"
         "    return down(n - 1)\n"
         "           ^~~~~~~~~~~\n"},
    };

    for (const Case &c : cases)
    {
        Diagnostic diag;
        Thrown thrown = runAndCatch(c.source, diag);
        check(thrown != Thrown::Nothing,
              std::string("case raises a diagnostic: ") + c.what);
        if (thrown == Thrown::Nothing)
            continue;
        checkText(c.what, renderDiagnostic(diag, "c.algo", c.source), c.rendered);
    }
}

// ON WHY THIS IS A TEST. The class an error is thrown as decides the process
// exit code, and item 3.2 moves the out-of-range check from evaluation time to
// parse time. If that move also changed the class, the observable exit code
// would shift 65 -> 70 or back in the middle of the ablation series and the
// measurement would be comparing two different programs. These checks pin the
// classification so that 3.2 changes only when the check runs, not what the
// caller sees.
void theExitCodeClassificationIsPinned()
{
    struct Case
    {
        const char *what;
        const char *source;
        Thrown thrown;
        int exitCode;
    };

    const std::vector<Case> cases = {
        {"a literal too wide for the value type is a compile-time error",
         "x = 9223372036854775808\n", Thrown::Compile, ExitCode::CompileTime},
        {"division by zero is a runtime fault",
         "x = 5 / 0\n", Thrown::Runtime, ExitCode::Runtime},
        {"an unknown character is a compile-time error",
         "x = 5 @ 3\n", Thrown::Compile, ExitCode::CompileTime},
        {"a syntax error is a compile-time error",
         "x 5\n", Thrown::Compile, ExitCode::CompileTime},
        {"use before assignment is a compile-time error",
         "y = zz\n", Thrown::Compile, ExitCode::CompileTime},
        // A type fault is a *runtime* fault. The language has no type checker
        // and the roadmap does not add one — item 1.3 is a resolver, which
        // assigns frame slots and reports use-before-declaration, not types —
        // so whether an operand has the right type depends on what the program
        // computed. That is the definition of a runtime fault here, and it is
        // the opposite classification from the out-of-range literal above,
        // which is a property of the token's text alone.
        {"a type mismatch is a runtime fault",
         "x = true + 1\n", Thrown::Runtime, ExitCode::Runtime},
        {"a mixed-type equality is a runtime fault",
         "x = 1 != false\n", Thrown::Runtime, ExitCode::Runtime},
        {"a unary operator on the wrong type is a runtime fault",
         "x = !5\n", Thrown::Runtime, ExitCode::Runtime},
        // Item 1.4's two, and they fall on opposite sides for the same reason
        // the literal and the type mismatch do. An argument count is a
        // property of the source text alone — the call site says how many, the
        // declaration says how many — so it is settled before the program runs
        // and is a compile-time error. How deep a chain of calls gets depends
        // on what the program computed, so it is a runtime fault.
        {"a wrong-arity call is a compile-time error",
         "fn f(a) {\n    return a\n}\nx = f(1, 2)\n",
         Thrown::Compile, ExitCode::CompileTime},
        {"an exhausted call depth is a runtime fault",
         "fn down(n) {\n    return down(n - 1)\n}\nx = down(1)\n",
         Thrown::Runtime, ExitCode::Runtime},
        {"a call of a function that does not exist is a compile-time error",
         "x = nope()\n", Thrown::Compile, ExitCode::CompileTime},
        {"'return' outside a function is a compile-time error",
         "return 1\n", Thrown::Compile, ExitCode::CompileTime},
        // Item 1.5's, and it falls on the same side as the type mismatch for
        // the same reason. Whether a sum fits in 64 bits depends on what the
        // program computed, so it cannot be settled before the program runs.
        // The literal one line above is its opposite and stays at 65: that one
        // is a property of the token's text alone. The pair is the whole point
        // — an arithmetic overflow and a literal too wide for the same range
        // are the two errors most likely to be conflated, and they exit
        // differently.
        {"an arithmetic overflow is a runtime fault",
         "x = 9223372036854775807\nx = x + 1\n",
         Thrown::Runtime, ExitCode::Runtime},
        {"an overflowing division is a runtime fault",
         "least = 0 - 9223372036854775807\nleast = least - 1\nx = least / -1\n",
         Thrown::Runtime, ExitCode::Runtime},
        {"an overflowing negation is a runtime fault",
         "least = 0 - 9223372036854775807\nleast = least - 1\nx = -least\n",
         Thrown::Runtime, ExitCode::Runtime},
    };

    for (const Case &c : cases)
    {
        Diagnostic diag;
        check(runAndCatch(c.source, diag) == c.thrown, c.what);
    }

    check(ExitCode::CompileTime == 65, "a compile-time error exits 65");
    check(ExitCode::Runtime == 70, "a runtime fault exits 70");
    check(ExitCode::Ok == 0, "success exits 0");
    check(ExitCode::Usage == 64, "a usage error exits 64 (EX_USAGE)");
    check(ExitCode::NoInput == 66, "an unreadable input exits 66 (EX_NOINPUT)");
}

} // namespace

int main()
{
    rendersTheConventionalForm();
    caretGeometry();
    tabsAreCopiedIntoThePadding();
    anUnfindableLineIsOmittedRatherThanInvented();
    toolErrorsCarryTheProgramName();
    everyErrorSiteCarriesARealSpan();
    theExitCodeClassificationIsPinned();

    if (failures != 0)
    {
        std::fprintf(stderr, "diagnostic_test: %d of %d checks failed\n",
                     failures, checks);
        return 1;
    }

    std::printf("diagnostic_test: %d checks passed\n", checks);
    return 0;
}
