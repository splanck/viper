//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LowerOvf.hpp
// Purpose: Expand overflow-checked arithmetic pseudo-opcodes for AArch64.
//          AddOvfRRR/SubOvfRRR/AddOvfRI/SubOvfRI/MulOvfRRR are replaced with
//          flag-setting real instructions (ADDS/SUBS, or MUL+SMULH for
//          multiply) followed by a conditional branch to a shared trap block
//          on signed overflow.
// Key invariants:
//   - Runs on MIR before register allocation; expansion is in-place per block.
//   - At most one shared trap block is created per function and reused by
//     every overflow site; it tail-calls rt_trap and never returns.
//   - Add/Sub overflow is detected via the V flag (b.vs); multiply overflow
//     via the smulh-vs-asr#63 sign-extension mismatch (b.ne).
// Ownership/Lifetime:
//   - Stateless free function; mutates the caller-owned MFunction and retains
//     no state across calls.
// Links: codegen/aarch64/LowerOvf.cpp,
//        codegen/aarch64/MachineIR.hpp,
//        codegen/aarch64/Noreturn.hpp (trap-call classification)
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

namespace zanna::codegen::aarch64 {

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

} // namespace zanna::codegen::aarch64
