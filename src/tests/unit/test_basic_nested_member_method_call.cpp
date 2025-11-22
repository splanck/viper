//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_nested_member_method_call.cpp
// Purpose: Verify nested member method calls lower to class method callee names. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
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

TEST(BasicNestedMemberMethod, ResolvesAndCallsClassMethod)
{
    const std::string src = "10 CLASS Team\n"
                            "20   SUB InitPlayer(num AS INTEGER, name AS STRING)\n"
                            "30     PRINT \"P\"; num; \" \"; name\n"
                            "40   END SUB\n"
                            "50 END CLASS\n"
                            "60 CLASS Game\n"
                            "70   awayTeam AS Team\n"
                            "80 END CLASS\n"
                            "90 DIM game AS Game\n"
                            "100 game = NEW Game()\n"
                            "110 game.awayTeam = NEW Team()\n"
                            "120 game.awayTeam.InitPlayer(1, \"A\")\n"
                            "130 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "nested_member_method.bas"};
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

    bool sawMethodCall = false;
    for (const auto &bb : mainFn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call && ieq(in.callee, "TEAM.INITPLAYER"))
            {
                sawMethodCall = true;
                break;
            }
        }
        if (sawMethodCall)
            break;
    }
    EXPECT_TRUE(sawMethodCall);
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
