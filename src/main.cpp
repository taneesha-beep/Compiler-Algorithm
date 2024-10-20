#include<iostream>
#include<sstream>
#include<fstream>
using namespace std;

enum class TokenType {

};

int main(int argc, char* argv[]) {

    string programName = argv[0];
    size_t lastSlash = programName.find_last_of("\\");
    if (lastSlash != string::npos) programName = programName.substr(lastSlash + 1);
    try {
        if (argc == 2) {
            cout << "Here's the compiler!" << endl;
            cout << programName << endl;
            cout << argv[1] << endl;
            stringstream contents;
            {
                fstream inputFile(argv[1],ios::in);
                contents<< inputFile.rdbuf();
            }
            cout << contents.str() << endl;
        }
        else if (argc > 2) {
            throw argc; // Throwing the number of arguments if too many
        }
        else {
            throw runtime_error("Insufficient arguments");
        }
    }
    catch (int argc) {
        cout << "Too many arguments\nCan accept max 2 arguments!" << endl;
    }
    catch (runtime_error& e) {
        cout << e.what() << endl;
        cout << "Correct syntax: " << programName << " <input file>" << endl;
    }

    return 0;
}
