// File: src/il/runtime/RuntimeSignatures.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Defines the shared runtime descriptor registry for IL consumers.
// Key invariants: Registry contents reflect the runtime C ABI signatures.
// Ownership/Lifetime: Uses static storage duration; entries are immutable after construction.
// Links: docs/il-guide.md#reference

#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"

#include "rt.hpp"
#include "rt_debug.h"
#include "rt_fp.h"
#include "rt_internal.h"
#include "rt_math.h"
#include "rt_numeric.h"
#include "rt_random.h"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace il::runtime
{
namespace
{
using Kind = il::core::Type::Kind;

constexpr std::size_t kRtSigCount = data::kRtSigCount;

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

bool isValid(RtSig sig)
{
    return static_cast<std::size_t>(sig) < kRtSigCount;
}

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

/// @brief Construct a runtime signature from the provided type kinds.
RuntimeSignature makeSignature(Kind ret, std::span<const Kind> params)
{
    RuntimeSignature sig;
    sig.retType = il::core::Type(ret);
    sig.paramTypes.reserve(params.size());
    for (Kind p : params)
        sig.paramTypes.emplace_back(p);
    return sig;
}

/// @brief Construct a runtime signature from an initializer list of type kinds.
RuntimeSignature makeSignature(Kind ret, std::initializer_list<Kind> params)
{
    return makeSignature(ret, std::span<const Kind>(params.begin(), params.size()));
}

/// @brief Generic adapter that invokes a runtime function with direct mapping.
template <auto Fn, typename Ret, typename... Args> struct DirectHandler
{
    static void invoke(void **args, void *result)
    {
        call(args, result, std::index_sequence_for<Args...>{});
    }

  private:
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

/// @brief Custom handler that extracts a C string from an rt_string before trapping.
void trapFromRuntimeString(void **args, void * /*result*/)
{
    rt_string str = args ? *reinterpret_cast<rt_string *>(args[0]) : nullptr;
    const char *msg = (str && str->data) ? str->data : "trap";
    rt_trap(msg);
}

constexpr RuntimeLowering makeLowering(RuntimeLoweringKind kind,
                                       RuntimeFeature feature = RuntimeFeature::Count,
                                       bool ordered = false)
{
    return RuntimeLowering{kind, feature, ordered};
}

/// @brief Adapter converting IL i64 arguments into size_t for array helpers.
void invokeRtArrI32New(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    int32_t *arr = rt_arr_i32_new(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

/// @brief Adapter converting array handle and index arguments for length queries.
void invokeRtArrI32Len(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t *const *>(arrPtr) : nullptr;
    const size_t len = rt_arr_i32_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

/// @brief Adapter reading array elements and widening i32 results to i64.
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

/// @brief Adapter writing array elements with truncation to 32 bits.
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

/// @brief Adapter resizing arrays while converting indices to size_t.
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

/// @brief Adapter invoking the noreturn out-of-bounds panic helper.
void invokeRtArrOobPanic(void **args, void * /*result*/)
{
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    rt_arr_oob_panic(idx, len);
}

/// @brief Adapter invoking INTEGER conversion while widening to 64-bit storage.
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

/// @brief Adapter invoking LONG conversion while widening to 64-bit storage.
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

/// @brief Adapter invoking SINGLE conversion while widening to double precision.
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

/// @brief Adapter narrowing double arguments to float for STR$ SINGLE formatting.
void invokeRtStrFAlloc(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const float value = xPtr ? static_cast<float>(*xPtr) : 0.0f;
    rt_string str = rt_str_f_alloc(value);
    if (result)
        *reinterpret_cast<rt_string *>(result) = str;
}

/// @brief Adapter invoking ROUND with integer digits sourced from 64-bit slots.
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

/// @brief Adapter invoking pow with an injected status pointer for trap classification.
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

/// @brief Populate the runtime descriptor registry with known helper declarations.
struct ManualDescriptorSpec
{
    std::string_view name;
    Kind ret;
    std::span<const Kind> params;
    RuntimeHandler handler;
    RuntimeLowering lowering;
    std::span<const RuntimeHiddenParam> hidden = {};
    RuntimeTrapClass trapClass = RuntimeTrapClass::None;
};

struct GeneratedDescriptorSpec
{
    std::string_view name;
    RtSig signatureId;
    RuntimeHandler handler;
    RuntimeLowering lowering;
    std::span<const RuntimeHiddenParam> hidden = {};
    RuntimeTrapClass trapClass = RuntimeTrapClass::None;
};

RuntimeDescriptor finalizeDescriptor(std::string_view name,
                                     RuntimeSignature signature,
                                     RuntimeHandler handler,
                                     RuntimeLowering lowering,
                                     std::span<const RuntimeHiddenParam> hidden,
                                     RuntimeTrapClass trapClass)
{
    signature.hiddenParams.assign(hidden.begin(), hidden.end());
    signature.trapClass = trapClass;

    RuntimeDescriptor descriptor;
    descriptor.name = name;
    descriptor.signature = std::move(signature);
    descriptor.handler = handler;
    descriptor.lowering = lowering;
    descriptor.trapClass = trapClass;
    return descriptor;
}

RuntimeDescriptor makeDescriptor(const ManualDescriptorSpec &spec)
{
    return finalizeDescriptor(spec.name,
                              makeSignature(spec.ret, spec.params),
                              spec.handler,
                              spec.lowering,
                              spec.hidden,
                              spec.trapClass);
}

RuntimeDescriptor makeDescriptor(const GeneratedDescriptorSpec &spec)
{
    RuntimeSignature signature = signatureFor(spec.signatureId);
    return finalizeDescriptor(spec.name,
                              std::move(signature),
                              spec.handler,
                              spec.lowering,
                              spec.hidden,
                              spec.trapClass);
}

template <typename Container>
void appendManualDescriptors(std::vector<RuntimeDescriptor> &entries, const Container &specs)
{
    for (const auto &spec : specs)
        entries.push_back(makeDescriptor(spec));
}

template <typename Container>
void appendGeneratedDescriptors(std::vector<RuntimeDescriptor> &entries, const Container &specs)
{
    for (const auto &spec : specs)
        entries.push_back(makeDescriptor(spec));
}

constexpr RuntimeLowering kAlwaysLowering = makeLowering(RuntimeLoweringKind::Always);
constexpr RuntimeLowering kBoundsCheckedLowering = makeLowering(RuntimeLoweringKind::BoundsChecked);
constexpr RuntimeLowering kManualLowering = makeLowering(RuntimeLoweringKind::Manual);

constexpr RuntimeLowering featureLowering(RuntimeFeature feature, bool ordered = false)
{
    return makeLowering(RuntimeLoweringKind::Feature, feature, ordered);
}

constexpr std::array<RuntimeHiddenParam, 1> kPowHidden{
    RuntimeHiddenParam{RuntimeHiddenParamKind::PowStatusPointer}};

constexpr std::array<Kind, 0> kParamsNone{};
constexpr std::array<Kind, 1> kParamsPtr{Kind::Ptr};
constexpr std::array<Kind, 1> kParamsStr{Kind::Str};
constexpr std::array<Kind, 1> kParamsI32{Kind::I32};
constexpr std::array<Kind, 1> kParamsI64{Kind::I64};
constexpr std::array<Kind, 1> kParamsF64{Kind::F64};
constexpr std::array<Kind, 2> kParamsStrI64{Kind::Str, Kind::I64};
constexpr std::array<Kind, 2> kParamsF64I32{Kind::F64, Kind::I32};
constexpr std::array<Kind, 2> kParamsF64Ptr{Kind::F64, Kind::Ptr};
constexpr std::array<Kind, 2> kParamsStrStr{Kind::Str, Kind::Str};
constexpr std::array<Kind, 2> kParamsI32I32{Kind::I32, Kind::I32};
constexpr std::array<Kind, 2> kParamsI32Str{Kind::I32, Kind::Str};
constexpr std::array<Kind, 2> kParamsI32Ptr{Kind::I32, Kind::Ptr};
constexpr std::array<Kind, 2> kParamsPtrPtr{Kind::Ptr, Kind::Ptr};
constexpr std::array<Kind, 2> kParamsPtrI64{Kind::Ptr, Kind::I64};
constexpr std::array<Kind, 2> kParamsI64I64{Kind::I64, Kind::I64};
constexpr std::array<Kind, 2> kParamsF64F64{Kind::F64, Kind::F64};
constexpr std::array<Kind, 2> kParamsI32I64{Kind::I32, Kind::I64};
constexpr std::array<Kind, 3> kParamsStrI64I64{Kind::Str, Kind::I64, Kind::I64};
constexpr std::array<Kind, 3> kParamsI64StrStr{Kind::I64, Kind::Str, Kind::Str};
constexpr std::array<Kind, 3> kParamsPtrI64I64{Kind::Ptr, Kind::I64, Kind::I64};
constexpr std::array<Kind, 3> kParamsStrI32I32{Kind::Str, Kind::I32, Kind::I32};

constexpr std::array<GeneratedDescriptorSpec, 3> kGeneratedPrintDescriptors{{
    {"rt_print_str",
     RtSig::PrintS,
     &DirectHandler<&rt_print_str, void, rt_string>::invoke,
     kAlwaysLowering},
    {"rt_print_i64",
     RtSig::PrintI,
     &DirectHandler<&rt_print_i64, void, int64_t>::invoke,
     kAlwaysLowering},
    {"rt_print_f64",
     RtSig::PrintF,
     &DirectHandler<&rt_print_f64, void, double>::invoke,
     kAlwaysLowering},
}};

constexpr std::array<ManualDescriptorSpec, 2> kManualPrintDescriptors{{
    {"rt_println_i32",
     Kind::Void,
     std::span<const Kind>{kParamsI32},
     &DirectHandler<&rt_println_i32, void, int32_t>::invoke,
     kManualLowering},
    {"rt_println_str",
     Kind::Void,
     std::span<const Kind>{kParamsPtr},
     &DirectHandler<&rt_println_str, void, const char *>::invoke,
     kManualLowering},
}};

constexpr std::array<GeneratedDescriptorSpec, 4> kGeneratedStringCoreDescriptors{{
    {"rt_len",
     RtSig::Len,
     &DirectHandler<&rt_len, int64_t, rt_string>::invoke,
     kAlwaysLowering},
    {"rt_substr",
     RtSig::Substr,
     &DirectHandler<&rt_substr, rt_string, rt_string, int64_t, int64_t>::invoke,
     kAlwaysLowering},
    {"rt_trap", RtSig::Trap, &trapFromRuntimeString, kBoundsCheckedLowering},
    {"rt_concat",
     RtSig::Concat,
     &DirectHandler<&rt_concat, rt_string, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Concat)},
}};

constexpr std::array<ManualDescriptorSpec, 1> kManualStringIoDescriptors{{
    {"rt_csv_quote_alloc",
     Kind::Str,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_csv_quote_alloc, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::CsvQuote)},
}};

constexpr std::array<GeneratedDescriptorSpec, 8> kGeneratedInputDescriptors{{
    {"rt_input_line",
     RtSig::InputLine,
     &DirectHandler<&rt_input_line, rt_string>::invoke,
     featureLowering(RuntimeFeature::InputLine)},
    {"rt_split_fields",
     RtSig::SplitFields,
     &DirectHandler<&rt_split_fields, int64_t, rt_string, rt_string *, int64_t>::invoke,
     featureLowering(RuntimeFeature::SplitFields)},
    {"rt_to_int",
     RtSig::ToInt,
     &DirectHandler<&rt_to_int, int64_t, rt_string>::invoke,
     featureLowering(RuntimeFeature::ToInt)},
    {"rt_to_double",
     RtSig::ToDouble,
     &DirectHandler<&rt_to_double, double, rt_string>::invoke,
     featureLowering(RuntimeFeature::ToDouble)},
    {"rt_parse_int64",
     RtSig::ParseInt64,
     &DirectHandler<&rt_parse_int64, int32_t, const char *, int64_t *>::invoke,
     featureLowering(RuntimeFeature::ParseInt64)},
    {"rt_parse_double",
     RtSig::ParseDouble,
     &DirectHandler<&rt_parse_double, int32_t, const char *, double *>::invoke,
     featureLowering(RuntimeFeature::ParseDouble)},
    {"rt_int_to_str",
     RtSig::IntToStr,
     &DirectHandler<&rt_int_to_str, rt_string, int64_t>::invoke,
     featureLowering(RuntimeFeature::IntToStr)},
    {"rt_f64_to_str",
     RtSig::F64ToStr,
     &DirectHandler<&rt_f64_to_str, rt_string, double>::invoke,
     featureLowering(RuntimeFeature::F64ToStr)},
}};

constexpr std::array<ManualDescriptorSpec, 5> kManualTermDescriptors{{
    {"rt_term_cls",
     Kind::Void,
     std::span<const Kind>{kParamsNone},
     &DirectHandler<&rt_term_cls, void>::invoke,
     featureLowering(RuntimeFeature::TermCls)},
    {"rt_term_color_i32",
     Kind::Void,
     std::span<const Kind>{kParamsI32I32},
     &DirectHandler<&rt_term_color_i32, void, int32_t, int32_t>::invoke,
     featureLowering(RuntimeFeature::TermColor)},
    {"rt_term_locate_i32",
     Kind::Void,
     std::span<const Kind>{kParamsI32I32},
     &DirectHandler<&rt_term_locate_i32, void, int32_t, int32_t>::invoke,
     featureLowering(RuntimeFeature::TermLocate)},
    {"rt_getkey_str",
     Kind::Str,
     std::span<const Kind>{kParamsNone},
     &DirectHandler<&rt_getkey_str, rt_string>::invoke,
     featureLowering(RuntimeFeature::GetKey)},
    {"rt_inkey_str",
     Kind::Str,
     std::span<const Kind>{kParamsNone},
     &DirectHandler<&rt_inkey_str, rt_string>::invoke,
     featureLowering(RuntimeFeature::InKey)},
}};

template <std::size_t ParamCount>
constexpr bool hasManualTermDescriptor(std::string_view symbol,
                                       Kind ret,
                                       const std::array<Kind, ParamCount> &expectedParams)
{
    for (const auto &spec : kManualTermDescriptors)
    {
        if (spec.name != symbol)
            continue;
        if (spec.ret != ret || spec.params.size() != expectedParams.size())
            return false;
        for (std::size_t i = 0; i < ParamCount; ++i)
        {
            if (spec.params[i] != expectedParams[i])
                return false;
        }
        return true;
    }
    return false;
}

static_assert(hasManualTermDescriptor("rt_term_cls", Kind::Void, kParamsNone),
              "Terminal clear helper must be registered as void().");
static_assert(hasManualTermDescriptor("rt_term_color_i32", Kind::Void, kParamsI32I32),
              "Terminal color helper must be registered as void(i32, i32).");
static_assert(hasManualTermDescriptor("rt_term_locate_i32", Kind::Void, kParamsI32I32),
              "Terminal locate helper must be registered as void(i32, i32).");

constexpr std::array<GeneratedDescriptorSpec, 4> kGeneratedStringAllocDescriptors{{
    {"rt_str_i16_alloc",
     RtSig::StrFromI16,
     &DirectHandler<&rt_str_i16_alloc, rt_string, int16_t>::invoke,
     featureLowering(RuntimeFeature::StrFromI16)},
    {"rt_str_i32_alloc",
     RtSig::StrFromI32,
     &DirectHandler<&rt_str_i32_alloc, rt_string, int32_t>::invoke,
     featureLowering(RuntimeFeature::StrFromI32)},
    {"rt_str_f_alloc",
     RtSig::StrFromSingle,
     &invokeRtStrFAlloc,
     featureLowering(RuntimeFeature::StrFromSingle)},
    {"rt_str_d_alloc",
     RtSig::StrFromDouble,
     &DirectHandler<&rt_str_d_alloc, rt_string, double>::invoke,
     featureLowering(RuntimeFeature::StrFromDouble)},
}};

constexpr std::array<ManualDescriptorSpec, 7> kManualConversionDescriptors{{
    {"rt_cint_from_double",
     Kind::I64,
     std::span<const Kind>{kParamsF64Ptr},
     &invokeRtCintFromDouble,
     featureLowering(RuntimeFeature::CintFromDouble)},
    {"rt_clng_from_double",
     Kind::I64,
     std::span<const Kind>{kParamsF64Ptr},
     &invokeRtClngFromDouble,
     featureLowering(RuntimeFeature::ClngFromDouble)},
    {"rt_csng_from_double",
     Kind::F64,
     std::span<const Kind>{kParamsF64Ptr},
     &invokeRtCsngFromDouble,
     featureLowering(RuntimeFeature::CsngFromDouble)},
    {"rt_cdbl_from_any",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_cdbl_from_any, double, double>::invoke,
     featureLowering(RuntimeFeature::CdblFromAny)},
    {"rt_int_floor",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_int_floor, double, double>::invoke,
     featureLowering(RuntimeFeature::IntFloor)},
    {"rt_fix_trunc",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_fix_trunc, double, double>::invoke,
     featureLowering(RuntimeFeature::FixTrunc)},
    {"rt_round_even",
     Kind::F64,
     std::span<const Kind>{kParamsF64I32},
     &invokeRtRoundEven,
     featureLowering(RuntimeFeature::RoundEven)},
}};

constexpr std::array<ManualDescriptorSpec, 1> kManualMemoryDescriptors{{
    {"rt_alloc",
     Kind::Ptr,
     std::span<const Kind>{kParamsI64},
     &DirectHandler<&rt_alloc, void *, int64_t>::invoke,
     featureLowering(RuntimeFeature::Alloc)},
}};

constexpr std::array<ManualDescriptorSpec, 8> kManualArrayDescriptors{{
    {"rt_arr_i32_new",
     Kind::Ptr,
     std::span<const Kind>{kParamsI64},
     &invokeRtArrI32New,
     kManualLowering},
    {"rt_arr_i32_retain",
     Kind::Void,
     std::span<const Kind>{kParamsPtr},
     &DirectHandler<&rt_arr_i32_retain, void, int32_t *>::invoke,
     kManualLowering},
    {"rt_arr_i32_release",
     Kind::Void,
     std::span<const Kind>{kParamsPtr},
     &DirectHandler<&rt_arr_i32_release, void, int32_t *>::invoke,
     kManualLowering},
    {"rt_arr_i32_len",
     Kind::I64,
     std::span<const Kind>{kParamsPtr},
     &invokeRtArrI32Len,
     kManualLowering},
    {"rt_arr_i32_get",
     Kind::I64,
     std::span<const Kind>{kParamsPtrI64},
     &invokeRtArrI32Get,
     kManualLowering},
    {"rt_arr_i32_set",
     Kind::Void,
     std::span<const Kind>{kParamsPtrI64I64},
     &invokeRtArrI32Set,
     kManualLowering},
    {"rt_arr_i32_resize",
     Kind::Ptr,
     std::span<const Kind>{kParamsPtrI64},
     &invokeRtArrI32Resize,
     kManualLowering},
    {"rt_arr_oob_panic",
     Kind::Void,
     std::span<const Kind>{kParamsI64I64},
     &invokeRtArrOobPanic,
     kManualLowering},
}};

constexpr std::array<ManualDescriptorSpec, 17> kManualStringManipDescriptors{{
    {"rt_left",
     Kind::Str,
     std::span<const Kind>{kParamsStrI64},
     &DirectHandler<&rt_left, rt_string, rt_string, int64_t>::invoke,
     featureLowering(RuntimeFeature::Left)},
    {"rt_right",
     Kind::Str,
     std::span<const Kind>{kParamsStrI64},
     &DirectHandler<&rt_right, rt_string, rt_string, int64_t>::invoke,
     featureLowering(RuntimeFeature::Right)},
    {"rt_mid2",
     Kind::Str,
     std::span<const Kind>{kParamsStrI64},
     &DirectHandler<&rt_mid2, rt_string, rt_string, int64_t>::invoke,
     featureLowering(RuntimeFeature::Mid2)},
    {"rt_mid3",
     Kind::Str,
     std::span<const Kind>{kParamsStrI64I64},
     &DirectHandler<&rt_mid3, rt_string, rt_string, int64_t, int64_t>::invoke,
     featureLowering(RuntimeFeature::Mid3)},
    {"rt_instr2",
     Kind::I64,
     std::span<const Kind>{kParamsStrStr},
     &DirectHandler<&rt_instr2, int64_t, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Instr2)},
    {"rt_instr3",
     Kind::I64,
     std::span<const Kind>{kParamsI64StrStr},
     &DirectHandler<&rt_instr3, int64_t, int64_t, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Instr3)},
    {"rt_ltrim",
     Kind::Str,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_ltrim, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Ltrim)},
    {"rt_rtrim",
     Kind::Str,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_rtrim, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Rtrim)},
    {"rt_trim",
     Kind::Str,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_trim, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Trim)},
    {"rt_ucase",
     Kind::Str,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_ucase, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Ucase)},
    {"rt_lcase",
     Kind::Str,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_lcase, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::Lcase)},
    {"rt_chr",
     Kind::Str,
     std::span<const Kind>{kParamsI64},
     &DirectHandler<&rt_chr, rt_string, int64_t>::invoke,
     featureLowering(RuntimeFeature::Chr)},
    {"rt_asc",
     Kind::I64,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_asc, int64_t, rt_string>::invoke,
     featureLowering(RuntimeFeature::Asc)},
    {"rt_str_eq",
     Kind::I1,
     std::span<const Kind>{kParamsStrStr},
     &DirectHandler<&rt_str_eq, int64_t, rt_string, rt_string>::invoke,
     featureLowering(RuntimeFeature::StrEq)},
    {"rt_val",
     Kind::F64,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_val, double, rt_string>::invoke,
     featureLowering(RuntimeFeature::Val)},
    {"rt_val_to_double",
     Kind::F64,
     std::span<const Kind>{kParamsPtrPtr},
     &DirectHandler<&rt_val_to_double, double, const char *, bool *>::invoke,
     featureLowering(RuntimeFeature::Val)},
    {"rt_string_cstr",
     Kind::Ptr,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_string_cstr, const char *, rt_string>::invoke,
     featureLowering(RuntimeFeature::Val)},
}};

