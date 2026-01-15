//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/AsmEmitter.hpp
// Purpose: AArch64 assembly text emitter for machine IR functions.
//
// This class converts Machine IR instructions to AArch64 assembly text output.
// It handles prologue/epilogue generation, instruction encoding to text form,
// and symbol/label emission following Mach-O conventions.
//
// Key invariants:
// - All physical registers must be valid AArch64 registers.
// - Immediate values must fit within AArch64 instruction encoding constraints.
// - Frame plan must be consistent with the MFunction's callee-saved registers.
//
// Ownership/Lifetime:
// - Emitter is stateless between function emissions (except during a single call).
// - TargetInfo reference must remain valid for the emitter's lifetime.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <string>

#include "codegen/aarch64/FramePlan.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

/// @brief Emits AArch64 assembly text from Machine IR.
///
/// Converts MIR instructions to assembly text, handling function headers,
/// prologue/epilogue generation, and instruction-level emission.
class AsmEmitter
{
  public:
    /// @brief Construct an AsmEmitter with target-specific configuration.
    /// @param target Target information providing ABI and register details.
    explicit AsmEmitter(const TargetInfo &target) noexcept : target_(&target) {}

    //=========================================================================
    // Function Structure Emission
    //=========================================================================

    /// @brief Emit a function header with symbol definition.
    /// @details Writes `.globl name` and `name:` label. Does not mangle names.
    /// @param os Output stream to write assembly text.
    /// @param name Function name (unmangled).
    void emitFunctionHeader(std::ostream &os, const std::string &name) const;

    /// @brief Emit ABI-conformant prologue for leaf-like functions.
    /// @details Saves fp/lr and establishes frame pointer. Uses default frame.
    /// @param os Output stream to write assembly text.
    void emitPrologue(std::ostream &os) const;

    /// @brief Emit ABI-conformant epilogue for leaf-like functions.
    /// @details Restores fp/lr and returns. Uses default frame.
    /// @param os Output stream to write assembly text.
    void emitEpilogue(std::ostream &os) const;

    /// @brief Emit prologue with explicit frame plan for callee-saved saves.
    /// @details Saves all registers specified in the frame plan and allocates
    ///          the requested stack frame size.
    /// @param os Output stream to write assembly text.
    /// @param plan Frame plan specifying callee-saved registers and frame size.
    void emitPrologue(std::ostream &os, const FramePlan &plan) const;

    /// @brief Emit epilogue with explicit frame plan for callee-saved restores.
    /// @details Restores all registers specified in the frame plan and
    ///          deallocates the stack frame before returning.
    /// @param os Output stream to write assembly text.
    /// @param plan Frame plan specifying callee-saved registers and frame size.
    void emitEpilogue(std::ostream &os, const FramePlan &plan) const;

    //=========================================================================
    // Integer Register Operations (64-bit)
    //=========================================================================

    /// @brief Emit register-to-register move: `mov dst, src`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param src Source GPR.
    void emitMovRR(std::ostream &os, PhysReg dst, PhysReg src) const;

    /// @brief Emit immediate move: `mov dst, #imm`.
    /// @details For small immediates uses single mov; larger values may need
    ///          movz/movk sequence (see emitMovImm64).
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param imm Immediate value to load.
    void emitMovRI(std::ostream &os, PhysReg dst, long long imm) const;

    /// @brief Emit 64-bit integer addition: `add dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR for result.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit 64-bit integer subtraction: `sub dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR for result.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit 64-bit integer multiplication: `mul dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR for result.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit signed 64-bit division: `sdiv dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR for quotient.
    /// @param lhs Dividend GPR.
    /// @param rhs Divisor GPR.
    void emitSDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit unsigned 64-bit division: `udiv dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR for quotient.
    /// @param lhs Dividend GPR.
    /// @param rhs Divisor GPR.
    void emitUDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit multiply-subtract: `msub dst, mul1, mul2, sub`.
    /// @details Computes dst = sub - (mul1 * mul2). Used for modulo operations.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR for result.
    /// @param mul1 First multiply operand GPR.
    /// @param mul2 Second multiply operand GPR.
    /// @param sub Subtraction operand GPR.
    void emitMSubRRRR(std::ostream &os, PhysReg dst, PhysReg mul1, PhysReg mul2, PhysReg sub) const;

    /// @brief Emit add with immediate: `add dst, lhs, #imm`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Source GPR.
    /// @param imm Immediate value (12-bit unsigned or shifted).
    void emitAddRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const;

