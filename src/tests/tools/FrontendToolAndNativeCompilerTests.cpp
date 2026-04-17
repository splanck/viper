//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/tools/FrontendToolAndNativeCompilerTests.cpp
// Purpose: Regress CLI/frontend parsing and native compiler temp-path behavior.
//
//===----------------------------------------------------------------------===//

#include "tools/common/frontend_tool.hpp"
#include "tools/common/native_compiler.hpp"
#include "tools/viper/cmd_codegen_x64.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void noopUsage() {}

void noopVersion() {}

int noopFrontend(int, char **) {
    return 0;
}

void testFrontendDoubleDashSeparatesProgramArgs() {
    using namespace viper::tools;

    FrontendToolCallbacks callbacks{
        ".bas",
        "BASIC",
        noopUsage,
        noopVersion,
        noopFrontend,
    };

    char exe[] = "vbasic";
    char src[] = "demo.bas";
    char sep[] = "--";
    char flag[] = "--trace";
    char value[] = "runtime-arg";
    char *argv[] = {exe, src, sep, flag, value};

    const FrontendToolConfig config = parseArgs(5, argv, callbacks);
    assert(config.sourcePath == "demo.bas");
    assert(config.run);
    assert(config.forwardedArgs.empty());
    assert(config.programArgs.size() == 2);
    assert(config.programArgs[0] == "--trace");
    assert(config.programArgs[1] == "runtime-arg");
}

void testNativeCompilerTempPathsAreUnique() {
    using namespace viper::tools;

    const std::string ilA = generateTempIlPath();
    const std::string ilB = generateTempIlPath();
    const std::string assetA = generateTempAssetPath();
    const std::string assetB = generateTempAssetPath();

    assert(ilA != ilB);
    assert(assetA != assetB);
    assert(std::filesystem::path(ilA).extension() == ".il");
    assert(std::filesystem::path(assetA).extension() == ".vpa");
}

void testX64CodegenAcceptsAssetBlobAndExtraObjectFlags() {
    char input[] = "missing.il";
    char assetFlag[] = "--asset-blob";
    char assetPath[] = "assets.vpa";
    char objFlag[] = "--extra-obj";
    char objPath[] = "assets.o";
    char *argv[] = {input, assetFlag, assetPath, objFlag, objPath};

    std::ostringstream errCapture;
    auto *oldErr = std::cerr.rdbuf(errCapture.rdbuf());
    const int rc = viper::tools::ilc::cmd_codegen_x64(5, argv);
    std::cerr.rdbuf(oldErr);

    const std::string diagnostics = errCapture.str();
    assert(rc != 0);
    assert(diagnostics.find("unknown flag") == std::string::npos);
    assert(diagnostics.find("--asset-blob requires") == std::string::npos);
    assert(diagnostics.find("--extra-obj requires") == std::string::npos);
}

} // namespace

int main() {
    testFrontendDoubleDashSeparatesProgramArgs();
    testNativeCompilerTempPathsAreUnique();
    testX64CodegenAcceptsAssetBlobAndExtraObjectFlags();
    return 0;
}
