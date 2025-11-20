// File: src/tests/unit/oop/TestStaticMembers.cpp
// Purpose: Verify static fields/methods are lowered as globals/free functions,
// //         and 'ME' is rejected in static methods.

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Module.hpp"

using namespace il::frontends::basic;
using il::core::Module;

namespace {
[[nodiscard]] bool hasGlobalStr(const Module &m, std::string_view name)
{
    for (const auto &g : m.globals)
        if (g.name == name)
            return true;
    return false;
}

[[nodiscard]] bool hasFunction(const Module &m, std::string_view name)
{
    for (const auto &fn : m.functions)
        if (fn.name == name)
            return true;
    return false;
}
} // namespace

TEST(OOP_StaticMembers, LowerAsGlobalAndFreeFunction)
{
    // Static field lowered to @Class::F; static method lowered as @Class.Method
    const char *src = R"BAS(
10 CLASS S
20   STATIC c AS INTEGER
30   STATIC SUB Ping()
40     ' do nothing
50   END SUB
60 END CLASS
70 END
)BAS";

    il::support::SourceManager sm;
    BasicCompilerInput in{src, "static_members.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    ASSERT_TRUE(res.succeeded());
    const Module &mod = res.module;
    EXPECT_TRUE(hasGlobalStr(mod, "S::c"));
    EXPECT_TRUE(hasFunction(mod, "S.Ping"));
}

TEST(OOP_StaticMembers, RejectMeInStaticMethod)
{
    const char *src = R"BAS(
10 CLASS S
20   STATIC SUB Bad()
30     PRINT ME
40   END SUB
50 END CLASS
60 END
)BAS";
    il::support::SourceManager sm;
    BasicCompilerInput in{src, "static_me.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    EXPECT_FALSE(res.succeeded());
}

#ifndef GTEST_HAS_MAIN
int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