constexpr std::array<ManualDescriptorSpec, 9> kManualMathDescriptors{{
    {"rt_sqrt",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_sqrt, double, double>::invoke,
     featureLowering(RuntimeFeature::Sqrt, true)},
    {"rt_abs_i64",
     Kind::I64,
     std::span<const Kind>{kParamsI64},
     &DirectHandler<&rt_abs_i64, long long, long long>::invoke,
     featureLowering(RuntimeFeature::AbsI64, true)},
    {"rt_abs_f64",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_abs_f64, double, double>::invoke,
     featureLowering(RuntimeFeature::AbsF64, true)},
    {"rt_floor",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_floor, double, double>::invoke,
     featureLowering(RuntimeFeature::Floor, true)},
    {"rt_ceil",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_ceil, double, double>::invoke,
     featureLowering(RuntimeFeature::Ceil, true)},
    {"rt_sin",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_sin, double, double>::invoke,
     featureLowering(RuntimeFeature::Sin, true)},
    {"rt_cos",
     Kind::F64,
     std::span<const Kind>{kParamsF64},
     &DirectHandler<&rt_cos, double, double>::invoke,
     featureLowering(RuntimeFeature::Cos, true)},
    {"rt_pow_f64_chkdom",
     Kind::F64,
     std::span<const Kind>{kParamsF64F64},
     &invokeRtPowF64Chkdom,
     featureLowering(RuntimeFeature::Pow, true),
     std::span<const RuntimeHiddenParam>{kPowHidden},
     RuntimeTrapClass::PowDomainOverflow},
    {"rt_randomize_i64",
     Kind::Void,
     std::span<const Kind>{kParamsI64},
     &DirectHandler<&rt_randomize_i64, void, long long>::invoke,
     featureLowering(RuntimeFeature::RandomizeI64, true)},
}};

