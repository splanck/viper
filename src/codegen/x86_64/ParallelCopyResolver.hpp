//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/ParallelCopyResolver.hpp
// Purpose: Backward-compatible forwarding header to common ParallelCopyResolver.
// Key invariants: Copy resolution order is deterministic; cycles are broken via
//                 a temporary register so that no source is overwritten before
//                 it has been read.
// Ownership/Lifetime: Stateless wrapper; all state lives in the common template
//                     instantiation and the caller-supplied CopyEmitter.
// Links: src/codegen/common/ParallelCopyResolver.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/ParallelCopyResolver.hpp"
#include "codegen/x86_64/TargetX64.hpp"

namespace viper::codegen::x64
{

/// @brief x86-64 specialisation of CopyPair (source/destination register pair).
using CopyPair = viper::codegen::common::CopyPair<RegClass>;
/// @brief x86-64 specialisation of CopyEmitter (callback interface for emitting moves).
using CopyEmitter = viper::codegen::common::CopyEmitter<RegClass>;

/// @brief Resolve a set of parallel register copies into a safe sequential order.
/// @param pairs The parallel copy pairs to resolve (consumed by move).
/// @param E The emitter callback used to output each sequential move instruction.
inline void resolveParallelCopies(std::vector<CopyPair> pairs, CopyEmitter &E)
{
    viper::codegen::common::resolveParallelCopies<RegClass>(std::move(pairs), E);
}

} // namespace viper::codegen::x64
