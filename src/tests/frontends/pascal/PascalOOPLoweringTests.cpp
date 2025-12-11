//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalOOPLoweringTests.cpp
// Purpose: Tests for Pascal OOP IL lowering (vtables, allocation, dispatch).
// Key invariants: Generated IL matches BASIC OOP runtime ABI for interop.
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

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/// @brief Find a function in the module by name.
const il::core::Function *findFunction(const il::core::Module &mod, const std::string &name)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == name)
            return &fn;
    }
    return nullptr;
}

/// @brief Check if module contains a global with a specific string value.
bool hasGlobalWithValue(const il::core::Module &mod, const std::string &value)
{
    for (const auto &global : mod.globals)
    {
        if (global.init == value)
            return true;
    }
    return false;
}

/// @brief Check if a function calls a specific callee.
bool functionCalls(const il::core::Function &fn, const std::string &callee)
{
    for (const auto &blk : fn.blocks)
    {
        for (const auto &instr : blk.instructions)
        {
            if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                return true;
        }
    }
    return false;
}

/// @brief Count how many times a function calls a specific callee.
size_t countCalls(const il::core::Function &fn, const std::string &callee)
{
    size_t count = 0;
    for (const auto &blk : fn.blocks)
    {
        for (const auto &instr : blk.instructions)
        {
            if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                count++;
        }
    }
    return count;
}

/// @brief Check if a function contains an indirect call (CallIndirect opcode).
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

/// @brief Count GEP instructions in a function.
size_t countGepInstructions(const il::core::Function &fn)
{
    size_t count = 0;
    for (const auto &blk : fn.blocks)
    {
        for (const auto &instr : blk.instructions)
        {
            if (instr.op == il::core::Opcode::GEP)
                count++;
        }
    }
    return count;
}

//===----------------------------------------------------------------------===//
// Module Init / Class Registration Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, GeneratesModuleInitFunction)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; end; begin end.";
    PascalCompilerInput input{.source = src, .path = "test_init.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Should generate __pas_oop_init function for class registration
    const auto *initFn = findFunction(result.module, "__pas_oop_init");
    EXPECT_NE(initFn, nullptr);
}

TEST(PascalOOPLowering, ClassRegistrationCallsRuntime)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; end; begin end.";
    PascalCompilerInput input{.source = src, .path = "test_reg.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    const auto *initFn = findFunction(result.module, "__pas_oop_init");
    ASSERT_NE(initFn, nullptr);

    // Should call rt_register_class_with_base_rs for class registration
    EXPECT_TRUE(functionCalls(*initFn, "rt_register_class_with_base_rs"));
}

TEST(PascalOOPLowering, VtableGlobalCreated)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; end; begin end.";
    PascalCompilerInput input{.source = src, .path = "test_vtable.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Should have the class name as a global string
    EXPECT_TRUE(hasGlobalWithValue(result.module, "TFoo"));
}

//===----------------------------------------------------------------------===//
// Object Allocation Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, ConstructorCallAllocatesObject)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; constructor Create; end; "
        "constructor TFoo.Create; begin X := 42 end; "
        "var f: TFoo; begin f := TFoo.Create end.";
    PascalCompilerInput input{.source = src, .path = "test_alloc.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Main function should call rt_obj_new_i64 for allocation
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_NE(mainFn, nullptr);

    EXPECT_TRUE(functionCalls(*mainFn, "rt_obj_new_i64"));
}

TEST(PascalOOPLowering, ConstructorSetsVtable)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; constructor Create; end; "
        "constructor TFoo.Create; begin X := 42 end; "
        "var f: TFoo; begin f := TFoo.Create end.";
    PascalCompilerInput input{.source = src, .path = "test_vtable_init.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Main function should call rt_get_class_vtable to get vtable pointer
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_NE(mainFn, nullptr);

    EXPECT_TRUE(functionCalls(*mainFn, "rt_get_class_vtable"));
}

//===----------------------------------------------------------------------===//
// Method Lowering Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, NonVirtualMethodDirectCall)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; "
        "procedure DoWork; end; "
        "procedure TFoo.DoWork; begin X := 1 end; "
        "var f: TFoo; begin f := TFoo.Create; f.DoWork end.";
    PascalCompilerInput input{.source = src, .path = "test_direct.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Main should call TFoo.DoWork directly (non-virtual)
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_NE(mainFn, nullptr);

    EXPECT_TRUE(functionCalls(*mainFn, "TFoo.DoWork"));
}

