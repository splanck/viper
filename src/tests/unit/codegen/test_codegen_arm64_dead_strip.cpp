//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_dead_strip.cpp
// Purpose: Verify native linking dead-strips unused runtime symbols.
//===----------------------------------------------------------------------===//
#include "common/RunProcess.hpp"
#include "tests/TestHarness.hpp"

#include <filesystem>
#include <fstream>
#include <string>

static std::string outPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

static std::filesystem::path findViperTool() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    for (int depth = 0; !ec && depth < 8 && !cur.empty(); ++depth) {
        fs::path candidate = cur / "src" / "tools" / "viper" / "viper";
        if (fs::exists(candidate, ec))
            return candidate;
        fs::path parent = cur.parent_path();
        if (parent == cur)
            break;
        cur = parent;
    }
    return {};
}

TEST(Arm64CLI, DeadStripsUnusedRuntimeSymbols) {
    namespace fs = std::filesystem;
    const std::string in = outPath("arm64_dead_strip.il");
    const std::string exeOut = outPath("arm64_dead_strip_exe");
    const std::string il = "il 0.1\n"
                           "extern @rt_print_i64(i64) -> void\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  call @rt_print_i64(123)\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);

    const fs::path viper = findViperTool();
    ASSERT_FALSE(viper.empty());

    const RunResult link = run_process(
        {viper.string(), "codegen", "arm64", in, "-o", exeOut, "--system-asm", "--system-link"});
    ASSERT_EQ(link.exit_code, 0);
    ASSERT_TRUE(fs::exists(exeOut));
    EXPECT_NE(link.err.find("warning: --system-link is deprecated; using the native linker"),
              std::string::npos);
    EXPECT_NE(link.err.find("dead-strip: removed"), std::string::npos);

    const RunResult exec = run_process({exeOut});
    ASSERT_EQ(exec.exit_code, 0);
    EXPECT_NE(exec.out.find("123"), std::string::npos);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
