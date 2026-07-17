//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

namespace zanna::codegen::x64 {

void lowerOverflowOps(MFunction &fn);

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
} // namespace zanna::codegen::x64

int main() {
    using namespace zanna::codegen::x64;

    const ILModule module = makeOverflowModule();
    CodegenOptions options{};
    options.targetPlatform = CodegenOptions::TargetPlatform::Linux;
    const CodegenResult result = emitModuleToAssembly(module, options);

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

    MFunction existingTrapNotLast{};
    existingTrapNotLast.name = "existing_trap_not_last";

    MBasicBlock entry{};
    entry.label = "entry";

    MBasicBlock trap{};
    trap.label = ".Ltrap_ovf_existing_trap_not_last";
    trap.append(MInstr::make(MOpcode::CALL, {makeLabelOperand("rt_trap_ovf")}));

    MBasicBlock continuation{};
    continuation.label = "continuation";
    continuation.append(MInstr::make(
        MOpcode::ADDOvfrr, {makeVRegOperand(RegClass::GPR, 1), makeVRegOperand(RegClass::GPR, 2)}));

    existingTrapNotLast.blocks = {entry, trap, continuation};
    lowerOverflowOps(existingTrapNotLast);

    bool sawPseudo = false;
    bool sawRealAdd = false;
    bool sawOverflowBranch = false;
    for (const auto &block : existingTrapNotLast.blocks) {
        for (const auto &instr : block.instructions) {
            sawPseudo = sawPseudo || instr.opcode == MOpcode::ADDOvfrr;
            sawRealAdd = sawRealAdd || instr.opcode == MOpcode::ADDrr;
            sawOverflowBranch = sawOverflowBranch || instr.opcode == MOpcode::JCC;
        }
    }

    if (sawPseudo || !sawRealAdd || !sawOverflowBranch) {
        std::cerr << "Overflow lowering failed when an existing trap block was not last\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
