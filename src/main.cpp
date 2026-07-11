#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "token.h"

// ============================================================
// MAIN
// ============================================================

int main(int argc, char *argv[])
{
    if (argc > 2)
    {
        std::cout << "Error: Too many arguments. Max 1 input file." << std::endl;
        return 1;
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
        return 1;
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
        Interpreter interpreter;
        interpreter.execute(ast);
    }
    catch (const std::exception &e)
    {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
