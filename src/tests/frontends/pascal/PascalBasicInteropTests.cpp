//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalBasicInteropTests.cpp
// Purpose: Tests for Pascal-BASIC OOP interoperability via common IL ABI.
// Key invariants: Both frontends generate IL using the same runtime calls
// and object layout conventions, enabling cross-language object sharing.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "frontends/pascal/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../unit/GTestStub.hpp"
#endif

using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/// @brief Check if any function in the module calls a specific runtime function.
bool moduleCallsRuntime(const il::core::Module &mod, const std::string &callee)
{
    for (const auto &fn : mod.functions)
    {
        for (const auto &blk : fn.blocks)
        {
            for (const auto &instr : blk.instructions)
            {
                if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                    return true;
            }
        }
    }
    return false;
}

/// @brief Find a function by name in a module.
const il::core::Function *findFunction(const il::core::Module &mod, const std::string &name)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == name)
            return &fn;
    }
    return nullptr;
}

/// @brief Check if a function uses CallIndirect (for vtable dispatch).
bool hasIndirectCall(const il::core::Function &fn)
{
    for (const auto &blk : fn.blocks)
    {
        for (const auto &instr : blk.instructions)
        {
            if (instr.op == il::core::Opcode::CallIndirect)
                return true;
        }
    }
    return false;
}

//===----------------------------------------------------------------------===//
// Runtime ABI Compatibility Tests
// Both Pascal and BASIC must use the same runtime calls for OOP operations.
//===----------------------------------------------------------------------===//

TEST(PascalBasicInterop, BothUseSameAllocationRuntime)
{
    // Pascal class with constructor
    SourceManager smPas;
    const std::string pasSrc =
        "program Test; type TFoo = class public X: Integer; constructor Create; end; "
        "constructor TFoo.Create; begin X := 0 end; "
        "var f: TFoo; begin f := TFoo.Create end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "interop.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC class with constructor
    SourceManager smBas;
    const std::string basSrc = "CLASS TBar\n"
                               "  PUBLIC X AS INTEGER\n"
                               "  PUBLIC SUB New()\n"
                               "    X = 0\n"
                               "  END SUB\n"
                               "END CLASS\n"
                               "DIM b AS TBar = NEW TBar()\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "interop.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Both must use rt_obj_new_i64 for allocation
    EXPECT_TRUE(moduleCallsRuntime(pasResult.module, "rt_obj_new_i64"));
    EXPECT_TRUE(moduleCallsRuntime(basResult.module, "rt_obj_new_i64"));
}

TEST(PascalBasicInterop, BothUseSameClassRegistration)
{
    // Pascal class
    SourceManager smPas;
    const std::string pasSrc = "program Test; type TFoo = class public X: Integer; end; begin end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "reg.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC class
    SourceManager smBas;
    const std::string basSrc = "CLASS TBar\n"
                               "  PUBLIC X AS INTEGER\n"
                               "END CLASS\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "reg.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Both must use rt_register_class_with_base_rs for class registration
    EXPECT_TRUE(moduleCallsRuntime(pasResult.module, "rt_register_class_with_base_rs"));
    EXPECT_TRUE(moduleCallsRuntime(basResult.module, "rt_register_class_with_base_rs"));
}

TEST(PascalBasicInterop, BothUseSameVtableAccess)
{
    // Pascal class
    SourceManager smPas;
    const std::string pasSrc =
        "program Test; type TFoo = class public X: Integer; constructor Create; end; "
        "constructor TFoo.Create; begin X := 0 end; "
        "var f: TFoo; begin f := TFoo.Create end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "vtable.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC class
    SourceManager smBas;
    const std::string basSrc = "CLASS TBar\n"
                               "  PUBLIC X AS INTEGER\n"
                               "  PUBLIC SUB New()\n"
                               "    X = 0\n"
                               "  END SUB\n"
                               "END CLASS\n"
                               "DIM b AS TBar = NEW TBar()\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "vtable.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Both must use rt_get_class_vtable for vtable initialization
    EXPECT_TRUE(moduleCallsRuntime(pasResult.module, "rt_get_class_vtable"));
    EXPECT_TRUE(moduleCallsRuntime(basResult.module, "rt_get_class_vtable"));
}

//===----------------------------------------------------------------------===//
// Virtual Dispatch Compatibility Tests
// Both languages must use the same virtual dispatch mechanism.
//===----------------------------------------------------------------------===//

TEST(PascalBasicInterop, BothUseIndirectCallForVirtual)
{
    // Pascal with virtual method called through base type
    SourceManager smPas;
    const std::string pasSrc =
        "program Test; type TBase = class public procedure Speak; virtual; end; "
        "TDog = class(TBase) public procedure Speak; override; end; "
        "procedure TBase.Speak; begin end; "
        "procedure TDog.Speak; begin end; "
        "var a: TBase; begin a := TDog.Create; a.Speak end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "virtual.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC with virtual method called through base type
    SourceManager smBas;
    const std::string basSrc = "CLASS TBase\n"
                               "  VIRTUAL SUB Speak()\n"
                               "  END SUB\n"
                               "END CLASS\n"
                               "CLASS TDog : TBase\n"
                               "  OVERRIDE SUB Speak()\n"
                               "  END SUB\n"
                               "END CLASS\n"
                               "DIM a AS TBase\n"
                               "LET a = NEW TDog()\n"
                               "a.Speak()\n"
                               "END\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "virtual.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Main functions in both should use indirect calls for virtual dispatch
    const auto *pasMain = findFunction(pasResult.module, "main");
    const auto *basMain = findFunction(basResult.module, "main");

    ASSERT_NE(pasMain, nullptr);
    ASSERT_NE(basMain, nullptr);

    EXPECT_TRUE(hasIndirectCall(*pasMain));
    EXPECT_TRUE(hasIndirectCall(*basMain));
}

