//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "il/runtime/RuntimeSignatures.hpp"
#include "viper/il/IRBuilder.hpp"
#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <unordered_set>

/// @file
/// @brief Runtime helper tracking used by the BASIC lowering pipeline.
/// @details Coordinates optional runtime feature requests with the registry of
///          helper descriptors, ensuring extern declarations are emitted exactly
///          once and in a deterministic order.  Manual helpers that are not part
///          of the registry share the same bookkeeping so lowering steps can
///          toggle them without worrying about deduplication.

namespace il::frontends::basic
{

/// @brief Compute a hash value for a runtime feature flag.
/// @details Features are encoded as compact enumerators.  Casting to their
///          underlying integer representation yields a stable hash without
///          auxiliary state, allowing the type to participate in unordered
///          containers with zero overhead.
/// @param f Feature to hash.
/// @return Integer hash suitable for unordered containers.
std::size_t RuntimeHelperTracker::RuntimeFeatureHash::operator()(RuntimeFeature f) const
{
    return static_cast<std::size_t>(f);
}

/// @brief Clear all runtime helper tracking state.
/// @details Drops any pending requests, the deduplicated set, and the ordered
///          replay list so a fresh lowering run can start from a clean slate.
void RuntimeHelperTracker::reset()
{
    requested_.reset();
    ordered_.clear();
    tracked_.clear();
    usedNames_.clear();
}

/// @brief Mark a runtime helper as required.
/// @details Records the request in the bitset that tracks optional helpers.
///          Ordering is handled separately; the bitset merely records that the
///          helper must be emitted when declarations are synthesised.
///
/// @param feature Feature whose helper must be available.
void RuntimeHelperTracker::requestHelper(RuntimeFeature feature)
{
    requested_.set(static_cast<std::size_t>(feature));
}

/// @brief Query whether a feature's helper has been requested.
/// @details Consults the internal bitset populated by @ref requestHelper to
///          determine whether a helper must be declared.
///
/// @param feature Feature whose helper requirement is being checked.
/// @return True when the helper has been requested.
bool RuntimeHelperTracker::isHelperNeeded(RuntimeFeature feature) const
{
    return requested_.test(static_cast<std::size_t>(feature));
}

/// @brief Record a runtime helper as used and maintain declaration ordering.
/// @details Ensures the helper is marked as requested and, if it has not been
///          seen before, appends it to the ordered replay list.  The ordered
///          list guarantees deterministic extern emission even when requests
///          arise out of order during lowering.
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

void RuntimeHelperTracker::trackCalleeName(std::string_view name)
{
    usedNames_.insert(std::string(name));
}

namespace
{
/// @brief Declare a runtime extern using the canonical signature database.
/// @details Centralises the IRBuilder call so declarations pulled from the
///          runtime registry share a single implementation.  Any future metadata
///          changes therefore need to be reflected in just this function.
///
/// @param b IR builder that will receive the extern declaration.
/// @param desc Runtime descriptor describing the helper to declare.
void declareRuntimeExtern(build::IRBuilder &b, const il::runtime::RuntimeDescriptor &desc)
{
    b.addExtern(std::string(desc.name), desc.signature.retType, desc.signature.paramTypes);
}

/// @brief Resolve a canonical runtime descriptor for a potentially aliased name.
/// @details Some helpers have multiple descriptors in the registry that share the
///          same signature id (e.g., legacy "rt_*" aliases and canonical
///          "Viper.*" names). Given any known name, prefer the canonical descriptor
///          when available by matching on the generated signature id; otherwise
///          fall back to the directly named descriptor.
/// @param name Any registered runtime helper name (legacy or canonical).
/// @return Pointer to the preferred descriptor when found; nullptr if unknown.
const il::runtime::RuntimeDescriptor *resolveCanonicalDescriptor(std::string_view name)
{
    using namespace il::runtime;

    const RuntimeDescriptor *desc = findRuntimeDescriptor(name);
    if (!desc)
        return nullptr;

    // Identify the generated signature id for the provided descriptor name.
    if (auto sigId = findRuntimeSignatureId(desc->name))
    {
        // Search for a descriptor that shares the same signature id but uses a
        // canonical namespace-qualified name (prefer entries containing a '.').
        const RuntimeDescriptor *best = desc;
        for (const auto &entry : runtimeRegistry())
        {
            const auto otherId = findRuntimeSignatureId(entry.name);
            if (!otherId || *otherId != *sigId)
                continue;
            // Prefer names that don't look like legacy flat symbols.
            const bool isCanonical = entry.name.find('.') != std::string_view::npos;
            if (isCanonical)
            {
                best = &entry;
                break; // Found a canonical alias; stop searching.
            }
        }
        return best;
    }

    // No signature id published; return the directly named descriptor.
    return desc;
}
} // namespace

/// @brief Declare every runtime helper required by the current lowering run.
/// @details Walks the runtime descriptor registry, emitting helpers that are
///          always needed plus those gated behind feature flags or bounds-check
///          settings.  The ordered feature list captured via @ref trackRuntime is
///          replayed afterwards to guarantee deterministic declaration ordering
///          for helpers that opted into sequencing.
///
/// @param b IR builder used to register extern declarations.
/// @param boundsChecks Whether array bounds helpers should be declared.
void RuntimeHelperTracker::declareRequiredRuntime(build::IRBuilder &b, bool boundsChecks) const
{
    std::unordered_set<std::string> declared;

    auto tryDeclare = [&](const il::runtime::RuntimeDescriptor &d)
    {
        // If any variant (alias/canonical) of this signature id was used at a
        // call site, declare only that used spelling and skip the others.
        std::optional<std::string_view> usedSpelling;
        if (auto sigId = il::runtime::findRuntimeSignatureId(d.name))
        {
            const auto &reg = il::runtime::runtimeRegistry();
            for (const auto &other : reg)
            {
                auto otherId = il::runtime::findRuntimeSignatureId(other.name);
                if (!otherId || *otherId != *sigId)
                    continue;
                if (usedNames_.count(std::string(other.name)) > 0)
                {
                    usedSpelling = other.name;
                    break;
                }
            }
        }
        if (usedSpelling)
        {
            // Only declare spellings that were actually used at call sites.
            // This allows declaring multiple aliases in the same signature
            // group when both were referenced (e.g., Viper.Strings.Mid and
            // Viper.String.Substring).
            if (usedNames_.count(std::string(d.name)) == 0)
                return;
        }
        else
        {
            // No specific spelling used: prefer canonical Viper.* names. If a
            // canonical exists for this signature id and we're looking at the alias,
            // skip the alias to avoid duplicates.
            const bool isAlias = d.name.find('.') == std::string_view::npos;
            if (isAlias)
            {
                if (auto sigId = il::runtime::findRuntimeSignatureId(d.name))
                {
                    const auto &reg = il::runtime::runtimeRegistry();
                    for (const auto &other : reg)
                    {
                        const bool isCanonical = other.name.find('.') != std::string_view::npos;
                        if (!isCanonical)
                            continue;
                        auto otherId = il::runtime::findRuntimeSignatureId(other.name);
                        if (otherId && *otherId == *sigId)
                        {
                            // Canonical exists; skip alias.
                            return;
                        }
                    }
                }
            }

            // Additional canonical preference: when both Viper.Strings.* and
            // Viper.String.* exist for the same signature id, prefer the
            // functions in Viper.Strings.* unless the Viper.String.* spelling
            // was explicitly used at a call site.
            if (d.name.rfind("Viper.String.", 0) == 0)
            {
                if (auto sigId = il::runtime::findRuntimeSignatureId(d.name))
                {
                    const auto &reg = il::runtime::runtimeRegistry();
                    for (const auto &other : reg)
                    {
                        auto otherId = il::runtime::findRuntimeSignatureId(other.name);
                        if (!otherId || *otherId != *sigId)
                            continue;
                        if (other.name.rfind("Viper.Strings.", 0) == 0)
                        {
                            // Prefer the Viper.Strings.* variant when no explicit usage forces otherwise.
                            return;
                        }
                    }
                }
            }

            // Avoid declaring certain OOP-style or ctor helpers unless used.
            // Tests/goldens expect these only when referenced.
            if (d.name == std::string_view{"Viper.String.get_IsEmpty"} ||
                d.name == std::string_view{"Viper.Strings.FromStr"})
            {
                return;
            }
        }

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

    // Declare any manually-lowered helpers that were explicitly used at call sites.
    // This keeps IL lean (no unconditional alias declarations) while ensuring
    // names like Viper.Text.StringBuilder.* and Viper.Strings.Builder.* appear
    // only when referenced.
    for (const auto &name : usedNames_)
    {
        // Skip rt_* manual helpers; Lowerer::declareRequiredRuntime handles them
        if (name.rfind("rt_", 0) == 0)
            continue;
        if (const auto *desc = il::runtime::findRuntimeDescriptor(name))
        {
            if (declared.insert(std::string(desc->name)).second)
                declareRuntimeExtern(b, *desc);
        }
    }
}

/// @brief Mark a manual runtime helper as required.
/// @details Manual helpers are not described in the runtime registry and
///          instead have dedicated toggles in the lowering pipeline.  This
///          function flips the boolean flag corresponding to the helper so
///          @ref declareRequiredRuntime can emit it.
///
/// @param helper Manual helper whose declaration should be emitted.
void Lowerer::setManualHelperRequired(ManualRuntimeHelper helper)
{
    manualHelperRequirements_[manualRuntimeHelperIndex(helper)] = true;
}

/// @brief Query whether a manual helper has been requested.
/// @details Reads the boolean toggle set by @ref setManualHelperRequired.
///
/// @param helper Manual helper whose requirement is being checked.
/// @return True when the helper should be emitted.
bool Lowerer::isManualHelperRequired(ManualRuntimeHelper helper) const
{
    return manualHelperRequirements_[manualRuntimeHelperIndex(helper)];
}

/// @brief Clear all manual helper requirements.
/// @details Reinitialises the manual helper bitset so a new lowering invocation
///          starts without stale requirements.
void Lowerer::resetManualHelpers()
{
    manualHelperRequirements_.fill(false);
}

/// @brief Ensure the trap helper is declared whenever traps are emitted.
/// @details Several lowering sites emit a call to the runtime trap helper
///          (e.g., GOSUB overflow/empty return, conversion failures, explicit
///          diagnostics). This helper must be declared regardless of the
///          bounds-checking configuration, since traps are not exclusive to
///          array bounds operations.
void Lowerer::requireTrap()
{
    setManualHelperRequired(ManualRuntimeHelper::Trap);
}

/// @brief Request the manual helper that allocates I32 arrays.
/// @details Sets the manual-helper toggle so the allocation routine for new
///          integer arrays is emitted alongside other runtime externs.
void Lowerer::requireArrayI32New()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32New);
}

