//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.CF.cpp
// Purpose: Implement control-flow lowering rules for the provisional IL
//          dialect, covering branches, selects, and returns.
// Key invariants: Emitters rely on EmitCommon for operand preparation and obey
//                 the register classes dictated by MIRBuilder.
// Ownership/Lifetime: Works with borrowed MIRBuilder and IL instruction data.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Control-flow lowering hooks for the x86-64 backend.
/// @details Provides thin wrappers that forward branch, select, and return IL
///          instructions to @ref viper::codegen::x64::EmitCommon, ensuring a
///          consistent emission strategy regardless of the caller.

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"
#include "MachineIR.hpp"
#include "Unsupported.hpp"

#include <limits>
#include <string>

namespace viper::codegen::x64::lowering {
namespace {

constexpr int64_t kErrBounds = 7;

MInstr makePlannedCall(Operand target, uint32_t callPlanId) {
    MInstr call = MInstr::make(MOpcode::CALL, std::vector<Operand>{std::move(target)});
    call.callPlanId = callPlanId;
    return call;
}

void emitBoundsTrap(MIRBuilder &builder) {
    CallLoweringPlan plan{};
    plan.callee = "rt_trap_raise_error";
    plan.numNamedArgs = 1;
    plan.args.push_back(
        CallArg{.cls = CallArgClass::GPR, .vreg = 0, .isImm = true, .imm = kErrBounds});
    const uint32_t callPlanId = builder.recordCallPlan(std::move(plan));
    builder.append(makePlannedCall(makeLabelOperand(std::string{"rt_trap_raise_error"}), callPlanId));
    builder.append(MInstr::make(MOpcode::UD2));
}

Operand emitSignExtendedToWidth(MIRBuilder &builder,
                                EmitCommon &emit,
                                Operand src,
                                std::uint8_t widthBits) {
    Operand value = emit.materialiseGpr(std::move(src));
    if (widthBits >= 64) {
        return value;
    }

    const int shift = 64 - static_cast<int>(widthBits);
    const VReg narrowedReg = builder.makeTempVReg(RegClass::GPR);
    const Operand narrowed = makeVRegOperand(narrowedReg.cls, narrowedReg.id);
    builder.append(MInstr::make(MOpcode::MOVrr, {emit.clone(narrowed), emit.clone(value)}));
    builder.append(MInstr::make(MOpcode::SHLri, {emit.clone(narrowed), makeImmOperand(shift)}));
    builder.append(MInstr::make(MOpcode::SARri, {emit.clone(narrowed), makeImmOperand(shift)}));
    return narrowed;
}

struct SwitchCase {
    int64_t value{0};
    Operand label{};
};

void emitSwitchTree(const std::vector<SwitchCase> &cases,
                    std::size_t begin,
                    std::size_t end,
                    const Operand &scrutinee,
                    const Operand &defaultLabel,
                    MIRBuilder &builder) {
    if (begin >= end) {
        builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{defaultLabel}));
        return;
    }

    const std::size_t mid = begin + (end - begin) / 2;
    const SwitchCase &pivot = cases[mid];
    builder.append(
        MInstr::make(MOpcode::CMPri, std::vector<Operand>{scrutinee, makeImmOperand(pivot.value)}));
    builder.append(MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(0), pivot.label}));

    const bool hasLeft = begin < mid;
    const bool hasRight = mid + 1 < end;
    const uint32_t labelSeed = builder.lower().nextLocalLabelId();
    const Operand leftLabel =
        makeLabelOperand(".Lswitch_left_" + std::to_string(labelSeed));
    const Operand rightLabel =
        makeLabelOperand(".Lswitch_right_" + std::to_string(labelSeed));

    if (hasLeft) {
        builder.append(
            MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(2), leftLabel}));
    }
    builder.append(MInstr::make(
        MOpcode::JMP, std::vector<Operand>{hasRight ? rightLabel : defaultLabel}));

    if (hasLeft) {
        builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{leftLabel}));
        emitSwitchTree(cases, begin, mid, scrutinee, defaultLabel, builder);
    }
    if (hasRight) {
        builder.append(MInstr::make(MOpcode::LABEL, std::vector<Operand>{rightLabel}));
        emitSwitchTree(cases, mid + 1, end, scrutinee, defaultLabel, builder);
    }
}

} // namespace

