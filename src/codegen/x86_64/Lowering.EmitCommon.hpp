//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.EmitCommon.hpp
// Purpose: Provide shared emission helpers used by the lowering rule
//          translation units.  The façade centralises register materialisation
//          and instruction construction so that opcode-specific emitters remain
//          focused on sequencing rather than boilerplate.
// Key invariants: Helper routines never mutate MIRBuilder state outside the
//                 provided block and honour the register classes requested by
//                 the caller.  Immediate operands are materialised only when
//                 necessary for the target opcode.
// Ownership/Lifetime: EmitCommon borrows the MIRBuilder reference supplied at
//                     construction time; no ownership of IL or MIR nodes is
//                     transferred.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "LowerILToMIR.hpp"
#include "MachineIR.hpp"

#include <optional>
#include <string_view>

namespace viper::codegen::x64
{

/// @brief Shared helper façade that consolidates common lowering utilities.
class EmitCommon
{
  public:
    explicit EmitCommon(MIRBuilder &builder) noexcept;

    [[nodiscard]] MIRBuilder &builder() const noexcept;

    [[nodiscard]] Operand clone(const Operand &operand) const;
    [[nodiscard]] Operand materialise(Operand operand, RegClass cls);
    [[nodiscard]] Operand materialiseGpr(Operand operand);

    void emitBinary(const ILInstr &instr,
                    MOpcode opcRR,
                    MOpcode opcRI,
                    RegClass cls,
                    bool requireImm32);
    void emitShift(const ILInstr &instr, MOpcode opcImm, MOpcode opcReg);
    void emitCmp(const ILInstr &instr, RegClass cls, int defaultCond);
    void emitSelect(const ILInstr &instr);
    void emitBranch(const ILInstr &instr);
    void emitCondBranch(const ILInstr &instr);
    void emitReturn(const ILInstr &instr);
    void emitLoad(const ILInstr &instr, RegClass cls);
    void emitStore(const ILInstr &instr);
    void emitCast(const ILInstr &instr, MOpcode opc, RegClass dstCls, RegClass srcCls);
    void emitDivRem(const ILInstr &instr, std::string_view opcode);

    [[nodiscard]] static std::optional<int> icmpConditionCode(std::string_view opcode) noexcept;
    [[nodiscard]] static std::optional<int> fcmpConditionCode(std::string_view opcode) noexcept;

  private:
    MIRBuilder *builder_{nullptr};
};

} // namespace viper::codegen::x64

