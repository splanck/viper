//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the bookkeeping that tracks which runtime helpers must be emitted
// while lowering BASIC programs to IL. The helper collects feature requests, is
// aware of mandatory descriptors from the runtime registry, and ensures extern
// declarations are emitted exactly once in a deterministic order.
//
//===----------------------------------------------------------------------===//

// Requires the consolidated Lowerer interface for runtime tracking declarations.
#include "frontends/basic/LowerRuntime.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <unordered_set>

namespace il::frontends::basic
{

/// @brief Compute a hash value for a runtime feature flag.
///
/// Features are represented as small enums, so hashing simply casts to the
/// underlying integer value. This keeps the type usable inside unordered
/// containers without introducing additional dependencies.
///
/// @param f Feature to hash.
/// @return Integer hash suitable for unordered containers.
std::size_t RuntimeHelperTracker::RuntimeFeatureHash::operator()(RuntimeFeature f) const
{
    return static_cast<std::size_t>(f);
}

/// @brief Clear all runtime helper tracking state.
///
/// Drops any pending requests, the deduplicated set, and the ordered replay
/// list so a fresh lowering run can start from a clean slate.
void RuntimeHelperTracker::reset()
{
    requested_.reset();
    ordered_.clear();
    tracked_.clear();
}

/// @brief Mark a runtime helper as required.
///
/// Records the request in the bitset that tracks optional helpers. The tracker
/// does not enforce ordering; it simply remembers the request for later when
/// declarations are emitted.
///
/// @param feature Feature whose helper must be available.
void RuntimeHelperTracker::requestHelper(RuntimeFeature feature)
{
    requested_.set(static_cast<std::size_t>(feature));
}

/// @brief Query whether a feature's helper has been requested.
///
/// Consults the internal bitset to determine whether `requestHelper` was called
/// for the given feature.
///
/// @param feature Feature whose helper requirement is being checked.
/// @return True when the helper has been requested.
bool RuntimeHelperTracker::isHelperNeeded(RuntimeFeature feature) const
{
    return requested_.test(static_cast<std::size_t>(feature));
}

/// @brief Record a runtime helper as used and maintain declaration ordering.
///
/// Ensures the helper is requested and, if it has not been seen before, appends
/// it to the ordered replay list. This preserves deterministic extern emission
/// even when runtime needs arise out of order.
///
/// @param feature Feature whose helper was touched during lowering.
void RuntimeHelperTracker::trackRuntime(RuntimeFeature feature)
{
    // Mark the feature as "needed" for the unordered pass.
    requestHelper(feature);

    // Look up its descriptor to decide if we should queue it for the ordered replay.
    const auto *desc = il::runtime::findRuntimeDescriptor(feature);
    if (!desc)
        return;

    // Only *ordered* Feature-lowered helpers belong in ordered_.
    if (desc->lowering.kind == il::runtime::RuntimeLoweringKind::Feature && desc->lowering.ordered)
    {
        if (tracked_.insert(feature).second)
        {
            ordered_.push_back(feature);
        }
    }
    else
    {
        // Keep bookkeeping for completeness (no push to ordered_).
        tracked_.insert(feature);
    }
}

namespace
{
/// @brief Declare a runtime extern using the canonical signature database.
///
/// Centralises the IRBuilder call so declarations pulled from the runtime
/// registry share a single implementation. Keeping the logic here also ensures
/// future metadata changes only need to be reflected in one location.
///
/// @param b IR builder that will receive the extern declaration.
/// @param desc Runtime descriptor describing the helper to declare.
void declareRuntimeExtern(build::IRBuilder &b, const il::runtime::RuntimeDescriptor &desc)
{
    b.addExtern(std::string(desc.name), desc.signature.retType, desc.signature.paramTypes);
}
} // namespace

/// @brief Declare every runtime helper required by the current lowering run.
///
/// Walks the runtime descriptor registry, emitting helpers that are always
/// needed plus those gated behind feature flags or bounds-check settings. It
/// then replays the ordered feature list captured via `trackRuntime` to ensure
/// deterministic declaration order for helpers whose descriptors requested
/// ordered emission.
///
/// @param b IR builder used to register extern declarations.
/// @param boundsChecks Whether array bounds helpers should be declared.
void RuntimeHelperTracker::declareRequiredRuntime(build::IRBuilder &b, bool boundsChecks) const
{
    std::unordered_set<std::string> declared;

    auto tryDeclare = [&](const il::runtime::RuntimeDescriptor &d)
    {
        if (declared.insert(std::string(d.name)).second)
        {
            declareRuntimeExtern(b, d);
        }
    };

    const auto &registry = il::runtime::runtimeRegistry();
    for (const auto &entry : registry)
    {
        switch (entry.lowering.kind)
        {
            case il::runtime::RuntimeLoweringKind::Always:
                tryDeclare(entry);
                break;
            case il::runtime::RuntimeLoweringKind::BoundsChecked:
                if (boundsChecks)
                    tryDeclare(entry);
                break;
            case il::runtime::RuntimeLoweringKind::Feature:
                if (!entry.lowering.ordered && isHelperNeeded(entry.lowering.feature))
                    tryDeclare(entry);
                break;
            case il::runtime::RuntimeLoweringKind::Manual:
                break;
        }
    }

    // Replay only ordered features; trackRuntime recorded them deterministically.
    for (RuntimeFeature feature : ordered_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(feature);
        assert(desc && "requested runtime feature missing from registry");
        tryDeclare(*desc);
    }
}

/// @brief Mark a manual runtime helper as required.
///
/// Manual helpers are not described in the runtime registry and instead have
/// dedicated toggles in the lowering pipeline. This function flips the boolean
/// flag corresponding to the helper so `declareRequiredRuntime` can emit it.
///
/// @param helper Manual helper whose declaration should be emitted.
void Lowerer::setManualHelperRequired(ManualRuntimeHelper helper)
{
    manualHelperRequirements_[manualRuntimeHelperIndex(helper)] = true;
}

/// @brief Query whether a manual helper has been requested.
///
/// Simply reads the boolean toggle set by `setManualHelperRequired`.
///
/// @param helper Manual helper whose requirement is being checked.
/// @return True when the helper should be emitted.
bool Lowerer::isManualHelperRequired(ManualRuntimeHelper helper) const
{
    return manualHelperRequirements_[manualRuntimeHelperIndex(helper)];
}

/// @brief Clear all manual helper requirements.
///
/// Reinitialises the manual helper bitset so a new lowering invocation starts
/// without stale requirements.
void Lowerer::resetManualHelpers()
{
    manualHelperRequirements_.fill(false);
}

/// @brief Ensure the trap helper is declared when bounds checks are disabled.
///
/// When bounds checking is turned off manual trap emission is required for
/// runtime panic sites. This toggles the trap helper requirement.
void Lowerer::requireTrap()
{
    if (boundsChecks)
        return;
    setManualHelperRequired(ManualRuntimeHelper::Trap);
}

/// @brief Request the manual helper that allocates I32 arrays.
void Lowerer::requireArrayI32New()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32New);
}

