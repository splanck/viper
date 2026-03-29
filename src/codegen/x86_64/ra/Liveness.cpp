//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Liveness.cpp
// Purpose: Implementation of CFG-aware liveness analysis for x86-64 regalloc.
//          Computes per-block liveIn/liveOut sets using standard backward
//          dataflow equations over the machine IR control-flow graph.
// Key invariants:
//   - gen[B] = vregs used in B before being defined
//   - kill[B] = vregs defined in B
//   - liveOut[B] = union of liveIn[S] for all successors S
//   - liveIn[B] = gen[B] union (liveOut[B] - kill[B])
//   - Iteration terminates when no set changes (monotone lattice)
// Links: src/codegen/x86_64/ra/Liveness.hpp
//
//===----------------------------------------------------------------------===//

#include "Liveness.hpp"

#include "codegen/common/ra/DataflowLiveness.hpp"

#include <cassert>

namespace viper::codegen::x64::ra {

namespace {

/// Extract the label target from an OpLabel operand.
const std::string *getLabel(const Operand &op) {
    if (const auto *lbl = std::get_if<OpLabel>(&op))
        return &lbl->name;
    return nullptr;
}

} // namespace

void LivenessAnalysis::run(const MFunction &func) {
    const std::size_t n = func.blocks.size();
    succs_.assign(n, {});
    gen_.assign(n, {});
    kill_.assign(n, {});
    blockIndex_.clear();

    buildBlockIndex(func);
    buildCFG(func);
    preds_ = viper::codegen::ra::buildPredecessors(succs_);
    computeGenKill(func);

    // Delegate to the shared backward dataflow solver.
    auto result = viper::codegen::ra::solveBackwardDataflow(succs_, gen_, kill_);
    liveIn_ = std::move(result.liveIn);
    liveOut_ = std::move(result.liveOut);
}

void LivenessAnalysis::buildBlockIndex(const MFunction &func) {
    for (std::size_t i = 0; i < func.blocks.size(); ++i)
        blockIndex_[func.blocks[i].label] = i;
}

void LivenessAnalysis::buildCFG(const MFunction &func) {
    for (std::size_t bi = 0; bi < func.blocks.size(); ++bi) {
        const auto &block = func.blocks[bi];
        bool hasExplicitSucc = false;

        for (const auto &instr : block.instructions) {
            if (instr.opcode == MOpcode::JMP) {
                // JMP label — single successor.
                for (const auto &op : instr.operands) {
                    if (const auto *lbl = getLabel(op)) {
                        auto it = blockIndex_.find(*lbl);
                        if (it != blockIndex_.end()) {
                            succs_[bi].push_back(it->second);
                        }
                    }
                }
                hasExplicitSucc = true;
            } else if (instr.opcode == MOpcode::JCC) {
                // JCC condCode, label — conditional successor (fallthrough is
                // the next block in layout order, added below).
                for (const auto &op : instr.operands) {
                    if (const auto *lbl = getLabel(op)) {
                        auto it = blockIndex_.find(*lbl);
                        if (it != blockIndex_.end()) {
                            succs_[bi].push_back(it->second);
                        }
                    }
                }
                // JCC always has a fallthrough to the next block.
                if (bi + 1 < func.blocks.size()) {
                    succs_[bi].push_back(bi + 1);
                }
                hasExplicitSucc = true;
            } else if (instr.opcode == MOpcode::RET) {
                hasExplicitSucc = true; // No successor — function exit.
            }
        }

        // If no explicit terminator, fall through to the next block.
        if (!hasExplicitSucc && bi + 1 < func.blocks.size()) {
            succs_[bi].push_back(bi + 1);
        }
    }
}

void LivenessAnalysis::collectVregs(const MInstr &instr,
                                    std::vector<uint16_t> &uses,
                                    std::vector<uint16_t> &defs) {
    // Classify operands based on opcode — simplified version of classifyOperands.
    // For liveness, we need to know which vregs are used and which are defined.
    // Default: operand 0 is use+def, rest are use-only.
    // Pure defs: MOVrr[0], MOVri[0], MOVmr[0], LEA[0], SETcc[1],
    //            MOVZXrr32[0], CVTSI2SD[0], CVTTSD2SI[0], MOVQrx[0],
    //            MOVSDrr[0], MOVSDmr[0], MOVUPSmr[0], XORrr32[0]

    auto isDefOnly = [&](std::size_t idx) -> bool {
        switch (instr.opcode) {
            case MOpcode::MOVrr:
            case MOpcode::MOVri:
            case MOpcode::MOVmr:
            case MOpcode::LEA:
            case MOpcode::MOVZXrr32:
            case MOpcode::CVTSI2SD:
            case MOpcode::CVTTSD2SI:
            case MOpcode::MOVQrx:
            case MOpcode::MOVSDrr:
            case MOpcode::MOVSDmr:
            case MOpcode::MOVUPSmr:
            case MOpcode::XORrr32:
                return idx == 0;
            case MOpcode::SETcc:
                return idx == 1;
            default:
                return false;
        }
    };

    auto isUseDef = [&](std::size_t idx) -> bool {
        switch (instr.opcode) {
            case MOpcode::ADDrr:
            case MOpcode::SUBrr:
            case MOpcode::IMULrr:
            case MOpcode::FADD:
            case MOpcode::FSUB:
            case MOpcode::FMUL:
            case MOpcode::FDIV:
            case MOpcode::ADDri:
            case MOpcode::CMOVNErr:
            case MOpcode::ANDrr:
            case MOpcode::ORrr:
            case MOpcode::XORrr:
            case MOpcode::SHLrc:
            case MOpcode::SHRrc:
            case MOpcode::SARrc:
            case MOpcode::SHLri:
            case MOpcode::SHRri:
            case MOpcode::SARri:
            case MOpcode::ANDri:
            case MOpcode::ORri:
            case MOpcode::XORri:
                return idx == 0;
            default:
                return false;
        }
    };

    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto &op = instr.operands[idx];

        // Handle register operands.
        if (const auto *reg = std::get_if<OpReg>(&op)) {
            if (reg->isPhys)
                continue; // Physical registers don't participate in vreg liveness.

            if (isDefOnly(idx)) {
                defs.push_back(reg->idOrPhys);
            } else if (isUseDef(idx)) {
                uses.push_back(reg->idOrPhys);
                defs.push_back(reg->idOrPhys);
            } else {
                uses.push_back(reg->idOrPhys);
            }
        }
        // Handle memory operand base/index registers as uses.
        else if (const auto *mem = std::get_if<OpMem>(&op)) {
            if (!mem->base.isPhys)
                uses.push_back(mem->base.idOrPhys);
            if (mem->hasIndex && !mem->index.isPhys)
                uses.push_back(mem->index.idOrPhys);
        }
    }
}

