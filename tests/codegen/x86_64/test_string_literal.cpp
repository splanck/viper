// File: tests/codegen/x86_64/test_string_literal.cpp
// Purpose: Ensure the x86-64 backend materialises string literals via the
//          runtime helper and emits matching .rodata entries.
// Key invariants: Emitted assembly must include the literal's label, argument
//                 setup for rt_str_from_lit, and the .rodata payload.
// Ownership/Lifetime: Test builds the IL module locally and checks the emitted
//                      assembly by value without touching external resources.
// Links: src/codegen/x86_64/Backend.cpp

#include "codegen/x86_64/Backend.hpp"

#include <string>
#include <string_view>

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
[[nodiscard]] ILModule makeStringLiteralModule()
{
    ILValue literal{};
    literal.kind = ILValue::Kind::STR;
    literal.str = "Hello, world!";
    literal.strLen = literal.str.size();

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {literal};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {ret};

    ILFunction func{};
    func.name = "greet";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasExpectedStringLiteralSequence(const std::string &asmText)
{
    const auto rodataPos = asmText.find(".section .rodata\n.LC_str_");
    if (rodataPos == std::string::npos)
    {
        return false;
    }

    const auto hasLea = asmText.find("leaq .LC_str_") != std::string::npos ||
                        asmText.find("lea .LC_str_") != std::string::npos;
    if (!hasLea)
    {
        return false;
    }

    const auto hasLenMove = asmText.find("movq $13, %rsi") != std::string::npos ||
                            asmText.find("mov $13, %rsi") != std::string::npos;
    if (!hasLenMove)
    {
        return false;
    }

    const auto hasRuntimeCall = asmText.find("callq rt_str_from_lit") != std::string::npos ||
                                asmText.find("call rt_str_from_lit") != std::string::npos;
    if (!hasRuntimeCall)
    {
        return false;
    }

    return true;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64StringLiteralTest, EmitsRuntimeCallAndRodata)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeStringLiteralModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    EXPECT_TRUE(result.errors.empty());
    EXPECT_TRUE(hasExpectedStringLiteralSequence(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeStringLiteralModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty() || !hasExpectedStringLiteralSequence(result.asmText))
    {
        std::cerr << "Unexpected assembly output:\n" << result.asmText;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
