#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "diagnostic.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "token.h"

// ============================================================
// MAIN
// ============================================================

namespace
{

// argv[0] with any directory stripped, for the leading name on a diagnostic
// that has no source position to print instead.
std::string programName(int argc, char *argv[])
{
    if (argc < 1 || argv[0] == nullptr)
        return "algo";
    std::string name = argv[0];
    std::size_t lastSlash = name.find_last_of("/\\");
    if (lastSlash != std::string::npos)
        name = name.substr(lastSlash + 1);
    return name;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string program = programName(argc, argv);

    // Phase narration is off unless asked for. `--trace` may appear before or
    // after the file; exactly one file operand is required.
    bool trace = false;
    std::string path;

    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--trace")
        {
            trace = true;
        }
        else if (arg.size() > 1 && arg[0] == '-')
        {
            std::cerr << renderToolError(program, "unknown option: " + arg);
            return ExitCode::Usage;
        }
        else if (path.empty())
        {
            path = arg;
        }
        else
        {
            std::cerr << renderToolError(program,
                                         "too many arguments: at most one input file");
            return ExitCode::Usage;
        }
    }

    if (path.empty())
    {
        std::cerr << renderToolError(program, "no input file")
                  << "usage: " << program << " [--trace] <input_file>" << std::endl;
        return ExitCode::Usage;
    }

    // The path and the source text are read here, before the stages run,
    // because the renderer needs both and no stage has them: an error is
    // thrown from the lexer, parser, resolver or interpreter carrying only a
    // span, and this is the one place that knows which file that span is in.
    std::ifstream inputFile(path);
    if (!inputFile.is_open())
    {
        std::cerr << renderToolError(program, "could not open file: " + path);
        return ExitCode::NoInput;
    }

    std::stringstream ss;
    ss << inputFile.rdbuf();
    const std::string source = ss.str();

    // Narration goes to stderr, not stdout. It is the compiler talking about
    // itself, so it belongs on the same stream as a diagnostic; putting it on
    // stdout would put it back in the middle of the program's own output,
    // which is the defect this item exists to remove. `algo prog.algo > out`
    // must yield only what the program printed, with or without --trace.
    std::ostream &narrate = std::cerr;

    try
    {
        if (trace)
        {
            narrate << "=== Source Code ===\n" << source << std::endl;
            narrate << "=== Stage 1: Lexing ===" << std::endl;
        }

        // Stage 1: Lex
        std::vector<Token> tokens = lex(source);
        if (trace)
        {
            for (auto &t : tokens)
            {
                if (t.type != TokenType::END_OF_FILE)
                    narrate << "  Token: [" << t.value << "]" << std::endl;
            }
            narrate << "=== Stage 2: Parsing ===" << std::endl;
        }

        // Stage 2: Parse
        Parser parser(tokens);
        std::vector<Node> ast = parser.parse();
        if (trace)
        {
            narrate << "  Parsed " << ast.size() << " statement(s) successfully." << std::endl;
            narrate << "=== Stage 3: Resolution ===" << std::endl;
        }

        // Stage 3: Resolution — scopes checked, calls checked against the
        // functions that exist, and a frame slot written onto every variable
        // reference. The slot count is narration only until item 3.4, which is
        // what sizes an environment with it.
        const int slots = resolve(ast);
        if (trace)
        {
            // The program's own frame, named as such: since item 1.4 every
            // function has a frame of its own, and `resolve` reports the
            // width of this one. A function's is on its node.
            narrate << "  Resolved " << slots
                    << " variable(s) into the program's frame." << std::endl;

            std::size_t functions = 0;
            for (const Node &statement : ast)
                if (tryAs<FunctionNode>(statement))
                    functions++;
            narrate << "  Resolved " << functions
                    << " function(s), each into a frame of its own." << std::endl;

            narrate << "=== Stage 4: Output ===" << std::endl;
        }

        // Stage 4: Execute — the only stage that writes to stdout
        Interpreter interpreter;
        interpreter.execute(ast);
    }
    catch (const CompileError &e)
    {
        std::cerr << renderDiagnostic(e.diagnostic(), path, source);
        return ExitCode::CompileTime;
    }
    catch (const RuntimeFault &e)
    {
        std::cerr << renderDiagnostic(e.diagnostic(), path, source);
        return ExitCode::Runtime;
    }
    catch (const std::exception &e)
    {
        // Nothing throws a positionless exception today. If something starts
        // to, it is a fault in the engine, so it reads as one.
        std::cerr << renderToolError(program, e.what());
        return ExitCode::Runtime;
    }

    return ExitCode::Ok;
}
