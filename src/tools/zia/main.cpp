//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the zia command-line tool (Zia compiler).
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the `zia` CLI tool.
/// @details Wires the Zia frontend into the shared frontend runner so
///          the tool supports common flags, usage text, and version reporting.

#include "common/Utf8CommandLine.hpp"
#include "tools/common/frontend_tool.hpp"
#include "tools/zanna/cli.hpp"
#include "usage.hpp"

/// @brief Main entry point for the Zia compiler CLI.
/// @details Configures frontend callbacks (file extension, language name,
///          usage/version printers, and compile command) and delegates to the
///          shared frontend tool runner for argument parsing and execution.
/// @param argc Number of command-line arguments in @p argv.
/// @param argv Argument vector passed to the process.
/// @return Exit status propagated from the frontend tool runner.
int main(int argc, char **argv) {
    zanna::tools::Utf8CommandLine commandLine(argc, argv);
    if (!commandLine.applyOrReport(argc, argv))
        return 1;
    zanna::tools::FrontendToolCallbacks callbacks{
        .fileExtension = ".zia",
        .languageName = "Zia",
        .printUsage = zia::printUsage,
        .printVersion = zia::printVersion,
        .frontendCommand = cmdFrontZia,
    };

    return zanna::tools::runFrontendTool(argc, argv, callbacks);
}
