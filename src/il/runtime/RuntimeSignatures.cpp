//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeSignatures.cpp
// Purpose: Build and expose the runtime descriptor registry used by IL
//          consumers to marshal calls into the C runtime. Contains the
//          descriptor table definitions, lookup APIs, and validation logic.
//
// Generated Data: This file includes generated/RuntimeSignatures.inc which
//                 contains DescriptorRow entries for all runtime functions.
//                 See docs/generated-files.md for regeneration instructions.
//
// Key invariants: The descriptor table is immutable, matches runtime helpers
//                 one-to-one, and is initialised lazily in a thread-safe manner.
// Ownership/Lifetime: All descriptors have static storage duration and remain
//                     valid for the lifetime of the process.
// Links: docs/generated-files.md, docs/il-guide.md#reference
//
// Related files:
//   - generated/RuntimeSignatures.inc: Generated descriptor rows
//   - RuntimeSignatures_Handlers.hpp: Handler templates (DirectHandler, etc.)
//   - RuntimeSignatures_Handlers.cpp: Adapter function implementations
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime descriptor registry and public lookup APIs.
/// @details Contains the descriptor table definitions mapping runtime symbol
///          names to their signatures, handlers, and lowering metadata. Handler
///          templates and adapter functions are defined in the companion
///          RuntimeSignatures_Handlers files.

#include "il/runtime/RuntimeSignatures.hpp"

#include "il/runtime/HelperEffects.hpp"
#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"
#include "il/runtime/RuntimeSignatures_Handlers.hpp"

#include "rt.hpp"
#include "rt_args.h"
#include "rt_array_f64.h"
#include "rt_array_i64.h"
#include "rt_array_obj.h"
#include "rt_fp.h"
#include "rt_datetime.h"
#include "rt_dictionary.h"
#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_math.h"
#include "rt_oop.h"
#include "viper/runtime/rt.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NDEBUG
#include "il/runtime/signatures/Registry.hpp"
#include <cassert>
#include <unordered_set>

namespace il::runtime::signatures
{
void register_fileio_signatures();
void register_string_signatures();
void register_math_signatures();
void register_array_signatures();
} // namespace il::runtime::signatures
#endif

