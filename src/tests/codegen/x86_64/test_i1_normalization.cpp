//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_i1_normalization.cpp
// Purpose: Ensure boolean materialisation via SETcc is followed by a movzx and
// Key invariants: The generated assembly must include a SETcc, a movz* that
// Ownership/Lifetime: The test constructs IL objects locally and validates the
// Links: src/codegen/x86_64/ISel.cpp, src/codegen/x86_64/AsmEmitter.cpp
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
[[nodiscard]] ILValue makeParam(int id) noexcept
{
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeConstI64(int64_t value) noexcept
{
    ILValue constant{};
    constant.kind = ILValue::Kind::I64;
    constant.id = -1;
    constant.i64 = value;
    return constant;
}

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept
{
    ILValue ref{};
    ref.kind = kind;
    ref.id = id;
    return ref;
}

[[nodiscard]] ILModule makeCmpSelectModule()
{
    ILValue lhs = makeParam(0);
    ILValue rhs = makeParam(1);

    ILInstr cmpInstr{};
    cmpInstr.opcode = "cmp";
    cmpInstr.ops = {lhs, rhs};
    cmpInstr.resultId = 2;
    cmpInstr.resultKind = ILValue::Kind::I1;

    ILInstr selectInstr{};
    selectInstr.opcode = "select";
    selectInstr.resultId = 3;
    selectInstr.resultKind = ILValue::Kind::I64;
    selectInstr.ops = {
        makeValueRef(cmpInstr.resultId, ILValue::Kind::I1), makeConstI64(1), makeConstI64(0)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(selectInstr.resultId, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {cmpInstr, selectInstr, retInstr};

    ILFunction func{};
    func.name = "cmp_to_i64";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasBooleanNormalizationPattern(const std::string &asmText)
{
    const std::size_t setPos = asmText.find("set");
    if (setPos == std::string::npos)
    {
        return false;
    }
    const std::size_t movzPos = asmText.find("movz", setPos);
    if (movzPos == std::string::npos || movzPos <= setPos)
    {
        return false;
    }
    const std::size_t movqPos = asmText.find("movq", movzPos);
    if (movqPos == std::string::npos || movqPos <= movzPos)
    {
        return false;
    }
    const std::size_t raxPos = asmText.find("%rax", movqPos);
    if (raxPos == std::string::npos || raxPos <= movqPos)
    {
        return false;
    }
    return true;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64I1NormalizationTest, EmitsSetccMovzxAndWidenToRax)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeCmpSelectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;
    EXPECT_TRUE(hasBooleanNormalizationPattern(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeCmpSelectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }
    if (!hasBooleanNormalizationPattern(result.asmText))
    {
        std::cerr << "Unexpected assembly output:\n" << result.asmText;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
