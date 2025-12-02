//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LowerILToMIR.hpp
// Purpose: Minimal IL→MIR lowering adapter for AArch64 (Phase A).
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <unordered_map>

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "il/core/Function.hpp"

namespace viper::codegen::aarch64
{

/// @brief Lowers IL functions to AArch64 Machine IR (MIR).
///
/// This class implements the instruction selection phase of code generation,
/// converting IL instructions into target-specific MIR instructions.
///
/// @invariant The lowerer is stateless between function calls - all per-function
///            state is cleared at the start of each lowerFunction() call.
/// @invariant The TargetInfo reference must remain valid for the lifetime of
///            this object.
class LowerILToMIR
{
  public:
    explicit LowerILToMIR(const TargetInfo &ti) noexcept : ti_(&ti) {}

    /// @brief Lower an IL function to MIR.
    /// @param fn The IL function to lower.
    /// @return The lowered MIR function.
    MFunction lowerFunction(const il::core::Function &fn) const;

  private:
    const TargetInfo *ti_{};
};

} // namespace viper::codegen::aarch64
