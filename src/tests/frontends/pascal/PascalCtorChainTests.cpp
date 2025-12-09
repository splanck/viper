//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalCtorChainTests.cpp
// Purpose: Tests for constructor chaining (same class and base class).
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
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

TEST(PascalCtorChainTest, SameClassCtorDelegationNoAllocation)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TDog = class public Name: String; Age: Integer; constructor CreateDefault; constructor CreateNamed(AName: String); end; "
        "constructor TDog.CreateDefault; begin CreateNamed('Dog'); Age := 1 end; "
        "constructor TDog.CreateNamed(AName: String); begin Name := AName end; var d: TDog; begin d := TDog.CreateDefault end.";
    PascalCompilerInput input{.source = src, .path = "ctor1.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    // Ensure TDog.CreateDefault contains a call to TDog.CreateNamed and no rt_obj_new_i64
    bool foundCallNamed = false;
    bool foundAlloc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "TDog.CreateDefault")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "TDog.CreateNamed")
                        foundCallNamed = true;
                    if (instr.op == il::core::Opcode::Call && instr.callee == "rt_obj_new_i64")
                        foundAlloc = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundCallNamed);
    EXPECT_FALSE(foundAlloc);
}

TEST(PascalCtorChainTest, InheritedCtorCall)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TBase = class public X: Integer; constructor CreateBase(V: Integer); end; "
        "TDer = class(TBase) public Y: Integer; constructor Create(V: Integer); end; "
        "constructor TBase.CreateBase(V: Integer); begin X := V end; "
        "constructor TDer.Create(V: Integer); begin inherited CreateBase(V); Y := V end; var d: TDer; begin d := TDer.Create(3) end.";
    PascalCompilerInput input{.source = src, .path = "ctor2.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    // Ensure TDer.Create calls TBase.CreateBase directly
    bool foundBaseCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "TDer.Create")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "TBase.CreateBase")
                        foundBaseCall = true;
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