    /// @brief Emit subtract with immediate: `sub dst, lhs, #imm`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Source GPR.
    /// @param imm Immediate value (12-bit unsigned or shifted).
    void emitSubRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const;

    //=========================================================================
    // Floating-Point Operations (64-bit scalar)
    //=========================================================================

    /// @brief Emit FP register-to-register move: `fmov dst, src`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param src Source FPR.
    void emitFMovRR(std::ostream &os, PhysReg dst, PhysReg src) const;

    /// @brief Emit FP immediate move: `fmov dst, #imm`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param imm Floating-point immediate value.
    void emitFMovRI(std::ostream &os, PhysReg dst, double imm) const;

    /// @brief Emit FP addition: `fadd dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param lhs Left-hand operand FPR.
    /// @param rhs Right-hand operand FPR.
    void emitFAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit FP subtraction: `fsub dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param lhs Left-hand operand FPR.
    /// @param rhs Right-hand operand FPR.
    void emitFSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit FP multiplication: `fmul dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param lhs Left-hand operand FPR.
    /// @param rhs Right-hand operand FPR.
    void emitFMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit FP division: `fdiv dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param lhs Left-hand operand FPR (dividend).
    /// @param rhs Right-hand operand FPR (divisor).
    void emitFDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit FP comparison: `fcmp lhs, rhs`.
    /// @details Sets NZCV flags based on comparison result.
    /// @param os Output stream to write assembly text.
    /// @param lhs Left-hand operand FPR.
    /// @param rhs Right-hand operand FPR.
    void emitFCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit FP round to nearest: `frintn dst, src`.
    /// @param os Output stream to write assembly text.
    /// @param dstFPR Destination FPR for rounded value.
    /// @param srcFPR Source FPR to round.
    void emitFRintN(std::ostream &os, PhysReg dstFPR, PhysReg srcFPR) const;

    //=========================================================================
    // Type Conversions
    //=========================================================================

    /// @brief Emit signed int to FP conversion: `scvtf dst, src`.
    /// @param os Output stream to write assembly text.
    /// @param dstFPR Destination FPR.
    /// @param srcGPR Source GPR (signed integer).
    void emitSCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const;

    /// @brief Emit FP to signed int conversion: `fcvtzs dst, src`.
    /// @details Truncates toward zero.
    /// @param os Output stream to write assembly text.
    /// @param dstGPR Destination GPR (signed integer).
    /// @param srcFPR Source FPR.
    void emitFCvtZS(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const;

    /// @brief Emit unsigned int to FP conversion: `ucvtf dst, src`.
    /// @param os Output stream to write assembly text.
    /// @param dstFPR Destination FPR.
    /// @param srcGPR Source GPR (unsigned integer).
    void emitUCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const;

    /// @brief Emit FP to unsigned int conversion: `fcvtzu dst, src`.
    /// @details Truncates toward zero.
    /// @param os Output stream to write assembly text.
    /// @param dstGPR Destination GPR (unsigned integer).
    /// @param srcFPR Source FPR.
    void emitFCvtZU(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const;

    //=========================================================================
    // Bitwise Operations
    //=========================================================================

    /// @brief Emit bitwise AND: `and dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitAndRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit bitwise OR: `orr dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitOrrRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit bitwise XOR: `eor dst, lhs, rhs`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitEorRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit logical shift left: `lsl dst, lhs, #shift`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Source GPR.
    /// @param sh Shift amount (0-63).
    void emitLslRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const;

    /// @brief Emit logical shift right: `lsr dst, lhs, #shift`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Source GPR.
    /// @param sh Shift amount (0-63).
    void emitLsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const;