void LivenessAnalysis::computeGenKill(const MFunction &func) {
    for (std::size_t bi = 0; bi < func.blocks.size(); ++bi) {
        auto &gen = gen_[bi];
        auto &kill = kill_[bi];
        const auto &block = func.blocks[bi];

        for (const auto &instr : block.instructions) {
            std::vector<uint16_t> uses;
            std::vector<uint16_t> defs;
            collectVregs(instr, uses, defs);

            // gen: vregs used before being defined in this block.
            for (uint16_t u : uses) {
                if (kill.count(u) == 0)
                    gen.insert(u);
            }
            // kill: vregs defined in this block.
            for (uint16_t d : defs) {
                kill.insert(d);
            }
        }
    }
}

const std::unordered_set<uint16_t> &LivenessAnalysis::liveOut(std::size_t blockIdx) const {
    return liveOut_[blockIdx];
}

const std::unordered_set<uint16_t> &LivenessAnalysis::liveIn(std::size_t blockIdx) const {
    return liveIn_[blockIdx];
}

const std::vector<std::size_t> &LivenessAnalysis::successors(std::size_t blockIdx) const {
    return succs_[blockIdx];
}

const std::vector<std::size_t> &LivenessAnalysis::predecessors(std::size_t blockIdx) const {
    return preds_[blockIdx];
}

} // namespace viper::codegen::x64::ra
