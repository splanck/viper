//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_udiv_urem.cpp
// Purpose: Validate unsigned 64-bit division/remainder lowering expands into the
// Key invariants: Emitted assembly must zero-extend the dividend via XOR on
// Ownership/Lifetime: Test constructs IL adapter modules locally and inspects
// Links: src/codegen/x86_64/LowerDiv.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <sstream>
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

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept
{
    ILValue ref{};
    ref.kind = kind;
    ref.id = id;
    return ref;
}

[[nodiscard]] ILModule makeUnsignedDivRemModule()
{
    ILValue dividend = makeI64Param(0);
    ILValue divisor = makeI64Param(1);

    ILInstr udivInstr{};
    udivInstr.opcode = "udiv";
    udivInstr.resultId = 2;
    udivInstr.resultKind = ILValue::Kind::I64;
    udivInstr.ops = {dividend, divisor};

    ILInstr uremInstr{};
    uremInstr.opcode = "urem";
    uremInstr.resultId = 3;
    uremInstr.resultKind = ILValue::Kind::I64;
    uremInstr.ops = {dividend, divisor};

    ILInstr xorInstr{};
    xorInstr.opcode = "xor";
    xorInstr.resultId = 4;
    xorInstr.resultKind = ILValue::Kind::I64;
    xorInstr.ops = {makeValueRef(udivInstr.resultId, ILValue::Kind::I64),
                    makeValueRef(uremInstr.resultId, ILValue::Kind::I64)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(xorInstr.resultId, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {dividend.id, divisor.id};
    entry.paramKinds = {dividend.kind, divisor.kind};
    entry.instrs = {udivInstr, uremInstr, xorInstr, retInstr};

    ILFunction func{};
    func.name = "udiv_urem";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasEdxZeroExtend(const std::string &asmText)
{
    std::istringstream stream{asmText};
    std::string line{};
    while (std::getline(stream, line))
    {
        if (line.find("xor") == std::string::npos)
        {
            continue;
        }

        const auto edxFirst = line.find("%edx");
        if (edxFirst != std::string::npos)
        {
            if (line.find("%edx", edxFirst + 4) != std::string::npos)
            {
                return true;
            }
        }

        const auto rdxFirst = line.find("%rdx");
        if (rdxFirst != std::string::npos && line.find("%rdx", rdxFirst + 4) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasDivqInstruction(const std::string &asmText)
{
    return asmText.find("divq") != std::string::npos;
}

[[nodiscard]] bool hasTrapGuard(const std::string &asmText)
{
    bool hasTest = false;
    bool hasJeTrap = false;
    bool hasTrapCall = asmText.find("rt_trap_div0") != std::string::npos;

    std::istringstream stream{asmText};
    std::string line{};
    while (std::getline(stream, line))
    {
        if (!hasTest && line.find("test") != std::string::npos)
        {
            hasTest = true;
        }
        if (!hasJeTrap && line.find("je") != std::string::npos &&
            line.find(".Ltrap_div0") != std::string::npos)
        {
            hasJeTrap = true;
        }
    }

    return hasTest && hasJeTrap && hasTrapCall;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64UnsignedDivRemTest, EmitsGuardedUnsignedDivSequence)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeUnsignedDivRemModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;

    EXPECT_TRUE(hasEdxZeroExtend(result.asmText)) << result.asmText;
    EXPECT_TRUE(hasDivqInstruction(result.asmText)) << result.asmText;
    EXPECT_TRUE(hasTrapGuard(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeUnsignedDivRemModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }

    if (!hasEdxZeroExtend(result.asmText) || !hasDivqInstruction(result.asmText) ||
        !hasTrapGuard(result.asmText))
    {
        std::cerr << "Unsigned division lowering missing expected pattern:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
