#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "token.h" // Span

// ============================================================
// DIAGNOSTICS — the error type, its rendered form, and the
// classification that decides the process exit code
// ============================================================

// Only Error exists. Nothing in the language produces a warning or a note yet,
// and an enumerator with no producer is dead weight. The field is here because
// severity is part of a diagnostic's rendered form, so the renderer must read
// it rather than hard-coding the word.
enum class Severity
{
    Error
};

// One thing that went wrong, and where. The message is the sentence after
// `error:` — lower case, no trailing period, and no "Runtime Error:" style
// prefix, because the renderer supplies the severity itself.
struct Diagnostic
{
    Severity severity;
    Span span;
    std::string message;
};

// Errors are thrown as one of the two subclasses below rather than as a bare
// Diagnostic, because the *class* of error decides the exit code, and only the
// site that raises it knows which class it is. Deriving from std::runtime_error
// keeps `what()` working for any caller that only catches std::exception.
class DiagnosticError : public std::runtime_error
{
    Diagnostic diag;

public:
    // The base is initialised before the member, so reading `d.message` here
    // happens before `d` is moved from.
    explicit DiagnosticError(Diagnostic d)
        : std::runtime_error(d.message), diag(std::move(d)) {}

    const Diagnostic &diagnostic() const noexcept { return diag; }
};

// A lexical, syntactic, resolution or type error: the program was rejected
// before it ran. Exit code 65.
class CompileError : public DiagnosticError
{
public:
    using DiagnosticError::DiagnosticError;
};

// A fault raised while the program was running. Exit code 70.
class RuntimeFault : public DiagnosticError
{
public:
    using DiagnosticError::DiagnosticError;
};

// Exit codes, sysexits-style. 65 and 70 are the roadmap's; 64 and 66 cover the
// driver's own failures, which are neither compile-time nor runtime faults of
// the program under test — see `renderToolError`.
namespace ExitCode
{
constexpr int Ok = 0;
constexpr int Usage = 64;       // EX_USAGE — bad command line
constexpr int CompileTime = 65; // EX_DATAERR — the source was rejected
constexpr int NoInput = 66;     // EX_NOINPUT — the input file could not be read
constexpr int Runtime = 70;     // EX_SOFTWARE — the program faulted while running
} // namespace ExitCode

// Renders `diag` against the source it came from, in the conventional form:
//
//     fib.algo:4:12: error: undefined variable 'couter'
//         return couter + 1
//                ^~~~~~
//
// `path` is printed verbatim, so the caller decides whether it is absolute or
// relative. `source` is the whole file: the renderer picks the line out of it.
// The returned string ends in a newline and is written to stderr by the caller.
//
// If the span carries no line, or names a line the source does not have, the
// echo and caret are omitted rather than invented.
std::string renderDiagnostic(const Diagnostic &diag, const std::string &path,
                             const std::string &source);

// A diagnostic with no source position — a usage error, or a file that could
// not be opened. There is no source to point into, so the program's own name
// stands where the source location would be:
//
//     algo: error: could not open file: missing.algo
std::string renderToolError(const std::string &program, const std::string &message);
