//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_ovf_trap.cpp
// Purpose: Verify signed overflow lowering branches to a dedicated runtime trap
//          helper instead of calling rt_trap(const char*) without an argument.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace viper::codegen::x64 {
namespace {

[[nodiscard]] ILValue makeParam(int id) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeValueRef(int id) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILModule makeOverflowModule() {
    ILValue lhs = makeParam(0);
    ILValue rhs = makeParam(1);

    ILInstr addInstr{};
    addInstr.opcode = "iadd.ovf";
    addInstr.resultId = 2;
    addInstr.resultKind = ILValue::Kind::I64;
    addInstr.ops = {lhs, rhs};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(addInstr.resultId)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {addInstr, retInstr};

    ILFunction func{};
    func.name = "ovf_guard";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

} // namespace
} // namespace viper::codegen::x64

int main() {
    using namespace viper::codegen::x64;

    const ILModule module = makeOverflowModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty()) {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }

    if (result.asmText.find("jo .Ltrap_ovf_ovf_guard") == std::string::npos ||
        result.asmText.find("callq rt_trap_ovf") == std::string::npos ||
        result.asmText.find("callq rt_trap\n") != std::string::npos) {
        std::cerr << "Overflow lowering emitted an unexpected trap sequence:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
