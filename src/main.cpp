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
#include "semantic.h"
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

    if (argc > 2)
    {
        std::cerr << renderToolError(program,
                                     "too many arguments: at most one input file");
        return ExitCode::Usage;
    }
    if (argc < 2)
    {
        std::cerr << renderToolError(program, "no input file")
                  << "usage: " << program << " <input_file>" << std::endl;
        return ExitCode::Usage;
    }

    // The path and the source text are read here, before the stages run,
    // because the renderer needs both and no stage has them: an error is
    // thrown from the lexer, parser, resolver or interpreter carrying only a
    // span, and this is the one place that knows which file that span is in.
    const std::string path = argv[1];

    std::ifstream inputFile(path);
    if (!inputFile.is_open())
    {
        std::cerr << renderToolError(program, "could not open file: " + path);
        return ExitCode::NoInput;
    }

    std::stringstream ss;
    ss << inputFile.rdbuf();
    const std::string source = ss.str();

    try
    {
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
