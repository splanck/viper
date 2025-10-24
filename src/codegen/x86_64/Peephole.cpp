//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Peephole.cpp
// Purpose: Implement conservative peephole optimisations over Machine IR for
//          the x86-64 backend.
// Key invariants: Rewrites preserve instruction ordering and only substitute
//                 encodings that are provably equivalent under the Phase A
//                 Machine IR conventions.
// Ownership/Lifetime: Mutates Machine IR graphs owned by the caller without
//                     retaining references to transient operands.
// Links: docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "Peephole.hpp"

namespace viper::codegen::x64
{
namespace
{
/// @brief Test whether an operand is the immediate integer zero.
///
/// @details Peephole rewrites often recognise the canonical pattern of moving
///          or comparing against literal zero.  The helper inspects the
///          discriminant and payload of @p operand to confirm it is an
///          @ref OpImm value whose @ref OpImm::val equals zero.  Using a helper
///          keeps the pattern checks readable at the call site.
///
/// @param operand Operand drawn from a Machine IR instruction.
/// @return @c true when the operand is an immediate zero literal.
[[nodiscard]] bool isZeroImm(const Operand &operand) noexcept
{
    const auto *imm = std::get_if<OpImm>(&operand);
    return imm != nullptr && imm->val == 0;
}

/// @brief Check whether an operand refers to a general-purpose register.
///
/// @details The peepholes implemented here only fold when the destination is a
///          GPR because the rewrite replaces `mov`/`cmp` with register-to-self
///          forms.  This helper verifies that @p operand holds an @ref OpReg and
///          that the register class is @ref RegClass::GPR.
///
/// @param operand Operand to classify.
/// @return @c true when the operand is a GPR register reference.
[[nodiscard]] bool isGprReg(const Operand &operand) noexcept
{
    const auto *reg = std::get_if<OpReg>(&operand);
    return reg != nullptr && reg->cls == RegClass::GPR;
}

/// @brief Rewrite a MOV immediate-to-register into XOR to synthesize zero.
///
/// @details Zeroing a register via `xor reg, reg` encodes shorter and avoids
///          materialising literal zero immediates.  The helper updates
///          @p instr in place by clearing existing operands, switching the
///          opcode to @ref MOpcode::XORrr32, and inserting the supplied register
///          operand twice to match the canonical encoding.
///
/// @param instr Instruction to mutate.
/// @param regOperand Register operand that will be zeroed.
void rewriteToXor(MInstr &instr, Operand regOperand)
{
    instr.opcode = MOpcode::XORrr32;
    instr.operands.clear();
    instr.operands.push_back(regOperand);
    instr.operands.push_back(regOperand);
}

/// @brief Convert a compare-against-zero into a register self-test.
///
/// @details The transformation mirrors the XOR rewrite but targets `cmp`.
///          Testing a register against itself produces the same flag pattern as
///          comparing to zero while avoiding the immediate operand.  The helper
///          swaps in @ref MOpcode::TESTrr and duplicates the register operand so
///          callers can apply the optimisation with a single call.
///
/// @param instr Instruction to rewrite in place.
/// @param regOperand Register operand participating in the test.
void rewriteToTest(MInstr &instr, Operand regOperand)
{
    instr.opcode = MOpcode::TESTrr;
    instr.operands.clear();
    instr.operands.push_back(regOperand);
    instr.operands.push_back(regOperand);
}
} // namespace

/// @brief Apply local peephole simplifications to the provided Machine IR.
///
/// @details Walks each instruction in every block and matches simple patterns
///          that can be reduced without altering control flow: moving a zero
///          immediate into a GPR and comparing a GPR against zero.  When a match
///          is found the instruction is rewritten in place using the helpers
///          above.  The routine intentionally avoids allocating additional data
///          structures so it can be invoked late in the pipeline without
///          impacting compile time.
///
/// @param fn Machine function to optimise.
void runPeepholes(MFunction &fn)
{
    for (auto &block : fn.blocks)
    {
        for (auto &instr : block.instructions)
        {
            switch (instr.opcode)
            {
                case MOpcode::MOVri:
                {
                    if (instr.operands.size() != 2)
                    {
                        break;
                    }

                    if (!isGprReg(instr.operands[0]) || !isZeroImm(instr.operands[1]))
                    {
                        break;
                    }

                    rewriteToXor(instr, instr.operands[0]);
                    break;
                }
                case MOpcode::CMPri:
                {
                    if (instr.operands.size() != 2)
                    {
                        break;
                    }

                    if (!isGprReg(instr.operands[0]) || !isZeroImm(instr.operands[1]))
                    {
                        break;
                    }

                    rewriteToTest(instr, instr.operands[0]);
                    break;
                }
                default:
                    break;
            }
        }
    }
}

} // namespace viper::codegen::x64
