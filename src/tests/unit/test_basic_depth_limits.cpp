//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for BASIC parser recursion depth limits.
// Generates deeply nested source to verify stack overflow prevention.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

/// @brief Check whether any diagnostic message contains @p needle.
bool hasDiagContaining(const DiagnosticEngine &diag, const std::string &needle)
{
    for (const auto &d : diag.diagnostics())
    {
        if (d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

/// @brief Compile BASIC source and return the result.
BasicCompilerResult compileBASIC(const std::string &source)
{
    SourceManager sm;
    BasicCompilerInput input{source, "depth_test.bas"};
    BasicCompilerOptions opts{};
    return compileBasic(input, opts, sm);
}

//===----------------------------------------------------------------------===//
// Statement depth tests (limit: 512)
//===----------------------------------------------------------------------===//

/// @brief 513 nested IF statements must trigger the depth limit.
TEST(BasicDepthLimits, DeepIfExceedsLimit)
{
    // Generate BASIC with line numbers:
    //   10 IF 1 THEN
    //   20 IF 1 THEN
    //   ...
    //   5140 LET X = 1
    //   5150 END IF
    //   ...
    std::string src;
    int lineNum = 10;
    for (int i = 0; i < 513; i++)
    {
        src += std::to_string(lineNum) + " IF 1 THEN\n";
        lineNum += 10;
    }
    src += std::to_string(lineNum) + " LET X = 1\n";
    lineNum += 10;
    for (int i = 0; i < 513; i++)
    {
        src += std::to_string(lineNum) + " END IF\n";
        lineNum += 10;
    }

    auto result = compileBASIC(src);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "nesting too deep"));
}

/// @brief 50 nested IF statements must succeed (well below limit).
TEST(BasicDepthLimits, ModerateIfSucceeds)
{
    std::string src;
    int lineNum = 10;
    for (int i = 0; i < 50; i++)
    {
        src += std::to_string(lineNum) + " IF 1 THEN\n";
        lineNum += 10;
    }
    src += std::to_string(lineNum) + " LET X = 1\n";
    lineNum += 10;
    for (int i = 0; i < 50; i++)
    {
        src += std::to_string(lineNum) + " END IF\n";
        lineNum += 10;
    }

    auto result = compileBASIC(src);
    EXPECT_FALSE(hasDiagContaining(result.diagnostics, "nesting too deep"));
}

//===----------------------------------------------------------------------===//
// Expression depth tests (limit: 512)
//===----------------------------------------------------------------------===//

/// @brief 600 nested parenthesized expressions must trigger the depth limit.
TEST(BasicDepthLimits, DeepExpressionExceedsLimit)
{
    std::string expr;
    for (int i = 0; i < 600; i++)
        expr += "(";
    expr += "1";
    for (int i = 0; i < 600; i++)
        expr += ")";

    std::string src = "10 LET X = " + expr + "\n";

    auto result = compileBASIC(src);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "nesting too deep"));
}

/// @brief 50 nested parenthesized expressions must succeed.
TEST(BasicDepthLimits, ModerateExpressionSucceeds)
{
    std::string expr;
    for (int i = 0; i < 50; i++)
        expr += "(";
    expr += "1";
    for (int i = 0; i < 50; i++)
        expr += ")";

    std::string src = "10 LET X = " + expr + "\n";

    auto result = compileBASIC(src);
    EXPECT_FALSE(hasDiagContaining(result.diagnostics, "nesting too deep"));
}

//===----------------------------------------------------------------------===//
// Counter reset tests
//===----------------------------------------------------------------------===//

/// @brief Depth counters reset between independent compilations.
TEST(BasicDepthLimits, CounterResetsAcrossCompilations)
{
    // First: compile something that hits the limit
    std::string deep;
    int lineNum = 10;
    for (int i = 0; i < 513; i++)
    {
        deep += std::to_string(lineNum) + " IF 1 THEN\n";
        lineNum += 10;
    }
    deep += std::to_string(lineNum) + " LET X = 1\n";
    lineNum += 10;
    for (int i = 0; i < 513; i++)
    {
        deep += std::to_string(lineNum) + " END IF\n";
        lineNum += 10;
    }

    auto result1 = compileBASIC(deep);
    EXPECT_FALSE(result1.succeeded());

    // Second: compile something normal — must succeed
    auto result2 = compileBASIC("10 LET X = 42\n");
    EXPECT_TRUE(result2.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
