//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rtgen_header_parse.cpp
// Purpose: Guard rtgen's header scanner against preprocessor-macro false positives.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#endif
#define main viper_rtgen_tool_main
#include "tools/rtgen/rtgen.cpp"
#undef main
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace {

struct TempDir {
    fs::path path;

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

TempDir makeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    TempDir dir{fs::temp_directory_path() / ("viper-rtgen-header-parse-" + std::to_string(stamp))};
    fs::create_directories(dir.path);
    return dir;
}

} // namespace

TEST(RtgenHeaderParse, StripPreprocessorDropsContinuedMacroBodies) {
    const std::string input = "#define RT_DECLARE_FAKE() \\\n"
                              "    void rt_fake_from_macro(void); \\\n"
                              "    void rt_fake_from_macro_2(void)\n"
                              "void rt_real_decl(void);\n";

    const std::string stripped = stripPreprocessor(input);
    EXPECT_TRUE(stripped.find("rt_fake_from_macro") == std::string::npos);
    EXPECT_TRUE(stripped.find("rt_fake_from_macro_2") == std::string::npos);
    EXPECT_TRUE(stripped.find("rt_real_decl") != std::string::npos);
}

TEST(RtgenHeaderParse, LoadRuntimeHeaderDeclarationsIgnoresMacroGeneratedPrototypes) {
    TempDir repo = makeTempDir();
    const fs::path runtimeDir = repo.path / "runtime";
    fs::create_directories(runtimeDir);

    const fs::path headerPath = runtimeDir / "macro_test.h";
    std::ofstream out(headerPath);
    out << "#pragma once\n"
           "#define RT_DECLARE_FAKE() \\\n"
           "    void rt_fake_from_macro(void); \\\n"
           "    void rt_fake_from_macro_2(void)\n"
           "typedef void (*rt_fake_fn)(void);\n"
           "void rt_real_decl(void);\n";
    out.close();

    const auto decls = loadRuntimeHeaderDeclarations(runtimeDir, repo.path);

    EXPECT_TRUE(decls.find("rt_fake_from_macro") == decls.end());
    EXPECT_TRUE(decls.find("rt_fake_from_macro_2") == decls.end());
    EXPECT_TRUE(decls.find("rt_fake_fn") == decls.end());
    EXPECT_TRUE(decls.find("rt_real_decl") != decls.end());
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