/// @brief Request the manual helper that resizes I32 arrays.
/// @details Marks the helper so the reallocating routine is emitted; required
///          when lowering constructs such as `REDIM`.
void Lowerer::requireArrayI32Resize()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Resize);
}

/// @brief Request the manual helper that reads the length of I32 arrays.
/// @details Ensures the length-query routine is declared for clients that need
///          to observe the current logical size of a runtime array.
void Lowerer::requireArrayI32Len()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Len);
}

/// @brief Request the manual helper that loads an element from an I32 array.
/// @details Flags the helper so bounds-checked element loads can be lowered to
///          the shared runtime routine.
void Lowerer::requireArrayI32Get()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Get);
}

/// @brief Request the manual helper that stores an element into an I32 array.
/// @details Marks the store routine as required so writes funnel through the
///          shared runtime implementation with consistent bounds checks.
void Lowerer::requireArrayI32Set()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Set);
}

/// @brief Request the manual helper that increments an I32 array reference.
/// @details Ensures the reference-counting retain helper is declared for array
///          handles shared across procedures.
void Lowerer::requireArrayI32Retain()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Retain);
}

/// @brief Request the manual helper that releases an I32 array reference.
/// @details Marks the release helper so decrements of reference counts reuse the
///          runtime-provided implementation.
void Lowerer::requireArrayI32Release()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayI32Release);
}

