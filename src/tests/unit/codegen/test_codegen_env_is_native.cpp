//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_env_is_native.cpp
// Purpose: Ensure Viper.Environment.IsNative reports VM vs native execution.
// Key invariants: VM path must return 0, native AArch64 path returns 1.
// Ownership/Lifetime: Tests generate ephemeral IL modules and files.
// Links: docs/devdocs/runtime-vm.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/verify/Verifier.hpp"
#include "tests/TestHarness.hpp"
#include "tools/ilc/cmd_codegen_arm64.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace viper::tests;
using namespace viper::tools::ilc;

namespace
{

constexpr const char kIlSource[] = R"(il 0.2.0

extern @Viper.Environment.IsNative() -> i1

func @main() -> i64 {
entry:
  %flag = call @Viper.Environment.IsNative()
  %wide = zext1 %flag
  ret %wide
}
)";

il::core::Module parseModule()
{
    std::istringstream iss{kIlSource};
    il::core::Module module;
    auto parseResult = il::io::Parser::parse(iss, module);
    if (!parseResult)
        std::cerr << "Failed to parse IL source\n";
    ASSERT_TRUE(parseResult);
    auto verifyResult = il::verify::Verifier::verify(module);
    if (!verifyResult)
        std::cerr << "IL failed verification\n";
    ASSERT_TRUE(verifyResult);
    return module;
}

bool isArm64Host()
{
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return true;
#else
    return false;
#endif
}

} // namespace

TEST(EnvironmentIsNative, VmReportsFalse)
{
    VmFixture fixture;
    il::core::Module module = parseModule();
    const auto result = fixture.run(module);
    EXPECT_EQ(result, 0);
}

TEST(EnvironmentIsNative, NativeArm64ReportsTrueWhenAvailable)
{
    if (!isArm64Host())
        VIPER_TEST_SKIP("ARM64 native backend not available on this host");

    // Write IL to a temp file
    const std::filesystem::path ilPath{"build/test-out/env_is_native.il"};
    std::filesystem::create_directories(ilPath.parent_path());
    {
        std::ofstream ofs(ilPath);
        ASSERT_TRUE(static_cast<bool>(ofs));
        ofs << kIlSource;
    }

    const std::string ilPathStr = ilPath.string();
    const char *argv[] = {ilPathStr.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    EXPECT_EQ(rc & 0xFF, 1);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
