//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the lowering pass that expands overflow-checked arithmetic pseudos
// (AddOvfRRR, SubOvfRRR, AddOvfRI, SubOvfRI, MulOvfRRR) into their real
// AArch64 instructions followed by a conditional branch to a trap block on
// signed overflow.
//
// The pass executes between IL->MIR lowering and register allocation. It keeps
// operand usage confined to virtual registers and reuses a single trap block per
// function to minimise code growth.
//
// Pattern generated for add/sub overflow:
//   adds/subs  Xd, Xn, Xm   (or #imm variant)
//   b.vs  .Ltrap_ovf_<funcname>
//
// Pattern generated for mul overflow:
//   mul    Xd, Xn, Xm
//   smulh  Xtmp, Xn, Xm
//   cmp    Xtmp, Xd, asr #63
//   b.ne   .Ltrap_ovf_<funcname>
//
// The trap block calls rt_trap to abort execution.
//
//===----------------------------------------------------------------------===//

#include "LowerOvf.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace viper::codegen::aarch64
{

namespace
{

/// @brief Check if an opcode is an overflow-checked pseudo.
[[nodiscard]] bool isOverflowPseudo(MOpcode opc)
{
    switch (opc)
    {
        case MOpcode::AddOvfRRR:
        case MOpcode::SubOvfRRR:
        case MOpcode::AddOvfRI:
        case MOpcode::SubOvfRI:
        case MOpcode::MulOvfRRR:
            return true;
        default:
            return false;
    }
}

/// @brief Find a basic block by name in the function.
[[nodiscard]] std::optional<std::size_t> findBlock(const MFunction &fn, const std::string &name)
{
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        if (fn.blocks[i].name == name)
            return i;
    }
    return std::nullopt;
}

} // namespace

void lowerOverflowOps(MFunction &fn)
{
    const std::string trapLabel = ".Ltrap_ovf_" + fn.name;
    std::optional<std::size_t> trapIndex{};

    auto ensureTrapBlock = [&]() -> std::size_t
    {
        if (trapIndex)
            return *trapIndex;

        if (auto existing = findBlock(fn, trapLabel))
        {
            trapIndex = *existing;
            return *trapIndex;
        }

        MBasicBlock trapBlock{};
        trapBlock.name = trapLabel;
        // bl rt_trap
        trapBlock.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
        fn.blocks.push_back(std::move(trapBlock));
        trapIndex = fn.blocks.size() - 1U;
        return *trapIndex;
    };

    // Pre-scan: if any overflow pseudo exists, create the trap block up front
    // so that fn.blocks is not reallocated while we hold references into it.
    bool hasOverflow = false;
    for (const auto &block : fn.blocks)
    {
        for (const auto &instr : block.instrs)
        {
            if (isOverflowPseudo(instr.opc))
            {
                hasOverflow = true;
                break;
            }
        }
        if (hasOverflow)
            break;
    }

    if (!hasOverflow)
        return;

    ensureTrapBlock();

    // The trap block index is now stable — fn.blocks will not reallocate.
    // Only iterate blocks that existed before we added the trap block.

    // Compute the maximum virtual register ID across the entire function so
    // that new temporaries introduced by the mul overflow expansion do not
    // collide with existing virtual registers.
    uint16_t maxVReg = 0;
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instrs)
            for (const auto &op : instr.ops)
                if (!op.reg.isPhys && op.kind == MOperand::Kind::Reg)
                    maxVReg = std::max(maxVReg, op.reg.idOrPhys);

    const std::size_t blockCount = fn.blocks.size() - 1U;
    for (std::size_t blockIdx = 0; blockIdx < blockCount; ++blockIdx)
    {
        auto &block = fn.blocks[blockIdx];
        for (std::size_t i = 0; i < block.instrs.size(); ++i)
        {
            const MInstr &instr = block.instrs[i];
            if (!isOverflowPseudo(instr.opc))
                continue;

            if (instr.opc == MOpcode::MulOvfRRR)
            {
                // Multiply overflow detection using smulh:
                //   mul    Xd, Xn, Xm          // low 64 bits
                //   smulh  Xtmp1, Xn, Xm       // high 64 bits (signed)
                //   asr    Xtmp2, Xd, #63       // sign extension of bit 63
                //   cmp    Xtmp1, Xtmp2         // compare high bits with expected sign
                //   b.ne   .Ltrap_ovf           // overflow if they don't match
                auto dst = instr.ops[0];
                auto lhs = instr.ops[1];
                auto rhs = instr.ops[2];

                // 1. mul Xd, Xn, Xm
                block.instrs[i] = MInstr{MOpcode::MulRRR, {dst, lhs, rhs}};

                // 2. smulh Xtmp1, Xn, Xm
                auto smulhDst = MOperand::vregOp(RegClass::GPR, ++maxVReg);
                MInstr smulh{MOpcode::SmulhRRR, {smulhDst, lhs, rhs}};

                // 3. asr Xtmp2, Xd, #63
                auto asrDst = MOperand::vregOp(RegClass::GPR, ++maxVReg);
                MInstr asr{MOpcode::AsrRI, {asrDst, dst, MOperand::immOp(63)}};

                // 4. cmp Xtmp1, Xtmp2
                MInstr cmp{MOpcode::CmpRR, {smulhDst, asrDst}};

                // 5. b.ne .Ltrap_ovf
                MInstr bne{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel)}};

                // Insert instructions after the mul
                auto insertPos =
                    block.instrs.begin() + static_cast<std::ptrdiff_t>(i + 1);
                insertPos = block.instrs.insert(insertPos, std::move(smulh));
                insertPos = block.instrs.insert(insertPos + 1, std::move(asr));
                insertPos = block.instrs.insert(insertPos + 1, std::move(cmp));
                block.instrs.insert(insertPos + 1, std::move(bne));

                // Skip past the 4 instructions we just inserted
                i += 4;
                continue;
            }

            // Determine the real flag-setting opcode.
            MOpcode realOpc;
            switch (instr.opc)
            {
                case MOpcode::AddOvfRRR:
                    realOpc = MOpcode::AddsRRR;
                    break;
                case MOpcode::SubOvfRRR:
                    realOpc = MOpcode::SubsRRR;
                    break;
                case MOpcode::AddOvfRI:
                    realOpc = MOpcode::AddsRI;
                    break;
                case MOpcode::SubOvfRI:
                    realOpc = MOpcode::SubsRI;
                    break;
                default:
                    continue; // unreachable
            }

            // Replace pseudo with real flag-setting instruction.
            block.instrs[i] = MInstr{realOpc, instr.ops};

            // Insert b.vs to trap block after the flag-setting instruction.
            MInstr bvs{MOpcode::BCond, {MOperand::condOp("vs"), MOperand::labelOp(trapLabel)}};
            block.instrs.insert(block.instrs.begin() + static_cast<std::ptrdiff_t>(i + 1U),
                                std::move(bvs));

            // Skip past the b.vs we just inserted.
            ++i;
        }
    }

    // Note: the trap block contains a `bl rt_trap` call, but rt_trap is noreturn.
    // The isLeaf scan in CodegenPipeline.cpp skips .Ltrap_ blocks so that the
    // hot path can benefit from leaf-function optimizations (no callee-saved
    // register save/restore, potentially frameless).
}

} // namespace viper::codegen::aarch64
