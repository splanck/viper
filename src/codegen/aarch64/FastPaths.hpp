//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FastPaths.hpp
// Purpose: Fast-path pattern matching for common IL patterns.
// Key invariants: Each fast-path returns a fully-lowered MFunction or nullopt;
//                 fast-path output must be semantically identical to generic lowering.
// Ownership/Lifetime: Stateless free function; borrows references for the
//                     duration of the call and does not retain them.
// Links: codegen/aarch64/fastpaths/FastPathsInternal.hpp, docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "FrameBuilder.hpp"
#include "MachineIR.hpp"
#include "TargetAArch64.hpp"
#include "il/core/Function.hpp"

#include <optional>

namespace viper::codegen::aarch64
{

/// @brief Try fast-path lowering for simple function patterns.
/// @returns The lowered MFunction if a fast-path matched, nullopt otherwise.
std::optional<MFunction> tryFastPaths(const il::core::Function &fn,
                                      const TargetInfo &ti,
                                      FrameBuilder &fb,
                                      MFunction &mf);

} // namespace viper::codegen::aarch64
