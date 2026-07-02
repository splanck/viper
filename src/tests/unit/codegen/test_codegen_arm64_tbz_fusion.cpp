//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_tbz_fusion.cpp
// Purpose: Verify the AArch64 peephole fuses a single-bit AND feeding a
//          compare-to-zero branch into tbz/tbnz.
// Links: src/codegen/aarch64/peephole/BranchOpt.cpp (tryTbzTbnzFusion)
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

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

// A loop whose body branches on one bit of the induction variable: the mask
// result has no other use, so the and+tst+b.cond chain must fuse to a single
// test-bit branch at -O2.
const char *kBitBranchLoop = "il 0.2.0\n"
                             "\n"
                             "func @main() -> i64 {\n"
                             "entry:\n"
                             "  br loop(0, 0)\n"
                             "loop(%i: i64, %acc: i64):\n"
                             "  %bit = and %i, 1\n"
                             "  %isodd = icmp_ne %bit, 0\n"
                             "  cbr %isodd, odd(%i, %acc), even(%i, %acc)\n"
                             "odd(%io: i64, %ao: i64):\n"
                             "  %a3 = iadd.ovf %ao, 3\n"
                             "  br next(%io, %a3)\n"
                             "even(%ie: i64, %ae: i64):\n"
                             "  %a1 = iadd.ovf %ae, 1\n"
                             "  br next(%ie, %a1)\n"
                             "next(%in: i64, %an: i64):\n"
                             "  %i1 = iadd.ovf %in, 1\n"
                             "  %done = scmp_lt %i1, 100\n"
                             "  cbr %done, loop(%i1, %an), exit(%an)\n"
                             "exit(%res: i64):\n"
                             "  %masked = and %res, 255\n"
                             "  ret %masked\n"
                             "}\n";

} // namespace

TEST(Arm64Peephole, TbzFusion_BitBranchUsesTbnz) {
    const std::string in = outPath("arm64_tbz_fusion.il");
    const std::string out = outPath("arm64_tbz_fusion.s");
    writeFile(in, kBitBranchLoop);
    const char *argv[] = {in.c_str(), "-O2", "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(4, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // The single-bit test lowers to a test-bit branch; the discrete mask and
    // its flag-setting consumer disappear.
    EXPECT_NE(asmText.find("tbnz x"), std::string::npos);
    EXPECT_EQ(asmText.find("tst x"), std::string::npos);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
