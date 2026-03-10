//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RegAllocLinear.cpp
/// @brief Linear-scan register allocator for AArch64 Machine IR.
///
/// This file implements a linear-scan register allocator that transforms MIR
/// with virtual registers into MIR with physical AArch64 registers. The
/// allocator handles register assignment, spill code insertion, and tracks
/// callee-saved register usage for prologue/epilogue generation.
///
/// **What is Register Allocation?**
/// Register allocation assigns physical registers to virtual registers in
/// the MIR. When there aren't enough physical registers, values are "spilled"
/// to stack memory and reloaded when needed.
///
/// **Linear-Scan Algorithm Overview:**
/// Linear-scan is a fast O(n log n) register allocation algorithm:
/// 1. Compute live intervals for each virtual register
/// 2. Sort intervals by start position
/// 3. Process intervals in order, assigning registers or spilling
///
/// ```
/// Live intervals:
///   v0: ─────────────
///   v1:     ───────
///   v2:         ─────────
///   v3:             ─────
///
/// Physical registers: x0, x1 (2 available)
///
/// Assignment:
///   Instruction 0: v0 gets x0
///   Instruction 2: v1 gets x1 (x0 still in use by v0)
///   Instruction 4: v0 ends, v2 gets x0
///   Instruction 5: v1 ends, v3 gets x1
/// ```
///
/// **Register Classes:**
/// The allocator handles two register classes independently:
/// | Class | Available Registers          | Used For           |
/// |-------|-----------------------------|--------------------|
/// | GPR   | x0-x28 (except reserved)    | Integers, pointers |
/// | FPR   | d0-d31                      | Floating-point     |
///
/// **Reserved Registers:**
/// | Register | Purpose                    |
/// |----------|----------------------------|
/// | x29 (fp) | Frame pointer              |
/// | x30 (lr) | Link register              |
/// | sp       | Stack pointer              |
/// | x18      | Platform register (Darwin) |
/// | x9       | Global scratch register    |
///
/// **Spill Slot Layout:**
/// ```
/// High addresses
/// ┌────────────────────────────────┐
/// │ Caller's frame                 │
/// ├────────────────────────────────┤ ← Old SP
/// │ Return address (x30)           │
/// │ Saved frame pointer (x29)      │
/// ├────────────────────────────────┤ ← FP (x29)
/// │ Callee-saved registers         │
/// │ Local variables (allocas)      │
/// │ Spill slots for vregs          │  ← Allocated by this pass
/// ├────────────────────────────────┤ ← SP
/// Low addresses
/// ```
///
/// **Callee-Saved Handling:**
/// Registers x19-x28 and d8-d15 are callee-saved per AAPCS64. When the
/// allocator assigns a callee-saved register, it records it in the frame
/// plan for save/restore in prologue/epilogue.
///
/// **Instruction Processing:**
/// For each instruction, the allocator:
/// 1. Identifies operands that are uses (inputs) vs defs (outputs)
/// 2. Ensures used vregs are in physical registers (reload if spilled)
/// 3. Assigns physical registers to defined vregs
/// 4. Frees registers for vregs whose live ranges end
///
/// **Example Transformation:**
/// ```
/// Before (virtual registers):
///   v0 = MovRI #42
///   v1 = AddRRR v0, v0
///   Bl print_i64          ; uses v1 in x0
///
/// After (physical registers):
///   x19 = MovRI #42       ; callee-saved for longer live range
///   x0 = AddRRR x19, x19
///   Bl print_i64
/// ```
///
/// **Spill Example:**
/// ```
/// Before:
///   v0 = ...              ; live across call
///   Bl some_function      ; clobbers caller-saved registers
///   ... = v0              ; need v0 after call
///
/// After:
///   x0 = ...
///   StrRegFpImm x0, [fp, #-24]  ; spill before call
///   Bl some_function
///   LdrRegFpImm x0, [fp, #-24]  ; reload after call
///   ... = x0
/// ```
///
/// @see MachineIR.hpp For MIR type definitions
/// @see FrameBuilder.hpp For stack frame layout
/// @see TargetAArch64.hpp For register definitions
///
//===----------------------------------------------------------------------===//

#include "RegAllocLinear.hpp"

#include "ra/Allocator.hpp"

namespace viper::codegen::aarch64
{

AllocationResult allocate(MFunction &fn, const TargetInfo &ti)
{
    ra::LinearAllocator allocator(fn, ti);
    return allocator.run();
}

} // namespace viper::codegen::aarch64
