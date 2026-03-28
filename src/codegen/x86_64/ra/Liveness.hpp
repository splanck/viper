//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Liveness.hpp
// Purpose: CFG-aware liveness analysis for the x86-64 register allocator.
//          Builds control-flow graph from MachineIR terminators and computes
//          gen/kill/liveIn/liveOut sets via backward dataflow iteration.
// Key invariants:
//   - liveOut[B] = union of liveIn[S] for all successors S of B
//   - liveIn[B] = gen[B] union (liveOut[B] - kill[B])
//   - Fixed-point iteration bounded by kMaxIterations (1000)
//   - Gen/kill computed from virtual register operands only (physregs ignored)
// Ownership/Lifetime:
//   - Value-owned containers valid for the lifetime of the LivenessAnalysis
// Links: src/codegen/x86_64/ra/Allocator.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::x64::ra {

/// @brief CFG-aware liveness analysis over Machine IR blocks.
///
/// @details Builds the control-flow graph from JMP/JCC terminators, computes
///          gen/kill sets per block, and solves the standard backward dataflow
///          equations to produce liveIn/liveOut sets for each block. This replaces
///          the conservative "unconditional spill" hack for cross-block vregs.
class LivenessAnalysis {
  public:
    LivenessAnalysis() = default;

    /// @brief Run the full analysis: build CFG, compute gen/kill, solve dataflow.
    void run(const MFunction &func);

    /// @brief Get the set of vregs live at the exit of block @p blockIdx.
    [[nodiscard]] const std::unordered_set<uint16_t> &liveOut(std::size_t blockIdx) const;

    /// @brief Get the set of vregs live at the entry of block @p blockIdx.
    [[nodiscard]] const std::unordered_set<uint16_t> &liveIn(std::size_t blockIdx) const;

    /// @brief Get the successor block indices for block @p blockIdx.
    [[nodiscard]] const std::vector<std::size_t> &successors(std::size_t blockIdx) const;

    /// @brief Get the predecessor block indices for block @p blockIdx.
    [[nodiscard]] const std::vector<std::size_t> &predecessors(std::size_t blockIdx) const;

    /// @brief Number of blocks in the analyzed function.
    [[nodiscard]] std::size_t numBlocks() const {
        return succs_.size();
    }

  private:
    static constexpr std::size_t kMaxIterations = 1000;

    std::unordered_map<std::string, std::size_t> blockIndex_;
    std::vector<std::vector<std::size_t>> succs_;
    std::vector<std::vector<std::size_t>> preds_;
    std::vector<std::unordered_set<uint16_t>> gen_;
    std::vector<std::unordered_set<uint16_t>> kill_;
    std::vector<std::unordered_set<uint16_t>> liveIn_;
    std::vector<std::unordered_set<uint16_t>> liveOut_;

    /// @brief Build the label → block index map.
    void buildBlockIndex(const MFunction &func);

    /// @brief Build successor/predecessor relations from terminators.
    void buildCFG(const MFunction &func);

    /// @brief Compute gen (upward-exposed uses) and kill (defs) per block.
    void computeGenKill(const MFunction &func);

    /// @brief Collect virtual register references from all operands of an instruction.
    static void collectVregs(const MInstr &instr,
                             std::vector<uint16_t> &uses,
                             std::vector<uint16_t> &defs);
};

} // namespace viper::codegen::x64::ra
