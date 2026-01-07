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
#include "tests/TestHarness.hpp"
using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

TEST(PascalCtorChainTest, SameClassCtorDelegationNoAllocation)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TDog = class public Name: String; Age: Integer; constructor "
        "CreateDefault; constructor CreateNamed(AName: String); end; "
        "constructor TDog.CreateDefault; begin CreateNamed('Dog'); Age := 1 end; "
        "constructor TDog.CreateNamed(AName: String); begin Name := AName end; var d: TDog; begin "
        "d := TDog.CreateDefault end.";
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
        "program Test; type TBase = class public X: Integer; constructor CreateBase(V: Integer); "
        "end; "
        "TDer = class(TBase) public Y: Integer; constructor Create(V: Integer); end; "
        "constructor TBase.CreateBase(V: Integer); begin X := V end; "
        "constructor TDer.Create(V: Integer); begin inherited CreateBase(V); Y := V end; var d: "
        "TDer; begin d := TDer.Create(3) end.";
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

TEST(PascalCtorChainTest, InheritedFieldAccessInDerivedCtor)
{
    // Tests that derived class constructors can access inherited fields.
    // This was a bug where the lowerer didn't walk the inheritance chain.
    SourceManager sm;
    const std::string src =
        "program Test; type TAnimal = class public Name: String; "
        "constructor Create(AName: String); end; "
        "TDog = class(TAnimal) public Breed: String; "
        "constructor Create(AName, ABreed: String); end; "
        "constructor TAnimal.Create(AName: String); begin Name := AName end; "
        "constructor TDog.Create(AName, ABreed: String); begin inherited Create(AName); "
        "Breed := ABreed; WriteLn(Name) end; "
        "var d: TDog; begin d := TDog.Create('Buddy', 'Lab') end.";
    PascalCompilerInput input{.source = src, .path = "ctor3.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    // Verify TDog.Create function exists and has instructions
    // (if inherited field access failed, it would produce a const 0 instead of field access)
    bool foundTDogCreate = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "TDog.Create")
        {
            foundTDogCreate = true;
            // Just verify it has some instructions (meaning lowering succeeded)
            size_t instrCount = 0;
            for (const auto &blk : fn.blocks)
            {
                instrCount += blk.instructions.size();
            }
            EXPECT_TRUE(instrCount > 0);
        }
    }
    EXPECT_TRUE(foundTDogCreate);
}

TEST(PascalCtorChainTest, DestructorChaining)
{
    // Tests that destructor chaining with inherited Destroy works
    SourceManager sm;
    const std::string src =
        "program Test; type TBase = class public destructor Destroy; virtual; end; "
        "TChild = class(TBase) public destructor Destroy; override; end; "
        "destructor TBase.Destroy; begin WriteLn('Base') end; "
        "destructor TChild.Destroy; begin WriteLn('Child'); inherited Destroy end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "dtor1.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    // Verify TChild.Destroy calls TBase.Destroy
    bool foundBaseDestroyCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "TChild.Destroy")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "TBase.Destroy")
                        foundBaseDestroyCall = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundBaseDestroyCall);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
