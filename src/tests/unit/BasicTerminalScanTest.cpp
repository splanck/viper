//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/BasicTerminalScanTest.cpp
// Purpose: Verify terminal statements declare required runtime externs during BASIC lowering. 
// Key invariants: Compiling terminal control statements registers runtime helpers in the module.
// Ownership/Lifetime: Test owns compilation inputs and diagnostics infrastructure.
// Links: docs/il-guide.md#reference, src/frontends/basic/LowerStmt_Runtime.cpp
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "viper/il/IO.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view kSrc = R"BASIC(
10 CLS
20 COLOR 14,0
30 LOCATE 5, 10
40 PRINT "HELLO"
)BASIC";

[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name)
{
    const auto &externs = module.externs;
    return std::any_of(externs.begin(),
                       externs.end(),
                       [&](const il::core::Extern &ext) { return ext.name == name; });
}
} // namespace

TEST(BasicTerminalScanTest, DeclaresRequiredExterns)
{
    il::support::SourceManager sourceManager;
    il::frontends::basic::BasicCompilerInput input{kSrc, "terminal.bas"};
    il::frontends::basic::BasicCompilerOptions options{};

    auto result = il::frontends::basic::compileBasic(input, options, sourceManager);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;

    EXPECT_TRUE(hasExtern(module, "rt_term_cls"));
    EXPECT_TRUE(hasExtern(module, "rt_term_color_i32"));
    EXPECT_TRUE(hasExtern(module, "rt_term_locate_i32"));
}

TEST(BasicTerminalScanTest, EmitsTerminalExternsInILText)
{
    il::support::SourceManager sourceManager;
    il::frontends::basic::BasicCompilerInput input{kSrc, "terminal.bas"};
    il::frontends::basic::BasicCompilerOptions options{};

    auto result = il::frontends::basic::compileBasic(input, options, sourceManager);
    ASSERT_TRUE(result.succeeded());

    const std::string ilText = il::io::Serializer::toString(result.module);

    EXPECT_NE(ilText.find("extern @rt_term_cls"), std::string::npos);
    EXPECT_NE(ilText.find("extern @rt_term_color_i32"), std::string::npos);
    EXPECT_NE(ilText.find("extern @rt_term_locate_i32"), std::string::npos);
    EXPECT_TRUE(ilText.find("unknown callee") == std::string::npos);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
