//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/peephole/DCE.hpp
// Purpose: Declaration for x86-64 dead code elimination peephole sub-pass.
//          Delegates to the shared template in PeepholeDCE.hpp with x86-64
//          specific traits.
// Key invariants:
//   - RSP modifications are never eliminated.
//   - Iterates to a fixed point within each basic block.
// Ownership/Lifetime:
//   - Operates on mutable instructions owned by the caller.
// Links: codegen/x86_64/peephole/DCE.cpp,
//        codegen/x86_64/peephole/PeepholeCommon.hpp,
//        codegen/common/PeepholeDCE.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "PeepholeCommon.hpp"

#include <cstddef>
#include <vector>

namespace zanna::codegen::x64::peephole {

/// @brief Run dead-code elimination on a single basic block.
/// @details Backward walk over @p instrs that tracks each operand's live set
///          and removes instructions whose only outputs are unused (or are
///          identity moves). Side-effecting instructions (calls, stores, traps,
///          flag-using branches, RSP modifications) are never removed. When
///          @p preservePhysRegsAtExit is true, physical registers live across
///          the block exit (e.g. return / call values that fall through to
///          the next block) are pinned and survive DCE.
/// @param instrs                Instruction list being scanned (mutated in place).
/// @param stats                 Peephole statistics counter (incremented per removal).
/// @param target                Target ABI metadata for identifying call-clobber
///                              and callee-saved registers.
/// @param preservePhysRegsAtExit Pin block-exit-live physical registers when true.
/// @return Number of instructions removed.
std::size_t runBlockDCE(std::vector<MInstr> &instrs,
                        PeepholeStats &stats,
                        const TargetInfo &target,
                        bool preservePhysRegsAtExit = false);

} // namespace zanna::codegen::x64::peephole
