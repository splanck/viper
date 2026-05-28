//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_i1_normalization.cpp
// Purpose: Ensure boolean materialisation via SETcc is followed by a movzx and
// Key invariants: The generated assembly must include a SETcc, a movz* that
// Ownership/Lifetime: The test constructs IL objects locally and validates the
// Links: src/codegen/x86_64/ISel.cpp, src/codegen/x86_64/AsmEmitter.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/OperandRoles.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viper::codegen::x64 {
namespace {
[[nodiscard]] ILValue makeParam(int id) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeConstI64(int64_t value) noexcept {
    ILValue constant{};
    constant.kind = ILValue::Kind::I64;
    constant.id = -1;
    constant.i64 = value;
    return constant;
}

[[nodiscard]] ILValue makeLabel(std::string label) noexcept {
    ILValue value{};
    value.kind = ILValue::Kind::LABEL;
    value.id = -1;
    value.label = std::move(label);
    return value;
}

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept {
    ILValue ref{};
    ref.kind = kind;
    ref.id = id;
    return ref;
}

[[nodiscard]] ILModule makeCmpSelectModule() {
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
        makeValueRef(cmpInstr.resultId, ILValue::Kind::I1), makeConstI64(1), makeConstI64(0)};

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    retInstr.ops = {makeValueRef(selectInstr.resultId, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {lhs.id, rhs.id};
    entry.paramKinds = {lhs.kind, rhs.kind};
    entry.instrs = {cmpInstr, selectInstr, retInstr};

    ILFunction func{};
    func.name = "cmp_to_i64";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] ILModule makeBranchArgBoolModule() {
    ILInstr cmpInstr{};
    cmpInstr.opcode = "icmp_ne";
    cmpInstr.ops = {makeConstI64(1), makeConstI64(2)};
    cmpInstr.resultId = 0;
    cmpInstr.resultKind = ILValue::Kind::I1;

    ILInstr branchInstr{};
    branchInstr.opcode = "cbr";
    branchInstr.ops = {
        makeValueRef(cmpInstr.resultId, ILValue::Kind::I1), makeLabel("taken"), makeLabel("merge")};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {cmpInstr, branchInstr};
    ILBlock::EdgeArg takenEdge{};
    takenEdge.to = "taken";
    ILBlock::EdgeArg mergeEdge{};
    mergeEdge.to = "merge";
    mergeEdge.argIds = {cmpInstr.resultId};
    entry.terminatorEdges = {takenEdge, mergeEdge};

    ILInstr takenRet{};
    takenRet.opcode = "ret";
    takenRet.ops = {makeConstI64(2)};

    ILBlock taken{};
    taken.name = "taken";
    taken.instrs = {takenRet};

    ILInstr mergeBranch{};
    mergeBranch.opcode = "cbr";
    mergeBranch.ops = {makeValueRef(1, ILValue::Kind::I1), makeLabel("fail"), makeLabel("ok")};

    ILBlock merge{};
    merge.name = "merge";
    merge.paramIds = {1};
    merge.paramKinds = {ILValue::Kind::I1};
    merge.instrs = {mergeBranch};

    ILInstr failRet{};
    failRet.opcode = "ret";
    failRet.ops = {makeConstI64(1)};

    ILBlock fail{};
    fail.name = "fail";
    fail.instrs = {failRet};

    ILInstr okRet{};
    okRet.opcode = "ret";
    okRet.ops = {makeConstI64(0)};

    ILBlock ok{};
    ok.name = "ok";
    ok.instrs = {okRet};

    ILFunction func{};
    func.name = "branch_arg_bool";
    func.blocks = {entry, taken, merge, fail, ok};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasBooleanNormalizationPattern(const std::string &asmText) {
    const std::size_t setPos = asmText.find("set");
    if (setPos == std::string::npos) {
        return false;
    }
    const std::size_t movzPos = asmText.find("movz", setPos);
    if (movzPos == std::string::npos || movzPos <= setPos) {
        return false;
    }
    const std::size_t movqPos = asmText.find("movq", movzPos);
    if (movqPos == std::string::npos || movqPos <= movzPos) {
        return false;
    }
    const std::size_t raxPos = asmText.find("%rax", movqPos);
    if (raxPos == std::string::npos || raxPos <= movqPos) {
        return false;
    }
    return true;
}

void collectVRegDefsAndUses(const MInstr &instr,
                            std::unordered_set<uint16_t> &defs,
                            std::vector<uint16_t> &uses) {
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        const auto &operand = instr.operands[idx];
        if (const auto *reg = std::get_if<OpReg>(&operand)) {
            if (reg->isPhys) {
                continue;
            }
            if (isDef) {
                defs.insert(reg->idOrPhys);
            }
            if (isUse) {
                uses.push_back(reg->idOrPhys);
            }
            continue;
        }

        const auto *mem = std::get_if<OpMem>(&operand);
        if (!mem || !isUse) {
            continue;
        }
        if (!mem->base.isPhys) {
            uses.push_back(mem->base.idOrPhys);
        }
        if (mem->hasIndex && !mem->index.isPhys) {
            uses.push_back(mem->index.idOrPhys);
        }
    }
}

[[nodiscard]] bool allVRegUsesAreDefined(const MFunction &func) {
    std::unordered_set<uint16_t> defs;
    std::vector<uint16_t> uses;
    for (const MBasicBlock &block : func.blocks) {
        for (const MInstr &instr : block.instructions) {
            collectVRegDefsAndUses(instr, defs, uses);
        }
    }
    for (const uint16_t use : uses) {
        if (!defs.contains(use)) {
            return false;
        }
    }
    return true;
}

} // namespace
} // namespace viper::codegen::x64

int main() {
    using namespace viper::codegen::x64;

    const ILModule cmpSelectModule = makeCmpSelectModule();
    const CodegenResult result = emitModuleToAssembly(cmpSelectModule, {});

    if (!result.errors.empty()) {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }
    if (!hasBooleanNormalizationPattern(result.asmText)) {
        std::cerr << "Unexpected assembly output:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    const ILModule branchArgModule = makeBranchArgBoolModule();
    AsmEmitter::RoDataPool roData{};
    std::vector<MFunction> mir;
    std::vector<FrameInfo> frames;
    std::string errors;
    CodegenOptions options{};
    if (!legalizeModuleToMIR(branchArgModule, hostTarget(), options, roData, mir, frames, errors)) {
        std::cerr << errors;
        return EXIT_FAILURE;
    }
    if (mir.empty() || !allVRegUsesAreDefined(mir.front())) {
        std::cerr << "Undefined vreg use after i1 branch-arg legalization:\n";
        if (!mir.empty()) {
            std::cerr << toString(mir.front());
        }
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
