//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_call_stack_args_multi.cpp
// Purpose: Ensure each multi-call stack-arg sequence reserves an explicit
//          outgoing area around the call instead of writing into a stale frame
//          reservation.
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/zanna/cmd_codegen_arm64.hpp"

using namespace zanna::tools::ilc;

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

static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

namespace {

size_t countSubstring(const std::string &text, const std::string &needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

} // namespace

TEST(Arm64CLI, CallWithStackArgsUsesPerCallOutgoingArea) {
    const std::string in = outPath("arm64_call_stack_multi.il");
    const std::string out = outPath("arm64_call_stack_multi.s");
    const std::string il = "il 0.1\n"
                           "extern @h1(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64) -> i64\n"
                           "extern @h2(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %x = call @h1(%a, %b, 3, 4, 5, 6, 7, 8, 9, 10)\n"
                           "  %y = call @h2(%a, %b, 13, 14, 15, 16, 17, 18, 19, 20)\n"
                           "  %r = iadd.ovf %x, %y\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str(), "--target-darwin"};
    ASSERT_EQ(cmd_codegen_arm64(4, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    const size_t subCount = countSubstring(asmText, "sub sp, sp");
    const size_t addCount = countSubstring(asmText, "add sp, sp");

    EXPECT_GE(subCount, 2u);
    EXPECT_GE(addCount, 2u);
    // Both calls present
    EXPECT_NE(asmText.find("bl _h1"), std::string::npos);
    EXPECT_NE(asmText.find("bl _h2"), std::string::npos);
    // Stores should target the temporary outgoing area around each call.
    EXPECT_NE(asmText.find("[sp, #0]"), std::string::npos);
    EXPECT_NE(asmText.find("[sp, #8]"), std::string::npos);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
