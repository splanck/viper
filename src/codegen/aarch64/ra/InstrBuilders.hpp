//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/InstrBuilders.hpp
// Purpose: Convenience factories for common MIR instructions emitted by
//          the AArch64 register allocator (MovRR, LdrFp, StrFp).
// Key invariants:
//   - Each builder returns a fully formed MInstr with correct opcode/operands.
// Ownership/Lifetime:
//   - Stateless free functions; returned MInstr is owned by caller.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

namespace viper::codegen::aarch64::ra {

inline MInstr makeMovRR(PhysReg dst, PhysReg src) {
    return MInstr{MOpcode::MovRR, {MOperand::regOp(dst), MOperand::regOp(src)}};
}

inline MInstr makeLdrFp(PhysReg dst, int offset) {
    return MInstr{MOpcode::LdrRegFpImm, {MOperand::regOp(dst), MOperand::immOp(offset)}};
}

inline MInstr makeStrFp(PhysReg src, int offset) {
    return MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(src), MOperand::immOp(offset)}};
}

} // namespace viper::codegen::aarch64::ra
