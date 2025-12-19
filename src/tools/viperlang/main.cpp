//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the viperlang command-line tool.
//
//===----------------------------------------------------------------------===//

#include "tools/common/frontend_tool.hpp"
#include "tools/ilc/cli.hpp"
#include "usage.hpp"

int main(int argc, char **argv)
{
    viper::tools::FrontendToolCallbacks callbacks{
        .fileExtension = ".viper",
        .languageName = "ViperLang",
        .printUsage = viperlang::printUsage,
        .printVersion = viperlang::printVersion,
        .frontendCommand = cmdFrontViperlang,
    };

    return viper::tools::runFrontendTool(argc, argv, callbacks);
}
