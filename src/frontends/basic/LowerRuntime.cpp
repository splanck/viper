// File: src/frontends/basic/LowerRuntime.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements runtime tracking and declaration utilities for BASIC lowering.
// Key invariants: Runtime declarations are emitted once in deterministic order.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <cassert>
#include <string>
#include <string_view>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Hash runtime helper identifiers by their enumerator value.
/// @param f Runtime helper enumerator to hash.
/// @return Hash value derived from the underlying enum ordinal.
std::size_t Lowerer::RuntimeFeatureTracker::RuntimeFeatureHash::operator()(RuntimeFeature f) const
{
    return static_cast<std::size_t>(f);
}

/// @brief Reset all runtime tracking state for a new lowering run.
void Lowerer::RuntimeFeatureTracker::reset()
{
    features_.reset();
    ordered_.clear();
    seen_.clear();
}

namespace
{
/// @brief Declare a runtime extern using the canonical signature database.
/// @details Consults the runtime descriptor registry so declarations, handler
///          wiring, and lowering metadata remain synchronized.
/// @param b IR builder that will receive the extern declaration.
/// @param desc Runtime descriptor describing the helper to declare.
/// @note This helper mutates the builder but does not touch runtime tracking
///       state managed by RuntimeFeatureTracker.
void declareRuntimeExtern(build::IRBuilder &b, const il::runtime::RuntimeDescriptor &desc)
{
    b.addExtern(std::string(desc.name), desc.signature.retType, desc.signature.paramTypes);
}
} // namespace

/// @brief Record that a runtime helper is needed for the current lowering run.
/// @param feature Runtime helper enumerator requested by lowering or scanning.
void Lowerer::RuntimeFeatureTracker::requestHelper(RuntimeFeature feature)
{
    features_.set(static_cast<std::size_t>(feature));
}

/// @brief Check whether a helper has been requested for the current run.
/// @param feature Runtime helper enumerator to query.
/// @return True when the helper should be declared.
bool Lowerer::RuntimeFeatureTracker::isHelperNeeded(RuntimeFeature feature) const
{
    return features_.test(static_cast<std::size_t>(feature));
}

/// @brief Track a runtime helper requiring deterministic emission ordering.
/// @param feature Runtime helper enumerator requested by lowering.
void Lowerer::RuntimeFeatureTracker::trackRuntime(RuntimeFeature feature)
{
    requestHelper(feature);
    if (seen_.insert(feature).second)
        ordered_.push_back(feature);
}

/// @brief Declare every runtime helper required by the current lowering run.
/// @details Emits baseline helpers unconditionally and consults lowering
///          toggles recorded through @ref requestHelper along with the
///          @p boundsChecks flag to decide which additional helpers are
///          necessary. Runtime math helpers recorded through @ref trackRuntime
///          are emitted in the order they were first requested to guarantee
///          deterministic declarations.
/// @param b IR builder used to register extern declarations.
/// @param boundsChecks Whether bounds-check helpers should be declared.
void Lowerer::RuntimeFeatureTracker::declareRequiredRuntime(build::IRBuilder &b,
                                                            bool boundsChecks) const
{
    const auto &registry = il::runtime::runtimeRegistry();
    for (const auto &entry : registry)
    {
        switch (entry.lowering.kind)
        {
            case il::runtime::RuntimeLoweringKind::Always:
                declareRuntimeExtern(b, entry);
                break;
            case il::runtime::RuntimeLoweringKind::BoundsChecked:
                if (boundsChecks)
                    declareRuntimeExtern(b, entry);
                break;
            case il::runtime::RuntimeLoweringKind::Feature:
                if (!entry.lowering.ordered &&
                    isHelperNeeded(entry.lowering.feature))
                    declareRuntimeExtern(b, entry);
                break;
            case il::runtime::RuntimeLoweringKind::Manual:
                break;
        }
    }

    for (RuntimeFeature feature : ordered_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(feature);
        assert(desc && "requested runtime feature missing from registry");
        declareRuntimeExtern(b, *desc);
    }
}

void Lowerer::declareRequiredRuntime(build::IRBuilder &b)
{
    runtimeTracker.declareRequiredRuntime(b, boundsChecks);
}

} // namespace il::frontends::basic
