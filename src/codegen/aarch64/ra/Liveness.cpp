//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/Liveness.cpp
// Purpose: Implementation of CFG-aware liveness analysis for AArch64 regalloc.
//          Computes per-block liveIn/liveOut sets using the shared backward
//          dataflow solver, with gen/kill split by register class (GPR/FPR).
// Key invariants:
//   - gen[B] = vregs used in B before being defined
//   - kill[B] = vregs defined in B
//   - GPR and FPR solved independently via shared solver
//   - CFG edges extracted from Br, BCond, Cbz terminators
// Links: src/codegen/common/ra/DataflowLiveness.hpp,
//        src/codegen/aarch64/ra/Liveness.hpp
//
//===----------------------------------------------------------------------===//

#include "Liveness.hpp"

#include "OperandRoles.hpp"
#include "codegen/common/ra/DataflowLiveness.hpp"

namespace viper::codegen::aarch64::ra
{

void LivenessAnalysis::run(const MFunction &func)
{
    const std::size_t n = func.blocks.size();
    succs_.assign(n, {});
    liveOutGPR_.assign(n, {});
    liveOutFPR_.assign(n, {});
    blockIndex_.clear();

    buildBlockIndex(func);
    buildCFG(func);
    preds_ = viper::codegen::ra::buildPredecessors(succs_);
    computeLiveOutSets(func);
}

void LivenessAnalysis::buildBlockIndex(const MFunction &func)
{
    for (std::size_t i = 0; i < func.blocks.size(); ++i)
        blockIndex_[func.blocks[i].name] = i;
}

void LivenessAnalysis::buildCFG(const MFunction &func)
{
    for (std::size_t i = 0; i < func.blocks.size(); ++i)
    {
        const auto &bb = func.blocks[i];
        for (const auto &mi : bb.instrs)
        {
            if (mi.opc == MOpcode::Br)
            {
                if (!mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Label)
                {
                    auto it = blockIndex_.find(mi.ops[0].label);
                    if (it != blockIndex_.end())
                        succs_[i].push_back(it->second);
                }
            }
            else if (mi.opc == MOpcode::BCond)
            {
                if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label)
                {
                    auto it = blockIndex_.find(mi.ops[1].label);
                    if (it != blockIndex_.end())
                        succs_[i].push_back(it->second);
                }
            }
            else if (mi.opc == MOpcode::Cbz)
            {
                if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label)
                {
                    auto it = blockIndex_.find(mi.ops[1].label);
                    if (it != blockIndex_.end())
                        succs_[i].push_back(it->second);
                }
            }
        }
    }
}

void LivenessAnalysis::computeLiveOutSets(const MFunction &func)
{
    const std::size_t N = func.blocks.size();

    // Step 1: Compute gen/kill per block, split by register class.
    std::vector<std::unordered_set<uint16_t>> genGPR(N), killGPR(N);
    std::vector<std::unordered_set<uint16_t>> genFPR(N), killFPR(N);

    for (std::size_t i = 0; i < N; ++i)
    {
        for (const auto &mi : func.blocks[i].instrs)
        {
            for (std::size_t k = 0; k < mi.ops.size(); ++k)
            {
                const auto &op = mi.ops[k];
                if (op.kind != MOperand::Kind::Reg || op.reg.isPhys)
                    continue;

                const uint16_t vid = op.reg.idOrPhys;
                const bool isFPR = (op.reg.cls == RegClass::FPR);
                auto &genSet = isFPR ? genFPR[i] : genGPR[i];
                auto &killSet = isFPR ? killFPR[i] : killGPR[i];

                auto [isUse, isDef] = operandRoles(mi, k);

                if (isUse && killSet.find(vid) == killSet.end())
                    genSet.insert(vid);

                if (isDef)
                    killSet.insert(vid);
            }
        }
    }

    // Step 2: Delegate to the shared dataflow solver.
    auto gprResult = viper::codegen::ra::solveBackwardDataflow(succs_, genGPR, killGPR);
    auto fprResult = viper::codegen::ra::solveBackwardDataflow(succs_, genFPR, killFPR);

    liveOutGPR_ = std::move(gprResult.liveOut);
    liveOutFPR_ = std::move(fprResult.liveOut);
}

const std::unordered_set<uint16_t> &LivenessAnalysis::liveOutGPR(std::size_t blockIdx) const
{
    return liveOutGPR_[blockIdx];
}

const std::unordered_set<uint16_t> &LivenessAnalysis::liveOutFPR(std::size_t blockIdx) const
{
    return liveOutFPR_[blockIdx];
}

const std::vector<std::size_t> &LivenessAnalysis::successors(std::size_t blockIdx) const
{
    return succs_[blockIdx];
}

const std::vector<std::size_t> &LivenessAnalysis::predecessors(std::size_t blockIdx) const
{
    return preds_[blockIdx];
}

} // namespace viper::codegen::aarch64::ra