/// @brief Request the manual helper that allocates string arrays.
/// @details Sets the manual-helper toggle so the allocation routine for new
///          string arrays is emitted alongside other runtime externs.
void Lowerer::requireArrayStrAlloc()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayStrAlloc);
}

/// @brief Request the manual helper that releases a string array reference.
/// @details Marks the release helper so decrements of reference counts reuse the
///          runtime-provided implementation.
void Lowerer::requireArrayStrRelease()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayStrRelease);
}

/// @brief Request the manual helper that loads an element from a string array.
/// @details Flags the helper so bounds-checked element loads can be lowered to
///          the shared runtime routine.
void Lowerer::requireArrayStrGet()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayStrGet);
}

/// @brief Request the manual helper that stores an element into a string array.
/// @details Marks the store routine as required so writes funnel through the
///          shared runtime implementation with consistent bounds checks.
void Lowerer::requireArrayStrPut()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayStrPut);
}

/// @brief Request the manual helper that reads the length of string arrays.
/// @details Ensures the length-query routine is declared for clients that need
///          to observe the current logical size of a runtime array.
void Lowerer::requireArrayStrLen()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayStrLen);
}

// --- Object array helper require hooks ---
void Lowerer::requireArrayObjNew()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayObjNew);
}

void Lowerer::requireArrayObjLen()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayObjLen);
}

