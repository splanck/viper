//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LivenessAnalysis.hpp
// Purpose: Cross-block liveness analysis for IL->MIR lowering.
//
// This header declares functions for analyzing which IL temps are used across
// block boundaries and need spill/reload handling during lowering.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "FrameBuilder.hpp"
#include "il/core/Function.hpp"

#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64
{

/// @brief Result of cross-block liveness analysis.
struct LivenessInfo
{
    /// @brief Map of temp ID to the block index where it's defined.
    std::unordered_map<unsigned, std::size_t> tempDefBlock;

    /// @brief Set of temp IDs that are used in a different block than where defined.
    std::unordered_set<unsigned> crossBlockTemps;

    /// @brief Map of cross-block temp ID to its spill slot offset.
    std::unordered_map<unsigned, int> crossBlockSpillOffset;
};

/// @brief Analyze which temps are used across block boundaries.
/// @details Temps that are defined in one block and used in another must be
///          spilled at definition and reloaded at use, since the register
///          allocator processes blocks independently.
/// @param fn The IL function to analyze.
/// @param allocaTemps Set of temp IDs that are allocas (excluded from analysis).
/// @param fb Frame builder for allocating spill slots.
/// @returns LivenessInfo containing the analysis results.
LivenessInfo analyzeCrossBlockLiveness(const il::core::Function &fn,
                                       const std::unordered_set<unsigned> &allocaTemps,
                                       FrameBuilder &fb);

} // namespace viper::codegen::aarch64