    /// @brief Emit arithmetic shift right: `asr dst, lhs, #shift`.
    /// @details Preserves sign bit during shift.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param lhs Source GPR.
    /// @param sh Shift amount (0-63).
    void emitAsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const;

    //=========================================================================
    // Compare and Conditional Set
    //=========================================================================

    /// @brief Emit register comparison: `cmp lhs, rhs`.
    /// @details Sets NZCV flags based on (lhs - rhs).
    /// @param os Output stream to write assembly text.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit immediate comparison: `cmp lhs, #imm`.
    /// @details Sets NZCV flags based on (lhs - imm).
    /// @param os Output stream to write assembly text.
    /// @param lhs Left-hand operand GPR.
    /// @param imm Immediate value to compare against.
    void emitCmpRI(std::ostream &os, PhysReg lhs, long long imm) const;

    /// @brief Emit bitwise test: `tst lhs, rhs`.
    /// @details Sets NZCV flags based on (lhs AND rhs) without storing result.
    /// @param os Output stream to write assembly text.
    /// @param lhs Left-hand operand GPR.
    /// @param rhs Right-hand operand GPR.
    void emitTstRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const;

    /// @brief Emit conditional set: `cset dst, cond`.
    /// @details Sets dst to 1 if condition is true, 0 otherwise.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param cond Condition code string (e.g., "eq", "ne", "lt", "gt").
    void emitCset(std::ostream &os, PhysReg dst, const char *cond) const;

    /// @brief Emit compare-and-branch-if-zero: `cbz reg, label`.
    /// @param os Output stream to write assembly text.
    /// @param reg Register to test.
    /// @param label Branch target label.
    void emitCbz(std::ostream &os, PhysReg reg, const std::string &label) const;

    //=========================================================================
    // Wide Immediate Materialization
    //=========================================================================

