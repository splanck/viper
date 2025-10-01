// File: src/frontends/basic/LowerRuntime.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements runtime tracking and declaration utilities for BASIC lowering.
// Key invariants: Runtime declarations are emitted once in deterministic order.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

// Requires the consolidated Lowerer interface for runtime tracking declarations.
#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <cassert>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

std::size_t RuntimeHelperTracker::RuntimeFeatureHash::operator()(RuntimeFeature f) const
{
    return static_cast<std::size_t>(f);
}

void RuntimeHelperTracker::reset()
{
    requested_.reset();
    ordered_.clear();
    tracked_.clear();
}

void RuntimeHelperTracker::requestHelper(RuntimeFeature feature)
{
    requested_.set(static_cast<std::size_t>(feature));
}

bool RuntimeHelperTracker::isHelperNeeded(RuntimeFeature feature) const
{
    return requested_.test(static_cast<std::size_t>(feature));
}

void RuntimeHelperTracker::trackRuntime(RuntimeFeature feature)
{
    requestHelper(feature);
    if (tracked_.insert(feature).second)
        ordered_.push_back(feature);
}

namespace
{
/// @brief Declare a runtime extern using the canonical signature database.
/// @details Consults the runtime descriptor registry so declarations, handler
///          wiring, and lowering metadata remain synchronized.
/// @param b IR builder that will receive the extern declaration.
/// @param desc Runtime descriptor describing the helper to declare.
/// @note This helper mutates the builder but leaves the tracker state untouched.
void declareRuntimeExtern(build::IRBuilder &b, const il::runtime::RuntimeDescriptor &desc)
{
    b.addExtern(std::string(desc.name), desc.signature.retType, desc.signature.paramTypes);
}
} // namespace

/// @brief Declare every runtime helper required by the current lowering run.
/// @details Emits baseline helpers unconditionally and consults tracker state
///          to determine optional helpers. Ordered helpers are replayed by
///          iterating the recorded sequence so declarations remain
///          deterministic.
/// @param b IR builder used to register extern declarations.
/// @param boundsChecks Whether array bounds helpers should be declared.
void RuntimeHelperTracker::declareRequiredRuntime(build::IRBuilder &b, bool boundsChecks) const
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

    for (RuntimeFeature feature : ordered_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(feature);
        assert(desc && "requested runtime feature missing from registry");
        declareRuntimeExtern(b, *desc);
    }
}

void Lowerer::requireArrayI32New()
{
    needsArrI32New = true;
}

void Lowerer::requireArrayI32Resize()
{
    needsArrI32Resize = true;
}

void Lowerer::requireArrayI32Len()
{
    needsArrI32Len = true;
}

void Lowerer::requireArrayI32Get()
{
    needsArrI32Get = true;
}

void Lowerer::requireArrayI32Set()
{
    needsArrI32Set = true;
}

void Lowerer::requireArrayI32Retain()
{
    needsArrI32Retain = true;
}

void Lowerer::requireArrayI32Release()
{
    needsArrI32Release = true;
}

void Lowerer::requireArrayOobPanic()
{
    needsArrOobPanic = true;
}

void Lowerer::requireOpenErrVstr()
{
    needsOpenErrVstr = true;
}

void Lowerer::requireCloseErr()
{
    needsCloseErr = true;
}

void Lowerer::requirePrintlnChErr()
{
    needsPrintlnChErr = true;
}

void Lowerer::requireLineInputChErr()
{
    needsLineInputChErr = true;
}

void Lowerer::requestHelper(RuntimeFeature feature)
{
    runtimeTracker.requestHelper(feature);
}

bool Lowerer::isHelperNeeded(RuntimeFeature feature) const
{
    return runtimeTracker.isHelperNeeded(feature);
}

void Lowerer::trackRuntime(RuntimeFeature feature)
{
    runtimeTracker.trackRuntime(feature);
}

void Lowerer::declareRequiredRuntime(build::IRBuilder &b)
{
    runtimeTracker.declareRequiredRuntime(b, boundsChecks);

    auto declareManual = [&](std::string_view name) {
        if (const auto *desc = il::runtime::findRuntimeDescriptor(name))
            b.addExtern(std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
    };

    if (needsArrI32New)
        declareManual("rt_arr_i32_new");
    if (needsArrI32Resize)
        declareManual("rt_arr_i32_resize");
    if (needsArrI32Len)
        declareManual("rt_arr_i32_len");
    if (needsArrI32Get)
        declareManual("rt_arr_i32_get");
    if (needsArrI32Set)
        declareManual("rt_arr_i32_set");
    if (needsArrI32Retain)
        declareManual("rt_arr_i32_retain");
    if (needsArrI32Release)
        declareManual("rt_arr_i32_release");
    if (needsArrOobPanic)
        declareManual("rt_arr_oob_panic");
    if (needsOpenErrVstr)
        declareManual("rt_open_err_vstr");
    if (needsCloseErr)
        declareManual("rt_close_err");
    if (needsPrintlnChErr)
        declareManual("rt_println_ch_err");
    if (needsLineInputChErr)
        declareManual("rt_line_input_ch_err");
}

} // namespace il::frontends::basic
