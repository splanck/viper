//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalAbstractTests.cpp
// Purpose: Tests enforcing abstract methods and classes in Pascal.
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

TEST(PascalAbstractTest, CannotInstantiateAbstractBase)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TAnimal = class public procedure Speak; virtual; abstract; end; var a: TAnimal; begin a := TAnimal.Create end.";
    PascalCompilerInput input{.source = src, .path = "abs1.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
    EXPECT_NE(result.diagnostics.errorCount(), 0u);
}

TEST(PascalAbstractTest, SubclassMustImplementOrRemainAbstract)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TAnimal = class public procedure Speak; virtual; abstract; end; TBad = class(TAnimal) end; var b: TBad; begin b := TBad.Create end.";
    PascalCompilerInput input{.source = src, .path = "abs2.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
    EXPECT_NE(result.diagnostics.errorCount(), 0u);
}

TEST(PascalAbstractTest, ConcreteOverrideInstantiableAndCall)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TAnimal = class public procedure Speak; virtual; abstract; end; TDog = class(TAnimal) public procedure Speak; override; end; procedure TDog.Speak; begin end; var d: TDog; begin d := TDog.Create; d.Speak end.";
    PascalCompilerInput input{.source = src, .path = "abs3.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

TEST(PascalAbstractTest, CannotCallAbstractMethodDirectly)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TAnimal = class public procedure Speak; virtual; abstract; end; var a: TAnimal; begin a.Speak end.";
    PascalCompilerInput input{.source = src, .path = "abs4.pas"};
    PascalCompilerOptions opts{};
    auto result = compilePascal(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
    EXPECT_NE(result.diagnostics.errorCount(), 0u);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif

