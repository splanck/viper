//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_shifts.cpp
// Purpose: Ensure x86-64 codegen lowers IL shift instructions to the
// Key invariants: Generated assembly must contain shl with an immediate
// Ownership/Lifetime: Tests build IL modules locally and inspect emitted
// Links: src/codegen/x86_64/LowerILToMIR.cpp,
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <string>

#if __has_include(<gtest/gtest.h>)
#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#include <cstdlib>
#include <iostream>
#define VIPER_HAS_GTEST 0
#endif

namespace viper::codegen::x64
{
namespace
{
[[nodiscard]] ILValue makeI64Param(int id) noexcept
{
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeI64Const(int64_t val) noexcept
{
    ILValue constant{};
    constant.kind = ILValue::Kind::I64;
    constant.id = -1;
    constant.i64 = val;
    return constant;
}

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept
{
    ILValue ref{};
    ref.kind = kind;
    ref.id = id;
    return ref;
}

[[nodiscard]] ILModule makeShiftModule()
{
    ILValue x = makeI64Param(0);
    ILValue s = makeI64Param(1);

    ILInstr shlInstr{};
    shlInstr.opcode = "shl";
    shlInstr.resultId = 2;
    shlInstr.resultKind = ILValue::Kind::I64;
    shlInstr.ops = {x, makeI64Const(3)};

    ILInstr ashrInstr{};
    ashrInstr.opcode = "ashr";
    ashrInstr.resultId = 3;
    ashrInstr.resultKind = ILValue::Kind::I64;
    ashrInstr.ops = {x, s};

    ILInstr lshrInstr{};
    lshrInstr.opcode = "lshr";
    lshrInstr.resultId = 4;
    lshrInstr.resultKind = ILValue::Kind::I64;
    lshrInstr.ops = {x, s};

    ILInstr addInstr{};
    addInstr.opcode = "add";
    addInstr.resultId = 5;
    addInstr.resultKind = ILValue::Kind::I64;
    addInstr.ops = {makeValueRef(shlInstr.resultId, ILValue::Kind::I64),
                    makeValueRef(ashrInstr.resultId, ILValue::Kind::I64)};

    ILInstr finalAddInstr{};
    finalAddInstr.opcode = "add";
    finalAddInstr.resultId = 6;
    finalAddInstr.resultKind = ILValue::Kind::I64;
    finalAddInstr.ops = {makeValueRef(addInstr.resultId, ILValue::Kind::I64),
                         makeValueRef(lshrInstr.resultId, ILValue::Kind::I64)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(finalAddInstr.resultId, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {x.id, s.id};
    entry.paramKinds = {x.kind, s.kind};
    entry.instrs = {shlInstr, ashrInstr, lshrInstr, addInstr, finalAddInstr, retInstr};

    ILFunction func{};
    func.name = "shift";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64ShiftTest, EmitsImmediateAndClBasedShifts)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeShiftModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;
    EXPECT_NE(std::string::npos, result.asmText.find("shlq $3, ")) << result.asmText;
    EXPECT_NE(std::string::npos, result.asmText.find("sarq %cl, ")) << result.asmText;
    EXPECT_NE(std::string::npos, result.asmText.find("shrq %cl, ")) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeShiftModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }

    if (result.asmText.find("shlq $3, ") == std::string::npos ||
        result.asmText.find("sarq %cl, ") == std::string::npos ||
        result.asmText.find("shrq %cl, ") == std::string::npos)
    {
        std::cerr << "Unexpected shift assembly:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
