//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the vbasic command-line tool.
// Provides a user-friendly interface to run and compile BASIC programs.
// When invoked with no arguments, launches the BASIC REPL.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the vbasic tool - a simplified interface to Viper BASIC.
/// @details Uses shared frontend tool infrastructure and delegates to ilc front basic.
///          When invoked with no arguments, launches the interactive BASIC REPL.

#include "repl/BasicReplAdapter.hpp"
#include "repl/ReplSession.hpp"
#include "tools/common/frontend_tool.hpp"
#include "tools/viper/cli.hpp"
#include "usage.hpp"

#include <memory>

/// @brief Main entry point for vbasic command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status: 0 on success, non-zero on error.
///
/// @details Provides a simplified, user-friendly interface to Viper BASIC:
///          - vbasic                      -> launches the BASIC REPL
///          - vbasic script.bas           -> runs the program
///          - vbasic script.bas --emit-il -> shows generated IL
///          - vbasic script.bas -o file   -> saves IL to file
int main(int argc, char **argv)
{
    // Zero-arg: launch the interactive BASIC REPL
    if (argc < 2)
    {
        auto adapter = std::make_unique<viper::repl::BasicReplAdapter>();
        viper::repl::ReplSession session(std::move(adapter));
        return session.run();
    }

    viper::tools::FrontendToolCallbacks callbacks{
        .fileExtension = ".bas",
        .languageName = "BASIC",
        .printUsage = vbasic::printUsage,
        .printVersion = vbasic::printVersion,
        .frontendCommand = cmdFrontBasic,
    };

    return viper::tools::runFrontendTool(argc, argv, callbacks);
}
