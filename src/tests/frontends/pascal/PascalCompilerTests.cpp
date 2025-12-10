//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the Pascal frontend skeleton.
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

#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

/// @brief Test that the Pascal compiler skeleton produces a valid module.
TEST(PascalCompilerTest, SkeletonProducesModule)
{
    SourceManager sm;
    const std::string source = "program Hello; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    // Should succeed (no errors)
    EXPECT_TRUE(result.succeeded());

    // Module should have @main function
    bool hasMain = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that the lowerer produces a main function with an entry block.
TEST(PascalCompilerTest, LowererProducesEntryBlock)
{
    SourceManager sm;
    const std::string source = "program Hello; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the main function has at least one basic block
    bool foundMainWithBlocks = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main" && !fn.blocks.empty())
        {
            foundMainWithBlocks = true;
            // Check that the first block is the entry block
            EXPECT_EQ(fn.blocks.front().label, "entry_0");
            break;
        }
    }
    EXPECT_TRUE(foundMainWithBlocks);
}

/// @brief Test that diagnostics engine reports no errors for valid (ignored) input.
TEST(PascalCompilerTest, NoDiagnosticsForValidInput)
{
    SourceManager sm;
    const std::string source = "program Test; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

/// @brief Test that WriteLn emits runtime calls.
TEST(PascalCompilerTest, WriteLnEmitsRuntimeCalls)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
begin
  WriteLn('Hello')
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the module has rt_print_str extern declaration
    bool hasExtern = false;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "rt_print_str")
        {
            hasExtern = true;
            break;
        }
    }
    EXPECT_TRUE(hasExtern);
}

/// @brief Test that math builtins compile successfully.
TEST(PascalCompilerTest, MathBuiltinsCompile)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
var x: Real;
begin
  x := Sqrt(16.0);
  x := Sin(0.5);
  x := Cos(0.5)
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

/// @brief Test that ordinal builtins (Pred, Succ) emit inline arithmetic.
TEST(PascalCompilerTest, OrdinalBuiltinsEmitArithmetic)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
var n: Integer;
begin
  n := Pred(10);
  n := Succ(n)
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the main function has arithmetic instructions
    // Pred/Succ emit ISubOvf/IAddOvf for integer overflow checking
    bool hasSubOrAdd = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::Sub || instr.op == il::core::Opcode::Add ||
                        instr.op == il::core::Opcode::ISubOvf ||
                        instr.op == il::core::Opcode::IAddOvf)
                    {
                        hasSubOrAdd = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(hasSubOrAdd);
}

//===----------------------------------------------------------------------===//
// OOP Lowering Tests
//===----------------------------------------------------------------------===//

/// @brief Test that a simple class declaration compiles and emits module init.
TEST(PascalCompilerTest, ClassDeclarationEmitsModuleInit)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TPoint = class
    X: Integer;
    Y: Integer;
  end;
begin
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Should have a __pas_oop_init function
    bool hasOopInit = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "__pas_oop_init")
        {
            hasOopInit = true;
            break;
        }
    }
    EXPECT_TRUE(hasOopInit);
}

/// @brief Test that constructor calls emit allocation and vtable init.
TEST(PascalCompilerTest, ConstructorCallEmitsAllocation)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TPoint = class
    X: Integer;
    Y: Integer;
    constructor Create;
  end;

constructor TPoint.Create;
begin
  X := 0;
  Y := 0
end;

var p: TPoint;
begin
  p := TPoint.Create
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Should have extern for rt_obj_new_i64 (allocation) or rt_alloc
    bool hasAllocExtern = false;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "rt_obj_new_i64" || ext.name == "rt_alloc")
        {
            hasAllocExtern = true;
            break;
        }
    }
    EXPECT_TRUE(hasAllocExtern);

    // Should have extern for rt_get_class_vtable (vtable lookup)
    bool hasVtableExtern = false;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "rt_get_class_vtable")
        {
            hasVtableExtern = true;
            break;
        }
    }
    EXPECT_TRUE(hasVtableExtern);
}

/// @brief Test that class methods compile and get mangled names.
TEST(PascalCompilerTest, MethodsGetMangledNames)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TCounter = class
    Value: Integer;
    procedure Increment;
    function GetValue: Integer;
  end;

procedure TCounter.Increment;
begin
  Value := Value + 1
end;

function TCounter.GetValue: Integer;
begin
  Result := Value
end;

begin
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that mangled method names exist
    bool hasIncrement = false;
    bool hasGetValue = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "TCounter.Increment")
        {
            hasIncrement = true;
        }
        if (fn.name == "TCounter.GetValue")
        {
            hasGetValue = true;
        }
    }
    EXPECT_TRUE(hasIncrement);
    EXPECT_TRUE(hasGetValue);
}

/// @brief Test virtual method dispatch emits call.indirect.
TEST(PascalCompilerTest, VirtualMethodDispatchEmitsIndirectCall)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TAnimal = class
    procedure Speak; virtual;
  end;

  TDog = class(TAnimal)
    procedure Speak; override;
  end;

procedure TAnimal.Speak;
begin
  WriteLn('Animal')
end;

procedure TDog.Speak;
begin
  WriteLn('Woof')
end;

var a: TAnimal;
begin
  a := TDog.Create;
  a.Speak
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // The main function should have a CallIndirect instruction for virtual dispatch
    bool hasCallIndirect = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::CallIndirect)
                    {
                        hasCallIndirect = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(hasCallIndirect);
}

/// @brief Test that class inheritance computes correct field offsets.
TEST(PascalCompilerTest, InheritedClassFieldOffsets)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TBase = class
    A: Integer;
  end;

  TDerived = class(TBase)
    B: Integer;
  end;

var d: TDerived;
begin
  d := TDerived.Create;
  d.A := 1;
  d.B := 2
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the main function has GEP instructions for field access
    // Fields A and B should be at different offsets
    int gepCount = 0;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::GEP)
                    {
                        gepCount++;
                    }
                }
            }
        }
    }
    // Expect at least 2 GEP instructions for field access (A and B)
    EXPECT_TRUE(gepCount >= 2);
}

/// @brief Field and method access inside methods resolves class fields and locals.
TEST(PascalCompilerTest, FieldAccessInMethodsCompiles)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TInner = class
  public
    Val: Integer;
    procedure IncVal;
  end;

  TOuter = class
  private
    Inner: TInner;
  public
    constructor Create;
    procedure Bump;
  end;

constructor TOuter.Create;
begin
  Inner := TInner.Create;
  Inner.Val := 1
end;

procedure TInner.IncVal;
begin
  Inc(Val)
end;

procedure TOuter.Bump;
var tmp: TInner;
begin
  Inner.IncVal;
  tmp := Inner;
  tmp.IncVal;
  self.Inner.IncVal
end;

begin
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    // Expect successful compilation with no undefined identifier errors
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

/// @brief Nested field access through class-typed locals and Self works in methods.
TEST(PascalCompilerTest, NestedFieldAccessThroughLocalsAndSelf)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TLeaf = class
  public
    N: Integer;
  end;

  TMid = class
  public
    Leaf: TLeaf;
  end;

  TRoot = class
  private
    M: TMid;
  public
    constructor Create;
    procedure Touch;
  end;

constructor TRoot.Create;
begin
  M := TMid.Create;
  M.Leaf := TLeaf.Create;
  M.Leaf.N := 0
end;

procedure TRoot.Touch;
var t: TMid;
begin
  t := M;
  t.Leaf.N := 1;
  self.M.Leaf.N := 2
end;

begin
end.
)";
    PascalCompilerInput input{.source = source, .path = "test2.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
