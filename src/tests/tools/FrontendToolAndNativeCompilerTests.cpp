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
#include "tools/common/project_loader.hpp"
#include "tools/viper/cmd_codegen_x64.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
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

void testX64CodegenAcceptsDebugLineFlags() {
    char input[] = "missing.il";
    char debugLines[] = "--debug-lines";
    char noDebugLines[] = "--no-debug-lines";
    char *argv[] = {input, debugLines, noDebugLines};

    std::ostringstream errCapture;
    auto *oldErr = std::cerr.rdbuf(errCapture.rdbuf());
    const int rc = viper::tools::ilc::cmd_codegen_x64(3, argv);
    std::cerr.rdbuf(oldErr);

    const std::string diagnostics = errCapture.str();
    assert(rc != 0);
    assert(diagnostics.find("unknown flag") == std::string::npos);
}

void writeText(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

void testProjectDefaultsUseBalancedOptimization() {
    using namespace il::tools::common;
    using namespace viper::tools;

    std::filesystem::path dir = std::filesystem::path(generateTempIlPath()).replace_extension("");
    std::filesystem::create_directories(dir);
    writeText(dir / "main.zia", "module main;\nfunc start() {}\n");

    auto resolved = resolveProject(dir.string());
    assert(resolved);
    assert(resolved.value().buildProfile == "balanced");
    assert(resolved.value().optimizeLevel == "O1");

    std::filesystem::remove_all(dir);
}

void testProjectProfilesMapToOptimizationLevels() {
    using namespace il::tools::common;
    using namespace viper::tools;

    std::filesystem::path dir = std::filesystem::path(generateTempIlPath()).replace_extension("");
    std::filesystem::create_directories(dir);
    writeText(dir / "main.zia", "module main;\nfunc start() {}\n");
    writeText(dir / "viper.project",
              "project perf\n"
              "version 0.1.0\n"
              "lang zia\n"
              "entry main.zia\n"
              "profile release\n");

    auto resolved = resolveProject(dir.string());
    assert(resolved);
    assert(resolved.value().buildProfile == "release");
    assert(resolved.value().optimizeLevel == "O2");

    writeText(dir / "viper.project",
              "project perf\n"
              "version 0.1.0\n"
              "lang zia\n"
              "entry main.zia\n"
              "profile release\n"
              "optimize O1\n");
    auto overridden = resolveProject(dir.string());
    assert(overridden);
    assert(overridden.value().buildProfile == "release");
    assert(overridden.value().optimizeLevel == "O1");

    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    testFrontendDoubleDashSeparatesProgramArgs();
    testNativeCompilerTempPathsAreUnique();
    testX64CodegenAcceptsAssetBlobAndExtraObjectFlags();
    testX64CodegenAcceptsDebugLineFlags();
    testProjectDefaultsUseBalancedOptimization();
    testProjectProfilesMapToOptimizationLevels();
    return 0;
}