constexpr std::array<ManualDescriptorSpec, 1> kManualRandomDescriptors{{
    {"rt_rnd",
     Kind::F64,
     std::span<const Kind>{kParamsNone},
     &DirectHandler<&rt_rnd, double>::invoke,
     featureLowering(RuntimeFeature::Rnd, true)},
}};

constexpr std::array<ManualDescriptorSpec, 9> kManualFileDescriptors{{
    {"rt_open_err_vstr",
     Kind::I32,
     std::span<const Kind>{kParamsStrI32I32},
     &DirectHandler<&rt_open_err_vstr, int32_t, ViperString *, int32_t, int32_t>::invoke,
     kManualLowering},
    {"rt_close_err",
     Kind::I32,
     std::span<const Kind>{kParamsI32},
     &DirectHandler<&rt_close_err, int32_t, int32_t>::invoke,
     kManualLowering},
    {"rt_write_ch_err",
     Kind::I32,
     std::span<const Kind>{kParamsI32Str},
     &DirectHandler<&rt_write_ch_err, int32_t, int32_t, ViperString *>::invoke,
     kManualLowering},
    {"rt_println_ch_err",
     Kind::I32,
     std::span<const Kind>{kParamsI32Str},
     &DirectHandler<&rt_println_ch_err, int32_t, int32_t, ViperString *>::invoke,
     kManualLowering},
    {"rt_line_input_ch_err",
     Kind::I32,
     std::span<const Kind>{kParamsI32Ptr},
     &DirectHandler<&rt_line_input_ch_err, int32_t, int32_t, ViperString **>::invoke,
     kManualLowering},
    {"rt_eof_ch",
     Kind::I32,
     std::span<const Kind>{kParamsI32},
     &DirectHandler<&rt_eof_ch, int32_t, int32_t>::invoke,
     kManualLowering},
    {"rt_lof_ch",
     Kind::I64,
     std::span<const Kind>{kParamsI32},
     &DirectHandler<&rt_lof_ch, int64_t, int32_t>::invoke,
     kManualLowering},
    {"rt_loc_ch",
     Kind::I64,
     std::span<const Kind>{kParamsI32},
     &DirectHandler<&rt_loc_ch, int64_t, int32_t>::invoke,
     kManualLowering},
    {"rt_seek_ch_err",
     Kind::I32,
     std::span<const Kind>{kParamsI32I64},
     &DirectHandler<&rt_seek_ch_err, int32_t, int32_t, int64_t>::invoke,
     kManualLowering},
}};

