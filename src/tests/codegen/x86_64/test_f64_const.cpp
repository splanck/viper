//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_f64_const.cpp
// Purpose: Ensure the x86-64 backend materialises f64 literals via the
// Key invariants: Generated assembly must include a .LC_f64_* label in the
// Ownership/Lifetime: The IL module is created within the test scope and the
// Links: src/codegen/x86_64/AsmEmitter.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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
