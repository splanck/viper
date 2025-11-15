//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements help text and usage information for the vbasic command-line tool.
//
//===----------------------------------------------------------------------===//

#include "usage.hpp"
#include "frontends/basic/Intrinsics.hpp"
#include "viper/version.hpp"
#include <iostream>

namespace vbasic
{

void printVersion()
{
    std::cout << "vbasic v" << VIPER_VERSION_STR << "\n";
    std::cout << "Viper BASIC Interpreter/Compiler\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

void printUsage()
{
    std::cerr << "vbasic v" << VIPER_VERSION_STR << " - Viper BASIC Interpreter\n"
              << "\n"
              << "Usage: vbasic [options] <file.bas>\n"
              << "\n"
              << "Usage Modes:\n"
              << "  vbasic script.bas              Run program (default)\n"
              << "  vbasic script.bas --emit-il    Emit IL to stdout\n"
              << "  vbasic script.bas -o file.il   Emit IL to file\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output FILE              Output file for IL\n"
              << "  --emit-il                      Emit IL instead of running\n"
              << "  --trace[=il|src]               Enable execution tracing\n"
              << "  --bounds-checks                Enable array bounds checking\n"
              << "  --stdin-from FILE              Redirect stdin from file\n"
              << "  --max-steps N                  Limit execution steps\n"
              << "  --dump-trap                    Show detailed trap diagnostics\n"
              << "  -h, --help                     Show this help message\n"
              << "  --version                      Show version information\n"
              << "\n"
              << "Examples:\n"
              << "  vbasic game.bas                           Run program\n"
              << "  vbasic game.bas --emit-il                 Show generated IL\n"
              << "  vbasic game.bas -o game.il                Save IL to file\n"
              << "  vbasic game.bas --trace --bounds-checks   Debug mode\n"
              << "  vbasic game.bas --stdin-from input.txt    Redirect input\n"
              << "\n"
              << "BASIC Language Notes:\n"
              << "  - FUNCTION must RETURN a value on all paths\n"
              << "  - SUB cannot be used as an expression\n"
              << "  - Array parameters are ByRef\n"
              << "  - Built-in functions: ";
    il::frontends::basic::intrinsics::dumpNames(std::cerr);
    std::cerr << "\n"
              << "\n"
              << "For detailed documentation, see: docs/basic-language.md\n";
}

} // namespace vbasic