TEST(PascalOOPLowering, VirtualMethodUsesVtable)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TBase = class public "
        "procedure Speak; virtual; end; "
        "TDog = class(TBase) public procedure Speak; override; end; "
        "procedure TBase.Speak; begin WriteLn('base') end; "
        "procedure TDog.Speak; begin WriteLn('dog') end; "
        "var a: TBase; begin a := TDog.Create; a.Speak end.";
    PascalCompilerInput input{.source = src, .path = "test_virtual.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Main should use indirect call (CallI) for virtual dispatch
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_NE(mainFn, nullptr);

    EXPECT_TRUE(hasIndirectCall(*mainFn));
}

TEST(PascalOOPLowering, MethodReceiverIsSelf)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; "
        "procedure SetX(V: Integer); end; "
        "procedure TFoo.SetX(V: Integer); begin Self.X := V end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "test_self.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Method should have 'self' as first parameter (implicitly)
    const auto *setXFn = findFunction(result.module, "TFoo.SetX");
    ASSERT_NE(setXFn, nullptr);

    // Method should have at least 2 params: self and V
    EXPECT_TRUE(setXFn->params.size() >= 2u);
}

//===----------------------------------------------------------------------===//
// Inheritance Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, DerivedClassIncludesBaseFields)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TBase = class public X: Integer; end; "
        "TDerived = class(TBase) public Y: Integer; end; "
        "var d: TDerived; begin d := TDerived.Create; d.X := 1; d.Y := 2 end.";
    PascalCompilerInput input{.source = src, .path = "test_inherit.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Should compile without errors - fields accessible
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

TEST(PascalOOPLowering, InheritedCallUsesBaseMethod)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TBase = class public procedure Speak; virtual; end; "
        "TDog = class(TBase) public procedure Speak; override; end; "
        "procedure TBase.Speak; begin WriteLn('base') end; "
        "procedure TDog.Speak; begin inherited; WriteLn('dog') end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "test_inherited.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // TDog.Speak should directly call TBase.Speak
    const auto *speakFn = findFunction(result.module, "TDog.Speak");
    ASSERT_NE(speakFn, nullptr);

    EXPECT_TRUE(functionCalls(*speakFn, "TBase.Speak"));
}