/// @brief Lower a SELECT IL instruction into Machine IR.
/// @details Delegates to @ref EmitCommon::emitSelect so the helper can implement
///          conditional move sequencing for both integer and floating-point
///          values.
/// @param instr IL select instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitSelect(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitSelect(instr);
}

/// @brief Lower an unconditional branch IL instruction.
/// @details Calls @ref EmitCommon::emitBranch to append a JMP to the target
///          label extracted from the IL operand list.
/// @param instr IL branch instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitBranch(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitBranch(instr);
}

/// @brief Lower a conditional branch IL instruction.
/// @details Uses @ref EmitCommon::emitCondBranch to build the TEST/JCC/JMP
///          sequence that mirrors IL conditional control flow.
/// @param instr IL conditional branch instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitCondBranch(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitCondBranch(instr);
}

/// @brief Lower a RETURN IL instruction.
/// @details Forwards to @ref EmitCommon::emitReturn so ABI-specific register
///          conventions and optional return values are handled uniformly.
/// @param instr IL return instruction.
/// @param builder Machine IR builder receiving the emitted code.
void emitReturn(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitReturn(instr);
}

/// @brief Lower an idx_chk instruction (bounds check with trap on out-of-bounds).
/// @details Emits inline CMP + JCC + UD2 sequences using in-block LABEL definitions
///          to conditionally trap when the index is outside [lower, upper).
///          The check verifies: lower <= index < upper (unsigned comparison).
///          The result is the index value passed through if the check succeeds.
/// @param instr IL idx_chk instruction: ops[0]=index, ops[1]=lower, ops[2]=upper.
/// @param builder Machine IR builder receiving the emitted code.
void emitIdxChk(const ILInstr &instr, MIRBuilder &builder) {
    if (instr.resultId < 0 || instr.ops.size() < 3) {
        phaseAUnsupported("idx_chk: missing operands");
    }

    EmitCommon emit(builder);
    const VReg destReg = builder.ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

    // Materialise the index into a GPR, truncating to the IL result width before comparing.
    Operand index = emitSignExtendedToWidth(
        builder, emit, builder.makeOperandForValue(instr.ops[0], RegClass::GPR), instr.resultBits);

    // Generate unique labels for the skip points
    const uint32_t labelId = builder.lower().nextLocalLabelId();
    const std::string passUpperLabel = ".Lidxchk_u_" + std::to_string(labelId);
    const std::string passLowerLabel = ".Lidxchk_l_" + std::to_string(labelId);

    const bool loIsZero =
        instr.ops[1].id < 0 && instr.ops[1].kind == ILValue::Kind::I64 && instr.ops[1].i64 == 0;

    // Check upper bound.
    Operand upper = builder.makeOperandForValue(instr.ops[2], RegClass::GPR);
    if (instr.resultBits < 64) {
        upper = emitSignExtendedToWidth(builder, emit, std::move(upper), instr.resultBits);
    }
    if (const auto *imm = std::get_if<OpImm>(&upper); imm && fitsImm32(imm->val)) {
        builder.append(MInstr::make(MOpcode::CMPri, std::vector<Operand>{index, upper}));
    } else {
        upper = emit.materialiseGpr(std::move(upper));
        builder.append(MInstr::make(MOpcode::CMPrr, std::vector<Operand>{index, upper}));
    }

    // With lo == 0, unsigned compare also rejects negative indices. Otherwise,
    // use signed compares so negative lower bounds behave like the VM.
    const int passUpperCond = loIsZero ? 8 : 2; // JB / JL
    builder.append(MInstr::make(
        MOpcode::JCC,
        std::vector<Operand>{makeImmOperand(passUpperCond), makeLabelOperand(passUpperLabel)}));
    emitBoundsTrap(builder);
    builder.append(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(passUpperLabel)}));

    Operand lower{};
    if (!loIsZero) {
        lower = builder.makeOperandForValue(instr.ops[1], RegClass::GPR);
        if (instr.resultBits < 64) {
            lower = emitSignExtendedToWidth(builder, emit, std::move(lower), instr.resultBits);
        }
        if (const auto *imm = std::get_if<OpImm>(&lower); imm && fitsImm32(imm->val)) {
            builder.append(MInstr::make(MOpcode::CMPri, std::vector<Operand>{index, lower}));
        } else {
            lower = emit.materialiseGpr(std::move(lower));
            builder.append(MInstr::make(MOpcode::CMPrr, std::vector<Operand>{index, lower}));
        }
    }

    if (!loIsZero) {
        builder.append(MInstr::make(
            MOpcode::JCC,
            std::vector<Operand>{makeImmOperand(5), makeLabelOperand(passLowerLabel)})); // JGE
        emitBoundsTrap(builder);
        builder.append(
            MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(passLowerLabel)}));
    }

    // Result is the normalized zero-based index (idx - lo).
    builder.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{dest, index}));
    if (loIsZero) {
        return;
    }

    if (const auto *imm = std::get_if<OpImm>(&lower); imm && fitsImm32(imm->val) &&
                                                 imm->val != std::numeric_limits<int64_t>::min()) {
        builder.append(
            MInstr::make(MOpcode::ADDri, std::vector<Operand>{dest, makeImmOperand(-imm->val)}));
        return;
    }

    if (std::holds_alternative<OpImm>(lower)) {
        lower = emit.materialiseGpr(std::move(lower));
    }
    builder.append(MInstr::make(MOpcode::SUBrr, std::vector<Operand>{dest, lower}));
}

