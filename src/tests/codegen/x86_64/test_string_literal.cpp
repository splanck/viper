//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_string_literal.cpp
// Purpose: Ensure the x86-64 backend materialises string literals via the
// Key invariants: Emitted assembly must include the literal's label, argument
// Ownership/Lifetime: Test builds the IL module locally and checks the emitted
// Links: src/codegen/x86_64/Backend.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <string>
#include <string_view>

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

[[nodiscard]] std::string_view lineContaining(const std::string &text, std::size_t pos) noexcept
{
    const std::size_t lineStart = text.rfind('\n', pos);
    const std::size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
    const std::size_t lineEnd = text.find('\n', pos);
    const std::size_t end = lineEnd == std::string::npos ? text.size() : lineEnd;
    return std::string_view{text}.substr(start, end - start);
}

[[nodiscard]] bool hasExpectedStringLiteralSequence(const std::string &asmText)
{
    const auto rodataPos = asmText.find(".section .rodata");
    if (rodataPos == std::string::npos)
    {
        return false;
    }

    const auto labelPos = asmText.find(".LC_str_", rodataPos);
    if (labelPos == std::string::npos)
    {
        return false;
    }

    const std::string_view rodataLabelLine = lineContaining(asmText, labelPos);
    if (rodataLabelLine.find(':') == std::string::npos)
    {
        return false;
    }

    bool hasLeaReference = false;
    std::size_t searchPos = 0;
    while ((searchPos = asmText.find(".LC_str_", searchPos)) != std::string::npos)
    {
        const std::string_view line = lineContaining(asmText, searchPos);
        if (line.find("lea") != std::string::npos)
        {
            hasLeaReference = true;
            break;
        }
        ++searchPos;
    }
    if (!hasLeaReference)
    {
        return false;
    }

    bool hasLenMove = false;
    std::size_t rsiPos = asmText.find("%rsi");
    while (rsiPos != std::string::npos)
    {
        const std::string_view line = lineContaining(asmText, rsiPos);
        if (line.find("mov") != std::string::npos &&
            (line.find("$13") != std::string::npos || line.find("$0xd") != std::string::npos))
        {
            hasLenMove = true;
            break;
        }
        rsiPos = asmText.find("%rsi", rsiPos + 4);
    }
    if (!hasLenMove)
    {
        return false;
    }

    const auto callPos = asmText.find("rt_str_from_lit");
    if (callPos == std::string::npos)
    {
        return false;
    }

    const std::string_view callLine = lineContaining(asmText, callPos);
    if (callLine.find("call") == std::string::npos)
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
