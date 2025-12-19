//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "usage.hpp"
#include "viper/version.hpp"
#include <iostream>

namespace viperlang
{

void printVersion()
{
    std::cout << "viperlang v" << VIPER_VERSION_STR << "\n";
    std::cout << "ViperLang Compiler\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

void printUsage()
{
    std::cerr << "viperlang v" << VIPER_VERSION_STR << " - ViperLang Compiler\n"
              << "\n"
              << "Usage: viperlang [options] <file.viper>\n"
              << "\n"
              << "Usage Modes:\n"
              << "  viperlang script.viper              Run program (default)\n"
              << "  viperlang script.viper --emit-il    Emit IL to stdout\n"
              << "  viperlang script.viper -o file.il   Emit IL to file\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output FILE              Output file for IL\n"
              << "  --emit-il                      Emit IL instead of running\n"
              << "  --trace[=il|src]               Enable execution tracing\n"
              << "  -h, --help                     Show this help message\n"
              << "  --version                      Show version information\n"
              << "\n"
              << "Examples:\n"
              << "  viperlang hello.viper              Run program\n"
              << "  viperlang hello.viper --emit-il    Show generated IL\n"
              << "\n";
}

} // namespace viperlang