/// @brief Request the manual helper that resizes I32 arrays.
void Lowerer::requireArrayI32Resize()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Resize);
}

/// @brief Request the manual helper that reads the length of I32 arrays.
void Lowerer::requireArrayI32Len()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Len);
}

/// @brief Request the manual helper that loads an element from an I32 array.
void Lowerer::requireArrayI32Get()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Get);
}

/// @brief Request the manual helper that stores an element into an I32 array.
void Lowerer::requireArrayI32Set()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Set);
}

/// @brief Request the manual helper that increments an I32 array reference.
void Lowerer::requireArrayI32Retain()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Retain);
}

/// @brief Request the manual helper that releases an I32 array reference.
void Lowerer::requireArrayI32Release()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Release);
}

/// @brief Request the helper that reports array out-of-bounds panics.
void Lowerer::requireArrayOobPanic()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayOobPanic);
}

/// @brief Request the helper that opens a file and reports errors via strings.
void Lowerer::requireOpenErrVstr()
{
    setManualHelperRequired(ManualRuntimeHelper::OpenErrVstr);
}

/// @brief Request the helper that closes a file descriptor and reports errors.
void Lowerer::requireCloseErr()
{
    setManualHelperRequired(ManualRuntimeHelper::CloseErr);
}

/// @brief Request the helper that repositions a channel with error reporting.
void Lowerer::requireSeekChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::SeekChErr);
}

/// @brief Request the helper that writes to a file channel without newline.
void Lowerer::requireWriteChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::WriteChErr);
}

/// @brief Request the helper that prints a character with error handling.
void Lowerer::requirePrintlnChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::PrintlnChErr);
}

/// @brief Request the helper that reads a line with error reporting.
void Lowerer::requireLineInputChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::LineInputChErr);
}

// --- begin: require implementations ---
/// @brief Request the helper that tests EOF status on a channel.
void Lowerer::requireEofCh()
{
    setManualHelperRequired(ManualRuntimeHelper::EofCh);
}

