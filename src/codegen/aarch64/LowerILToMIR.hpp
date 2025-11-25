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

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "il/core/Function.hpp"

namespace viper::codegen::aarch64
{

class LowerILToMIR
{
  public:
    explicit LowerILToMIR(const TargetInfo &ti) noexcept : ti_(&ti) {}

    MFunction lowerFunction(const il::core::Function &fn) const;

  private:
    const TargetInfo *ti_{};
};

} // namespace viper::codegen::aarch64