void Lowerer::requireArrayObjGet()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayObjGet);
}

void Lowerer::requireArrayObjPut()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayObjPut);
}

void Lowerer::requireArrayObjResize()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayObjResize);
}

void Lowerer::requireArrayObjRelease()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayObjRelease);
}

/// @brief Request the helper that reports array out-of-bounds panics.
/// @details Ensures the trap routine used for bounds failures is available when
///          lowering explicit checks.
void Lowerer::requireArrayOobPanic()
{
    setManualHelperRequired(ManualRuntimeHelper::ArrayOobPanic);
}

/// @brief Request the helper that opens a file and reports errors via strings.
/// @details Marks the manual helper responsible for returning structured error
///          data when opening file channels fails.
void Lowerer::requireOpenErrVstr()
{
    setManualHelperRequired(ManualRuntimeHelper::OpenErrVstr);
}

/// @brief Request the helper that closes a file descriptor and reports errors.
/// @details Flags the close helper so file teardown code can reuse the shared
///          error-reporting path.
void Lowerer::requireCloseErr()
{
    setManualHelperRequired(ManualRuntimeHelper::CloseErr);
}

/// @brief Request the helper that repositions a channel with error reporting.
/// @details Toggles the manual helper used to implement SEEK with structured
///          error handling.
void Lowerer::requireSeekChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::SeekChErr);
}

/// @brief Request the helper that writes to a file channel without newline.
/// @details Ensures the runtime routine used by PRINT# without newline is
///          declared when lowering file output statements.
void Lowerer::requireWriteChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::WriteChErr);
}

/// @brief Request the helper that prints a character with error handling.
/// @details Marks the newline-print helper used for PRINT# statements that
///          append a terminator.
void Lowerer::requirePrintlnChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::PrintlnChErr);
}

/// @brief Request the helper that reads a line with error reporting.
/// @details Toggles the manual helper used for LINE INPUT# so runtime errors are
///          surfaced consistently.
void Lowerer::requireLineInputChErr()
{
    setManualHelperRequired(ManualRuntimeHelper::LineInputChErr);
}

// --- begin: require implementations ---
/// @brief Request the helper that tests EOF status on a channel.
/// @details Marks the EOF-check helper so BASIC's EOF functions can be lowered
///          without duplicating runtime logic.
void Lowerer::requireEofCh()
{
    setManualHelperRequired(ManualRuntimeHelper::EofCh);
}

/// @brief Request the helper that computes the length of a file channel.
/// @details Ensures the LOF implementation is emitted for consumers that query
///          the logical file length.
void Lowerer::requireLofCh()
{
    setManualHelperRequired(ManualRuntimeHelper::LofCh);
}

/// @brief Request the helper that reports the current position of a channel.
/// @details Marks the LOC helper so callers can query file offsets through the
///          shared runtime API.
void Lowerer::requireLocCh()
{
    setManualHelperRequired(ManualRuntimeHelper::LocCh);
}

// Module-level globals helpers
void Lowerer::requireModvarAddrI64()
{
    setManualHelperRequired(ManualRuntimeHelper::ModvarAddrI64);
}

void Lowerer::requireModvarAddrF64()
{
    setManualHelperRequired(ManualRuntimeHelper::ModvarAddrF64);
}

void Lowerer::requireModvarAddrI1()
{
    setManualHelperRequired(ManualRuntimeHelper::ModvarAddrI1);
}

void Lowerer::requireModvarAddrPtr()
{
    setManualHelperRequired(ManualRuntimeHelper::ModvarAddrPtr);
}

void Lowerer::requireModvarAddrStr()
{
    setManualHelperRequired(ManualRuntimeHelper::ModvarAddrStr);
}

// --- end: require implementations ---

/// @brief Request the helper that conditionally retains a string handle.
/// @details Ensures the helper that guards null handles before retaining is
///          emitted; used when lowering optional string temporaries.
void Lowerer::requireStrRetainMaybe()
{
    setManualHelperRequired(ManualRuntimeHelper::StrRetainMaybe);
}

