//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_global_string_array_sub_store.cpp
// Purpose: Verify that assigning to a global STRING array from a SUB uses rt_arr_str_put. 
// Key invariants: Lowering selects string array helper even from SUB scope.
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

[[nodiscard]] static const il::core::Function *findFn(const il::core::Module &m,
                                                      std::string_view name)
{
    for (const auto &fn : m.functions)
        if (ieq(fn.name, name))
            return &fn;
    return nullptr;
}
} // namespace

TEST(BasicGlobalStringArrayStore, SubAssignUsesStringArrayHelper)
{
    const std::string src = "10 DIM names(3) AS STRING\n"
                            "20 SUB S()\n"
                            "30   names(1) = \"Alice\"\n"
                            "40 END SUB\n"
                            "50 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "global_str_arr_sub.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;
    const auto *subFn = findFn(mod, "S");
    ASSERT_NE(subFn, nullptr);

    bool sawStrPut = false;
    for (const auto &bb : subFn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call && ieq(in.callee, "rt_arr_str_put"))
            {
                sawStrPut = true;
                break;
            }
        }
        if (sawStrPut)
            break;
    }
    EXPECT_TRUE(sawStrPut);
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