    /// @brief Emit move with zero: `movz dst, #imm16, lsl #shift`.
    /// @details Moves 16-bit immediate with shift, zeroing other bits.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param imm16 16-bit immediate value.
    /// @param lsl Shift amount (0, 16, 32, or 48).
    void emitMovZ(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const;

    /// @brief Emit move with keep: `movk dst, #imm16, lsl #shift`.
    /// @details Moves 16-bit immediate with shift, keeping other bits.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param imm16 16-bit immediate value.
    /// @param lsl Shift amount (0, 16, 32, or 48).
    void emitMovK(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const;

    /// @brief Emit full 64-bit immediate load using movz/movk sequence.
    /// @details Uses optimal instruction sequence based on value.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param value 64-bit immediate value to load.
    void emitMovImm64(std::ostream &os, PhysReg dst, unsigned long long value) const;

    /// @brief Emit return instruction: `ret`.
    /// @param os Output stream to write assembly text.
    void emitRet(std::ostream &os) const;

    //=========================================================================
    // Stack Pointer Operations
    //=========================================================================

    /// @brief Emit stack pointer decrement: `sub sp, sp, #bytes`.
    /// @details Allocates stack space for outgoing arguments.
    /// @param os Output stream to write assembly text.
    /// @param bytes Number of bytes to allocate (must be 16-aligned).
    void emitSubSp(std::ostream &os, long long bytes) const;

    /// @brief Emit stack pointer increment: `add sp, sp, #bytes`.
    /// @details Deallocates stack space.
    /// @param os Output stream to write assembly text.
    /// @param bytes Number of bytes to deallocate (must be 16-aligned).
    void emitAddSp(std::ostream &os, long long bytes) const;

    /// @brief Store GPR to stack pointer offset: `str src, [sp, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param src Source GPR.
    /// @param offset Byte offset from stack pointer.
    void emitStrToSp(std::ostream &os, PhysReg src, long long offset) const;

    /// @brief Store FPR to stack pointer offset: `str src, [sp, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param src Source FPR.
    /// @param offset Byte offset from stack pointer.
    void emitStrFprToSp(std::ostream &os, PhysReg src, long long offset) const;

    //=========================================================================
    // Frame Pointer Memory Operations (for locals)
    //=========================================================================

    /// @brief Load GPR from frame pointer offset: `ldr dst, [fp, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param offset Byte offset from frame pointer (negative for locals).
    void emitLdrFromFp(std::ostream &os, PhysReg dst, long long offset) const;

    /// @brief Store GPR to frame pointer offset: `str src, [fp, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param src Source GPR.
    /// @param offset Byte offset from frame pointer (negative for locals).
    void emitStrToFp(std::ostream &os, PhysReg src, long long offset) const;

    /// @brief Load FPR from frame pointer offset: `ldr dst, [fp, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param offset Byte offset from frame pointer (negative for locals).
    void emitLdrFprFromFp(std::ostream &os, PhysReg dst, long long offset) const;

    /// @brief Store FPR to frame pointer offset: `str src, [fp, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param src Source FPR.
    /// @param offset Byte offset from frame pointer (negative for locals).
    void emitStrFprToFp(std::ostream &os, PhysReg src, long long offset) const;

    /// @brief Compute address relative to frame pointer: `add dst, fp, #offset`.
    /// @details Used for alloca to get pointer to stack-allocated memory.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR for computed address.
    /// @param offset Byte offset from frame pointer.
    void emitAddFpImm(std::ostream &os, PhysReg dst, long long offset) const;

    //=========================================================================
    // Base Register Memory Operations
    //=========================================================================

    /// @brief Load GPR from base register offset: `ldr dst, [base, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination GPR.
    /// @param base Base address GPR.
    /// @param offset Byte offset from base.
    void emitLdrFromBase(std::ostream &os, PhysReg dst, PhysReg base, long long offset) const;

    /// @brief Store GPR to base register offset: `str src, [base, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param src Source GPR.
    /// @param base Base address GPR.
    /// @param offset Byte offset from base.
    void emitStrToBase(std::ostream &os, PhysReg src, PhysReg base, long long offset) const;

    /// @brief Load FPR from base register offset: `ldr dst, [base, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param dst Destination FPR.
    /// @param base Base address GPR.
    /// @param offset Byte offset from base.
    void emitLdrFprFromBase(std::ostream &os, PhysReg dst, PhysReg base, long long offset) const;

    /// @brief Store FPR to base register offset: `str src, [base, #offset]`.
    /// @param os Output stream to write assembly text.
    /// @param src Source FPR.
    /// @param base Base address GPR.
    /// @param offset Byte offset from base.
    void emitStrFprToBase(std::ostream &os, PhysReg src, PhysReg base, long long offset) const;

    //=========================================================================
    // Machine IR Emission
    //=========================================================================

    /// @brief Emit complete function from Machine IR.
    /// @details Emits function header, prologue, all blocks, and epilogue.
    /// @param os Output stream to write assembly text.
    /// @param fn Machine function to emit.
    void emitFunction(std::ostream &os, const MFunction &fn) const;

    /// @brief Emit a basic block from Machine IR.
    /// @details Emits label and all instructions in the block.
    /// @param os Output stream to write assembly text.
    /// @param bb Machine basic block to emit.
    void emitBlock(std::ostream &os, const MBasicBlock &bb) const;

    /// @brief Emit a single instruction from Machine IR.
    /// @details Dispatches to appropriate emit method based on opcode.
    /// @param os Output stream to write assembly text.
    /// @param mi Machine instruction to emit.
    void emitInstruction(std::ostream &os, const MInstr &mi) const;

  private:
    const TargetInfo *target_{nullptr};
    // Mutable state used during emitFunction to pass frame plan to Ret instructions
    mutable const FramePlan *currentPlan_{nullptr};
    mutable bool currentPlanValid_{false};

    [[nodiscard]] static const char *rn(PhysReg r) noexcept
    {
        return regName(r);
    }

    // Print FPR as dN (64-bit scalar view)
    static void printD(std::ostream &os, PhysReg r)
    {
        // Map Vn -> dn
        const char *name = regName(r); // e.g., "v8"
        if (name[0] == 'v')
        {
            os << 'd' << (name + 1);
        }
        else
        {
            // Fallback: if mis-specified, still print name
            os << name;
        }
    }
};

} // namespace viper::codegen::aarch64
