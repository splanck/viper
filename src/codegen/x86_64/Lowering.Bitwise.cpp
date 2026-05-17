//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Lowering.Bitwise.cpp
// Purpose: Implement bitwise opcode lowering rules for the x86-64 backend.
//          Emitters rely on EmitCommon to manage register materialisation and
//          operand cloning.
// Key invariants:
//   - Emitters only trigger when operands are valid.
//   - Resulting machine instructions operate on GPR registers only.
// Ownership/Lifetime:
//   - Operates on borrowed IL instructions and MIR builders; no ownership transfer.
// Links: codegen/x86_64/LoweringRules.hpp,
//        codegen/x86_64/Lowering.EmitCommon.hpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"
#include "Lowering.EmitCommon.hpp"
#include "Unsupported.hpp"

namespace viper::codegen::x64::lowering {
namespace {

[[nodiscard]] bool isIntegerLikeKind(ILValue::Kind kind) noexcept {
    return kind == ILValue::Kind::I64 || kind == ILValue::Kind::I1 ||
           kind == ILValue::Kind::PTR;
}

void requireBitwiseResult(const ILInstr &instr, MIRBuilder &builder, const char *context) {
    if (!isIntegerLikeKind(instr.resultKind) ||
        builder.regClassFor(instr.resultKind) != RegClass::GPR) {
        phaseAUnsupported(context);
    }
}

} // namespace

/// @brief Lower a bitwise AND IL instruction.
/// @details Emits an `AND` binary instruction when the IL result type maps to a
///          general-purpose register, folding immediates through
///          @ref EmitCommon::emitBinary.
/// @param instr IL bitwise AND instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitAnd(const ILInstr &instr, MIRBuilder &builder) {
    requireBitwiseResult(instr, builder, "bitwise and: expected integer-like result");
    EmitCommon(builder).emitBinary(instr, MOpcode::ANDrr, MOpcode::ANDri, RegClass::GPR, true);
}

/// @brief Lower a bitwise OR IL instruction.
/// @details Restricts lowering to general-purpose registers and emits either the
///          register or immediate form of the OR instruction via
///          @ref EmitCommon::emitBinary.
/// @param instr IL bitwise OR instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitOr(const ILInstr &instr, MIRBuilder &builder) {
    requireBitwiseResult(instr, builder, "bitwise or: expected integer-like result");
    EmitCommon(builder).emitBinary(instr, MOpcode::ORrr, MOpcode::ORri, RegClass::GPR, true);
}

/// @brief Lower a bitwise XOR IL instruction.
/// @details Emits XOR register or immediate forms when the result type maps to
///          a general-purpose register, using @ref EmitCommon::emitBinary to
///          keep operand handling consistent.
/// @param instr IL bitwise XOR instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitXor(const ILInstr &instr, MIRBuilder &builder) {
    requireBitwiseResult(instr, builder, "bitwise xor: expected integer-like result");
    EmitCommon(builder).emitBinary(instr, MOpcode::XORrr, MOpcode::XORri, RegClass::GPR, true);
}

/// @brief Lower a shift-left IL instruction.
/// @details Delegates to @ref EmitCommon::emitShift so the helper can choose
///          between immediate and RCX-based shift encodings.
/// @param instr IL shift-left instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitShiftLeft(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitShift(instr, MOpcode::SHLri, MOpcode::SHLrc);
}

/// @brief Lower a logical right-shift IL instruction.
/// @details Uses @ref EmitCommon::emitShift to emit either the immediate or
///          variable shift form corresponding to the SHR opcode family.
/// @param instr IL logical right-shift instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitShiftLshr(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitShift(instr, MOpcode::SHRri, MOpcode::SHRrc);
}

/// @brief Lower an arithmetic right-shift IL instruction.
/// @details Invokes @ref EmitCommon::emitShift with SAR opcodes so signed shifts
///          normalise their operand handling across immediate and register
///          counts.
/// @param instr IL arithmetic right-shift instruction.
/// @param builder Machine IR builder receiving the lowered sequence.
void emitShiftAshr(const ILInstr &instr, MIRBuilder &builder) {
    EmitCommon(builder).emitShift(instr, MOpcode::SARri, MOpcode::SARrc);
}

} // namespace viper::codegen::x64::lowering
