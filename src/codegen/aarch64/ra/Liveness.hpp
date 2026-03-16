//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/Liveness.hpp
// Purpose: CFG-aware liveness analysis for the AArch64 register allocator.
//          Builds the control-flow graph from AArch64 MIR terminators (Br,
//          BCond, Cbz) and computes per-block gen/kill/liveIn/liveOut sets
//          via backward dataflow iteration, split by register class (GPR/FPR).
// Key invariants:
//   - liveOut[B] = union of liveIn[S] for all successors S of B
//   - liveIn[B] = gen[B] union (liveOut[B] - kill[B])
//   - GPR and FPR liveness computed independently
//   - Uses shared dataflow solver from common/ra/DataflowLiveness.hpp
// Ownership/Lifetime:
//   - Value-owned containers valid for the lifetime of the LivenessAnalysis
// Links: src/codegen/common/ra/DataflowLiveness.hpp,
//        src/codegen/aarch64/ra/Allocator.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::aarch64::ra
{

/// @brief CFG-aware liveness analysis over AArch64 Machine IR blocks.
///
/// @details Builds the control-flow graph from Br/BCond/Cbz terminators,
///          computes gen/kill sets per block split by register class, and
///          delegates the fixed-point dataflow iteration to the shared solver.
class LivenessAnalysis
{
  public:
    LivenessAnalysis() = default;

    /// @brief Run the full analysis: build CFG, compute gen/kill, solve dataflow.
    void run(const MFunction &func);

    /// @brief Get the set of GPR vregs live at the exit of block @p blockIdx.
    [[nodiscard]] const std::unordered_set<uint16_t> &liveOutGPR(std::size_t blockIdx) const;

    /// @brief Get the set of FPR vregs live at the exit of block @p blockIdx.
    [[nodiscard]] const std::unordered_set<uint16_t> &liveOutFPR(std::size_t blockIdx) const;

    /// @brief Get the successor block indices for block @p blockIdx.
    [[nodiscard]] const std::vector<std::size_t> &successors(std::size_t blockIdx) const;

    /// @brief Get the predecessor block indices for block @p blockIdx.
    [[nodiscard]] const std::vector<std::size_t> &predecessors(std::size_t blockIdx) const;

    /// @brief Number of blocks in the analyzed function.
    [[nodiscard]] std::size_t numBlocks() const
    {
        return succs_.size();
    }

  private:
    std::unordered_map<std::string, std::size_t> blockIndex_;
    std::vector<std::vector<std::size_t>> succs_;
    std::vector<std::vector<std::size_t>> preds_;
    std::vector<std::unordered_set<uint16_t>> liveOutGPR_;
    std::vector<std::unordered_set<uint16_t>> liveOutFPR_;

    /// @brief Build the label -> block index map.
    void buildBlockIndex(const MFunction &func);

    /// @brief Build successor relations from Br/BCond/Cbz terminators.
    void buildCFG(const MFunction &func);

    /// @brief Compute gen/kill sets and solve dataflow for both register classes.
    void computeLiveOutSets(const MFunction &func);
};

} // namespace viper::codegen::aarch64::ra
