// File: tests/codegen/x86_64/test_bitwise.cpp
// Purpose: Validate that x86-64 codegen lowers IL bitwise and/or/xor
//          sequences into immediate and register forms as expected.
// Key invariants: Generated assembly must contain an immediate-based and/or
//                 sequence followed by a register xor using adapter IL.
// Ownership/Lifetime: Test constructs IL modules locally and inspects emitted
//                      assembly strings without persistent resources.
// Links: src/codegen/x86_64/LowerILToMIR.cpp,
//        src/codegen/x86_64/AsmEmitter.cpp

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

[[nodiscard]] ILModule makeBitwiseModule()
{
    ILValue a = makeI64Param(0);
    ILValue b = makeI64Param(1);

    ILInstr andInstr{};
    andInstr.opcode = "and";
    andInstr.resultId = 2;
    andInstr.resultKind = ILValue::Kind::I64;
    andInstr.ops = {a, makeI64Const(0xFF00FF00)};

    ILInstr orInstr{};
    orInstr.opcode = "or";
    orInstr.resultId = 3;
    orInstr.resultKind = ILValue::Kind::I64;
    orInstr.ops = {makeValueRef(andInstr.resultId, ILValue::Kind::I64),
                   makeI64Const(0x100)};

    ILInstr xorInstr{};
    xorInstr.opcode = "xor";
    xorInstr.resultId = 4;
    xorInstr.resultKind = ILValue::Kind::I64;
    xorInstr.ops = {makeValueRef(orInstr.resultId, ILValue::Kind::I64), b};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(xorInstr.resultId, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {a.id, b.id};
    entry.paramKinds = {a.kind, b.kind};
    entry.instrs = {andInstr, orInstr, xorInstr, retInstr};

    ILFunction func{};
    func.name = "bitwise";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool containsImmediateAnd(const std::string &asmText)
{
    const bool hexMatch = asmText.find("andq $0xff00ff00") != std::string::npos ||
                           asmText.find("andq $0x00000000ff00ff00") != std::string::npos;
    const bool decMatch = asmText.find("andq $-16711936") != std::string::npos;
    return hexMatch || decMatch || asmText.find("andq $") != std::string::npos;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64BitwiseTest, EmitsBitwiseImmediateAndRegisterSequence)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeBitwiseModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;
    EXPECT_TRUE(containsImmediateAnd(result.asmText)) << result.asmText;
    EXPECT_NE(std::string::npos, result.asmText.find("orq $")) << result.asmText;
    EXPECT_NE(std::string::npos, result.asmText.find("xorq %")) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeBitwiseModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }

    if (!containsImmediateAnd(result.asmText) ||
        result.asmText.find("orq $") == std::string::npos ||
        result.asmText.find("xorq %") == std::string::npos)
    {
        std::cerr << "Unexpected bitwise assembly:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
