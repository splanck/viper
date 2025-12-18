//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalIsTests.cpp
// Purpose: Tests for Pascal 'is' type-check and 'as' safe-cast operators.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

TEST(PascalIsTest, ClassIsChecksCompileAndLower)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TAnimal = class end;
  TDog = class(TAnimal) end;
  TCat = class(TAnimal) end;
var
  a: TAnimal;
  d: TDog;
  c: TCat;
  b1, b2, b3: Boolean;
begin
  d := TDog.Create;
  c := TCat.Create;
  a := d;
  b1 := a is TAnimal;
  b2 := a is TDog;
  b3 := a is TCat
end.
)";
    PascalCompilerInput input{.source = source, .path = "test_is.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);

    // Expect an extern for rt_cast_as used by 'is'
    bool hasCastExtern = false;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "rt_cast_as")
        {
            hasCastExtern = true;
            break;
        }
    }
    EXPECT_TRUE(hasCastExtern);
}

TEST(PascalAsTest, ClassAsChecksCompileAndLower)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TAnimal = class end;
  TDog = class(TAnimal) end;
  TCat = class(TAnimal) end;
var
  a: TAnimal;
  d: TDog;
  c: TCat;
begin
  d := TDog.Create;
  c := TCat.Create;
  a := d;
  d := a as TDog;
  c := a as TCat
end.
)";
    PascalCompilerInput input{.source = source, .path = "test_as.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);

    // Expect an extern for rt_cast_as used by 'as'
    bool hasCastExtern = false;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "rt_cast_as")
        {
            hasCastExtern = true;
            break;
        }
    }
    EXPECT_TRUE(hasCastExtern);
}

TEST(PascalAsTest, AsWithInheritanceCompiles)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TBase = class
  public
    X: Integer;
  end;
  TDerived = class(TBase)
  public
    Y: Integer;
  end;
var
  b: TBase;
  d: TDerived?;
begin
  b := TDerived.Create;
  d := b as TDerived;
  if d <> nil then
    d.Y := 42
end.
)";
    PascalCompilerInput input{.source = source, .path = "test_as_inherit.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

TEST(PascalAsTest, IsAsComboCompiles)
{
    // Test using 'is' to check before 'as' cast
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TAnimal = class
  public
    procedure Speak; virtual;
  end;
  TDog = class(TAnimal)
  public
    procedure Speak; override;
  end;
procedure TAnimal.Speak; begin end;
procedure TDog.Speak; begin WriteLn('Woof!') end;
var
  a: TAnimal;
  d: TDog;
begin
  a := TDog.Create;
  if a is TDog then
  begin
    d := a as TDog;
    d.Speak
  end
end.
)";
    PascalCompilerInput input{.source = source, .path = "test_is_as_combo.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

TEST(PascalAsTest, AsWithNilCheckCompiles)
{
    // 'as' returns nil on failure; test nil check pattern with optional types
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TBase = class end;
  TChild1 = class(TBase) end;
  TChild2 = class(TBase) end;
var
  b: TBase;
  c1: TChild1?;
  c2: TChild2?;
begin
  b := TChild1.Create;
  c1 := b as TChild1;
  c2 := b as TChild2;
  if c1 <> nil then
    WriteLn('c1 is valid')
end.
)";
    PascalCompilerInput input{.source = source, .path = "test_as_nil_check.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

TEST(PascalIsAsTest, IsReturnsBooleanType)
{
    // 'is' should return Boolean type (usable in if/while/repeat conditions)
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TBase = class end;
  TChild = class(TBase) end;
var
  b: TBase;
  result: Boolean;
begin
  b := TChild.Create;
  result := b is TChild;
  while b is TBase do break;
  repeat until not (b is TChild)
end.
)";
    PascalCompilerInput input{.source = source, .path = "test_is_bool.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
