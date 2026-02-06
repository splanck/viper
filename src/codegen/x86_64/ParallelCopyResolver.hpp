//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/ParallelCopyResolver.hpp
// Purpose: Backward-compatible forwarding header to common ParallelCopyResolver.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/ParallelCopyResolver.hpp"
#include "codegen/x86_64/TargetX64.hpp"

namespace viper::codegen::x64
{

// Backward-compatible type aliases — existing code uses these unchanged.
using CopyPair = viper::codegen::common::CopyPair<RegClass>;
using CopyEmitter = viper::codegen::common::CopyEmitter<RegClass>;

inline void resolveParallelCopies(std::vector<CopyPair> pairs, CopyEmitter &E)
{
    viper::codegen::common::resolveParallelCopies<RegClass>(std::move(pairs), E);
}

} // namespace viper::codegen::x64
