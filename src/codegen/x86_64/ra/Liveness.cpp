//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/ra/Liveness.cpp
// Purpose: Implementation of CFG-aware liveness analysis for x86-64 regalloc.
//          Computes per-block liveIn/liveOut sets using standard backward
//          dataflow equations over the machine IR control-flow graph.
// Key invariants:
//   - gen[B] = vregs used in B before being defined.
//   - kill[B] = vregs defined in B.
//   - liveOut[B] = union of liveIn[S] for all successors S.
//   - liveIn[B] = gen[B] union (liveOut[B] - kill[B]).
//   - Iteration terminates when no set changes (monotone lattice).
// Ownership/Lifetime:
//   - Operates on const MIR reference; results stored on the analysis instance.
// Links: codegen/x86_64/ra/Liveness.hpp,
//        codegen/x86_64/OperandRoles.hpp,
//        codegen/common/ra/DataflowLiveness.hpp
//
//===----------------------------------------------------------------------===//

#include "Liveness.hpp"

#include "codegen/common/ra/DataflowLiveness.hpp"
#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::x64::ra {

namespace {

/// Extract the label target from an OpLabel operand.
const std::string *getLabel(const Operand &op) {
    if (const auto *lbl = std::get_if<OpLabel>(&op))
        return &lbl->name;
    return nullptr;
}

/// @brief Append the successor index for each label operand of @p instr.
/// @details Used by CFG construction: every label-typed operand that names
///          a known block becomes a successor of the block whose terminator
///          we are analysing. Returns true if at least one successor was
///          appended so the caller can detect dead-end terminators.
/// @param blockIndex Map from block label to block index.
/// @param succs Successor list being appended to.
/// @param instr Terminator instruction supplying labels.
/// @return True if any successor was appended.
bool addLabelSuccessor(const std::unordered_map<std::string, std::size_t> &blockIndex,
                       std::vector<std::size_t> &succs,
                       const MInstr &instr) {
    bool added = false;
    for (const auto &op : instr.operands) {
        if (const auto *lbl = getLabel(op)) {
            auto it = blockIndex.find(*lbl);
            if (it != blockIndex.end()) {
                succs.push_back(it->second);
                added = true;
            }
        }
    }
    return added;
}

/// @brief Predicate: does @p opcode terminate control flow for a block?
/// @details The four MIR opcodes that may end a block — direct/conditional
///          jumps, returns, and the trap instruction used for unreachable
///          paths. Anything else (CALL, ALU ops) is treated as falling
///          through to the next block.
bool isControlTerminator(MOpcode opcode) {
    return opcode == MOpcode::JMP || opcode == MOpcode::JCC || opcode == MOpcode::RET ||
           opcode == MOpcode::UD2;
}

} // namespace

/// @brief Top-level: build CFG, gen/kill, and solve backward dataflow.
/// @details Initialises per-block data containers, populates the label
///          index, builds successor/predecessor relations, computes
///          gen/kill from the operand role tables, then delegates to the
///          shared backward dataflow solver. The final liveIn/liveOut
///          vectors are stored on this instance for later queries.
/// @param func Machine function to analyse (consumed read-only).
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

/// @brief Build the label -> block-index map used by CFG construction.
/// @details A simple linear scan; later passes consult this map to resolve
///          a JMP/JCC's label operand to the destination block index.
void LivenessAnalysis::buildBlockIndex(const MFunction &func) {
    for (std::size_t i = 0; i < func.blocks.size(); ++i)
        blockIndex_[func.blocks[i].label] = i;
}

/// @brief Compute the successor list for every block in @p func.
/// @details Handles three terminator patterns:
///          - @c JMP after a @c JCC: both labels are successors (the JCC
///            two-way fork plus the following unconditional jump),
///          - @c JMP alone: the jump label is the sole successor,
///          - @c JCC alone: the explicit label plus the next physical block
///            (the JCC's implicit fall-through).
///          @c RET / @c UD2 leave the successor list empty (no exit).
void LivenessAnalysis::buildCFG(const MFunction &func) {
    for (std::size_t bi = 0; bi < func.blocks.size(); ++bi) {
        const auto &block = func.blocks[bi];
        auto termIt =
            std::find_if(block.instructions.rbegin(),
                         block.instructions.rend(),
                         [](const MInstr &instr) { return isControlTerminator(instr.opcode); });
        if (termIt == block.instructions.rend()) {
            if (bi + 1 < func.blocks.size()) {
                succs_[bi].push_back(bi + 1);
            }
            continue;
        }

        const auto termIndex =
            static_cast<std::size_t>(std::distance(termIt, block.instructions.rend()) - 1);
        const MInstr &term = *termIt;
        if (term.opcode == MOpcode::RET) {
            continue;
        }

        if (term.opcode == MOpcode::JMP) {
            for (std::size_t scan = termIndex; scan > 0; --scan) {
                const MInstr &candidate = block.instructions[scan - 1];
                if (candidate.opcode == MOpcode::JCC) {
                    addLabelSuccessor(blockIndex_, succs_[bi], candidate);
                    break;
                }
                if (candidate.opcode == MOpcode::JMP || candidate.opcode == MOpcode::RET ||
                    candidate.opcode == MOpcode::UD2) {
                    break;
                }
            }
            addLabelSuccessor(blockIndex_, succs_[bi], term);
        } else if (term.opcode == MOpcode::JCC) {
            addLabelSuccessor(blockIndex_, succs_[bi], term);
            if (bi + 1 < func.blocks.size()) {
                succs_[bi].push_back(bi + 1);
            }
        }

        auto &succs = succs_[bi];
        std::sort(succs.begin(), succs.end());
        succs.erase(std::unique(succs.begin(), succs.end()), succs.end());
    }
}

/// @brief Decompose an instruction's operands into use/def vreg lists.
/// @details Iterates operands and consults the operand-role table to
///          decide whether each register reference is a use, a def, or
///          both. Memory operands contribute base/index as uses since
///          they are read while computing the effective address.
///          Physical registers are skipped — only virtual-register
///          liveness is tracked at this stage.
/// @param instr Instruction whose operands are decomposed.
/// @param uses Output: virtual registers read by @p instr.
/// @param defs Output: virtual registers written by @p instr.
void LivenessAnalysis::collectVregs(const MInstr &instr,
                                    std::vector<uint16_t> &uses,
                                    std::vector<uint16_t> &defs) {
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto &op = instr.operands[idx];

        // Handle register operands.
        if (const auto *reg = std::get_if<OpReg>(&op)) {
            if (reg->isPhys)
                continue; // Physical registers don't participate in vreg liveness.

            const auto [isUse, isDef] = operandRoles(instr, idx);
            if (isUse)
                uses.push_back(reg->idOrPhys);
            if (isDef)
                defs.push_back(reg->idOrPhys);
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

/// @brief Compute @c gen and @c kill sets for every block.
/// @details For each block, an instruction's uses contribute to @c gen
///          only if they are not already killed earlier in the block
///          (i.e., upward-exposed uses). Defs are added to @c kill
///          unconditionally. This formulation matches the textbook
///          backward dataflow equations for liveness.
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