TEST(PascalOOPLowering, MultiLevelInheritance)
{
    SourceManager sm;
    const std::string src =
        "program Test; "
        "type TGrandparent = class public X: Integer; end; "
        "TParent = class(TGrandparent) public Y: Integer; end; "
        "TChild = class(TParent) public Z: Integer; end; "
        "var c: TChild; begin c := TChild.Create; c.X := 1; c.Y := 2; c.Z := 3 end.";
    PascalCompilerInput input{.source = src, .path = "test_multi_inherit.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Interface Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, InterfaceImplementationCompiles)
{
    SourceManager sm;
    const std::string src =
        "program Test; "
        "type IGreeter = interface procedure Greet; end; "
        "TFriendly = class(IGreeter) public procedure Greet; end; "
        "procedure TFriendly.Greet; begin WriteLn('Hello') end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "test_iface.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

TEST(PascalOOPLowering, InterfaceRegistration)
{
    SourceManager sm;
    const std::string src =
        "program Test; "
        "type IGreeter = interface procedure Greet; end; "
        "TFriendly = class(IGreeter) public procedure Greet; end; "
        "procedure TFriendly.Greet; begin WriteLn('Hello') end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "test_iface_reg.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    const auto *initFn = findFunction(result.module, "__pas_oop_init");
    ASSERT_NE(initFn, nullptr);

    // Should call rt_register_interface_impl for interface implementation
    EXPECT_TRUE(functionCalls(*initFn, "rt_register_interface_impl"));
}

//===----------------------------------------------------------------------===//
// Constructor/Destructor Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, ConstructorChainingSameClass)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TDog = class public Name: String; Age: Integer; "
        "constructor CreateDefault; constructor CreateNamed(AName: String); end; "
        "constructor TDog.CreateDefault; begin CreateNamed('Dog'); Age := 1 end; "
        "constructor TDog.CreateNamed(AName: String); begin Name := AName end; "
        "var d: TDog; begin d := TDog.CreateDefault end.";
    PascalCompilerInput input{.source = src, .path = "test_ctor_chain.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // CreateDefault should call CreateNamed (no allocation inside delegating ctor)
    const auto *ctorFn = findFunction(result.module, "TDog.CreateDefault");
    ASSERT_NE(ctorFn, nullptr);

    EXPECT_TRUE(functionCalls(*ctorFn, "TDog.CreateNamed"));
    EXPECT_FALSE(functionCalls(*ctorFn, "rt_obj_new_i64"));
}

TEST(PascalOOPLowering, DestructorLowering)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public destructor Destroy; virtual; end; "
        "destructor TFoo.Destroy; begin WriteLn('destroyed') end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "test_dtor.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Destructor should be lowered as TFoo.Destroy
    const auto *dtorFn = findFunction(result.module, "TFoo.Destroy");
    EXPECT_NE(dtorFn, nullptr);
}

TEST(PascalOOPLowering, DestructorChaining)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TBase = class public destructor Destroy; virtual; end; "
        "TChild = class(TBase) public destructor Destroy; override; end; "
        "destructor TBase.Destroy; begin WriteLn('Base') end; "
        "destructor TChild.Destroy; begin WriteLn('Child'); inherited Destroy end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "test_dtor_chain.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // TChild.Destroy should call TBase.Destroy
    const auto *childDtor = findFunction(result.module, "TChild.Destroy");
    ASSERT_NE(childDtor, nullptr);

    EXPECT_TRUE(functionCalls(*childDtor, "TBase.Destroy"));
}

//===----------------------------------------------------------------------===//
// Field Access Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, FieldAccessUsesGEP)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TPoint = class public X: Integer; Y: Integer; "
        "constructor Create(aX, aY: Integer); end; "
        "constructor TPoint.Create(aX, aY: Integer); begin X := aX; Y := aY end; "
        "var p: TPoint; begin p := TPoint.Create(10, 20) end.";
    PascalCompilerInput input{.source = src, .path = "test_field_gep.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Constructor should use GEP for field access
    const auto *ctorFn = findFunction(result.module, "TPoint.Create");
    ASSERT_NE(ctorFn, nullptr);

    size_t gepCount = countGepInstructions(*ctorFn);
    EXPECT_TRUE(gepCount > 0);
}

TEST(PascalOOPLowering, InheritedFieldAccess)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TAnimal = class public Name: String; end; "
        "TDog = class(TAnimal) public Breed: String; "
        "procedure Print; end; "
        "procedure TDog.Print; begin WriteLn(Name); WriteLn(Breed) end; "
        "begin end.";
    PascalCompilerInput input{.source = src, .path = "test_inherit_field.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // TDog.Print should have GEP instructions for both Name and Breed
    const auto *printFn = findFunction(result.module, "TDog.Print");
    ASSERT_NE(printFn, nullptr);

    size_t gepCount = countGepInstructions(*printFn);
    EXPECT_TRUE(gepCount >= 2u);
}

//===----------------------------------------------------------------------===//
// ABI Compatibility Tests (for BASIC interop)
//===----------------------------------------------------------------------===//

TEST(PascalOOPLowering, UsesCorrectRuntimeCalls)
{
    SourceManager sm;
    const std::string src =
        "program Test; type TFoo = class public X: Integer; constructor Create; end; "
        "constructor TFoo.Create; begin X := 0 end; "
        "var f: TFoo; begin f := TFoo.Create end.";
    PascalCompilerInput input{.source = src, .path = "test_rt_calls.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Verify runtime calls use the same names as BASIC OOP
    bool foundObjNew = false;
    bool foundGetVtable = false;
    bool foundRegisterClass = false;

    for (const auto &fn : result.module.functions)
    {
        for (const auto &blk : fn.blocks)
        {
            for (const auto &instr : blk.instructions)
            {
                if (instr.op == il::core::Opcode::Call)
                {
                    if (instr.callee == "rt_obj_new_i64")
                        foundObjNew = true;
                    if (instr.callee == "rt_get_class_vtable")
                        foundGetVtable = true;
                    if (instr.callee == "rt_register_class_with_base_rs")
                        foundRegisterClass = true;
                }
            }
        }
    }

    EXPECT_TRUE(foundObjNew);
    EXPECT_TRUE(foundGetVtable);
    EXPECT_TRUE(foundRegisterClass);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
