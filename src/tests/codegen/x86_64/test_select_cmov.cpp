//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_select_cmov.cpp
// Purpose: Ensure GPR selects lower to TEST/MOV/CMOV when targeting x86-64.
// Key invariants: The generated assembly must contain the cmovne idiom in the
// Ownership/Lifetime: Constructs IL locally and inspects emitted assembly by
// Links: src/codegen/x86_64/LowerILToMIR.cpp, src/codegen/x86_64/ISel.cpp
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
[[nodiscard]] ILValue makeParam(int id) noexcept
{
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeConst(int64_t val) noexcept
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

[[nodiscard]] ILModule makeSelectModule()
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
        makeValueRef(cmpInstr.resultId, ILValue::Kind::I1), makeConst(7), makeConst(0)};

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

[[nodiscard]] bool hasSelectSequence(const std::string &asmText)
{
    const std::size_t testPos = asmText.find("testq");
    if (testPos == std::string::npos)
    {
        return false;
    }

    const std::size_t cmovPos = asmText.find("cmovne", testPos);
    if (cmovPos == std::string::npos || cmovPos <= testPos)
    {
        return false;
    }

    // Ensure the false path is materialised between the test and cmov by
    // looking for either a zeroing XOR or an explicit MOV immediate.
    const std::size_t xorPos = asmText.find("xor", testPos);
    const std::size_t movPos = asmText.find("movq $", testPos);
    const bool hasFalseMaterialisation = (xorPos != std::string::npos && xorPos < cmovPos) ||
                                         (movPos != std::string::npos && movPos < cmovPos);
    return hasFalseMaterialisation;
}

} // namespace
} // namespace viper::codegen::x64

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeSelectModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty() || !hasSelectSequence(result.asmText))
    {
        std::cerr << "Unexpected assembly output:\n" << result.asmText;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
