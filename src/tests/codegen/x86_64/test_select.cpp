//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_select.cpp
// Purpose: Assert x86-64 select lowering emits cmovne for integer selects and
// Key invariants: Generated assembly from the adapter IL must contain the
// Ownership/Lifetime: Tests build IL modules locally and inspect the emitted
// Links: src/codegen/x86_64/ISel.cpp, src/codegen/x86_64/LowerILToMIR.cpp
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

[[nodiscard]] ILValue makeF64Const(double val) noexcept
{
    ILValue constant{};
    constant.kind = ILValue::Kind::F64;
    constant.id = -1;
    constant.f64 = val;
    return constant;
}

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept
{
    ILValue ref{};
    ref.kind = kind;
    ref.id = id;
    return ref;
}

[[nodiscard]] ILModule makeI64SelectModule()
{
    ILValue lhs = makeI64Param(0);
    ILValue rhs = makeI64Param(1);

    ILInstr icmpInstr{};
    icmpInstr.opcode = "icmp_ne";
    icmpInstr.ops = {lhs, rhs};
    icmpInstr.resultId = 2;
    icmpInstr.resultKind = ILValue::Kind::I1;

    ILInstr selectInstr{};
    selectInstr.opcode = "select";
    selectInstr.resultId = 3;
    selectInstr.resultKind = ILValue::Kind::I64;
    selectInstr.ops = {
        makeValueRef(icmpInstr.resultId, ILValue::Kind::I1), makeI64Const(42), makeI64Const(7)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(selectInstr.resultId, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {icmpInstr, selectInstr, retInstr};

    ILFunction func{};
    func.name = "select_i64";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] ILModule makeF64SelectModule()
{
    ILValue lhs = makeI64Param(0);
    ILValue rhs = makeI64Param(1);

    ILInstr icmpInstr{};
    icmpInstr.opcode = "icmp_ne";
    icmpInstr.ops = {lhs, rhs};
    icmpInstr.resultId = 2;
    icmpInstr.resultKind = ILValue::Kind::I1;

    ILInstr selectInstr{};
    selectInstr.opcode = "select";
    selectInstr.resultId = 3;
    selectInstr.resultKind = ILValue::Kind::F64;
    selectInstr.ops = {
        makeValueRef(icmpInstr.resultId, ILValue::Kind::I1), makeF64Const(42.0), makeF64Const(7.0)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(selectInstr.resultId, ILValue::Kind::F64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {icmpInstr, selectInstr, retInstr};

    ILFunction func{};
    func.name = "select_f64";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasCmovPattern(const std::string &asmText)
{
    const std::size_t testPos = asmText.find("testq");
    if (testPos == std::string::npos)
    {
        return false;
    }

    const std::size_t movPos = asmText.find("movq", testPos);
    if (movPos == std::string::npos)
    {
        return false;
    }

    const std::size_t cmovPos = asmText.find("cmovne", movPos);
    return cmovPos != std::string::npos;
}

[[nodiscard]] bool hasBranchyMovsdPattern(const std::string &asmText)
{
    const std::size_t testPos = asmText.find("testq");
    if (testPos == std::string::npos)
    {
        return false;
    }

    const std::size_t jePos = asmText.find("je ", testPos);
    if (jePos == std::string::npos)
    {
        return false;
    }

    const std::size_t movsdTruePos = asmText.find("movsd", jePos);
    if (movsdTruePos == std::string::npos)
    {
        return false;
    }

    const std::size_t jmpPos = asmText.find("jmp", movsdTruePos);
    if (jmpPos == std::string::npos)
    {
        return false;
    }

    const std::size_t falseLabelPos = asmText.find(".Lfalse", jmpPos);
    if (falseLabelPos == std::string::npos)
    {
        return false;
    }

    const std::size_t movsdFalsePos = asmText.find("movsd", falseLabelPos);
    if (movsdFalsePos == std::string::npos)
    {
        return false;
    }

    const std::size_t endLabelPos = asmText.find(".Lend", movsdFalsePos);
    return endLabelPos != std::string::npos;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64SelectTest, EmitsTestMovCmovneSequence)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeI64SelectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;
    EXPECT_TRUE(hasCmovPattern(result.asmText)) << result.asmText;
}

TEST(CodegenX64SelectTest, EmitsBranchyMovsdSequence)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeF64SelectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;
    EXPECT_TRUE(hasBranchyMovsdPattern(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule intModule = makeI64SelectModule();
    const CodegenResult intResult = emitModuleToAssembly(intModule, {});
    if (!intResult.errors.empty() || !hasCmovPattern(intResult.asmText))
    {
        std::cerr << "Unexpected integer select assembly:\n" << intResult.asmText;
        return EXIT_FAILURE;
    }

    const ILModule floatModule = makeF64SelectModule();
    const CodegenResult floatResult = emitModuleToAssembly(floatModule, {});
    if (!floatResult.errors.empty() || !hasBranchyMovsdPattern(floatResult.asmText))
    {
        std::cerr << "Unexpected floating select assembly:\n" << floatResult.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
