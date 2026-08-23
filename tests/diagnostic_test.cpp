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
        {"literal out of range covers the literal",
         "x = 9999999999\n",
         Thrown::Compile,
         "c.algo:1:5: error: integer literal out of range: 9999999999\n"
         "x = 9999999999\n"
         "    ^~~~~~~~~~\n"},
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
         "x = 9999999999\n", Thrown::Compile, ExitCode::CompileTime},
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