namespace il::runtime
{
namespace
{

using Kind = il::core::Type::Kind;

constexpr std::size_t kRtSigCount = data::kRtSigCount;

/// @brief Retrieve the parsed runtime signature for a generated enumerator.
///
/// @details Lazily initialises an array of signatures by parsing the table of
///          specification strings emitted at build time.  Subsequent lookups
///          return references into the cached array without additional parsing.
///
/// @param sig Enumerated runtime signature identifier.
/// @return Reference to the parsed runtime signature.
const RuntimeSignature &signatureFor(RtSig sig)
{
    static const auto table = []
    {
        std::array<RuntimeSignature, kRtSigCount> entries;
        for (std::size_t i = 0; i < kRtSigCount; ++i)
        {
            entries[i] = parseSignatureSpec(data::kRtSigSpecs[i]);
            const auto effects = classifyHelperEffects(data::kRtSigSymbolNames[i]);
            entries[i].nothrow = effects.nothrow;
            entries[i].readonly = effects.readonly;
            entries[i].pure = effects.pure;
        }
        return entries;
    }();
    return table[static_cast<std::size_t>(sig)];
}

/// @brief Check whether a runtime signature enumerator is in range.
///
/// @param sig Enumerated runtime signature identifier.
/// @return True when @p sig maps to a generated signature entry.
bool isValid(RtSig sig)
{
    return static_cast<std::size_t>(sig) < kRtSigCount;
}

// Handler templates (DirectHandler, ConsumingStringHandler) and adapter functions
// (invokeRtArr*, trapFromRuntimeString, etc.) are defined in RuntimeSignatures_Handlers.hpp/cpp.

/// @brief Construct a runtime lowering descriptor with optional feature gating.
///
/// @details Packs the lowering metadata into a @ref RuntimeLowering structure,
///          optionally recording the feature enum and whether ordering matters
///          when comparing runtime calls.
constexpr RuntimeLowering makeLowering(RuntimeLoweringKind kind,
                                       RuntimeFeature feature = RuntimeFeature::Count,
                                       bool ordered = false)
{
    return RuntimeLowering{kind, feature, ordered};
}

// Adapter functions (invokeRtArr*, invokeRtCint*, etc.) are defined in
// RuntimeSignatures_Handlers.cpp and declared in RuntimeSignatures_Handlers.hpp.

constexpr RuntimeLowering kAlwaysLowering = makeLowering(RuntimeLoweringKind::Always);
constexpr RuntimeLowering kBoundsCheckedLowering = makeLowering(RuntimeLoweringKind::BoundsChecked);
constexpr RuntimeLowering kManualLowering = makeLowering(RuntimeLoweringKind::Manual);

/// @brief Helper that records a runtime feature requirement for lowering.
///
/// @details Produces a @ref RuntimeLowering descriptor indicating that the
///          runtime function should only be linked when the given feature is
///          requested by the front end.
constexpr RuntimeLowering featureLowering(RuntimeFeature feature, bool ordered = false)
{
    return makeLowering(RuntimeLoweringKind::Feature, feature, ordered);
}

constexpr std::array<RuntimeHiddenParam, 1> kPowHidden{
    RuntimeHiddenParam{RuntimeHiddenParamKind::PowStatusPointer}};

struct DescriptorRow
{
    std::string_view name;
    std::optional<RtSig> signatureId;
    std::string_view spec;
    RuntimeHandler handler;
    RuntimeLowering lowering;
    const RuntimeHiddenParam *hidden;
    std::size_t hiddenCount;
    RuntimeTrapClass trapClass;
};

// Use deduced array size to avoid mismatches that would create an empty default row.
constexpr auto kDescriptorRows = std::to_array<DescriptorRow>({
    DescriptorRow{"rt_abort",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_abort, void, const char *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
#include "generated/RuntimeSignatures.inc"
    DescriptorRow{"rt_println_i32",
                  std::nullopt,
                  "void(i32)",
                  &DirectHandler<&rt_println_i32, void, int32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_println_str",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_println_str, void, const char *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_csv_quote_alloc",
                  std::nullopt,
                  "string(string)",
                  &DirectHandler<&rt_csv_quote_alloc, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::CsvQuote),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_term_cls",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_cls, void>::invoke,
                  featureLowering(RuntimeFeature::TermCls),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Canonical Viper.Terminal.* dotted names mapping to terminal helpers
    DescriptorRow{"Viper.Terminal.Clear",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_cls, void>::invoke,
                  featureLowering(RuntimeFeature::TermCls),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_term_color_i32",
                  std::nullopt,
                  "void(i32,i32)",
                  &DirectHandler<&rt_term_color_i32, void, int32_t, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermColor),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.SetColor",
                  std::nullopt,
                  "void(i32,i32)",
                  &DirectHandler<&rt_term_color_i32, void, int32_t, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermColor),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_term_locate_i32",
                  std::nullopt,
                  "void(i32,i32)",
                  &DirectHandler<&rt_term_locate_i32, void, int32_t, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermLocate),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.SetPosition",
                  std::nullopt,
                  "void(i32,i32)",
                  &DirectHandler<&rt_term_locate_i32, void, int32_t, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermLocate),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_term_cursor_visible_i32",
                  std::nullopt,
                  "void(i32)",
                  &DirectHandler<&rt_term_cursor_visible_i32, void, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermCursor),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.SetCursorVisible",
                  std::nullopt,
                  "void(i32)",
                  &DirectHandler<&rt_term_cursor_visible_i32, void, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermCursor),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_term_alt_screen_i32",
                  std::nullopt,
                  "void(i32)",
                  &DirectHandler<&rt_term_alt_screen_i32, void, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermAltScreen),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.SetAltScreen",
                  std::nullopt,
                  "void(i32)",
                  &DirectHandler<&rt_term_alt_screen_i32, void, int32_t>::invoke,
                  featureLowering(RuntimeFeature::TermAltScreen),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_bell",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_bell, void>::invoke,
                  featureLowering(RuntimeFeature::TermBell),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.Bell",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_bell, void>::invoke,
                  featureLowering(RuntimeFeature::TermBell),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Provide dotted name for INKEY$ access through the Viper.Terminal namespace
    DescriptorRow{"Viper.Terminal.InKey",
                  std::nullopt,
                  "string()",
                  &DirectHandler<&rt_inkey_str, rt_string>::invoke,
                  featureLowering(RuntimeFeature::InKey),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Blocking key input - waits for a key press
    DescriptorRow{"Viper.Terminal.GetKey",
                  std::nullopt,
                  "string()",
                  &DirectHandler<&rt_getkey_str, rt_string>::invoke,
                  featureLowering(RuntimeFeature::GetKey),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Blocking key input with timeout (milliseconds)
    DescriptorRow{"Viper.Terminal.GetKeyTimeout",
                  std::nullopt,
                  "string(i32)",
                  &DirectHandler<&rt_getkey_timeout_i32, rt_string, int32_t>::invoke,
                  featureLowering(RuntimeFeature::GetKeyTimeout),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Output buffering for improved terminal rendering performance
    DescriptorRow{"rt_term_begin_batch",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_begin_batch, void>::invoke,
                  featureLowering(RuntimeFeature::TermBeginBatch),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.BeginBatch",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_begin_batch, void>::invoke,
                  featureLowering(RuntimeFeature::TermBeginBatch),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_term_end_batch",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_end_batch, void>::invoke,
                  featureLowering(RuntimeFeature::TermEndBatch),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.EndBatch",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_end_batch, void>::invoke,
                  featureLowering(RuntimeFeature::TermEndBatch),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_term_flush",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_flush, void>::invoke,
                  featureLowering(RuntimeFeature::TermFlush),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Terminal.Flush",
                  std::nullopt,
                  "void()",
                  &DirectHandler<&rt_term_flush, void>::invoke,
                  featureLowering(RuntimeFeature::TermFlush),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_sleep_ms",
                  std::nullopt,
                  "void(i32)",
                  &DirectHandler<&rt_sleep_ms, void, int32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Canonical Viper.Time.* dotted names mapping to time helpers
    DescriptorRow{"Viper.Time.SleepMs",
                  std::nullopt,
                  "void(i32)",
                  &DirectHandler<&rt_sleep_ms, void, int32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_timer_ms",
                  std::nullopt,
                  "i64()",
                  &DirectHandler<&rt_timer_ms, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Time.GetTickCount",
                  std::nullopt,
                  "i64()",
                  &DirectHandler<&rt_timer_ms, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // --- Argument store helpers ---
    DescriptorRow{"rt_args_count",
                  std::nullopt,
                  "i64()",
                  &DirectHandler<&rt_args_count, int64_t>::invoke,
                  featureLowering(RuntimeFeature::ArgsCount),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_args_get",
                  std::nullopt,
                  "string(i64)",
                  &DirectHandler<&rt_args_get, rt_string, int64_t>::invoke,
                  featureLowering(RuntimeFeature::ArgsGet),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_cmdline",
                  std::nullopt,
                  "string()",
                  &DirectHandler<&rt_cmdline, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Cmdline),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_getkey_str",
                  std::nullopt,
                  "string()",
                  &DirectHandler<&rt_getkey_str, rt_string>::invoke,
                  featureLowering(RuntimeFeature::GetKey),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_getkey_timeout_i32",
                  std::nullopt,
                  "string(i32)",
                  &DirectHandler<&rt_getkey_timeout_i32, rt_string, int32_t>::invoke,
                  featureLowering(RuntimeFeature::GetKeyTimeout),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_inkey_str",
                  std::nullopt,
                  "string()",
                  &DirectHandler<&rt_inkey_str, rt_string>::invoke,
                  featureLowering(RuntimeFeature::InKey),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_cint_from_double",
                  std::nullopt,
                  "i64(f64,ptr)",
                  &invokeRtCintFromDouble,
                  featureLowering(RuntimeFeature::CintFromDouble),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_clng_from_double",
                  std::nullopt,
                  "i64(f64,ptr)",
                  &invokeRtClngFromDouble,
                  featureLowering(RuntimeFeature::ClngFromDouble),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_csng_from_double",
                  std::nullopt,
                  "f64(f64,ptr)",
                  &invokeRtCsngFromDouble,
                  featureLowering(RuntimeFeature::CsngFromDouble),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_cdbl_from_any",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_cdbl_from_any, double, double>::invoke,
                  featureLowering(RuntimeFeature::CdblFromAny),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_int_floor",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_int_floor, double, double>::invoke,
                  featureLowering(RuntimeFeature::IntFloor),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_fix_trunc",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_fix_trunc, double, double>::invoke,
                  featureLowering(RuntimeFeature::FixTrunc),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_round_even",
                  std::nullopt,
                  "f64(f64,i32)",
                  &invokeRtRoundEven,
                  featureLowering(RuntimeFeature::RoundEven),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_alloc",
                  std::nullopt,
                  "ptr(i64)",
                  &DirectHandler<&rt_alloc, void *, int64_t>::invoke,
                  featureLowering(RuntimeFeature::Alloc),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_new",
                  std::nullopt,
                  "ptr(i64)",
                  &invokeRtArrI32New,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_retain",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_arr_i32_retain, void, int32_t *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_release",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_arr_i32_release, void, int32_t *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_len",
                  std::nullopt,
                  "i64(ptr)",
                  &invokeRtArrI32Len,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_get",
                  std::nullopt,
                  "i64(ptr,i64)",
                  &invokeRtArrI32Get,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_set",
                  std::nullopt,
                  "void(ptr,i64,i64)",
                  &invokeRtArrI32Set,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_resize",
                  std::nullopt,
                  "ptr(ptr,i64)",
                  &invokeRtArrI32Resize,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // I64 (LONG) array helpers
    DescriptorRow{"rt_arr_i64_new",
                  std::nullopt,
                  "ptr(i64)",
                  &invokeRtArrI64New,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i64_retain",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_arr_i64_retain, void, int64_t *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i64_release",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_arr_i64_release, void, int64_t *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i64_len",
                  std::nullopt,
                  "i64(ptr)",
                  &invokeRtArrI64Len,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i64_get",
                  std::nullopt,
                  "i64(ptr,i64)",
                  &invokeRtArrI64Get,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i64_set",
                  std::nullopt,
                  "void(ptr,i64,i64)",
                  &invokeRtArrI64Set,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i64_resize",
                  std::nullopt,
                  "ptr(ptr,i64)",
                  &invokeRtArrI64Resize,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // F64 (SINGLE/DOUBLE) array helpers
    DescriptorRow{"rt_arr_f64_new",
                  std::nullopt,
                  "ptr(i64)",
                  &invokeRtArrF64New,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_f64_retain",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_arr_f64_retain, void, double *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_f64_release",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_arr_f64_release, void, double *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_f64_len",
                  std::nullopt,
                  "i64(ptr)",
                  &invokeRtArrF64Len,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_f64_get",
                  std::nullopt,
                  "f64(ptr,i64)",
                  &invokeRtArrF64Get,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_f64_set",
                  std::nullopt,
                  "void(ptr,i64,f64)",
                  &invokeRtArrF64Set,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_f64_resize",
                  std::nullopt,
                  "ptr(ptr,i64)",
                  &invokeRtArrF64Resize,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_oob_panic",
                  std::nullopt,
                  "void(i64,i64)",
                  &invokeRtArrOobPanic,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_str_alloc",
                  std::nullopt,
                  "ptr(i64)",
                  &invokeRtArrStrAlloc,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_str_release",
                  std::nullopt,
                  "void(ptr,i64)",
                  &invokeRtArrStrRelease,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_str_get",
                  std::nullopt,
                  "string(ptr,i64)",
                  &invokeRtArrStrGet,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_str_put",
                  std::nullopt,
                  "void(ptr,i64,str)",
                  &invokeRtArrStrPut,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_str_len",
                  std::nullopt,
                  "i64(ptr)",
                  &invokeRtArrStrLen,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Object array helpers (void* elements).
    DescriptorRow{"rt_arr_obj_new",
                  std::nullopt,
                  "ptr(i64)",
                  &invokeRtArrObjNew,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_obj_len",
                  std::nullopt,
                  "i64(ptr)",
                  &invokeRtArrObjLen,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_obj_get",
                  std::nullopt,
                  "ptr(ptr,i64)",
                  &invokeRtArrObjGet,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_obj_put",
                  std::nullopt,
                  "void(ptr,i64,ptr)",
                  &invokeRtArrObjPut,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_obj_resize",
                  std::nullopt,
                  "ptr(ptr,i64)",
                  &invokeRtArrObjResize,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_obj_release",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_arr_obj_release, void, void **>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_left",
                  std::nullopt,
                  "string(string,i64)",
                  &DirectHandler<&rt_left, rt_string, rt_string, int64_t>::invoke,
                  featureLowering(RuntimeFeature::Left),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_right",
                  std::nullopt,
                  "string(string,i64)",
                  &DirectHandler<&rt_right, rt_string, rt_string, int64_t>::invoke,
                  featureLowering(RuntimeFeature::Right),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_mid2",
                  std::nullopt,
                  "string(string,i64)",
                  &DirectHandler<&rt_mid2, rt_string, rt_string, int64_t>::invoke,
                  featureLowering(RuntimeFeature::Mid2),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_mid3",
                  std::nullopt,
                  "string(string,i64,i64)",
                  &DirectHandler<&rt_mid3, rt_string, rt_string, int64_t, int64_t>::invoke,
                  featureLowering(RuntimeFeature::Mid3),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_instr2",
                  std::nullopt,
                  "i64(string,string)",
                  &DirectHandler<&rt_instr2, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Instr2),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_instr3",
                  std::nullopt,
                  "i64(i64,string,string)",
                  &DirectHandler<&rt_instr3, int64_t, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Instr3),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_ltrim",
                  std::nullopt,
                  "string(string)",
                  &DirectHandler<&rt_ltrim, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Ltrim),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_rtrim",
                  std::nullopt,
                  "string(string)",
                  &DirectHandler<&rt_rtrim, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Rtrim),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_trim",
                  std::nullopt,
                  "string(string)",
                  &DirectHandler<&rt_trim, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Trim),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_ucase",
                  std::nullopt,
                  "string(string)",
                  &DirectHandler<&rt_ucase, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Ucase),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_lcase",
                  std::nullopt,
                  "string(string)",
                  &DirectHandler<&rt_lcase, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Lcase),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_chr",
                  std::nullopt,
                  "string(i64)",
                  &DirectHandler<&rt_chr, rt_string, int64_t>::invoke,
                  featureLowering(RuntimeFeature::Chr),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_asc",
                  std::nullopt,
                  "i64(string)",
                  &DirectHandler<&rt_asc, int64_t, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Asc),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_eq",
                  std::nullopt,
                  "i1(string,string)",
                  &DirectHandler<&rt_str_eq, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::StrEq),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Canonical name for string equality
    DescriptorRow{"Viper.Strings.Equals",
                  std::nullopt,
                  "i1(string,string)",
                  &DirectHandler<&rt_str_eq, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::StrEq),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_lt",
                  std::nullopt,
                  "i1(string,string)",
                  &DirectHandler<&rt_str_lt, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::StrLt),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_le",
                  std::nullopt,
                  "i1(string,string)",
                  &DirectHandler<&rt_str_le, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::StrLe),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_gt",
                  std::nullopt,
                  "i1(string,string)",
                  &DirectHandler<&rt_str_gt, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::StrGt),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_ge",
                  std::nullopt,
                  "i1(string,string)",
                  &DirectHandler<&rt_str_ge, int64_t, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::StrGe),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_val",
                  std::nullopt,
                  "f64(string)",
                  &DirectHandler<&rt_val, double, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Val),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_val_to_double",
                  std::nullopt,
                  "f64(ptr,ptr)",
                  &DirectHandler<&rt_val_to_double, double, const char *, bool *>::invoke,
                  featureLowering(RuntimeFeature::Val),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_string_cstr",
                  std::nullopt,
                  "ptr(string)",
                  &DirectHandler<&rt_string_cstr, const char *, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Val),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_sqrt",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_sqrt, double, double>::invoke,
                  featureLowering(RuntimeFeature::Sqrt, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_abs_i64",
                  std::nullopt,
                  "i64(i64)",
                  &DirectHandler<&rt_abs_i64, long long, long long>::invoke,
                  featureLowering(RuntimeFeature::AbsI64, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_abs_f64",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_abs_f64, double, double>::invoke,
                  featureLowering(RuntimeFeature::AbsF64, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_floor",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_floor, double, double>::invoke,
                  featureLowering(RuntimeFeature::Floor, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_ceil",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_ceil, double, double>::invoke,
                  featureLowering(RuntimeFeature::Ceil, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_sin",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_sin, double, double>::invoke,
                  featureLowering(RuntimeFeature::Sin, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_cos",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_cos, double, double>::invoke,
                  featureLowering(RuntimeFeature::Cos, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_tan",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_tan, double, double>::invoke,
                  featureLowering(RuntimeFeature::Tan, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_atan",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_atan, double, double>::invoke,
                  featureLowering(RuntimeFeature::Atan, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_exp",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_exp, double, double>::invoke,
                  featureLowering(RuntimeFeature::Exp, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_log",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_log, double, double>::invoke,
                  featureLowering(RuntimeFeature::Log, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_sgn_i64",
                  std::nullopt,
                  "i64(i64)",
                  &DirectHandler<&rt_sgn_i64, long long, long long>::invoke,
                  featureLowering(RuntimeFeature::SgnI64, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_sgn_f64",
                  std::nullopt,
                  "f64(f64)",
                  &DirectHandler<&rt_sgn_f64, double, double>::invoke,
                  featureLowering(RuntimeFeature::SgnF64, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_pow_f64_chkdom",
                  std::nullopt,
                  "f64(f64,f64)",
                  &invokeRtPowF64Chkdom,
                  featureLowering(RuntimeFeature::Pow, true),
                  kPowHidden.data(),
                  kPowHidden.size(),
                  RuntimeTrapClass::PowDomainOverflow},
    DescriptorRow{"rt_randomize_i64",
                  std::nullopt,
                  "void(i64)",
                  &DirectHandler<&rt_randomize_i64, void, long long>::invoke,
                  featureLowering(RuntimeFeature::RandomizeI64, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Canonical name for RANDOMIZE
    DescriptorRow{"Viper.Math.Randomize",
                  std::nullopt,
                  "void(i64)",
                  &DirectHandler<&rt_randomize_i64, void, long long>::invoke,
                  featureLowering(RuntimeFeature::RandomizeI64, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_rnd",
                  std::nullopt,
                  "f64()",
                  &DirectHandler<&rt_rnd, double>::invoke,
                  featureLowering(RuntimeFeature::Rnd, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Canonical name for RND
    DescriptorRow{"Viper.Math.Rnd",
                  std::nullopt,
                  "f64()",
                  &DirectHandler<&rt_rnd, double>::invoke,
                  featureLowering(RuntimeFeature::Rnd, true),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{
        "rt_open_err_vstr",
        std::nullopt,
        "i32(string,i32,i32)",
        &DirectHandler<&rt_open_err_vstr, int32_t, ViperString *, int32_t, int32_t>::invoke,
        kManualLowering,
        nullptr,
        0,
        RuntimeTrapClass::None},
    DescriptorRow{"rt_close_err",
                  std::nullopt,
                  "i32(i32)",
                  &DirectHandler<&rt_close_err, int32_t, int32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_write_ch_err",
                  std::nullopt,
                  "i32(i32,string)",
                  &DirectHandler<&rt_write_ch_err, int32_t, int32_t, ViperString *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_println_ch_err",
                  std::nullopt,
                  "i32(i32,string)",
                  &DirectHandler<&rt_println_ch_err, int32_t, int32_t, ViperString *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_line_input_ch_err",
                  std::nullopt,
                  "i32(i32,ptr)",
                  &DirectHandler<&rt_line_input_ch_err, int32_t, int32_t, ViperString **>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_eof_ch",
                  std::nullopt,
                  "i32(i32)",
                  &DirectHandler<&rt_eof_ch, int32_t, int32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_lof_ch",
                  std::nullopt,
                  "i32(i32)",
                  &DirectHandler<&rt_lof_ch_i32, int32_t, int32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_loc_ch",
                  std::nullopt,
                  "i32(i32)",
                  &DirectHandler<&rt_loc_ch_i32, int32_t, int32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_seek_ch_err",
                  std::nullopt,
                  "i32(i32,i64)",
                  &DirectHandler<&rt_seek_ch_err, int32_t, int32_t, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_empty",
                  std::nullopt,
                  "string()",
                  &DirectHandler<&rt_str_empty, rt_string>::invoke,
                  kAlwaysLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_const_cstr",
                  std::nullopt,
                  "string(ptr)",
                  &DirectHandler<&rt_const_cstr, rt_string, const char *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // Module-level variable address helpers: return ptr from string key
    DescriptorRow{"rt_modvar_addr_i64",
                  std::nullopt,
                  "ptr(string)",
                  &DirectHandler<&rt_modvar_addr_i64, void *, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_modvar_addr_f64",
                  std::nullopt,
                  "ptr(string)",
                  &DirectHandler<&rt_modvar_addr_f64, void *, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_modvar_addr_i1",
                  std::nullopt,
                  "ptr(string)",
                  &DirectHandler<&rt_modvar_addr_i1, void *, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_modvar_addr_ptr",
                  std::nullopt,
                  "ptr(string)",
                  &DirectHandler<&rt_modvar_addr_ptr, void *, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_modvar_addr_str",
                  std::nullopt,
                  "ptr(string)",
                  &DirectHandler<&rt_modvar_addr_str, void *, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
// Canonical dotted names for string retain/release, with legacy aliases gated by
// VIPER_RUNTIME_NS_DUAL
#if VIPER_RUNTIME_NS_DUAL
    DescriptorRow{"Viper.Strings.RetainMaybe",
                  std::nullopt,
                  "void(string)",
                  &DirectHandler<&rt_str_retain_maybe, void, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_retain_maybe",
                  std::nullopt,
                  "void(string)",
                  &DirectHandler<&rt_str_retain_maybe, void, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Strings.ReleaseMaybe",
                  std::nullopt,
                  "void(string)",
                  &DirectHandler<&rt_str_release_maybe, void, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_release_maybe",
                  std::nullopt,
                  "void(string)",
                  &DirectHandler<&rt_str_release_maybe, void, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
#else
    DescriptorRow{"Viper.Strings.RetainMaybe",
                  std::nullopt,
                  "void(string)",
                  &DirectHandler<&rt_str_retain_maybe, void, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"Viper.Strings.ReleaseMaybe",
                  std::nullopt,
                  "void(string)",
                  &DirectHandler<&rt_str_release_maybe, void, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
#endif
    DescriptorRow{"rt_test_bridge_mutate_str",
                  std::nullopt,
                  "void(string)",
                  &testMutateStringNoStack,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_new_i64",
                  std::nullopt,
                  "ptr(i64,i64)",
                  &DirectHandler<&rt_obj_new_i64, void *, int64_t, int64_t>::invoke,
                  featureLowering(RuntimeFeature::ObjNew),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // OOP virtual/interface dispatch helpers
    DescriptorRow{"rt_get_vfunc",
                  std::nullopt,
                  "ptr(ptr,i64)",
                  &DirectHandler<&rt_get_vfunc, void *, const rt_object *, uint32_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_itable_lookup",
                  std::nullopt,
                  "ptr(ptr,i64)",
                  &DirectHandler<&rt_itable_lookup, void **, void *, int>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_retain_maybe",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_obj_retain_maybe, void, void *>::invoke,
                  featureLowering(RuntimeFeature::ObjRetainMaybe),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_release_check0",
                  std::nullopt,
                  "i1(ptr)",
                  &DirectHandler<&rt_obj_release_check0, int32_t, void *>::invoke,
                  featureLowering(RuntimeFeature::ObjReleaseChk0),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_free",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_obj_free, void, void *>::invoke,
                  featureLowering(RuntimeFeature::ObjFree),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_heap_mark_disposed",
                  std::nullopt,
                  "i1(ptr)",
                  &DirectHandler<&rt_heap_mark_disposed, int32_t, void *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    // --- Graphics runtime helpers ---
    DescriptorRow{"rt_gfx_window_new",
                  std::nullopt,
                  "ptr(i64,i64,string)",
                  &DirectHandler<&rt_gfx_window_new, void *, int64_t, int64_t, rt_string>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_window_destroy",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_gfx_window_destroy, void, void *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_window_update",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_gfx_window_update, void, void *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_window_clear",
                  std::nullopt,
                  "void(ptr,i64)",
                  &DirectHandler<&rt_gfx_window_clear, void, void *, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_window_width",
                  std::nullopt,
                  "i64(ptr)",
                  &DirectHandler<&rt_gfx_window_width, int64_t, void *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_window_height",
                  std::nullopt,
                  "i64(ptr)",
                  &DirectHandler<&rt_gfx_window_height, int64_t, void *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_window_should_close",
                  std::nullopt,
                  "i64(ptr)",
                  &DirectHandler<&rt_gfx_window_should_close, int64_t, void *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_draw_line",
                  std::nullopt,
                  "void(ptr,i64,i64,i64,i64,i64)",
                  &DirectHandler<&rt_gfx_draw_line, void, void *, int64_t, int64_t, int64_t, int64_t,
                                 int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_draw_rect",
                  std::nullopt,
                  "void(ptr,i64,i64,i64,i64,i64)",
                  &DirectHandler<&rt_gfx_draw_rect, void, void *, int64_t, int64_t, int64_t, int64_t,
                                 int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_draw_rect_outline",
                  std::nullopt,
                  "void(ptr,i64,i64,i64,i64,i64)",
                  &DirectHandler<&rt_gfx_draw_rect_outline, void, void *, int64_t, int64_t, int64_t,
                                 int64_t, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_draw_circle",
                  std::nullopt,
                  "void(ptr,i64,i64,i64,i64)",
                  &DirectHandler<&rt_gfx_draw_circle, void, void *, int64_t, int64_t, int64_t,
                                 int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_draw_circle_outline",
                  std::nullopt,
                  "void(ptr,i64,i64,i64,i64)",
                  &DirectHandler<&rt_gfx_draw_circle_outline, void, void *, int64_t, int64_t,
                                 int64_t, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_set_pixel",
                  std::nullopt,
                  "void(ptr,i64,i64,i64)",
                  &DirectHandler<&rt_gfx_set_pixel, void, void *, int64_t, int64_t, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_poll_event",
                  std::nullopt,
                  "i64(ptr)",
                  &DirectHandler<&rt_gfx_poll_event, int64_t, void *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_key_down",
                  std::nullopt,
                  "i64(ptr,i64)",
                  &DirectHandler<&rt_gfx_key_down, int64_t, void *, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_gfx_rgb",
                  std::nullopt,
                  "i64(i64,i64,i64)",
                  &DirectHandler<&rt_gfx_rgb, int64_t, int64_t, int64_t, int64_t>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
});

struct Descriptor
{
    std::string_view name{};
    std::size_t rowIndex{};
};

/// @brief Build a sorted index over @ref kDescriptorRows by symbol name.
/// @details Copies every descriptor row alongside its row index and performs a
///          simple insertion-sort so lookups can binary search the resulting
///          array without constructing dynamic state at runtime.
constexpr auto makeDescriptorIndex()
{
    std::array<Descriptor, kDescriptorRows.size()> index{};
    for (std::size_t i = 0; i < index.size(); ++i)
        index[i] = Descriptor{kDescriptorRows[i].name, i};

    for (std::size_t i = 0; i < index.size(); ++i)
    {
        for (std::size_t j = i + 1; j < index.size(); ++j)
        {
            if (index[j].name < index[i].name)
                std::swap(index[i], index[j]);
        }
    }
    return index;
}

constexpr std::array<Descriptor, kDescriptorRows.size()> kDescriptors = makeDescriptorIndex();

/// @brief Extract just the descriptor names for binary search comparisons.
/// @details Projects @ref kDescriptors into an array of string views so
///          @ref indexOf can use `std::lower_bound` without touching the row
///          metadata.
constexpr auto makeDescriptorNames()
{
    std::array<std::string_view, kDescriptors.size()> names{};
    for (std::size_t i = 0; i < names.size(); ++i)
        names[i] = kDescriptors[i].name;
    return names;
}

constexpr std::array<std::string_view, kDescriptors.size()> kNames = makeDescriptorNames();

/// @brief Build a lookup table from runtime features to descriptor rows.
/// @details Initialises an array keyed by @ref RuntimeFeature enumerators and
///          records the first descriptor row that requires each feature.  Entries
///          left at -1 indicate the feature has no dedicated runtime helper.
constexpr auto makeFeatureIndex()
{
    std::array<int, static_cast<std::size_t>(RuntimeFeature::Count)> featureIndex{};
    for (auto &entry : featureIndex)
        entry = -1;

    for (std::size_t i = 0; i < kDescriptorRows.size(); ++i)
    {
        const auto &row = kDescriptorRows[i];
        if (row.lowering.kind == RuntimeLoweringKind::Feature)
        {
            auto &slot = featureIndex[static_cast<std::size_t>(row.lowering.feature)];
            if (slot < 0)
                slot = static_cast<int>(i);
        }
    }

    return featureIndex;
}

const std::array<int, static_cast<std::size_t>(RuntimeFeature::Count)> kFeatureIndex =
    makeFeatureIndex();

/// @brief Locate the sorted descriptor index entry for a symbol name.
/// @details Performs a binary search over @ref kNames and returns the matching
///          index or -1 when the symbol is absent, enabling callers to fetch
///          either the descriptor row or parsed signature without allocating.
static constexpr int indexOf(std::string_view name) noexcept
{
    const auto it = std::lower_bound(kNames.begin(), kNames.end(), name);
    if (it == kNames.end() || *it != name)
        return -1;
    return static_cast<int>(std::distance(kNames.begin(), it));
}

/// @brief Construct a @ref RuntimeSignature from a descriptor table row.
///
/// @details Uses a pre-generated signature when available or parses the spec
///          string otherwise.  Hidden parameters and trap metadata are copied
///          from the descriptor row.
RuntimeSignature buildSignature(const DescriptorRow &row)
{
    RuntimeSignature signature =
        row.signatureId ? signatureFor(*row.signatureId) : parseSignatureSpec(row.spec);
    signature.hiddenParams.assign(row.hidden, row.hidden + row.hiddenCount);
    signature.trapClass = row.trapClass;
    const auto effects = classifyHelperEffects(row.name);
    signature.nothrow = signature.nothrow || effects.nothrow;
    signature.readonly = signature.readonly || effects.readonly;
    signature.pure = signature.pure || effects.pure;
    return signature;
}

/// @brief Convert a descriptor table row into a @ref RuntimeDescriptor.
///
/// @details Populates the descriptor with the name, parsed signature, handler
///          thunk, lowering metadata, and trap classification.
RuntimeDescriptor buildDescriptor(const DescriptorRow &row)
{
    RuntimeDescriptor descriptor;
    descriptor.name = row.name;
    descriptor.signature = buildSignature(row);
    descriptor.handler = row.handler;
    descriptor.lowering = row.lowering;
    descriptor.trapClass = row.trapClass;
    return descriptor;
}

#ifndef NDEBUG
/// @brief Convert a signature parameter kind into a printable name.
/// @details Used exclusively in debug assertions to emit human-friendly
///          diagnostics when runtime descriptors drift from the expected
///          registry entries.
const char *sigParamKindName(signatures::SigParam::Kind kind)
{
    using signatures::SigParam;
    switch (kind)
    {
        case SigParam::Kind::I1:
            return "i1";
        case SigParam::Kind::I32:
            return "i32";
        case SigParam::Kind::I64:
            return "i64";
        case SigParam::Kind::F32:
            return "f32";
        case SigParam::Kind::F64:
            return "f64";
        case SigParam::Kind::Ptr:
            return "ptr";
        case SigParam::Kind::Str:
            return "str";
    }
    return "unknown";
}

/// @brief Translate IL type kinds into signature parameter kinds.
/// @details Normalises several IL integer types down to the ABI shapes used in
///          runtime signatures, asserting in debug builds when encountering
///          unsupported kinds so descriptor drift is caught early.
signatures::SigParam::Kind mapToSigParamKind(il::core::Type::Kind kind)
{
    using Kind = il::core::Type::Kind;
    using signatures::SigParam;
    switch (kind)
    {
        case Kind::I1:
            return SigParam::Kind::I1;
        case Kind::I16:
        case Kind::I32:
            return SigParam::Kind::I32;
        case Kind::I64:
            return SigParam::Kind::I64;
        case Kind::F64:
            return SigParam::Kind::F64;
        case Kind::Ptr:
        case Kind::Str:
        case Kind::ResumeTok:
            return SigParam::Kind::Ptr;
        case Kind::Void:
            assert(false && "void type cannot appear in parameter list");
            reportInvariantViolation("void type cannot appear in parameter list");
        case Kind::Error:
            assert(false && "error type cannot appear in runtime signature");
            reportInvariantViolation("error type cannot appear in runtime signature");
    }
    assert(false && "unhandled runtime type kind");
    reportInvariantViolation("unhandled runtime type kind");
}

/// @brief Build the list of parameter kinds expected by a runtime signature.
/// @details Iterates the IL type descriptors recorded in @p signature and maps
///          them into the signature::SigParam domain using @ref mapToSigParamKind
///          so debug validation can compare against whitelisted expectations.
std::vector<signatures::SigParam::Kind> makeParamKinds(const RuntimeSignature &signature)
{
    std::vector<signatures::SigParam::Kind> kinds;
    kinds.reserve(signature.paramTypes.size());
    for (const auto &param : signature.paramTypes)
        kinds.push_back(mapToSigParamKind(param.kind));
    return kinds;
}

/// @brief Describe the return type of a runtime signature in signature kind form.
/// @details Produces either an empty vector (for void results) or a single entry
///          that mirrors the ABI-visible type, enabling uniform comparison logic
///          for both parameters and results.
std::vector<signatures::SigParam::Kind> makeReturnKinds(const RuntimeSignature &signature)
{
    std::vector<signatures::SigParam::Kind> kinds;
    if (signature.retType.kind != il::core::Type::Kind::Void)
        kinds.push_back(mapToSigParamKind(signature.retType.kind));
    return kinds;
}

/// @brief Ensure the debug-only signature registry is populated.
/// @details Registers every runtime signature group the first time validation
///          runs so later checks can compare descriptors against the whitelist
///          emitted by the dedicated signature modules.
void ensureSignatureWhitelist()
{
    static const bool registered = []
    {
        signatures::register_fileio_signatures();
        signatures::register_string_signatures();
        signatures::register_math_signatures();
        signatures::register_array_signatures();
        return true;
    }();
    (void)registered;
}

/// @brief Compare generated descriptors against the debug whitelist.
/// @details Builds a map of actual descriptors, verifies that every expected
///          signature is present exactly once, and checks that parameter/return
///          kinds match.  Any mismatch triggers assertions accompanied by a
///          descriptive message to simplify debugging.
void validateRuntimeDescriptors(const std::vector<RuntimeDescriptor> &descriptors)
{
    ensureSignatureWhitelist();
    const auto &expected = signatures::all_signatures();

    std::unordered_map<std::string_view, const RuntimeDescriptor *> actual;
    actual.reserve(descriptors.size());
    for (const auto &descriptor : descriptors)
    {
        auto [it, inserted] = actual.emplace(descriptor.name, &descriptor);
        if (!inserted)
        {
            std::fprintf(stderr,
                         "Duplicate runtime descriptor registered for symbol '%s'.\n",
                         descriptor.name.data());
            assert(false && "duplicate runtime descriptor registration");
            continue;
        }
    }

    std::unordered_set<std::string_view> seen;
    seen.reserve(expected.size());
    for (const auto &signature : expected)
    {
        if (!seen.insert(signature.name).second)
        {
            std::fprintf(stderr,
                         "Duplicate expected runtime signature entry for '%s'.\n",
                         signature.name.c_str());
            assert(false && "duplicate expected runtime signature");
            continue;
        }

        auto it = actual.find(signature.name);
        if (it == actual.end())
        {
            std::fprintf(stderr,
                         "Expected runtime signature '%s' missing from registry.\n",
                         signature.name.c_str());
            assert(false && "missing runtime signature");
            continue;
        }

        const auto &descriptor = *it->second;
        const auto params = makeParamKinds(descriptor.signature);
        const auto returns = makeReturnKinds(descriptor.signature);

        if (params.size() != signature.params.size())
        {
            std::fprintf(
                stderr,
                "Runtime signature '%s' parameter count mismatch (expected %zu, got %zu).\n",
                signature.name.c_str(),
                signature.params.size(),
                params.size());
            assert(false && "runtime signature parameter count mismatch");
        }
        else
        {
            for (std::size_t index = 0; index < params.size(); ++index)
            {
                const auto expectedKind = signature.params[index].kind;
                const auto actualKind = params[index];
                if (expectedKind != actualKind)
                {
                    std::fprintf(stderr,
                                 "Runtime signature '%s' parameter %zu type mismatch (expected %s, "
                                 "got %s).\n",
                                 signature.name.c_str(),
                                 index,
                                 sigParamKindName(expectedKind),
                                 sigParamKindName(actualKind));
                    assert(false && "runtime signature parameter type mismatch");
                    break;
                }
            }
        }

        if (returns.size() != signature.rets.size())
        {
            std::fprintf(stderr,
                         "Runtime signature '%s' return count mismatch (expected %zu, got %zu).\n",
                         signature.name.c_str(),
                         signature.rets.size(),
                         returns.size());
            assert(false && "runtime signature return count mismatch");
        }
        else
        {
            for (std::size_t index = 0; index < returns.size(); ++index)
            {
                const auto expectedKind = signature.rets[index].kind;
                const auto actualKind = returns[index];
                if (expectedKind != actualKind)
                {
                    std::fprintf(
                        stderr,
                        "Runtime signature '%s' return %zu type mismatch (expected %s, got %s).\n",
                        signature.name.c_str(),
                        index,
                        sigParamKindName(expectedKind),
                        sigParamKindName(actualKind));
                    assert(false && "runtime signature return type mismatch");
                    break;
                }
            }
        }
    }
}
#endif

} // namespace

/// @brief Retrieve the immutable list of runtime descriptors.
///
/// @details Lazily constructs the registry from @ref kDescriptorRows on first
///          use and caches it for subsequent lookups.
const std::vector<RuntimeDescriptor> &runtimeRegistry()
{
    static const std::vector<RuntimeDescriptor> registry = []
    {
        std::vector<RuntimeDescriptor> entries;
        entries.reserve(kDescriptorRows.size());
        for (const auto &row : kDescriptorRows)
            entries.push_back(buildDescriptor(row));
#ifndef NDEBUG
        validateRuntimeDescriptors(entries);
#endif
        return entries;
    }();
    return registry;
}

/// @brief Find a runtime descriptor by its exported name.
///
/// @details Performs a binary search over the compile-time sorted descriptor
///          index, avoiding dynamic map construction on lookup.
const RuntimeDescriptor *findRuntimeDescriptor(std::string_view name)
{
    const int idx = indexOf(name);
    if (idx < 0)
        return nullptr;
    const auto rowIndex = kDescriptors[static_cast<std::size_t>(idx)].rowIndex;
    const auto &registry = runtimeRegistry();
    return &registry[rowIndex];
}

/// @brief Locate the descriptor that provides a particular runtime feature.
///
/// @details Uses a precomputed table mapping features to descriptor indices for
///          constant-time lookup without allocating supporting data structures.
const RuntimeDescriptor *findRuntimeDescriptor(RuntimeFeature feature)
{
    const auto index = kFeatureIndex[static_cast<std::size_t>(feature)];
    if (index < 0)
        return nullptr;
    return &runtimeRegistry()[static_cast<std::size_t>(index)];
}

/// @brief Provide a map from runtime names to parsed signatures.
///
/// @details Materialises an unordered_map on first access by iterating over the
///          registry, enabling quick signature lookups by string name.
const std::unordered_map<std::string_view, RuntimeSignature> &runtimeSignatures()
{
    static const std::unordered_map<std::string_view, RuntimeSignature> table = []
    {
        std::unordered_map<std::string_view, RuntimeSignature> map;
        for (const auto &entry : runtimeRegistry())
            map.emplace(entry.name, entry.signature);
        return map;
    }();
    return table;
}

/// @brief Look up the generated signature enumerator for a runtime symbol name.
///
/// @param name Runtime symbol to resolve.
/// @return Enumerator when found or std::nullopt otherwise.
std::optional<RtSig> findRuntimeSignatureId(std::string_view name)
{
    const int idx = indexOf(name);
    if (idx < 0)
        return std::nullopt;
    const auto &row = kDescriptorRows[kDescriptors[static_cast<std::size_t>(idx)].rowIndex];
    if (!row.signatureId)
        return std::nullopt;
    return row.signatureId;
}

/// @brief Retrieve a signature descriptor by enumerator.
///
/// @param sig Enumerated signature identifier.
/// @return Pointer to the signature when valid, otherwise nullptr.
const RuntimeSignature *findRuntimeSignature(RtSig sig)
{
    if (!isValid(sig))
        return nullptr;
    return &signatureFor(sig);
}

/// @brief Retrieve a signature descriptor by runtime symbol name.
///
/// @details First attempts to resolve the generated enumerator; when absent it
///          falls back to descriptors registered in @ref runtimeRegistry.
///
/// @param name Runtime symbol to search for.
/// @return Pointer to the signature when found, otherwise nullptr.
const RuntimeSignature *findRuntimeSignature(std::string_view name)
{
    if (auto id = findRuntimeSignatureId(name))
        return findRuntimeSignature(*id);
    if (const auto *desc = findRuntimeDescriptor(name))
        return &desc->signature;
    return nullptr;
}

/// @brief Check whether a callee uses C-style variadic arguments.
///
/// @details Consults the runtime registry first; falls back to a hardcoded list
///          of known C library functions that are not registered but require
///          vararg calling convention (e.g., rt_snprintf, rt_sb_printf).
///
/// @param name Symbol name of the callee to query.
/// @return True when the callee is known to be variadic.
bool isVarArgCallee(std::string_view name)
{
    // First check the runtime registry for registered signatures with isVarArg set.
    if (const auto *sig = findRuntimeSignature(name))
    {
        if (sig->isVarArg)
            return true;
    }

    // Fallback: known C library vararg functions used by the runtime but not
    // registered in the signature registry. This list is maintained here to
    // centralise vararg metadata rather than scattering it across backends.
    static constexpr std::string_view kKnownVarArgCallees[] = {
        "rt_snprintf",
        "rt_sb_printf",
    };

    for (const auto &known : kKnownVarArgCallees)
    {
        if (name == known)
            return true;
    }

    return false;
}

/// @brief Lightweight runtime descriptor self-check for both debug and release builds.
///
/// @details Performs essential sanity checks that are cheap enough to run in release:
///          1. No duplicate descriptor names in the registry
///          2. Each descriptor's parsed signature has a consistent parameter count
///
///          In debug builds, additionally runs the full whitelist validation.
///          This function is idempotent and thread-safe via static initialization.
///
/// @return True if all checks pass. In release builds, logs errors and returns false
///         on failure. In debug builds, also asserts.
bool selfCheckRuntimeDescriptors()
{
    // Run once per process via static init guard
    static const bool result = []() -> bool
    {
        const auto &descriptors = runtimeRegistry();
        bool valid = true;

        // Check 1: No duplicate descriptor names
        // Use a simple O(n^2) check to avoid heap allocation for hash map
        // This runs once at startup so the cost is acceptable
        for (std::size_t i = 0; i < descriptors.size(); ++i)
        {
            for (std::size_t j = i + 1; j < descriptors.size(); ++j)
            {
                if (descriptors[i].name == descriptors[j].name)
                {
                    std::fprintf(stderr,
                                 "[FATAL] Runtime descriptor duplicate: '%.*s' at indices %zu and "
                                 "%zu\n",
                                 static_cast<int>(descriptors[i].name.size()),
                                 descriptors[i].name.data(),
                                 i,
                                 j);
#ifndef NDEBUG
                    assert(false && "duplicate runtime descriptor name");
#endif
                    valid = false;
                }
            }
        }

        // Check 2: Parameter count consistency
        // For each descriptor, verify the signature's parameter count is reasonable
        // (non-negative and within expected bounds for runtime functions)
        constexpr std::size_t kMaxReasonableParams = 16;
        for (std::size_t i = 0; i < descriptors.size(); ++i)
        {
            const auto &desc = descriptors[i];
            const std::size_t paramCount = desc.signature.paramTypes.size();
            if (paramCount > kMaxReasonableParams)
            {
                std::fprintf(stderr,
                             "[FATAL] Runtime descriptor '%.*s' has %zu parameters (max %zu)\n",
                             static_cast<int>(desc.name.size()),
                             desc.name.data(),
                             paramCount,
                             kMaxReasonableParams);
#ifndef NDEBUG
                assert(false && "runtime descriptor has too many parameters");
#endif
                valid = false;
            }
        }

        // In debug builds, the full whitelist validation already ran during
        // runtimeRegistry() initialization. We just report the combined result.
        // The heavy validation (ensureSignatureWhitelist, full type matching)
        // only runs in debug builds via validateRuntimeDescriptors().

        return valid;
    }();

    return result;
}

// =============================================================================
// Invariant Violation Mode Configuration
// =============================================================================

namespace
{
/// @brief Current mode for handling invariant violations.
InvariantViolationMode g_violationMode = InvariantViolationMode::Abort;

/// @brief Registered trap handler for invariant violations.
InvariantTrapHandler g_trapHandler = nullptr;
} // namespace

void setInvariantViolationMode(InvariantViolationMode mode)
{
    g_violationMode = mode;
}

InvariantViolationMode getInvariantViolationMode()
{
    return g_violationMode;
}

void setInvariantTrapHandler(InvariantTrapHandler handler)
{
    g_trapHandler = handler;
}

InvariantTrapHandler getInvariantTrapHandler()
{
    return g_trapHandler;
}

[[noreturn]] void reportInvariantViolation(const char *message)
{
    // In Trap mode with a registered handler, attempt to route through the VM.
    if (g_violationMode == InvariantViolationMode::Trap && g_trapHandler != nullptr)
    {
        // Handler returns true if the trap was processed (e.g., caught by an exception
        // handler in the VM). In this case, the handler should not return at all.
        // If it returns false, we fall through to abort.
        if (g_trapHandler(message))
        {
            // Handler claimed success but returned - this is a logic error.
            // The trap mechanism should not allow normal return.
            std::fprintf(stderr,
                         "[FATAL] Invariant trap handler returned after claiming success.\n");
        }
        // Fall through to abort if handler returned false or unexpectedly.
    }

    // Default/fallback behavior: log and abort.
    std::fprintf(stderr, "[FATAL] Runtime signature invariant violation: %s\n", message);
    std::abort();
}

} // namespace il::runtime
