// File: tests/codegen/x86_64/test_select.cpp
// Purpose: Verify select lowering emits canonical CMOV and branchy MOVSD
//          sequences for integer and floating-point selects respectively.
// Key invariants: Integer selects with SSA boolean conditions lower to a
//                 TEST/MOV/CMOVNE idiom, while floating-point selects lower to
//                 a small branch with MOVSD transfers across true/false paths.
// Ownership/Lifetime: Constructs IL modules locally and inspects the emitted
//                      assembly without external fixtures.
// Links: src/codegen/x86_64/LowerILToMIR.cpp, src/codegen/x86_64/ISel.cpp

#include "codegen/x86_64/Backend.hpp"

#include <string>

#if __has_include(<gtest/gtest.h>)
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

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept
{
    ILValue ref{};
    ref.kind = kind;
    ref.id = id;
    return ref;
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

[[nodiscard]] ILModule makeI64SelectModule()
{
    ILValue lhs = makeI64Param(0);
    ILValue rhs = makeI64Param(1);

    ILInstr cmpInstr{};
    cmpInstr.opcode = "cmp";
    cmpInstr.resultId = 2;
    cmpInstr.resultKind = ILValue::Kind::I1;
    cmpInstr.ops = {lhs, rhs};

    ILInstr selectInstr{};
    selectInstr.opcode = "select";
    selectInstr.resultId = 3;
    selectInstr.resultKind = ILValue::Kind::I64;
    selectInstr.ops = {
        makeValueRef(cmpInstr.resultId, ILValue::Kind::I1),
        makeI64Const(42),
        makeI64Const(7)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(selectInstr.resultId, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {cmpInstr, selectInstr, retInstr};

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

    ILInstr cmpInstr{};
    cmpInstr.opcode = "cmp";
    cmpInstr.resultId = 2;
    cmpInstr.resultKind = ILValue::Kind::I1;
    cmpInstr.ops = {lhs, rhs};

    ILInstr selectInstr{};
    selectInstr.opcode = "select";
    selectInstr.resultId = 3;
    selectInstr.resultKind = ILValue::Kind::F64;
    selectInstr.ops = {
        makeValueRef(cmpInstr.resultId, ILValue::Kind::I1),
        makeF64Const(42.0),
        makeF64Const(7.0)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(selectInstr.resultId, ILValue::Kind::F64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {cmpInstr, selectInstr, retInstr};

    ILFunction func{};
    func.name = "select_f64";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasGprSelectPattern(const std::string &asmText)
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

    const std::size_t cmovnePos = asmText.find("cmovne", movPos);
    return cmovnePos != std::string::npos;
}

[[nodiscard]] bool hasXmmSelectBranchPattern(const std::string &asmText)
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

    const std::size_t falseLabelPos = asmText.find(".Lfalse", jePos);
    if (falseLabelPos == std::string::npos)
    {
        return false;
    }

    const std::size_t firstMovsdPos = asmText.find("movsd", jePos);
    if (firstMovsdPos == std::string::npos || firstMovsdPos > falseLabelPos)
    {
        return false;
    }

    const std::size_t secondMovsdPos = asmText.find("movsd", falseLabelPos);
    if (secondMovsdPos == std::string::npos || secondMovsdPos <= falseLabelPos)
    {
        return false;
    }

    const std::size_t endLabelPos = asmText.find(".Lend", secondMovsdPos);
    if (endLabelPos == std::string::npos || secondMovsdPos > endLabelPos)
    {
        return false;
    }

    return true;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64SelectTest, LowersI64SelectToCmovPattern)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeI64SelectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    EXPECT_TRUE(result.errors.empty());
    EXPECT_TRUE(hasGprSelectPattern(result.asmText)) << result.asmText;
}

TEST(CodegenX64SelectTest, LowersF64SelectToBranchyMovsdPattern)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeF64SelectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    EXPECT_TRUE(result.errors.empty());
    EXPECT_TRUE(hasXmmSelectBranchPattern(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule i64Module = makeI64SelectModule();
    const CodegenResult i64Result = emitModuleToAssembly(i64Module, {});
    if (!i64Result.errors.empty() || !hasGprSelectPattern(i64Result.asmText))
    {
        std::cerr << "Unexpected integer select assembly:\n" << i64Result.asmText;
        return EXIT_FAILURE;
    }

    const ILModule f64Module = makeF64SelectModule();
    const CodegenResult f64Result = emitModuleToAssembly(f64Module, {});
    if (!f64Result.errors.empty() || !hasXmmSelectBranchPattern(f64Result.asmText))
    {
        std::cerr << "Unexpected floating select assembly:\n" << f64Result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