/// @brief Request the helper that conditionally releases a string handle.
/// @details Flags the corresponding release helper so shared string lifetimes
///          remain balanced even when handles are optional.
void Lowerer::requireStrReleaseMaybe()
{
    setManualHelperRequired(ManualRuntimeHelper::StrReleaseMaybe);
}

/// @brief Request the sleep helper used by the SLEEP statement.
/// @details Flags the `rt_sleep_ms` helper for extern declaration.
void Lowerer::requireSleepMs()
{
    setManualHelperRequired(ManualRuntimeHelper::SleepMs);
}

/// @brief Request the timer helper used by the TIMER builtin.
/// @details Flags the `rt_timer_ms` helper for extern declaration.
void Lowerer::requireTimerMs()
{
    setManualHelperRequired(ManualRuntimeHelper::TimerMs);
}

/// @brief Forward a runtime feature request to the shared tracker.
/// @details Invokes @ref RuntimeHelperTracker::requestHelper so the
///          feature-specific helper is considered during extern emission.
void Lowerer::requestHelper(RuntimeFeature feature)
{
    runtimeTracker.requestHelper(feature);
}

/// @brief Query whether a runtime feature helper has been requested.
/// @details Pass-through convenience wrapper around
///          @ref RuntimeHelperTracker::isHelperNeeded used by lowering code to
///          gate feature-dependent behaviour.
bool Lowerer::isHelperNeeded(RuntimeFeature feature) const
{
    return runtimeTracker.isHelperNeeded(feature);
}

/// @brief Forward runtime usage information to the shared tracker.
/// @details Calls @ref RuntimeHelperTracker::trackRuntime so that ordered
///          helpers are replayed deterministically when declarations are
///          emitted.
void Lowerer::trackRuntime(RuntimeFeature feature)
{
    runtimeTracker.trackRuntime(feature);
}

