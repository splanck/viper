//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LowerOvf.hpp
// Purpose: Expand overflow-checked arithmetic pseudo-opcodes for AArch64.
//
// Overflow pseudo-opcodes (AddOvfRRR, SubOvfRRR, etc.) are replaced with
// flag-setting instructions (ADDS/SUBS) followed by a conditional branch
// to a shared trap block on signed overflow (b.vs).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

namespace viper::codegen::aarch64
{

/// @brief Expand overflow-checked arithmetic pseudo-opcodes into guarded sequences.
///
/// Walks each basic block looking for AddOvfRRR, SubOvfRRR, AddOvfRI, SubOvfRI,
/// and MulOvfRRR pseudo-ops. Each is replaced with the real arithmetic instruction
/// (AddsRRR/SubsRRR/etc.) followed by a BCond "vs" to a shared trap block.
///
/// For multiply overflow (MulOvfRRR), the expansion uses:
///   mul  Xd, Xn, Xm
///   smulh Xtmp, Xn, Xm
///   cmp  Xtmp, Xd, asr #63
///   b.ne .Ltrap
///
/// The trap block calls rt_trap to abort execution.
///
/// @param fn Machine IR function being rewritten in place.
void lowerOverflowOps(MFunction &fn);

} // namespace viper::codegen::aarch64
