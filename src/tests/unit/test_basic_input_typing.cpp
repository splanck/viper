// File: tests/unit/test_basic_input_typing.cpp
// Purpose: Verify INPUT emits correct conversions/stores for STRING and SINGLE.
// Key invariants: STRING target stores directly; SINGLE target uses rt_to_double.

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"

#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
[[nodiscard]] static bool ieq(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        unsigned char ac = static_cast<unsigned char>(a[i]);
        unsigned char bc = static_cast<unsigned char>(b[i]);
        if (std::tolower(ac) != std::tolower(bc))
            return false;
    }
    return true;
}
} // namespace

TEST(BasicInputTyping, EmitsToDoubleForSingle)
{
    const std::string src = "10 DIM s AS STRING\n"
                            "20 DIM x AS SINGLE\n"
                            "30 PRINT \"Enter name: \";\n"
                            "40 INPUT s\n"
                            "50 PRINT \"Enter score: \";\n"
                            "60 INPUT x\n"
                            "70 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "input_typing.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;
    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : mod.functions)
    {
        if (ieq(fn.name, "main"))
        {
            mainFn = &fn;
            break;
        }
    }
    ASSERT_NE(mainFn, nullptr);

    bool sawToDouble = false;
    for (const auto &bb : mainFn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call && ieq(in.callee, "rt_to_double"))
            {
                sawToDouble = true;
                break;
            }
        }
        if (sawToDouble)
            break;
    }
    EXPECT_TRUE(sawToDouble);
}

#if __has_include(<gtest/gtest.h>)
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#else
int main()
{
    return RUN_ALL_TESTS();
}
#endif
