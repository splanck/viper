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
size_t Lowerer::RuntimeFeatureHash::operator()(RuntimeFeature f) const
{
    return static_cast<size_t>(f);
}

namespace
{
/// @brief Declare a runtime extern using the canonical signature database.
/// @details Consults the runtime descriptor registry so declarations, handler
///          wiring, and lowering metadata remain synchronized.
/// @param b IR builder that will receive the extern declaration.
/// @param desc Runtime descriptor describing the helper to declare.
/// @note This helper mutates the builder but does not touch runtimeOrder or
///       runtimeSet tracking.
void declareRuntimeExtern(build::IRBuilder &b, const il::runtime::RuntimeDescriptor &desc)
{
    b.addExtern(std::string(desc.name), desc.signature.retType, desc.signature.paramTypes);
}
} // namespace

/// @brief Declare every runtime helper required by the current lowering run.
/// @details Emits baseline helpers unconditionally and consults lowering
///          toggles recorded through requestHelper/isHelperNeeded along with
///          boundsChecks to decide which additional helpers are necessary. Runtime
///          math helpers requested dynamically are iterated in the order
///          recorded by trackRuntime so declaration emission remains
///          deterministic. Each declaration delegates to declareRuntimeExtern,
///          which looks up the canonical signature via RuntimeSignatures.
/// @param b IR builder used to register extern declarations.
/// @note Assumes runtimeOrder has been populated through trackRuntime calls;
///       missing signatures are enforced by the assertion in
///       declareRuntimeExtern.
void Lowerer::requestHelper(RuntimeFeature feature)
{
    runtimeFeatures.set(static_cast<size_t>(feature));
}

bool Lowerer::isHelperNeeded(RuntimeFeature feature) const
{
    return runtimeFeatures.test(static_cast<size_t>(feature));
}

void Lowerer::declareRequiredRuntime(build::IRBuilder &b)
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
                if (!entry.lowering.ordered && isHelperNeeded(entry.lowering.feature))
                    declareRuntimeExtern(b, entry);
                break;
            case il::runtime::RuntimeLoweringKind::Manual:
                break;
        }
    }

    for (RuntimeFeature feature : runtimeOrder)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(feature);
        assert(desc && "requested runtime feature missing from registry");
        declareRuntimeExtern(b, *desc);
    }
}

/// @brief Record that a runtime helper is needed for the current program.
/// @details Inserts @p fn into runtimeSet, using it as a visited filter, and
///          appends @p fn to runtimeOrder the first time it appears. This
///          preserves deterministic declaration order when
///          declareRequiredRuntime later walks runtimeOrder.
/// @param fn Identifier for the runtime helper that lowering just requested.
/// @post runtimeSet always contains @p fn; runtimeOrder grows only when @p fn
///       was not previously tracked.
void Lowerer::trackRuntime(RuntimeFeature feature)
{
    runtimeFeatures.set(static_cast<size_t>(feature));
    if (runtimeSet.insert(feature).second)
        runtimeOrder.push_back(feature);
}

} // namespace il::frontends::basic
