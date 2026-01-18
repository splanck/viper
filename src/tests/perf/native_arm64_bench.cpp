//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/perf/native_arm64_bench.cpp
// Purpose: Native ARM64 code generation performance regression tests.
//
// This test compiles and runs a recursive fibonacci benchmark to validate:
// 1. Code generation produces correct results
// 2. Performance is within expected bounds (informational, not enforced)
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "il/io/Parser.hpp"
#include "il/transform/PassManager.hpp"
#include "tools/viper/cmd_codegen_arm64.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#if defined(__aarch64__) || defined(_M_ARM64)
#define NATIVE_ARM64 1
#else
#define NATIVE_ARM64 0
#endif

namespace
{

// Find the build directory by looking for CMakeCache.txt
std::string findBuildDir()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    if (!ec)
    {
        for (int depth = 0; depth < 8; ++depth)
        {
            if (fs::exists(cur / "CMakeCache.txt", ec))
                return cur.string();
            if (!cur.has_parent_path())
                break;
            cur = cur.parent_path();
        }
    }
    // Fallback for running from repo root
    if (fs::exists("build/CMakeCache.txt", ec))
        return "build";
    return "";
}

constexpr const char *kFibIL = R"(
il 0.2.0

func @fib(i64 %n) -> i64 {
entry(%n:i64):
  %cmp = scmp_le %n, 1
  cbr %cmp, base(%n), recurse(%n)
base(%n1:i64):
  ret %n1
recurse(%n2:i64):
  %nm1 = isub.ovf %n2, 1
  %r1 = call @fib(%nm1)
  %nm2 = isub.ovf %n2, 2
  %r2 = call @fib(%nm2)
  %sum = iadd.ovf %r1, %r2
  ret %sum
}

func @main() -> i64 {
entry:
  %result = call @fib(35)
  ret %result
}
)";

// Expected result for fib(35) = 9227465
constexpr int64_t kExpectedFib35 = 9227465;

// Helper to write content to a file
void writeFile(const std::string &path, const std::string &content)
{
    std::ofstream ofs(path);
    ASSERT_TRUE(ofs.is_open());
    ofs << content;
}

// Helper to read a file into a string
std::string readFile(const std::string &path)
{
    std::ifstream ifs(path);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

} // namespace

#if NATIVE_ARM64

TEST(NativeArm64Perf, FibCompileAndLink)
{
    // This test verifies the native codegen pipeline produces correct results
    const std::string tmpDir = "/tmp";
    const std::string ilFile = tmpDir + "/perf_fib_test.il";
    const std::string asmFile = tmpDir + "/perf_fib_test.s";
    const std::string objFile = tmpDir + "/perf_fib_test.o";
    const std::string exeFile = tmpDir + "/perf_fib_test";

    // Write IL source
    writeFile(ilFile, kFibIL);

    // Step 1: Compile IL to ARM64 assembly
    const char *codegenArgs[] = {ilFile.c_str(), "-S", asmFile.c_str()};
    const int codegenResult =
        viper::tools::ilc::cmd_codegen_arm64(3, const_cast<char **>(codegenArgs));
    ASSERT_EQ(codegenResult, 0);

    // Step 2: Assemble
    std::string asmCmd = "as " + asmFile + " -o " + objFile;
    const int asmResult = std::system(asmCmd.c_str());
    ASSERT_EQ(asmResult, 0);

    // Step 3: Link with runtime library (use full runtime to satisfy all dependencies)
    const std::string buildDir = findBuildDir();
    std::string linkCmd = "clang++ " + objFile;
    if (!buildDir.empty())
    {
        const std::string runtimeLib = buildDir + "/src/runtime/libviper_runtime.a";
        if (std::filesystem::exists(runtimeLib))
        {
            linkCmd += " " + runtimeLib;
        }
    }
    // Link system libraries required by runtime
    linkCmd += " -framework IOKit -framework CoreFoundation -lpthread";
    linkCmd += " -o " + exeFile;
    const int linkResult = std::system(linkCmd.c_str());
    ASSERT_EQ(linkResult, 0);

    // Step 4: Run and time
    auto start = std::chrono::high_resolution_clock::now();
    std::string runCmd = exeFile + " > /dev/null; echo $?";
    const int runResult = std::system(exeFile.c_str());
    auto end = std::chrono::high_resolution_clock::now();
    const double durationMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // The exit code should match the expected fib(35) value (truncated to 8 bits)
    const int expectedExitCode = static_cast<int>(kExpectedFib35 & 0xFF);
    EXPECT_EQ(WEXITSTATUS(runResult), expectedExitCode);

    // Report execution time (informational, not enforced)
    std::cout << "  fib(35) native execution: " << durationMs << "ms\n";

    // Cleanup
    std::remove(ilFile.c_str());
    std::remove(asmFile.c_str());
    std::remove(objFile.c_str());
    std::remove(exeFile.c_str());
}

#else // !NATIVE_ARM64

TEST(NativeArm64Perf, FibCompileAndLink)
{
    // Skip on non-ARM64 platforms
    std::cout << "  [SKIPPED] Not an ARM64 platform\n";
}

#endif // NATIVE_ARM64

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
