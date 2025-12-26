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

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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
#ifdef _WIN32
    // Windows x64 ABI: length in RDX (second arg)
    const char *lenReg = "%rdx";
#else
    // SysV ABI: length in RSI (second arg)
    const char *lenReg = "%rsi";
#endif
    std::size_t regPos = asmText.find(lenReg);
    while (regPos != std::string::npos)
    {
        const std::string_view line = lineContaining(asmText, regPos);
        if (line.find("mov") != std::string::npos &&
            (line.find("$13") != std::string::npos || line.find("$0xd") != std::string::npos))
        {
            hasLenMove = true;
            break;
        }
        regPos = asmText.find(lenReg, regPos + 4);
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
