//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalInheritedTests.cpp
// Purpose: Tests for Pascal 'inherited' calls in methods.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../unit/GTestStub.hpp"
#endif

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

TEST(PascalInheritedTest, CallsBaseImplementation)
{
    SourceManager sm;
    const std::string source =
        "program Test; type TAnimal = class public procedure Speak; virtual; end; TDog = "
        "class(TAnimal) public procedure Speak; override; end; procedure TAnimal.Speak; begin "
        "WriteLn('Animal') end; procedure TDog.Speak; begin inherited; WriteLn('Dog') end; var a: "
        "TAnimal; begin a := TDog.Create; a.Speak end.";
    PascalCompilerInput input{.source = source, .path = "test_inherited.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);

    // Verify that a direct call to TAnimal.Speak appears in the module
    bool foundBaseCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "TDog.Speak")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "TAnimal.Speak")
                    {
                        foundBaseCall = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundBaseCall);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