/// @brief Emit extern declarations for all helpers requested via the tracker or manual toggles.
/// @details Delegates feature-driven helpers to @ref RuntimeHelperTracker and
///          then walks the manual helper table, declaring any entries whose
///          toggles were flipped earlier in lowering.
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
        {"rt_arr_str_alloc", ManualRuntimeHelper::ArrayStrAlloc, &Lowerer::requireArrayStrAlloc},
        {"rt_arr_str_release",
         ManualRuntimeHelper::ArrayStrRelease,
         &Lowerer::requireArrayStrRelease},
        {"rt_arr_str_get", ManualRuntimeHelper::ArrayStrGet, &Lowerer::requireArrayStrGet},
        {"rt_arr_str_put", ManualRuntimeHelper::ArrayStrPut, &Lowerer::requireArrayStrPut},
        {"rt_arr_str_len", ManualRuntimeHelper::ArrayStrLen, &Lowerer::requireArrayStrLen},
        {"rt_arr_obj_new", ManualRuntimeHelper::ArrayObjNew, &Lowerer::requireArrayObjNew},
        {"rt_arr_obj_len", ManualRuntimeHelper::ArrayObjLen, &Lowerer::requireArrayObjLen},
        {"rt_arr_obj_get", ManualRuntimeHelper::ArrayObjGet, &Lowerer::requireArrayObjGet},
        {"rt_arr_obj_put", ManualRuntimeHelper::ArrayObjPut, &Lowerer::requireArrayObjPut},
        {"rt_arr_obj_resize", ManualRuntimeHelper::ArrayObjResize, &Lowerer::requireArrayObjResize},
        {"rt_arr_obj_release",
         ManualRuntimeHelper::ArrayObjRelease,
         &Lowerer::requireArrayObjRelease},
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
        {"rt_str_retain_maybe",
         ManualRuntimeHelper::StrRetainMaybe,
         &Lowerer::requireStrRetainMaybe},
        {"rt_str_release_maybe",
         ManualRuntimeHelper::StrReleaseMaybe,
         &Lowerer::requireStrReleaseMaybe},
        // Module-level globals address helpers
        {"rt_modvar_addr_i64", ManualRuntimeHelper::ModvarAddrI64, &Lowerer::requireModvarAddrI64},
        {"rt_modvar_addr_f64", ManualRuntimeHelper::ModvarAddrF64, &Lowerer::requireModvarAddrF64},
        {"rt_modvar_addr_i1", ManualRuntimeHelper::ModvarAddrI1, &Lowerer::requireModvarAddrI1},
        {"rt_modvar_addr_ptr", ManualRuntimeHelper::ModvarAddrPtr, &Lowerer::requireModvarAddrPtr},
        {"rt_modvar_addr_str", ManualRuntimeHelper::ModvarAddrStr, &Lowerer::requireModvarAddrStr},
        // --- end: declarable manual helpers ---
        {"rt_sleep_ms", ManualRuntimeHelper::SleepMs, &Lowerer::requireSleepMs},
        {"rt_timer_ms", ManualRuntimeHelper::TimerMs, &Lowerer::requireTimerMs},
    }};

    auto declareManual = [&](std::string_view name)
    {
        // Prefer the spelling observed at call sites when available so extern
        // declarations match emitted calls (avoids alias duplicates under
        // dual-namespace mode). Fall back to the provided default name.
        std::string spelling(name);
        if (const auto *base = il::runtime::findRuntimeDescriptor(name))
        {
            if (auto sigId = il::runtime::findRuntimeSignatureId(base->name))
            {
                for (const auto &other : il::runtime::runtimeRegistry())
                {
                    auto otherId = il::runtime::findRuntimeSignatureId(other.name);
                    if (!otherId || *otherId != *sigId)
                        continue;
                    if (runtimeTracker.usedNames().count(std::string(other.name)))
                    {
                        spelling = std::string(other.name);
                        break;
                    }
                }
            }
        }
        if (const auto *desc = il::runtime::findRuntimeDescriptor(spelling))
        {
            // Avoid duplicates when a previous pass declared the chosen spelling.
            bool alreadyDeclared = false;
            if (mod)
            {
                for (const auto &ex : mod->externs)
                {
                    if (ex.name == spelling)
                    {
                        alreadyDeclared = true;
                        break;
                    }
                }
            }
            if (!alreadyDeclared)
                b.addExtern(std::string(spelling), desc->signature.retType, desc->signature.paramTypes);
        }
    };

    for (const auto &helper : manualHelpers)
    {
        if (isManualHelperRequired(helper.helper))
            declareManual(helper.name);
    }

    // Note: String retain/release helpers are declared via the pre-scan and
    // explicit require* hooks in lowering. Avoid unconditional declarations
    // here to keep IL stable and prevent duplicate externs in simple programs.

    // Ensure explicit alias spellings used at call sites are declared when they
    // map to descriptors marked as ManualLowering. Under dual-namespace mode the
    // canonical Viper.* variants are declared as Always, and legacy rt_* aliases
    // are Manual. When lowering emits calls to rt_* names, the alias-specific
    // externs would otherwise be skipped. Declare those alias spellings here so
    // the verifier can resolve the callees while preserving stable output.
    for (const auto &used : runtimeTracker.usedNames())
    {
        if (const auto *desc = il::runtime::findRuntimeDescriptor(used))
        {
            if (desc->lowering.kind == il::runtime::RuntimeLoweringKind::Manual)
            {
                bool alreadyDeclared = false;
                if (mod)
                {
                    for (const auto &ex : mod->externs)
                    {
                        if (ex.name == used)
                        {
                            alreadyDeclared = true;
                            break;
                        }
                    }
                }
                if (!alreadyDeclared)
                {
                    b.addExtern(std::string(used), desc->signature.retType, desc->signature.paramTypes);
                }
            }
        }
    }

    auto ensureExtern = [&](std::string_view name)
    {
        if (!mod)
            return;
        for (const auto &ex : mod->externs)
        {
            if (ex.name == name)
                return; // already present
        }
        if (const auto *desc = il::runtime::findRuntimeDescriptor(name))
        {
            b.addExtern(std::string(name), desc->signature.retType, desc->signature.paramTypes);
        }
    };
    const auto &used = runtimeTracker.usedNames();
    // Declare any used bounds-checked helpers regardless of the global
    // boundsChecks flag, since some ops (e.g., Diagnostics.Trap) are invoked
    // explicitly in generated IL even without array bounds checks enabled.
    for (const auto &name : used)
    {
        if (const auto *desc = il::runtime::findRuntimeDescriptor(name))
        {
            if (desc->lowering.kind == il::runtime::RuntimeLoweringKind::BoundsChecked)
            {
                ensureExtern(name);
            }
        }
    }
}

} // namespace il::frontends::basic
