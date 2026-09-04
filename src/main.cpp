#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "compiler.h"
#include "diagnostic.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "token.h"
#include "vm.h"

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

    // WHICH BACK END RUNS. Item 4.3 added a second one — the bytecode VM,
    // configuration V — and this flag is how it is reached.
    //
    // IT DEFAULTS TO `tree`, AND THAT IS NOT A PLACEHOLDER. Every golden case
    // invokes `algo <file>` with no flag, so the default decides what the whole
    // suite exercises; changing it would silently re-point 29 cases at an
    // engine item 4.4 has not yet finished checking. Running each case against
    // both engines is item 4.4's job, and it is the item that earns the right
    // to trust the VM.
    enum class Engine
    {
        Tree,
        Vm
    };
    Engine engine = Engine::Tree;

    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--trace")
        {
            trace = true;
        }
        else if (arg.rfind("--engine=", 0) == 0)
        {
            const std::string value = arg.substr(std::string("--engine=").size());
            if (value == "tree")
            {
                engine = Engine::Tree;
            }
            else if (value == "vm")
            {
                engine = Engine::Vm;
            }
            else
            {
                // A bad command line, like any other: the value names no engine
                // this build has, and that is settled before a byte of the
                // program is read.
                std::cerr << renderToolError(program, "unknown engine: " + value);
                return ExitCode::Usage;
            }
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
                  << "usage: " << program
                  << " [--trace] [--engine=tree|vm] <input_file>" << std::endl;
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
        // reference. Since item 3.4 the slot count is not narration: it is the
        // width of the program's own frame, and `execute` sizes that frame
        // with it. A function's own width travels on its node instead.
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

        // Stage 4: Execute — the only stage that writes to stdout.
        //
        // ON WHY BOTH ARMS LIVE IN THIS SCOPE. `SpanEntry::op` in a chunk points
        // into `BinOpNode::op` / `UnaryOpNode::op`, and the chunk owns none of
        // that text — so **the tree must outlive the chunk compiled from it**.
        // Here that is free, `ast` being a local of the same `try` block. The
        // shape that breaks it is the natural refactor: a `runFile(path)` helper
        // that compiles, lets the tree go, and then executes. It fails quietly —
        // the pointers stay non-null and read as empty strings, so a fault
        // renders `operator '' cannot be applied to ...` — which is exactly how
        // item 4.2's first acceptance driver went wrong. Do not extract it.
        if (engine == Engine::Vm)
        {
            if (trace)
                narrate << "  Engine: bytecode VM" << std::endl;
            const Chunk chunk = compile(ast, slots);
            VM vm;
            vm.run(chunk);
        }
        else
        {
            if (trace)
                narrate << "  Engine: tree-walking interpreter" << std::endl;
            Interpreter interpreter;
            interpreter.execute(ast, slots);
        }
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
