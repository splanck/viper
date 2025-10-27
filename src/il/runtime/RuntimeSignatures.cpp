//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeSignatures.cpp
// Purpose: Build and expose the runtime descriptor registry used by IL
//          consumers to marshal calls into the C runtime.
// Key invariants: The descriptor table is immutable, matches runtime helpers
//                 one-to-one, and is initialised lazily in a thread-safe manner.
// Ownership/Lifetime: All descriptors have static storage duration and remain
//                     valid for the lifetime of the process.
// Links: docs/il-guide.md#reference, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"
#ifndef NDEBUG
#include "il/runtime/signatures/Registry.hpp"
#include <cassert>
#include <cstdio>
#include <unordered_set>
namespace il::runtime::signatures
{
void register_fileio_signatures();
void register_string_signatures();
void register_math_signatures();
void register_array_signatures();
} // namespace il::runtime::signatures
#endif

#include "rt.hpp"
#include "rt_debug.h"
#include "rt_fp.h"
#include "rt_internal.h"
#include "rt_math.h"
#include "rt_numeric.h"
#include "rt_random.h"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::runtime
{
namespace
{

using Kind = il::core::Type::Kind;

constexpr std::size_t kRtSigCount = data::kRtSigCount;

/// @brief Clamp a 64-bit runtime file position/length to a 32-bit IL value.
///
/// @details Runtime helpers such as @ref rt_lof_ch and @ref rt_loc_ch expose
///          64-bit offsets so large files can be supported.  BASIC however
///          models these builtins as returning 32-bit signed integers.  The
///          bridge narrows the runtime result using saturation so overflow
///          produces INT32_MAX/INT32_MIN instead of wrapping.
int32_t clampRuntimeOffset(int64_t value)
{
    if (value > std::numeric_limits<int32_t>::max())
        return std::numeric_limits<int32_t>::max();
    if (value < std::numeric_limits<int32_t>::min())
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(value);
}

/// @brief Adapter that narrows @ref rt_lof_ch results to 32 bits.
int32_t rt_lof_ch_i32(int32_t channel)
{
    return clampRuntimeOffset(rt_lof_ch(channel));
}

/// @brief Adapter that narrows @ref rt_loc_ch results to 32 bits.
int32_t rt_loc_ch_i32(int32_t channel)
{
    return clampRuntimeOffset(rt_loc_ch(channel));
}

constexpr const char kTestBridgeMutatedText[] = "bridge-mutated";

void testMutateStringNoStack(void **args, void * /*result*/)
{
    if (!args)
        return;
    auto *slot = reinterpret_cast<rt_string *>(args[0]);
    if (!slot)
        return;
    rt_string updated = rt_string_from_bytes(kTestBridgeMutatedText,
                                             sizeof(kTestBridgeMutatedText) - 1);
    *slot = updated;
}

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
            entries[i] = parseSignatureSpec(data::kRtSigSpecs[i]);
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

/// @brief Return a lazily initialised index mapping symbol names to signatures.
///
/// @details Constructs an unordered_map once by iterating over the generated
///          symbol name table.  The index enables fast lookup of enumerators by
///          their canonical name.
///
/// @return Map from runtime symbol names to signature identifiers.
const std::unordered_map<std::string_view, RtSig> &generatedSigIndex()
{
    static const auto map = []
    {
        std::unordered_map<std::string_view, RtSig> table;
        table.reserve(kRtSigCount);
        for (std::size_t i = 0; i < kRtSigCount; ++i)
            table.emplace(data::kRtSigSymbolNames[i], static_cast<RtSig>(i));
        return table;
    }();
    return map;
}

/// @brief Adapter that invokes a concrete runtime function from VM call stubs.
///
/// @details Each instantiation binds a runtime C function pointer and translates
///          the generic `void **` argument array provided by the VM into typed
///          parameters.  The @ref invoke entry point matches the signature
///          expected by @ref RuntimeDescriptor.
template <auto Fn, typename Ret, typename... Args> struct DirectHandler
{
    /// @brief Dispatch the runtime call using the supplied argument array.
    ///
    /// @details Expands the argument pack through @ref call using an index
    ///          sequence so each operand is reinterpreted to the function's
    ///          declared type.  Results are written into the provided buffer when
    ///          applicable.
    ///
    /// @param args Pointer to the marshalled argument array.
    /// @param result Optional pointer to storage for the return value.
    static void invoke(void **args, void *result)
    {
        call(args, result, std::index_sequence_for<Args...>{});
    }

  private:
    /// @brief Helper that expands the argument pack and forwards to the target.
    ///
    /// @details Uses `reinterpret_cast` to view each `void *` slot as the
    ///          appropriate type.  When the function returns a value the helper
    ///          stores it through @p result.
    template <std::size_t... I>
    static void call(void **args, void *result, std::index_sequence<I...>)
    {
        if constexpr (std::is_void_v<Ret>)
        {
            Fn(*reinterpret_cast<Args *>(args[I])...);
        }
        else
        {
            Ret value = Fn(*reinterpret_cast<Args *>(args[I])...);
            *reinterpret_cast<Ret *>(result) = value;
        }
    }
};

template <auto Fn, typename Ret, typename... Args> struct ConsumingStringHandler
{
    static void invoke(void **args, void *result)
    {
        retainStrings(args, std::index_sequence_for<Args...>{});
        DirectHandler<Fn, Ret, Args...>::invoke(args, result);
    }

  private:
    template <std::size_t... I>
    static void retainStrings(void **args, std::index_sequence<I...>)
    {
        (retainArg<Args>(args, I), ...);
    }

    template <typename T>
    static void retainArg(void **args, std::size_t index)
    {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, rt_string>)
        {
            if (!args)
                return;
            void *slot = args[index];
            if (!slot)
                return;
            rt_string value = *reinterpret_cast<rt_string *>(slot);
            if (value)
                rt_string_ref(value);
        }
    }
};

/// @brief Bridge runtime string trap requests into the VM trap mechanism.
///
/// @details Extracts the string argument from the generic argument array and
///          forwards its contents to @ref rt_trap, defaulting to the literal
///          "trap" when no message is provided.
///
/// @param args Argument array provided by the VM call bridge.
void trapFromRuntimeString(void **args, void * /*result*/)
{
    rt_string str = args ? *reinterpret_cast<rt_string *>(args[0]) : nullptr;
    const char *msg = (str && str->data) ? str->data : "trap";
    rt_trap(msg);
}

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

/// @brief Wrapper for @ref rt_arr_i32_new that converts VM arguments.
///
/// @details Reads the desired array length from the argument array, invokes the
///          runtime allocator, and stores the resulting handle back into the
///          VM-provided result buffer.
void invokeRtArrI32New(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    int32_t *arr = rt_arr_i32_new(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

/// @brief Wrapper for @ref rt_arr_i32_len that returns the array length.
///
/// @details Unpacks the handle from the argument array, queries the runtime for
///          its length, and writes the value as a 64-bit integer for the VM.
void invokeRtArrI32Len(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t *const *>(arrPtr) : nullptr;
    const size_t len = rt_arr_i32_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

/// @brief Wrapper for @ref rt_arr_i32_get that exposes 32-bit elements.
///
/// @details Reads the array handle and index from the VM argument array, invokes
///          the runtime accessor, and widens the result to 64 bits before
///          storing it for the VM.
void invokeRtArrI32Get(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t *const *>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const int32_t value = rt_arr_i32_get(arr, idx);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(value);
}

/// @brief Wrapper for @ref rt_arr_i32_set that writes an element.
///
/// @details Unpacks the array handle, index, and value from the argument array
///          before delegating to the runtime setter.  No result is produced.
void invokeRtArrI32Set(void **args, void * /*result*/)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const auto valPtr = args ? reinterpret_cast<const int64_t *>(args[2]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t **>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const int32_t value = valPtr ? static_cast<int32_t>(*valPtr) : 0;
    rt_arr_i32_set(arr, idx, value);
}

/// @brief Wrapper for @ref rt_arr_i32_resize that resizes an array in place.
///
/// @details Extracts the handle and desired length, requests the runtime to
///          resize the array, updates the caller-visible handle when successful,
///          and returns the resized pointer when a result buffer is provided.
void invokeRtArrI32Resize(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto newLenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t **>(arrPtr) : nullptr;
    const size_t newLen = newLenPtr ? static_cast<size_t>(*newLenPtr) : 0;
    int32_t *local = arr;
    int32_t **handle = arrPtr ? reinterpret_cast<int32_t **>(arrPtr) : &local;
    int rc = rt_arr_i32_resize(handle, newLen);
    int32_t *resized = (rc == 0) ? *handle : nullptr;
    if (arrPtr && rc == 0)
        *reinterpret_cast<int32_t **>(arrPtr) = resized;
    if (result)
        *reinterpret_cast<void **>(result) = resized;
}

/// @brief Wrapper that forwards out-of-bounds diagnostics to the runtime.
///
/// @details Converts VM-provided index and length operands to @c size_t before
///          calling @ref rt_arr_oob_panic, which triggers a fatal runtime trap.
void invokeRtArrOobPanic(void **args, void * /*result*/)
{
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    rt_arr_oob_panic(idx, len);
}

/// @brief Wrapper for `rt_cint_from_double` with VM argument handling.
///
/// @details Extracts the input value and optional success flag pointer, invokes
///          the runtime conversion, and widens the result to 64 bits for the VM
///          register file.
void invokeRtCintFromDouble(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto okPtr = args ? reinterpret_cast<bool *const *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    bool *ok = okPtr ? *okPtr : nullptr;
    const int16_t value = rt_cint_from_double(x, ok);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(value);
}

/// @brief Wrapper for `rt_clng_from_double` following VM calling conventions.
///
/// @details Mirrors @ref invokeRtCintFromDouble but calls the runtime to
///          produce a 32-bit integer, returning it widened to 64 bits for VM
///          storage.
void invokeRtClngFromDouble(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto okPtr = args ? reinterpret_cast<bool *const *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    bool *ok = okPtr ? *okPtr : nullptr;
    const int32_t value = rt_clng_from_double(x, ok);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(value);
}

/// @brief Wrapper for `rt_csng_from_double` that returns a float as double.
///
/// @details Reads the double argument, forwards it to the runtime conversion,
///          and stores the result in the VM buffer after promoting to double so
///          the interpreter can treat it uniformly.
void invokeRtCsngFromDouble(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto okPtr = args ? reinterpret_cast<bool *const *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    bool *ok = okPtr ? *okPtr : nullptr;
    const float value = rt_csng_from_double(x, ok);
    if (result)
        *reinterpret_cast<double *>(result) = static_cast<double>(value);
}

/// @brief Wrapper for `rt_str_f_alloc` that returns a runtime string handle.
///
/// @details Converts the incoming double operand to @c float, invokes the
///          runtime allocator, and stores the resulting `rt_string` handle into
///          the VM result buffer.
void invokeRtStrFAlloc(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const float value = xPtr ? static_cast<float>(*xPtr) : 0.0f;
    rt_string str = rt_str_f_alloc(value);
    if (result)
        *reinterpret_cast<rt_string *>(result) = str;
}

/// @brief Wrapper for `rt_round_even` that computes banker’s rounding.
///
/// @details Extracts the operand and digit count, calls the runtime helper, and
///          stores the rounded double back into the VM buffer.
void invokeRtRoundEven(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto digitsPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    const int ndigits = digitsPtr ? static_cast<int>(*digitsPtr) : 0;
    const double rounded = rt_round_even(x, ndigits);
    if (result)
        *reinterpret_cast<double *>(result) = rounded;
}

/// @brief Wrapper for `rt_pow_f64_chkdom` that reports domain errors.
///
/// @details Reads the base, exponent, and optional status pointer, delegates to
///          the runtime implementation, and stores the computed power while
///          allowing the runtime to set the status flag via the provided pointer.
void invokeRtPowF64Chkdom(void **args, void *result)
{
    const auto basePtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto expPtr = args ? reinterpret_cast<const double *>(args[1]) : nullptr;
    const auto okPtrPtr = args ? reinterpret_cast<bool *const *>(args[2]) : nullptr;
    const double base = basePtr ? *basePtr : 0.0;
    const double exponent = expPtr ? *expPtr : 0.0;
    bool *ok = okPtrPtr ? *okPtrPtr : nullptr;
    const double value = rt_pow_f64_chkdom(base, exponent, ok);
    if (result)
        *reinterpret_cast<double *>(result) = value;
}

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

constexpr std::array<DescriptorRow, 89> kDescriptorRows{{
    DescriptorRow{"rt_abort",
                  std::nullopt,
                  "void(ptr)",
                  &DirectHandler<&rt_abort, void, const char *>::invoke,
                  kManualLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_print_str",
                  RtSig::PrintS,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintS)],
                  &DirectHandler<&rt_print_str, void, rt_string>::invoke,
                  kAlwaysLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_print_i64",
                  RtSig::PrintI,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintI)],
                  &DirectHandler<&rt_print_i64, void, int64_t>::invoke,
                  kAlwaysLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_print_f64",
                  RtSig::PrintF,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintF)],
                  &DirectHandler<&rt_print_f64, void, double>::invoke,
                  kAlwaysLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
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
    DescriptorRow{"rt_len",
                  RtSig::Len,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Len)],
                  &DirectHandler<&rt_len, int64_t, rt_string>::invoke,
                  kAlwaysLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_substr",
                  RtSig::Substr,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Substr)],
                  &DirectHandler<&rt_substr, rt_string, rt_string, int64_t, int64_t>::invoke,
                  kAlwaysLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_trap",
                  RtSig::Trap,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Trap)],
                  &trapFromRuntimeString,
                  kBoundsCheckedLowering,
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_concat",
                  RtSig::Concat,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Concat)],
                  &ConsumingStringHandler<&rt_concat, rt_string, rt_string, rt_string>::invoke,
                  featureLowering(RuntimeFeature::Concat),
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
    DescriptorRow{"rt_input_line",
                  RtSig::InputLine,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::InputLine)],
                  &DirectHandler<&rt_input_line, rt_string>::invoke,
                  featureLowering(RuntimeFeature::InputLine),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{
        "rt_split_fields",
        RtSig::SplitFields,
        data::kRtSigSpecs[static_cast<std::size_t>(RtSig::SplitFields)],
        &DirectHandler<&rt_split_fields, int64_t, rt_string, rt_string *, int64_t>::invoke,
        featureLowering(RuntimeFeature::SplitFields),
        nullptr,
        0,
        RuntimeTrapClass::None},
    DescriptorRow{"rt_to_int",
                  RtSig::ToInt,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ToInt)],
                  &DirectHandler<&rt_to_int, int64_t, rt_string>::invoke,
                  featureLowering(RuntimeFeature::ToInt),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_to_double",
                  RtSig::ToDouble,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ToDouble)],
                  &DirectHandler<&rt_to_double, double, rt_string>::invoke,
                  featureLowering(RuntimeFeature::ToDouble),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_parse_int64",
                  RtSig::ParseInt64,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ParseInt64)],
                  &DirectHandler<&rt_parse_int64, int32_t, const char *, int64_t *>::invoke,
                  featureLowering(RuntimeFeature::ParseInt64),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_parse_double",
                  RtSig::ParseDouble,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ParseDouble)],
                  &DirectHandler<&rt_parse_double, int32_t, const char *, double *>::invoke,
                  featureLowering(RuntimeFeature::ParseDouble),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_int_to_str",
                  RtSig::IntToStr,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::IntToStr)],
                  &DirectHandler<&rt_int_to_str, rt_string, int64_t>::invoke,
                  featureLowering(RuntimeFeature::IntToStr),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_f64_to_str",
                  RtSig::F64ToStr,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::F64ToStr)],
                  &DirectHandler<&rt_f64_to_str, rt_string, double>::invoke,
                  featureLowering(RuntimeFeature::F64ToStr),
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
    DescriptorRow{"rt_term_color_i32",
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
    DescriptorRow{"rt_getkey_str",
                  std::nullopt,
                  "string()",
                  &DirectHandler<&rt_getkey_str, rt_string>::invoke,
                  featureLowering(RuntimeFeature::GetKey),
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
    DescriptorRow{"rt_str_i16_alloc",
                  RtSig::StrFromI16,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromI16)],
                  &DirectHandler<&rt_str_i16_alloc, rt_string, int16_t>::invoke,
                  featureLowering(RuntimeFeature::StrFromI16),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_i32_alloc",
                  RtSig::StrFromI32,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromI32)],
                  &DirectHandler<&rt_str_i32_alloc, rt_string, int32_t>::invoke,
                  featureLowering(RuntimeFeature::StrFromI32),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_f_alloc",
                  RtSig::StrFromSingle,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromSingle)],
                  &invokeRtStrFAlloc,
                  featureLowering(RuntimeFeature::StrFromSingle),
                  nullptr,
                  0,
                  RuntimeTrapClass::None},
    DescriptorRow{"rt_str_d_alloc",
                  RtSig::StrFromDouble,
                  data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromDouble)],
                  &DirectHandler<&rt_str_d_alloc, rt_string, double>::invoke,
                  featureLowering(RuntimeFeature::StrFromDouble),
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
    DescriptorRow{"rt_arr_oob_panic",
                  std::nullopt,
                  "void(i64,i64)",
                  &invokeRtArrOobPanic,
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
    DescriptorRow{"rt_rnd",
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
    DescriptorRow{"rt_str_retain_maybe",
                  std::nullopt,
                  "void(string)",
                  &DirectHandler<&rt_str_retain_maybe, void, rt_string>::invoke,
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
}};

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
const char *sigParamKindName(signatures::SigParam::Kind kind)
{
    using signatures::SigParam;
    switch (kind)
    {
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
    }
    return "unknown";
}

signatures::SigParam::Kind mapToSigParamKind(il::core::Type::Kind kind)
{
    using Kind = il::core::Type::Kind;
    using signatures::SigParam;
    switch (kind)
    {
    case Kind::I1:
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
        std::fprintf(stderr, "Unexpected void type in parameter list.\n");
        assert(false && "void type cannot appear in parameter list");
        return SigParam::Kind::Ptr;
    case Kind::Error:
        std::fprintf(stderr, "Unexpected error type in runtime signature.\n");
        assert(false && "error type cannot appear in runtime signature");
        return SigParam::Kind::Ptr;
    }
    std::fprintf(stderr,
                 "Unhandled runtime type kind: %s.\n",
                 il::core::kindToString(kind).c_str());
    assert(false && "unhandled runtime type kind");
    return SigParam::Kind::Ptr;
}

std::vector<signatures::SigParam::Kind> makeParamKinds(const RuntimeSignature &signature)
{
    std::vector<signatures::SigParam::Kind> kinds;
    kinds.reserve(signature.paramTypes.size());
    for (const auto &param : signature.paramTypes)
        kinds.push_back(mapToSigParamKind(param.kind));
    return kinds;
}

std::vector<signatures::SigParam::Kind> makeReturnKinds(const RuntimeSignature &signature)
{
    std::vector<signatures::SigParam::Kind> kinds;
    if (signature.retType.kind != il::core::Type::Kind::Void)
        kinds.push_back(mapToSigParamKind(signature.retType.kind));
    return kinds;
}

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
            std::fprintf(stderr,
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
                                 "Runtime signature '%s' parameter %zu type mismatch (expected %s, got %s).\n",
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
                    std::fprintf(stderr,
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
/// @details Builds a map on first use from descriptor names to pointers and
///          returns the matching entry when present.
const RuntimeDescriptor *findRuntimeDescriptor(std::string_view name)
{
    static const auto index = []
    {
        std::unordered_map<std::string_view, const RuntimeDescriptor *> map;
        for (const auto &entry : runtimeRegistry())
            map.emplace(entry.name, &entry);
        return map;
    }();
    auto it = index.find(name);
    return it == index.end() ? nullptr : it->second;
}

/// @brief Locate the descriptor that provides a particular runtime feature.
///
/// @details Indexes descriptors by the feature recorded in their lowering
///          metadata so callers can query for optional runtime helpers.
const RuntimeDescriptor *findRuntimeDescriptor(RuntimeFeature feature)
{
    static const auto index = []
    {
        std::unordered_map<RuntimeFeature, const RuntimeDescriptor *> map;
        for (const auto &entry : runtimeRegistry())
        {
            if (entry.lowering.kind == RuntimeLoweringKind::Feature)
                map.emplace(entry.lowering.feature, &entry);
        }
        return map;
    }();
    auto it = index.find(feature);
    return it == index.end() ? nullptr : it->second;
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
    const auto &index = generatedSigIndex();
    auto it = index.find(name);
    if (it == index.end())
        return std::nullopt;
    return it->second;
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

} // namespace il::runtime
