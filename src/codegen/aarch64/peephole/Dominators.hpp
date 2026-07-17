//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/peephole/Dominators.hpp
// Purpose: Shared bit-vector dominator analysis for the AArch64 peephole
//          optimizer. Replaces three previously-duplicated implementations
//          (one bitset in Peephole.cpp, two set-based in LoopOpt.cpp).
//
// Key invariants:
//   - Entry block (index 0) dominates only itself in the result.
//   - Unreachable blocks (no predecessors after index 0) dominate only themselves.
//   - Predecessor list is indexed: preds[i] is the set of predecessor block
//     indices for block i. Callers must pre-build this list; the analysis
//     itself does not inspect terminators.
//
// Ownership/Lifetime:
//   - Free function returning a value type; no shared state.
//
// Links: codegen/aarch64/peephole/PeepholeCommon.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zanna::codegen::aarch64::peephole {

/// @brief Bit-vector dominator sets — one 64-bit-word vector per basic block.
///        `dom[i][w]` is the wᵗʰ 64-bit word of the dominator bitset for block i.
struct DominatorSets {
    std::vector<std::vector<std::uint64_t>> bits;
    std::size_t blockCount = 0;

    /// @brief Return true if @p block is dominated by @p dominator.
    [[nodiscard]] bool dominates(std::size_t dominator, std::size_t block) const noexcept {
        if (block >= bits.size())
            return false;
        const std::size_t word = dominator / 64;
        if (word >= bits[block].size())
            return false;
        return (bits[block][word] & (std::uint64_t{1} << (dominator % 64))) != 0;
    }
};

/// @brief Compute dominator sets for all blocks using iterative bit-vector dataflow.
/// @details Standard iterative algorithm: entry block dominates only itself, all
///          others start as the universal set and are intersected with each
///          predecessor's dominator set until fixpoint.
/// @param blockCount Total number of blocks (entry is index 0).
/// @param preds      preds[i] is the list of predecessor block indices for block i.
///                   For the entry block this is typically empty.
/// @return Dominator sets sized to @p blockCount.
[[nodiscard]] DominatorSets computeDominators(std::size_t blockCount,
                                              const std::vector<std::vector<std::size_t>> &preds);

} // namespace zanna::codegen::aarch64::peephole
