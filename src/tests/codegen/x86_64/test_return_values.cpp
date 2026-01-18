//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_return_values.cpp
// Purpose: Ensure the x86-64 backend correctly copies return registers for
// Key invariants: The emitted assembly must contain a mov into the canonical
// Ownership/Lifetime: The IL module is constructed locally for the duration
// Links: src/codegen/x86_64/Backend.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>

namespace viper::codegen::x64
{
namespace
{
[[nodiscard]] ILValue makeParam(int id, ILValue::Kind kind) noexcept
{
    ILValue value{};
    value.kind = kind;
    value.id = id;
    return value;
}

[[nodiscard]] ILInstr makeReturnInstr(const ILValue &value)
{
    ILInstr instr{};
    instr.opcode = "ret";
    instr.ops = {value};
    return instr;
}

[[nodiscard]] ILModule makeReturnModule()
{
    ILValue i64Param = makeParam(0, ILValue::Kind::I64);

    ILBlock i64Entry{};
    i64Entry.name = "entry";
    i64Entry.paramIds = {i64Param.id};
    i64Entry.paramKinds = {i64Param.kind};
    i64Entry.instrs = {makeReturnInstr(i64Param)};

    ILFunction i64Func{};
    i64Func.name = "ret_i64";
    i64Func.blocks = {i64Entry};

    // For f64, return a constant (3.14159) instead of a parameter.
    // This ensures the movsd instruction is actually emitted and not optimized
    // away as an identity move (when returning the same XMM0 register it came in on).
    ILValue f64Const{};
    f64Const.kind = ILValue::Kind::F64;
    f64Const.id = -1; // immediate
    f64Const.f64 = 3.14159;

    ILBlock f64Entry{};
    f64Entry.name = "entry";
    f64Entry.instrs = {makeReturnInstr(f64Const)};

    ILFunction f64Func{};
    f64Func.name = "ret_f64";
    f64Func.blocks = {f64Entry};

    ILModule module{};
    module.funcs = {i64Func, f64Func};
    return module;
}

[[nodiscard]] bool hasMovRetSequence(std::string_view asmText, const std::regex &movPattern)
{
    std::cmatch match;
    const char *const begin = asmText.data();
    const char *const end = begin + asmText.size();

    if (!std::regex_search(begin, end, match, movPattern))
    {
        return false;
    }

    const std::size_t movEndOffset =
        static_cast<std::size_t>(match.position()) + static_cast<std::size_t>(match.length());
    const std::size_t retPos = asmText.find("ret", movEndOffset);
    return retPos != std::string_view::npos;
}

} // namespace
} // namespace viper::codegen::x64

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeReturnModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << "Unexpected errors during codegen\n";
        return EXIT_FAILURE;
    }

    const std::regex intPattern{"movq %[^,]+, %rax"};
    const std::regex floatPattern{"movsd [^,]+, %xmm0"};

    if (!hasMovRetSequence(result.asmText, intPattern) ||
        !hasMovRetSequence(result.asmText, floatPattern))
    {
        std::cerr << "Assembly missing expected return moves:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
