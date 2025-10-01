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
#include <array>
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

void Lowerer::setManualHelperRequired(ManualRuntimeHelper helper)
{
    manualHelperRequirements_[manualRuntimeHelperIndex(helper)] = true;
}

bool Lowerer::isManualHelperRequired(ManualRuntimeHelper helper) const
{
    return manualHelperRequirements_[manualRuntimeHelperIndex(helper)];
}

void Lowerer::resetManualHelpers()
{
    manualHelperRequirements_.fill(false);
}

void Lowerer::requireArrayI32New()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32New);
}

void Lowerer::requireArrayI32Resize()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Resize);
}

void Lowerer::requireArrayI32Len()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Len);
}

void Lowerer::requireArrayI32Get()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Get);
}

void Lowerer::requireArrayI32Set()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Set);
}

void Lowerer::requireArrayI32Retain()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Retain);
}

void Lowerer::requireArrayI32Release()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Release);
}

void Lowerer::requireArrayOobPanic()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayOobPanic);
}

void Lowerer::requireOpenErrVstr()
{
    setManualHelperRequired(ManualRuntimeHelper::OpenErrVstr);
}

void Lowerer::requireCloseErr()
{
    setManualHelperRequired(ManualRuntimeHelper::CloseErr);
}

void Lowerer::requirePrintlnChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::PrintlnChErr);
}

void Lowerer::requireLineInputChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::LineInputChErr);
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

    struct ManualHelperDescriptor
    {
        std::string_view name;
        ManualRuntimeHelper helper;
        [[maybe_unused]] void (Lowerer::*requireHook)();
    };

    static constexpr std::array<ManualHelperDescriptor, manualRuntimeHelperCount> manualHelpers{{
        {"rt_arr_i32_new", ManualRuntimeHelper::ArrayI32New, &Lowerer::requireArrayI32New},
        {"rt_arr_i32_resize", ManualRuntimeHelper::ArrayI32Resize, &Lowerer::requireArrayI32Resize},
        {"rt_arr_i32_len", ManualRuntimeHelper::ArrayI32Len, &Lowerer::requireArrayI32Len},
        {"rt_arr_i32_get", ManualRuntimeHelper::ArrayI32Get, &Lowerer::requireArrayI32Get},
        {"rt_arr_i32_set", ManualRuntimeHelper::ArrayI32Set, &Lowerer::requireArrayI32Set},
        {"rt_arr_i32_retain", ManualRuntimeHelper::ArrayI32Retain, &Lowerer::requireArrayI32Retain},
        {"rt_arr_i32_release", ManualRuntimeHelper::ArrayI32Release, &Lowerer::requireArrayI32Release},
        {"rt_arr_oob_panic", ManualRuntimeHelper::ArrayOobPanic, &Lowerer::requireArrayOobPanic},
        {"rt_open_err_vstr", ManualRuntimeHelper::OpenErrVstr, &Lowerer::requireOpenErrVstr},
        {"rt_close_err", ManualRuntimeHelper::CloseErr, &Lowerer::requireCloseErr},
        {"rt_println_ch_err", ManualRuntimeHelper::PrintlnChErr, &Lowerer::requirePrintlnChErr},
        {"rt_line_input_ch_err", ManualRuntimeHelper::LineInputChErr, &Lowerer::requireLineInputChErr},
    }};

    auto declareManual = [&](std::string_view name) {
        if (const auto *desc = il::runtime::findRuntimeDescriptor(name))
            b.addExtern(std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
    };

    for (const auto &helper : manualHelpers)
    {
        if (isManualHelperRequired(helper.helper))
            declareManual(helper.name);
    }
}

} // namespace il::frontends::basic
