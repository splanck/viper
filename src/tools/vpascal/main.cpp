//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the vpascal command-line tool.
// Provides a user-friendly interface to run and compile Pascal programs.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the vpascal tool - a simplified interface to Viper Pascal.
/// @details Uses shared frontend tool infrastructure and delegates to ilc front pascal.

#include "tools/common/frontend_tool.hpp"
#include "tools/ilc/cli.hpp"
#include "usage.hpp"

/// @brief Main entry point for vpascal command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status: 0 on success, non-zero on error.
///
/// @details Provides a simplified, user-friendly interface to Viper Pascal:
///          - vpascal program.pas           -> runs the program
///          - vpascal program.pas --emit-il -> shows generated IL
///          - vpascal program.pas -o file   -> saves IL to file
int main(int argc, char **argv)
{
    viper::tools::FrontendToolCallbacks callbacks{
        .fileExtension = ".pas",
        .languageName = "Pascal",
        .printUsage = vpascal::printUsage,
        .printVersion = vpascal::printVersion,
        .frontendCommand = cmdFrontPascal,
    };

    return viper::tools::runFrontendTool(argc, argv, callbacks);
}