/// @brief Request the helper that computes the length of a file channel.
void Lowerer::requireLofCh()
{
    setManualHelperRequired(ManualRuntimeHelper::LofCh);
}

/// @brief Request the helper that reports the current position of a channel.
void Lowerer::requireLocCh()
{
    setManualHelperRequired(ManualRuntimeHelper::LocCh);
}

// --- end: require implementations ---

/// @brief Request the helper that conditionally retains a string handle.
void Lowerer::requireStrRetainMaybe()
{
    setManualHelperRequired(ManualRuntimeHelper::StrRetainMaybe);
}

/// @brief Request the helper that conditionally releases a string handle.
void Lowerer::requireStrReleaseMaybe()
{
    setManualHelperRequired(ManualRuntimeHelper::StrReleaseMaybe);
}

/// @brief Forward a runtime feature request to the shared tracker.
void Lowerer::requestHelper(RuntimeFeature feature)
{
    runtimeTracker.requestHelper(feature);
}

/// @brief Query whether a runtime feature helper has been requested.
bool Lowerer::isHelperNeeded(RuntimeFeature feature) const
{
    return runtimeTracker.isHelperNeeded(feature);
}

/// @brief Forward runtime usage information to the shared tracker.
void Lowerer::trackRuntime(RuntimeFeature feature)
{
    runtimeTracker.trackRuntime(feature);
}

/// @brief Emit extern declarations for all helpers requested via the tracker or manual toggles.
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
        {"rt_trap", ManualRuntimeHelper::Trap, &Lowerer::requireTrap},
        {"rt_arr_i32_new", ManualRuntimeHelper::ArrayI32New, &Lowerer::requireArrayI32New},
        {"rt_arr_i32_resize", ManualRuntimeHelper::ArrayI32Resize, &Lowerer::requireArrayI32Resize},
        {"rt_arr_i32_len", ManualRuntimeHelper::ArrayI32Len, &Lowerer::requireArrayI32Len},
        {"rt_arr_i32_get", ManualRuntimeHelper::ArrayI32Get, &Lowerer::requireArrayI32Get},
        {"rt_arr_i32_set", ManualRuntimeHelper::ArrayI32Set, &Lowerer::requireArrayI32Set},
        {"rt_arr_i32_retain", ManualRuntimeHelper::ArrayI32Retain, &Lowerer::requireArrayI32Retain},
        {"rt_arr_i32_release",
         ManualRuntimeHelper::ArrayI32Release,
         &Lowerer::requireArrayI32Release},
        {"rt_arr_oob_panic", ManualRuntimeHelper::ArrayOobPanic, &Lowerer::requireArrayOobPanic},
        {"rt_open_err_vstr", ManualRuntimeHelper::OpenErrVstr, &Lowerer::requireOpenErrVstr},
        {"rt_close_err", ManualRuntimeHelper::CloseErr, &Lowerer::requireCloseErr},
        {"rt_seek_ch_err", ManualRuntimeHelper::SeekChErr, &Lowerer::requireSeekChErr},
        {"rt_write_ch_err", ManualRuntimeHelper::WriteChErr, &Lowerer::requireWriteChErr},
        {"rt_println_ch_err", ManualRuntimeHelper::PrintlnChErr, &Lowerer::requirePrintlnChErr},
        {"rt_line_input_ch_err",
         ManualRuntimeHelper::LineInputChErr,
         &Lowerer::requireLineInputChErr},
        // --- begin: declarable manual helpers ---
        {"rt_eof_ch", ManualRuntimeHelper::EofCh, &Lowerer::requireEofCh},
        {"rt_lof_ch", ManualRuntimeHelper::LofCh, &Lowerer::requireLofCh},
        {"rt_loc_ch", ManualRuntimeHelper::LocCh, &Lowerer::requireLocCh},
        // --- end: declarable manual helpers ---
        {"rt_str_retain_maybe",
         ManualRuntimeHelper::StrRetainMaybe,
         &Lowerer::requireStrRetainMaybe},
        {"rt_str_release_maybe",
         ManualRuntimeHelper::StrReleaseMaybe,
         &Lowerer::requireStrReleaseMaybe},
    }};

    auto declareManual = [&](std::string_view name)
    {
        if (const auto *desc = il::runtime::findRuntimeDescriptor(name))
            b.addExtern(
                std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
    };

    for (const auto &helper : manualHelpers)
    {
        if (isManualHelperRequired(helper.helper))
            declareManual(helper.name);
    }
}

} // namespace il::frontends::basic