constexpr std::array<ManualDescriptorSpec, 4> kManualLifetimeDescriptors{{
    {"rt_str_empty",
     Kind::Str,
     std::span<const Kind>{kParamsNone},
     &DirectHandler<&rt_str_empty, rt_string>::invoke,
     kAlwaysLowering},
    {"rt_const_cstr",
     Kind::Str,
     std::span<const Kind>{kParamsPtr},
     &DirectHandler<&rt_const_cstr, rt_string, const char *>::invoke,
     kManualLowering},
    {"rt_str_retain_maybe",
     Kind::Void,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_str_retain_maybe, void, rt_string>::invoke,
     kManualLowering},
    {"rt_str_release_maybe",
     Kind::Void,
     std::span<const Kind>{kParamsStr},
     &DirectHandler<&rt_str_release_maybe, void, rt_string>::invoke,
     kManualLowering},
}};

RuntimeDescriptor makeAbortDescriptor()
{
    return finalizeDescriptor("rt_abort",
                              makeSignature(Kind::Void, std::span<const Kind>{kParamsPtr}),
                              &DirectHandler<&rt_abort, void, const char *>::invoke,
                              kManualLowering,
                              {},
                              RuntimeTrapClass::None);
}

std::vector<RuntimeDescriptor> buildRegistry()
{
    std::vector<RuntimeDescriptor> entries;
    entries.reserve(1 + kGeneratedPrintDescriptors.size() + kManualPrintDescriptors.size() +
                    kGeneratedStringCoreDescriptors.size() + kManualStringIoDescriptors.size() +
                    kGeneratedInputDescriptors.size() + kManualTermDescriptors.size() +
                    kGeneratedStringAllocDescriptors.size() + kManualConversionDescriptors.size() +
                    kManualMemoryDescriptors.size() + kManualArrayDescriptors.size() +
                    kManualStringManipDescriptors.size() + kManualMathDescriptors.size() +
                    kManualRandomDescriptors.size() + kManualFileDescriptors.size() +
                    kManualLifetimeDescriptors.size());

    entries.push_back(makeAbortDescriptor());
    appendGeneratedDescriptors(entries, kGeneratedPrintDescriptors);
    appendManualDescriptors(entries, kManualPrintDescriptors);
    appendGeneratedDescriptors(entries, kGeneratedStringCoreDescriptors);
    appendManualDescriptors(entries, kManualStringIoDescriptors);
    appendGeneratedDescriptors(entries, kGeneratedInputDescriptors);
    appendManualDescriptors(entries, kManualTermDescriptors);
    appendGeneratedDescriptors(entries, kGeneratedStringAllocDescriptors);
    appendManualDescriptors(entries, kManualConversionDescriptors);
    appendManualDescriptors(entries, kManualMemoryDescriptors);
    appendManualDescriptors(entries, kManualArrayDescriptors);
    appendManualDescriptors(entries, kManualStringManipDescriptors);
    appendManualDescriptors(entries, kManualMathDescriptors);
    appendManualDescriptors(entries, kManualRandomDescriptors);
    appendManualDescriptors(entries, kManualFileDescriptors);
    appendManualDescriptors(entries, kManualLifetimeDescriptors);
    return entries;
}


} // namespace

const std::vector<RuntimeDescriptor> &runtimeRegistry()
{
    static const std::vector<RuntimeDescriptor> registry = buildRegistry();
    return registry;
}

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
    if (it == index.end())
        return nullptr;
    return it->second;
}

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
    if (it == index.end())
        return nullptr;
    return it->second;
}

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

std::optional<RtSig> findRuntimeSignatureId(std::string_view name)
{
    const auto &index = generatedSigIndex();
    auto it = index.find(name);
    if (it == index.end())
        return std::nullopt;
    return it->second;
}

const RuntimeSignature *findRuntimeSignature(RtSig sig)
{
    if (!isValid(sig))
        return nullptr;
    return &signatureFor(sig);
}

const RuntimeSignature *findRuntimeSignature(std::string_view name)
{
    if (auto id = findRuntimeSignatureId(name))
        return findRuntimeSignature(*id);
    if (const auto *desc = findRuntimeDescriptor(name))
        return &desc->signature;
    return nullptr;
}

} // namespace il::runtime
