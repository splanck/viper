// File: tests/codegen/x86_64/test_f64_const.cpp
// Purpose: Ensure the x86-64 backend materialises f64 literals via the
//          read-only data pool and loads them using RIP-relative movsd.
// Key invariants: Generated assembly must include a .LC_f64_* label in the
//                 .rodata section and a movsd instruction that references that
//                 label from the .text section.
// Ownership/Lifetime: The IL module is created within the test scope and the
//                     resulting assembly is inspected by value only.
// Links: src/codegen/x86_64/AsmEmitter.cpp

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
[[nodiscard]] ILModule makePiLiteralModule()
{
    ILValue literal{};
    literal.kind = ILValue::Kind::F64;
    literal.id = -1;
    literal.f64 = 3.14159;

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {literal};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {ret};

    ILFunction func{};
    func.name = "const_pi";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool rodataContainsF64Label(const std::string &asmText)
{
    const std::size_t rodataPos = asmText.find(".section .rodata");
    if (rodataPos == std::string::npos)
    {
        return false;
    }
    const std::size_t labelPos = asmText.find(".LC_f64_", rodataPos);
    return labelPos != std::string::npos;
}

[[nodiscard]] bool textLoadsF64Literal(const std::string &asmText)
{
    const std::size_t textPos = asmText.find(".text");
    if (textPos == std::string::npos)
    {
        return false;
    }

    const std::size_t movsdPos = asmText.find("movsd", textPos);
    if (movsdPos == std::string::npos)
    {
        return false;
    }

    const std::size_t lineEnd = asmText.find('\n', movsdPos);
    const std::size_t lineStart = movsdPos;
    const std::size_t length =
        (lineEnd == std::string::npos) ? std::string::npos : lineEnd - lineStart;
    const std::string line = asmText.substr(lineStart, length);
    return line.find(".LC_f64_") != std::string::npos;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64F64ConstTest, EmitsRodataLiteral)
{
    using namespace viper::codegen::x64;

    const ILModule module = makePiLiteralModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.asmText;
    EXPECT_TRUE(rodataContainsF64Label(result.asmText)) << result.asmText;
}

TEST(CodegenX64F64ConstTest, LoadsLiteralViaMovsd)
{
    using namespace viper::codegen::x64;

    const ILModule module = makePiLiteralModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.asmText;
    EXPECT_TRUE(textLoadsF64Literal(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makePiLiteralModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << "Unexpected errors during codegen\n";
        return EXIT_FAILURE;
    }

    if (!rodataContainsF64Label(result.asmText) || !textLoadsF64Literal(result.asmText))
    {
        std::cerr << "Assembly missing expected f64 literal patterns:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