/// @brief Lower a switch_i32 instruction (multi-way branch).
/// @details Emits a chain of CMP + JCC pairs, one per case, followed by a JMP to the
///          default label. The operands are: ops[0]=scrutinee, then (value, label) pairs,
///          then an optional default label as the final operand.
/// @param instr IL switch_i32 instruction with variable-length operands.
/// @param builder Machine IR builder receiving the emitted code.
void emitSwitchI32(const ILInstr &instr, MIRBuilder &builder) {
    if (instr.ops.empty()) {
        phaseAUnsupported("switch: missing scrutinee");
    }

    EmitCommon emit(builder);
    Operand scrutinee =
        emit.materialiseGpr(builder.makeOperandForValue(instr.ops[0], RegClass::GPR));
    std::vector<SwitchCase> cases{};
    std::size_t idx = 1;
    while (idx + 1 < instr.ops.size() && instr.ops[idx].kind != ILValue::Kind::LABEL) {
        cases.push_back(
            SwitchCase{instr.ops[idx].i64, builder.makeLabelOperand(instr.ops[idx + 1])});
        idx += 2;
    }

    Operand defLabel = (idx < instr.ops.size()) ? builder.makeLabelOperand(instr.ops[idx])
                                                : makeLabelOperand(".Lswitch_default_missing");
    std::sort(cases.begin(), cases.end(), [](const SwitchCase &lhs, const SwitchCase &rhs) {
        return lhs.value < rhs.value;
    });

    if (cases.size() <= 3) {
        for (const auto &caseEntry : cases) {
            builder.append(MInstr::make(
                MOpcode::CMPri, std::vector<Operand>{scrutinee, makeImmOperand(caseEntry.value)}));
            builder.append(
                MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(0), caseEntry.label}));
        }
        builder.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{defLabel}));
        return;
    }

    emitSwitchTree(cases, 0, cases.size(), scrutinee, defLabel, builder);
}

} // namespace viper::codegen::x64::lowering
