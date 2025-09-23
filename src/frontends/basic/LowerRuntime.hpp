// File: src/frontends/basic/LowerRuntime.hpp
// Purpose: Declares runtime tracking and declaration helpers for BASIC lowering.
// Key invariants: Runtime declarations are emitted exactly once per function.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md
#pragma once

#include "il/runtime/RuntimeSignatures.hpp"
#include <bitset>
#include <unordered_set>
#include <vector>

namespace il::build
{
class IRBuilder;
} // namespace il::build

namespace il::frontends::basic
{

/// @brief Tracks runtime helper usage across scanning and lowering.
/// @invariant Helpers are declared at most once and maintain first-use order.
/// @ownership Owned by Lowerer; stores transient state per lowering run.
class RuntimeHelperTracker
{
  public:
    using RuntimeFeature = il::runtime::RuntimeFeature;

    /// @brief Reset helper tracking to an empty state.
    void reset();

    /// @brief Mark a runtime helper as required.
    void requestHelper(RuntimeFeature feature);

    /// @brief Query whether a runtime helper has been requested.
    [[nodiscard]] bool isHelperNeeded(RuntimeFeature feature) const;

    /// @brief Record an ordered runtime helper requirement.
    void trackRuntime(RuntimeFeature feature);

    /// @brief Declare all helpers requested during the current lowering run.
    void declareRequiredRuntime(build::IRBuilder &b, bool boundsChecks) const;

  private:
    struct RuntimeFeatureHash
    {
        std::size_t operator()(RuntimeFeature f) const;
    };

    static constexpr std::size_t kRuntimeFeatureCount =
        static_cast<std::size_t>(RuntimeFeature::Count);

    std::bitset<kRuntimeFeatureCount> requested_;
    std::vector<RuntimeFeature> ordered_;
    std::unordered_set<RuntimeFeature, RuntimeFeatureHash> tracked_;
};

} // namespace il::frontends::basic
