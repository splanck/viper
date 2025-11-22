//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestLowering.cpp
// Purpose: Ensure lowering of "\"abcd\".Length" emits exactly one call to Viper.Strings.Len. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Module.hpp"
#include "il/core/Instr.hpp"

#include <string>

namespace
{
constexpr const char *kSrc = R"BASIC(
10 PRINT ("abcd").Length
20 END
)BASIC";

static int countCallsTo(const il::core::Module &M, const std::string &name)
{
    using namespace il::core;
    int count = 0;
    for (const auto &fn : M.functions)
    {
        for (const auto &bb : fn.blocks)
        {
            for (const auto &ins : bb.instructions)
            {
                if (ins.op == Opcode::Call && ins.callee == name)
                    ++count;
            }
        }
    }
    return count;
}
} // namespace

TEST(RuntimeClassLowering, StringLiteralLengthLowersToStringsLen)
{
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    std::string src(kSrc);
    il::frontends::basic::BasicCompilerInput input{src, "lit_len.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    // One extern call to Viper.Strings.Len should appear in IL
    EXPECT_EQ(countCallsTo(result.module, "Viper.Strings.Len"), 1);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
