//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the Pascal frontend skeleton.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "support/source_manager.hpp"

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../unit/GTestStub.hpp"
#endif

#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

/// @brief Test that the Pascal compiler skeleton produces a valid module.
TEST(PascalCompilerTest, SkeletonProducesModule)
{
    SourceManager sm;
    const std::string source = "program Hello; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    // Should succeed (no errors)
    EXPECT_TRUE(result.succeeded());

    // Module should have @main function
    bool hasMain = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that the skeleton module contains the expected hello message.
TEST(PascalCompilerTest, SkeletonContainsHelloMessage)
{
    SourceManager sm;
    const std::string source = "program Hello; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the module has a global string containing the hello message
    bool foundHello = false;
    for (const auto &global : result.module.globals)
    {
        if (global.init.find("Hello from Viper Pascal") != std::string::npos)
        {
            foundHello = true;
            break;
        }
    }
    EXPECT_TRUE(foundHello);
}

/// @brief Test that diagnostics engine reports no errors for valid (ignored) input.
TEST(PascalCompilerTest, NoDiagnosticsForValidInput)
{
    SourceManager sm;
    const std::string source = "program Test; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
