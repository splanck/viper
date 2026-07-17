//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FpCompareLowering.hpp
// Purpose: Shared AArch64 FCMP result-materialization helpers. Maps each IL
//          floating-point compare opcode to an AArch64 condition code and
//          emits the instruction sequence that lands a 0/1 boolean in a GPR.
// Key invariants:
//   - AArch64 FCMP sets NZCV = 0011 (N=0, Z=0, C=1, V=1) for unordered (NaN)
//     operands. The condition codes chosen here (eq, mi, ls, gt, ge) all
//     evaluate FALSE under that flag state, so ordered predicates need no
//     extra unordered masking — a single CSET is exact. FCmpNE uses `ne`,
//     which is intentionally unordered-TRUE (Z=0), matching IL FCmpNE
//     semantics; FCmpOrd/FCmpUno map directly to `vc`/`vs`.
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

namespace zanna::codegen::aarch64 {

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
/// @details After FCMP, unordered operands set NZCV = 0011 (N=0, Z=0, C=1,
///          V=1). Every condition code returned by fpCondCode() for the
///          ordered predicates — eq (Z), mi (N), ls (!C || Z), gt (Z==0 &&
///          N==V), ge (N==V) — evaluates FALSE under that state, so a single
///          CSET captures the exact IL semantics; no unordered masking is
///          needed. `ne` is unordered-true by construction (Z=0), which is
///          the documented IL FCmpNE behaviour, and `vc`/`vs` directly encode
///          FCmpOrd/FCmpUno.
inline void emitFpCompareResult(MBasicBlock &out,
                                il::core::Opcode op,
                                uint16_t dst,
                                uint16_t &nextVRegId) {
    (void)nextVRegId;
    out.instrs.push_back(MInstr{
        MOpcode::Cset, {MOperand::vregOp(RegClass::GPR, dst), MOperand::condOp(fpCondCode(op))}});
}

} // namespace zanna::codegen::aarch64
