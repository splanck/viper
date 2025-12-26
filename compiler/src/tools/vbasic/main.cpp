//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the vbasic command-line tool.
// Provides a user-friendly interface to run and compile BASIC programs.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the vbasic tool - a simplified interface to Viper BASIC.
/// @details Uses shared frontend tool infrastructure and delegates to ilc front basic.

#include "tools/common/frontend_tool.hpp"
#include "tools/ilc/cli.hpp"
#include "usage.hpp"

/// @brief Main entry point for vbasic command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status: 0 on success, non-zero on error.
///
/// @details Provides a simplified, user-friendly interface to Viper BASIC:
///          - vbasic script.bas           -> runs the program
///          - vbasic script.bas --emit-il -> shows generated IL
///          - vbasic script.bas -o file   -> saves IL to file
int main(int argc, char **argv)
{
    viper::tools::FrontendToolCallbacks callbacks{
        .fileExtension = ".bas",
        .languageName = "BASIC",
        .printUsage = vbasic::printUsage,
        .printVersion = vbasic::printVersion,
        .frontendCommand = cmdFrontBasic,
    };

    return viper::tools::runFrontendTool(argc, argv, callbacks);
}
