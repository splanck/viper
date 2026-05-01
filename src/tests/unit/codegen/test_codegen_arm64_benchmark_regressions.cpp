//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_benchmark_regressions.cpp
// Purpose: Native ARM64 regressions for benchmark-discovered O2 failures.
// Key invariants: O2 preserves owned string work and keeps checked unsigned
//                 div/rem verifier-clean after loop optimizations.
// Ownership/Lifetime: Temporary IL files are written under build/test-out.
// Links: docs/il-passes.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

namespace {

std::string outPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

int runArm64(std::vector<std::string> args) {
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (auto &arg : args)
        argv.push_back(arg.data());
    return cmd_codegen_arm64(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST(Arm64BenchmarkRegressions, O2StringLoopPreservesOwnedConstStrLifetime) {
    const std::string in = outPath("arm64_bench_string_o2.il");
    const std::string il = R"(il 0.2.0
extern @rt_str_concat(str, str) -> str
extern @rt_str_len(str) -> i64
extern @rt_str_release_maybe(str) -> void
global const str @.La = "abcdef"
global const str @.Lb = "ghijkl"
func @main() -> i64 {
entry:
  br loop(0, 0)
loop(%i:i64, %sum:i64):
  %done = scmp_ge %i, 10
  cbr %done, exit(%sum), body(%i, %sum)
body(%bi:i64, %bsum:i64):
  %a = const_str @.La
  %b = const_str @.Lb
  %c = call @rt_str_concat(%a, %b)
  %n = call @rt_str_len(%c)
  call @rt_str_release_maybe(%c)
  %next_sum = iadd.ovf %bsum, %n
  %next_i = iadd.ovf %bi, 1
  br loop(%next_i, %next_sum)
exit(%out:i64):
  ret %out
}
)";
    writeFile(in, il);

    EXPECT_EQ(runArm64({in, "-O2", "--native-asm", "-run-native"}), 120);
}

TEST(Arm64BenchmarkRegressions, O2CheckedUnsignedDivLoopVerifiesAndRuns) {
    const std::string in = outPath("arm64_bench_udiv_o2.il");
    const std::string il = R"(il 0.2.0
func @main() -> i64 {
entry:
  br loop(1, 0)
loop(%i:i64, %sum:i64):
  %done = scmp_gt %i, 32
  cbr %done, exit(%sum), body(%i, %sum)
body(%bi:i64, %bsum:i64):
  %q = udiv.chk0 %bi, 2
  %r = urem.chk0 %bi, 8
  %part = iadd.ovf %q, %r
  %next_sum = iadd.ovf %bsum, %part
  %next_i = iadd.ovf %bi, 1
  br loop(%next_i, %next_sum)
exit(%out:i64):
  ret %out
}
)";
    writeFile(in, il);

    EXPECT_EQ(runArm64({in, "-O2", "--native-asm", "-run-native"}), 112);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
