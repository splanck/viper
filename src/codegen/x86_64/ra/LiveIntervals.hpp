//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/LiveIntervals.hpp
// Purpose: Declare data structures and analysis utilities for block-local
//          live interval construction used by the linear-scan allocator.
// Key invariants: Live intervals are expressed as half-open instruction index
//                 ranges relative to per-function numbering. Analysis is
//                 deterministic and does not mutate the input Machine IR.
// Ownership/Lifetime: Live interval data is stored in value-owned containers
//                     that remain valid for the lifetime of the analysis
//                     instance.
// Links: docs/codemap.md, src/codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"

#include <cstddef>
#include <unordered_map>

namespace viper::codegen::x64::ra
{

/// @brief Half-open interval describing the lifetime of a virtual register.
/// @invariant `start` <= `end` and both are measured in instruction indices.
struct LiveInterval
{
    uint16_t vreg{0U};           ///< Virtual register identifier.
    RegClass cls{RegClass::GPR}; ///< Register class constraining allocation.
    std::size_t start{0U};       ///< Index of the first instruction touching the vreg.
    std::size_t end{0U};         ///< Index just past the last instruction touching the vreg.
};

/// @brief Result of the local live interval analysis over a machine function.
class LiveIntervals
{
  public:
    LiveIntervals() = default;

    /// @brief Compute instruction-local live intervals for @p func.
    void run(const MFunction &func);

    /// @brief Retrieve the interval for a virtual register if it was observed.
    [[nodiscard]] const LiveInterval *lookup(uint16_t vreg) const noexcept;

  private:
    std::unordered_map<uint16_t, LiveInterval> intervals_{};
};

} // namespace viper::codegen::x64::ra
