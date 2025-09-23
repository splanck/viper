// File: src/frontends/basic/LowerRuntime.hpp
// Purpose: Declares runtime tracking and declaration helpers for BASIC lowering.
// Key invariants: Runtime declarations are emitted exactly once per function.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md
#pragma once

class RuntimeFeatureTracker
{
  public:
    void reset();

    void requestHelper(RuntimeFeature feature);
    bool isHelperNeeded(RuntimeFeature feature) const;
    void trackRuntime(RuntimeFeature feature);
    void declareRequiredRuntime(build::IRBuilder &b, bool boundsChecks) const;

  private:
    struct RuntimeFeatureHash
    {
        std::size_t operator()(RuntimeFeature f) const;
    };

    static constexpr std::size_t kRuntimeFeatureCount =
        static_cast<std::size_t>(RuntimeFeature::Count);

    std::bitset<kRuntimeFeatureCount> features_{};
    std::vector<RuntimeFeature> ordered_{};
    std::unordered_set<RuntimeFeature, RuntimeFeatureHash> seen_{};
};
