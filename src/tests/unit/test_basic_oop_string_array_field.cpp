//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_oop_string_array_field.cpp
// Purpose: Verify that string array fields in classes store via rt_arr_str_put
// Key invariants: OOP lowering derives element type from class layout and uses
// Ownership/Lifetime: Compiles BASIC source into an IL module and inspects the
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

#include <algorithm>
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

TEST(BasicOOPStringArrayField, ImplicitStoreAndLoadUseStringArrayHelpers)
{
    const std::string src = "10 CLASS Player\n"
                            "20   DIM inventory(10) AS STRING\n"
                            "30   SUB Add(item$)\n"
                            "40     inventory(0) = item$\n"
                            "50   END SUB\n"
                            "60   FUNCTION First$()\n"
                            "70     RETURN inventory(0)\n"
                            "80   END FUNCTION\n"
                            "90 END CLASS\n"
                            "100 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "oop_str_arr_field.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;
    const auto *addFn = findFn(mod, "Player.Add");
    ASSERT_NE(addFn, nullptr);
    const auto *firstFn = findFn(mod, "Player.First$");
    ASSERT_NE(firstFn, nullptr);

    bool sawStrPut = false;
    for (const auto &bb : addFn->blocks)
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

    bool sawStrGet = false;
    for (const auto &bb : firstFn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call && ieq(in.callee, "rt_arr_str_get"))
            {
                sawStrGet = true;
                break;
            }
        }
        if (sawStrGet)
            break;
    }
    EXPECT_TRUE(sawStrGet);
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
