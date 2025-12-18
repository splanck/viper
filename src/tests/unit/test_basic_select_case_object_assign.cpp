//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_select_case_object_assign.cpp
// Purpose: Verify object assignment inside SELECT CASE lowers with object retain path.
// Key invariants: Stores to object vars are pointer-typed and emit rt_obj_retain_maybe.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

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

TEST(BasicSelectCaseObjectAssign, EmitsObjectRetainInArms)
{
    const std::string src = "10 CLASS Player\n"
                            "20 END CLASS\n"
                            "30 FUNCTION GetPlayer(i AS INTEGER) AS Player\n"
                            "40   DIM result AS Player\n"
                            "50   SELECT CASE i\n"
                            "60     CASE 1\n"
                            "70       result = NEW Player()\n"
                            "80     CASE ELSE\n"
                            "90       result = NEW Player()\n"
                            "100   END SELECT\n"
                            "110   RETURN result\n"
                            "120 END FUNCTION\n"
                            "130 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "select_case_obj_assign.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;
    const auto *fn = findFn(mod, "GetPlayer");
    ASSERT_NE(fn, nullptr);

    bool sawRetain = false;
    for (const auto &bb : fn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call && ieq(in.callee, "rt_obj_retain_maybe"))
            {
                sawRetain = true;
                break;
            }
        }
        if (sawRetain)
            break;
    }
    EXPECT_TRUE(sawRetain);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
