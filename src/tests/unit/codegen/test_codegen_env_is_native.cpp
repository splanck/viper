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
#include "tests/unit/GTestStub.hpp"
#include "tools/ilc/cmd_codegen_arm64.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace viper::tests;
using namespace viper::tools::ilc;

#ifdef VIPER_HAS_GTEST

namespace
{

constexpr const char kIlSource[] = R"(il 0.1.2

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
    ASSERT_TRUE(parseResult) << "Failed to parse IL source";
    auto verifyResult = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verifyResult) << "IL failed verification";
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
    EXPECT_EQ(result, 0) << "VM should report non-native execution";
}

TEST(EnvironmentIsNative, NativeArm64ReportsTrueWhenAvailable)
{
    if (!isArm64Host())
        GTEST_SKIP() << "ARM64 native backend not available on this host";

    // Write IL to a temp file
    const std::filesystem::path ilPath{"build/test-out/env_is_native.il"};
    std::filesystem::create_directories(ilPath.parent_path());
    {
        std::ofstream ofs(ilPath);
        ASSERT_TRUE(static_cast<bool>(ofs));
        ofs << kIlSource;
    }

    const char *argv[] = {ilPath.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    EXPECT_EQ(rc & 0xFF, 1) << "Native execution should report IsNative=1";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#else

int main()
{
    // No gtest available; nothing to run.
    return 0;
}

#endif // VIPER_HAS_GTEST
