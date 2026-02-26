//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia parser recursion depth limits.
// Generates deeply nested source to verify stack overflow prevention.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
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

/// @brief Compile Zia source and return the result.
CompilerResult compileSource(const std::string &source)
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = "depth_test.zia"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

//===----------------------------------------------------------------------===//
// Statement depth tests (limit: 512)
//===----------------------------------------------------------------------===//

/// @brief 513 nested block statements must trigger the depth limit.
TEST(ZiaDepthLimits, DeepBlocksExceedLimit)
{
    std::string src = "module Test;\nfunc start() {\n";
    for (int i = 0; i < 513; i++)
        src += "{ ";
    for (int i = 0; i < 513; i++)
        src += "} ";
    src += "\n}\n";

    auto result = compileSource(src);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "statement nesting too deep"));
}

/// @brief 100 nested block statements must succeed (well below limit).
TEST(ZiaDepthLimits, ModerateBlocksSucceed)
{
    std::string src = "module Test;\nfunc start() {\n";
    for (int i = 0; i < 100; i++)
        src += "{ ";
    for (int i = 0; i < 100; i++)
        src += "} ";
    src += "\n}\n";

    auto result = compileSource(src);
    EXPECT_FALSE(hasDiagContaining(result.diagnostics, "nesting too deep"));
}

//===----------------------------------------------------------------------===//
// Type depth tests (limit: 256)
// Uses 'var x: Type' syntax to avoid speculative parsing (which suppresses
// diagnostics). The colon triggers parseType() in a non-speculative context.
//===----------------------------------------------------------------------===//

/// @brief 257 nested generic types must trigger the type depth limit.
TEST(ZiaDepthLimits, DeepTypeExceedsLimit)
{
    // Generate: var x: List[List[...[Integer]...]]
    std::string type;
    for (int i = 0; i < 257; i++)
        type += "List[";
    type += "Integer";
    for (int i = 0; i < 257; i++)
        type += "]";

    std::string src = "module Test;\nfunc start() {\n    var x: " + type + " = 0;\n}\n";

    auto result = compileSource(src);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "type nesting too deep"));
}

/// @brief 50 nested generic types must succeed (well below limit).
TEST(ZiaDepthLimits, ModerateTypeSucceeds)
{
    std::string type;
    for (int i = 0; i < 50; i++)
        type += "List[";
    type += "Integer";
    for (int i = 0; i < 50; i++)
        type += "]";

    std::string src = "module Test;\nfunc start() {\n    var x: " + type + " = 0;\n}\n";

    auto result = compileSource(src);
    EXPECT_FALSE(hasDiagContaining(result.diagnostics, "type nesting too deep"));
}

//===----------------------------------------------------------------------===//
// Expression depth tests (limit: 256)
// Uses 'var x = ...' syntax to avoid speculative parsing. The 'var' keyword
// is handled directly (non-speculatively), so expression depth errors propagate.
//===----------------------------------------------------------------------===//

/// @brief 300 nested parenthesized expressions must trigger the expression depth limit.
TEST(ZiaDepthLimits, DeepExpressionExceedsLimit)
{
    std::string expr;
    for (int i = 0; i < 300; i++)
        expr += "(";
    expr += "0";
    for (int i = 0; i < 300; i++)
        expr += ")";

    std::string src = "module Test;\nfunc start() {\n    var x = " + expr + ";\n}\n";

    auto result = compileSource(src);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "expression nesting too deep"));
}

/// @brief 50 nested parenthesized expressions must succeed.
TEST(ZiaDepthLimits, ModerateExpressionSucceeds)
{
    std::string expr;
    for (int i = 0; i < 50; i++)
        expr += "(";
    expr += "0";
    for (int i = 0; i < 50; i++)
        expr += ")";

    std::string src = "module Test;\nfunc start() {\n    var x = " + expr + ";\n}\n";

    auto result = compileSource(src);
    EXPECT_FALSE(hasDiagContaining(result.diagnostics, "expression nesting too deep"));
}

//===----------------------------------------------------------------------===//
// Counter reset tests
//===----------------------------------------------------------------------===//

/// @brief Depth counters reset between independent compilations.
TEST(ZiaDepthLimits, CounterResetsAcrossCompilations)
{
    // First: compile something that hits the limit
    std::string deep = "module Test;\nfunc start() {\n";
    for (int i = 0; i < 513; i++)
        deep += "{ ";
    for (int i = 0; i < 513; i++)
        deep += "} ";
    deep += "\n}\n";

    auto result1 = compileSource(deep);
    EXPECT_FALSE(result1.succeeded());

    // Second: compile something normal — must succeed (counter must not carry over)
    std::string normal = "module Test;\nfunc start() {\n    var x = 42;\n}\n";
    auto result2 = compileSource(normal);
    EXPECT_TRUE(result2.succeeded());
}

/// @brief Multiple sequential deep compilations each fail independently.
TEST(ZiaDepthLimits, RepeatedDeepCompilationsFail)
{
    std::string deep = "module Test;\nfunc start() {\n";
    for (int i = 0; i < 513; i++)
        deep += "{ ";
    for (int i = 0; i < 513; i++)
        deep += "} ";
    deep += "\n}\n";

    for (int trial = 0; trial < 3; trial++)
    {
        auto result = compileSource(deep);
        EXPECT_FALSE(result.succeeded());
        EXPECT_TRUE(hasDiagContaining(result.diagnostics, "statement nesting too deep"));
    }
}

//===----------------------------------------------------------------------===//
// Namespace depth tests (shares statement depth limit: 512)
//===----------------------------------------------------------------------===//

/// @brief Deeply nested namespaces must trigger the depth limit.
TEST(ZiaDepthLimits, DeepNamespaceExceedsLimit)
{
    std::string src = "module Test;\n";
    for (int i = 0; i < 513; i++)
        src += "namespace N" + std::to_string(i) + " { ";
    src += "func foo() {} ";
    for (int i = 0; i < 513; i++)
        src += "} ";
    src += "\n";

    auto result = compileSource(src);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "nesting too deep"));
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
