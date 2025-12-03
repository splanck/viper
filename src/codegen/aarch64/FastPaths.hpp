//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FastPaths.hpp
// Purpose: Fast-path pattern matching for common IL patterns.
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
