// File: src/frontends/basic/LowerRuntime.hpp
// Purpose: Declares runtime tracking and declaration helpers for BASIC lowering.
// Key invariants: Runtime declarations are emitted exactly once per function.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md
#pragma once

struct RuntimeFeatureHash
{
    size_t operator()(RuntimeFeature f) const;
};

std::vector<RuntimeFeature> runtimeOrder;
std::unordered_set<RuntimeFeature, RuntimeFeatureHash> runtimeSet;

void requestHelper(RuntimeFeature feature);
bool isHelperNeeded(RuntimeFeature feature) const;
void trackRuntime(RuntimeFeature feature);
void declareRequiredRuntime(build::IRBuilder &b);