//===----------------------------------------------------------------------===//
// Method Naming Convention Tests
// Both languages should generate compatible method names for cross-calls.
//===----------------------------------------------------------------------===//

TEST(PascalBasicInterop, MethodNamingConvention)
{
    // Pascal method naming: ClassName.MethodName (original case)
    SourceManager smPas;
    const std::string pasSrc = "program Test; type TFoo = class public procedure DoWork; end; "
                               "procedure TFoo.DoWork; begin end; begin end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "naming.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC method naming: CLASSNAME.METHODNAME (uppercase)
    SourceManager smBas;
    const std::string basSrc = "CLASS TBar\n"
                               "  SUB DoWork()\n"
                               "  END SUB\n"
                               "END CLASS\n"
                               "END\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "naming.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Pascal uses ClassName.MethodName (case-preserved)
    const auto *pasFn = findFunction(pasResult.module, "TFoo.DoWork");
    EXPECT_NE(pasFn, nullptr);

    // BASIC uses CLASSNAME.METHODNAME (uppercase)
    const auto *basFn = findFunction(basResult.module, "TBAR.DOWORK");
    EXPECT_NE(basFn, nullptr);
}

TEST(PascalBasicInterop, ConstructorNamingConvention)
{
    // Pascal constructor: ClassName.CtorName (case-preserved)
    SourceManager smPas;
    const std::string pasSrc = "program Test; type TFoo = class public constructor Create; end; "
                               "constructor TFoo.Create; begin end; begin end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "ctor.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC constructor: CLASSNAME.__ctor (uppercase, __ctor suffix)
    SourceManager smBas;
    const std::string basSrc = "CLASS TBar\n"
                               "  SUB New()\n"
                               "  END SUB\n"
                               "END CLASS\n"
                               "END\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "ctor.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Pascal uses ClassName.CtorName for constructors
    const auto *pasFn = findFunction(pasResult.module, "TFoo.Create");
    EXPECT_NE(pasFn, nullptr);

    // BASIC uses CLASSNAME.__ctor for constructors
    const auto *basFn = findFunction(basResult.module, "TBAR.__ctor");
    EXPECT_NE(basFn, nullptr);
}

//===----------------------------------------------------------------------===//
// RTTI Interoperability Tests
// Both languages must use the same runtime helpers for 'is' and 'as'.
//===----------------------------------------------------------------------===//

TEST(PascalBasicInterop, BothUseSameRTTIRuntimeForIs)
{
    // Pascal using 'is' operator
    SourceManager smPas;
    const std::string pasSrc =
        "program Test; type TBase = class end; TChild = class(TBase) end; "
        "var b: TBase; r: Boolean; begin b := TChild.Create; r := b is TChild end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "is.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC using IS operator
    SourceManager smBas;
    const std::string basSrc = "CLASS TBase\nEND CLASS\n"
                               "CLASS TChild : TBase\nEND CLASS\n"
                               "DIM b AS TBase = NEW TChild()\n"
                               "DIM r AS BOOLEAN = b IS TChild\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "is.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Both must use rt_cast_as for type checking (Pascal implementation)
    // or rt_typeid_of + rt_type_is_a (BASIC implementation)
    bool pasUsesRtCast = moduleCallsRuntime(pasResult.module, "rt_cast_as");
    bool basUsesTypeOf = moduleCallsRuntime(basResult.module, "rt_typeid_of");
    bool basUsesIsA = moduleCallsRuntime(basResult.module, "rt_type_is_a");

    // Pascal uses rt_cast_as for 'is'
    EXPECT_TRUE(pasUsesRtCast);
    // BASIC uses rt_typeid_of + rt_type_is_a
    EXPECT_TRUE(basUsesTypeOf);
    EXPECT_TRUE(basUsesIsA);
}

TEST(PascalBasicInterop, BothUseSameRTTIRuntimeForAs)
{
    // Pascal using 'as' operator
    SourceManager smPas;
    const std::string pasSrc =
        "program Test; type TBase = class end; TChild = class(TBase) end; "
        "var b: TBase; c: TChild?; begin b := TChild.Create; c := b as TChild end.";
    il::frontends::pascal::PascalCompilerInput pasInput{.source = pasSrc, .path = "as.pas"};
    il::frontends::pascal::PascalCompilerOptions pasOpts{};
    auto pasResult = il::frontends::pascal::compilePascal(pasInput, pasOpts, smPas);

    // BASIC using AS operator
    SourceManager smBas;
    const std::string basSrc = "CLASS TBase\nEND CLASS\n"
                               "CLASS TChild : TBase\nEND CLASS\n"
                               "DIM b AS TBase = NEW TChild()\n"
                               "DIM c AS TChild = b AS TChild\n";
    il::frontends::basic::BasicCompilerInput basInput{.source = basSrc, .path = "as.bas"};
    il::frontends::basic::BasicCompilerOptions basOpts{};
    auto basResult = il::frontends::basic::compileBasic(basInput, basOpts, smBas);

    ASSERT_TRUE(pasResult.succeeded());
    ASSERT_TRUE(basResult.succeeded());

    // Both must use rt_cast_as for safe downcast
    EXPECT_TRUE(moduleCallsRuntime(pasResult.module, "rt_cast_as"));
    EXPECT_TRUE(moduleCallsRuntime(basResult.module, "rt_cast_as"));
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
