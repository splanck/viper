// File: tests/unit/test_basic_builtins_args_cmd.cpp
// Purpose: Ensure ARGC, ARG$, and COMMAND$ compile and lower to correct runtime calls.
// Key invariants: Module contains calls to rt_args_count, rt_args_get, rt_cmdline.
// Ownership/Lifetime: Standalone unit test.

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAVE_GTEST 1
#else
#include "GTestStub.hpp"
#define VIPER_HAVE_GTEST 0
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
static bool containsCall(const il::core::Module &m, std::string_view name)
{
    auto ieq = [](char a, char b)
    { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
    auto eq = [&](std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!ieq(a[i], b[i]))
                return false;
        return true;
    };
    for (const auto &fn : m.functions)
        for (const auto &bb : fn.blocks)
            for (const auto &in : bb.instructions)
                if (in.op == il::core::Opcode::Call && eq(in.callee, name))
                    return true;
    return false;
}
} // namespace

#if VIPER_HAVE_GTEST
TEST(BasicBuiltinsArgsCmd, LowersToRuntime)
#else
int main()
#endif
{
    const std::string src = "10 PRINT ARGC()\n"
                            "20 PRINT ARG$(0)\n"
                            "30 PRINT COMMAND$()\n"
                            "40 END\n";
    SourceManager sm;
    BasicCompilerInput input{src, "args_cmd.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    const il::core::Module &mod = result.module;
    EXPECT_TRUE(containsCall(mod, "rt_args_count"));
    EXPECT_TRUE(containsCall(mod, "rt_args_get"));
    EXPECT_TRUE(containsCall(mod, "rt_cmdline"));
#if VIPER_HAVE_GTEST
    (void)0;
#else
    return 0;
#endif
}
