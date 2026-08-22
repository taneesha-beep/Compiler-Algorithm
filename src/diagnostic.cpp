#include "diagnostic.h"

namespace
{

const char *severityName(Severity severity)
{
    switch (severity)
    {
    case Severity::Error:
        return "error";
    }
    return "error";
}

// Copies line number `line` (1-based) out of `source` into `out`, returning
// false if the source has no such line. A trailing '\r' is dropped so that a
// CRLF file does not push a stray carriage return into the terminal.
bool sourceLine(const std::string &source, int line, std::string &out)
{
    if (line < 1)
        return false;

    int current = 1;
    std::size_t start = 0;
    while (current < line)
    {
        std::size_t newline = source.find('\n', start);
        if (newline == std::string::npos)
            return false;
        start = newline + 1;
        current++;
    }
    if (start > source.size())
        return false;

    std::size_t end = source.find('\n', start);
    if (end == std::string::npos)
        end = source.size();

    out = source.substr(start, end - start);
    if (!out.empty() && out.back() == '\r')
        out.pop_back();
    return true;
}

// The blank run that puts the caret under column `col` of `text`. Tabs are
// copied through rather than replaced by a space: item 0.2 defined a column as
// a character offset, so the only way the caret stays under its character at
// any tab width is to indent it with the same characters.
std::string caretPadding(const std::string &text, int col)
{
    std::string padding;
    for (int i = 0; i + 1 < col; i++)
    {
        if (static_cast<std::size_t>(i) < text.size() && text[i] == '\t')
            padding += '\t';
        else
            padding += ' ';
    }
    return padding;
}

// '^' under the first character of the span, then a '~' under each remaining
// one. Clamped to the end of the line so the run cannot trail past the text it
// is pointing at, and never shorter than the single caret — a zero-length span
// (the end-of-file token) still has to point somewhere.
std::string caretRun(const std::string &text, int col, int len)
{
    int available = static_cast<int>(text.size()) - col + 1;
    if (len > available)
        len = available;
    if (len < 1)
        len = 1;

    std::string run(1, '^');
    run.append(static_cast<std::size_t>(len - 1), '~');
    return run;
}

} // namespace

std::string renderDiagnostic(const Diagnostic &diag, const std::string &path,
                             const std::string &source)
{
    // A default-constructed Span has line 0, meaning "no position". Nothing
    // should reach here with one, but inventing a location would be worse than
    // omitting it.
    if (diag.span.line < 1)
        return renderToolError(path, diag.message);

    std::string rendered = path + ":" + std::to_string(diag.span.line) + ":" +
                           std::to_string(diag.span.col) + ": " +
                           severityName(diag.severity) + ": " + diag.message + "\n";

    std::string line;
    if (!sourceLine(source, diag.span.line, line))
        return rendered;

    rendered += line + "\n";
    rendered += caretPadding(line, diag.span.col) +
                caretRun(line, diag.span.col, diag.span.len) + "\n";
    return rendered;
}

std::string renderToolError(const std::string &program, const std::string &message)
{
    return program + ": error: " + message + "\n";
}
