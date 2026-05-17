//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FpCompareLowering.hpp
// Purpose: Shared AArch64 FCMP result-materialization helpers. Maps each IL
//          floating-point compare opcode to an AArch64 condition code and
//          emits the instruction sequence that lands a 0/1 boolean in a GPR,
//          applying the NaN/unordered masking the ISA requires.
// Key invariants:
//   - AArch64 FCMP reports unordered (NaN) operands as Z=1, C=1, V=1. Ordered
//     predicates must therefore be AND-masked with `vc` (no-overflow ==
//     ordered); FCmpNE is intentionally unordered-true (OR with `vs`) to
//     match the existing IL FCmpNE / FCmpOrd / FCmpUno semantics.
//   - Helpers are pure with respect to control flow: they only append MInstrs
//     to the supplied MBasicBlock; they never remove or reorder instructions.
//   - Temporary booleans are allocated through the caller-owned vreg counter
//     (nextVRegId) so ids stay unique within the function being lowered.
// Ownership/Lifetime:
//   - Stateless inline free functions; no heap allocation and no retained
//     state. The MBasicBlock and vreg counter are caller-owned.
// Links: codegen/aarch64/LoweringContext.hpp (allocateNextVReg),
//        codegen/aarch64/MachineIR.hpp, il/core/Opcode.hpp,
//        codegen/aarch64/InstrLowering.cpp (primary caller)
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/LoweringContext.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "il/core/Opcode.hpp"

namespace viper::codegen::aarch64 {

/// @brief Return the primitive AArch64 condition used by an FCMP opcode.
/// @details Ordered comparisons still need the boolean helpers below to mask
///          unordered NaN cases with `vc`.
inline const char *fpCondCode(il::core::Opcode op) {
    switch (op) {
        case il::core::Opcode::FCmpEQ:
            return "eq";
        case il::core::Opcode::FCmpNE:
            return "ne";
        case il::core::Opcode::FCmpLT:
            return "mi";
        case il::core::Opcode::FCmpLE:
            return "ls";
        case il::core::Opcode::FCmpGT:
            return "gt";
        case il::core::Opcode::FCmpGE:
            return "ge";
        case il::core::Opcode::FCmpOrd:
            return "vc";
        case il::core::Opcode::FCmpUno:
            return "vs";
        default:
            return "eq";
    }
}

/// @brief Materialize an IL FP-comparison result into a 0/1 GPR vreg.
/// @details AArch64 FCMP reports unordered operands as Z=1, C=1, V=1. Ordered
///          predicates must therefore mask with `vc`; `ne` is intentionally
///          unordered-true to match the existing IL behavior alongside `ord/uno`.
inline void emitFpCompareResult(MBasicBlock &out,
                                il::core::Opcode op,
                                uint16_t dst,
                                uint16_t &nextVRegId) {
    const auto csetInto = [&](const char *cond) {
        const uint16_t tmp = allocateNextVReg(nextVRegId);
        out.instrs.push_back(
            MInstr{MOpcode::Cset, {MOperand::vregOp(RegClass::GPR, tmp), MOperand::condOp(cond)}});
        return tmp;
    };

    switch (op) {
        case il::core::Opcode::FCmpNE: {
            const uint16_t ne = csetInto("ne");
            const uint16_t uno = csetInto("vs");
            out.instrs.push_back(MInstr{MOpcode::OrrRRR,
                                        {MOperand::vregOp(RegClass::GPR, dst),
                                         MOperand::vregOp(RegClass::GPR, ne),
                                         MOperand::vregOp(RegClass::GPR, uno)}});
            return;
        }
        case il::core::Opcode::FCmpOrd:
        case il::core::Opcode::FCmpUno:
            out.instrs.push_back(
                MInstr{MOpcode::Cset,
                       {MOperand::vregOp(RegClass::GPR, dst), MOperand::condOp(fpCondCode(op))}});
            return;
        default: {
            const uint16_t pred = csetInto(fpCondCode(op));
            const uint16_t ord = csetInto("vc");
            out.instrs.push_back(MInstr{MOpcode::AndRRR,
                                        {MOperand::vregOp(RegClass::GPR, dst),
                                         MOperand::vregOp(RegClass::GPR, pred),
                                         MOperand::vregOp(RegClass::GPR, ord)}});
            return;
        }
    }
}

} // namespace viper::codegen::aarch64
