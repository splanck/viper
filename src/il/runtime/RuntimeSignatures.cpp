// File: src/il/runtime/RuntimeSignatures.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Defines the shared runtime descriptor registry for IL consumers.
// Key invariants: Descriptor table is immutable and maps 1:1 with runtime helpers.
// Ownership/Lifetime: Static storage duration; descriptors live for the entire process.
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

void invokeRtArrI32New(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    int32_t *arr = rt_arr_i32_new(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

void invokeRtArrI32Len(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t *const *>(arrPtr) : nullptr;
    const size_t len = rt_arr_i32_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

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

void invokeRtArrOobPanic(void **args, void * /*result*/)
{
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    rt_arr_oob_panic(idx, len);
}

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

void invokeRtStrFAlloc(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const float value = xPtr ? static_cast<float>(*xPtr) : 0.0f;
    rt_string str = rt_str_f_alloc(value);
    if (result)
        *reinterpret_cast<rt_string *>(result) = str;
}

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

constexpr std::array<DescriptorRow, 88> kDescriptorRows{{
    DescriptorRow{"rt_abort", std::nullopt, "void(ptr)", &DirectHandler<&rt_abort, void, const char *>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_print_str", RtSig::PrintS, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintS)], &DirectHandler<&rt_print_str, void, rt_string>::invoke, kAlwaysLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_print_i64", RtSig::PrintI, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintI)], &DirectHandler<&rt_print_i64, void, int64_t>::invoke, kAlwaysLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_print_f64", RtSig::PrintF, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintF)], &DirectHandler<&rt_print_f64, void, double>::invoke, kAlwaysLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_println_i32", std::nullopt, "void(i32)", &DirectHandler<&rt_println_i32, void, int32_t>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_println_str", std::nullopt, "void(ptr)", &DirectHandler<&rt_println_str, void, const char *>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_len", RtSig::Len, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Len)], &DirectHandler<&rt_len, int64_t, rt_string>::invoke, kAlwaysLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_substr", RtSig::Substr, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Substr)], &DirectHandler<&rt_substr, rt_string, rt_string, int64_t, int64_t>::invoke, kAlwaysLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_trap", RtSig::Trap, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Trap)], &trapFromRuntimeString, kBoundsCheckedLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_concat", RtSig::Concat, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Concat)], &DirectHandler<&rt_concat, rt_string, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Concat), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_csv_quote_alloc", std::nullopt, "string(string)", &DirectHandler<&rt_csv_quote_alloc, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::CsvQuote), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_input_line", RtSig::InputLine, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::InputLine)], &DirectHandler<&rt_input_line, rt_string>::invoke, featureLowering(RuntimeFeature::InputLine), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_split_fields", RtSig::SplitFields, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::SplitFields)], &DirectHandler<&rt_split_fields, int64_t, rt_string, rt_string *, int64_t>::invoke, featureLowering(RuntimeFeature::SplitFields), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_to_int", RtSig::ToInt, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ToInt)], &DirectHandler<&rt_to_int, int64_t, rt_string>::invoke, featureLowering(RuntimeFeature::ToInt), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_to_double", RtSig::ToDouble, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ToDouble)], &DirectHandler<&rt_to_double, double, rt_string>::invoke, featureLowering(RuntimeFeature::ToDouble), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_parse_int64", RtSig::ParseInt64, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ParseInt64)], &DirectHandler<&rt_parse_int64, int32_t, const char *, int64_t *>::invoke, featureLowering(RuntimeFeature::ParseInt64), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_parse_double", RtSig::ParseDouble, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ParseDouble)], &DirectHandler<&rt_parse_double, int32_t, const char *, double *>::invoke, featureLowering(RuntimeFeature::ParseDouble), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_int_to_str", RtSig::IntToStr, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::IntToStr)], &DirectHandler<&rt_int_to_str, rt_string, int64_t>::invoke, featureLowering(RuntimeFeature::IntToStr), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_f64_to_str", RtSig::F64ToStr, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::F64ToStr)], &DirectHandler<&rt_f64_to_str, rt_string, double>::invoke, featureLowering(RuntimeFeature::F64ToStr), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_term_cls", std::nullopt, "void()", &DirectHandler<&rt_term_cls, void>::invoke, featureLowering(RuntimeFeature::TermCls), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_term_color_i32", std::nullopt, "void(i32,i32)", &DirectHandler<&rt_term_color_i32, void, int32_t, int32_t>::invoke, featureLowering(RuntimeFeature::TermColor), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_term_locate_i32", std::nullopt, "void(i32,i32)", &DirectHandler<&rt_term_locate_i32, void, int32_t, int32_t>::invoke, featureLowering(RuntimeFeature::TermLocate), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_getkey_str", std::nullopt, "string()", &DirectHandler<&rt_getkey_str, rt_string>::invoke, featureLowering(RuntimeFeature::GetKey), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_inkey_str", std::nullopt, "string()", &DirectHandler<&rt_inkey_str, rt_string>::invoke, featureLowering(RuntimeFeature::InKey), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_i16_alloc", RtSig::StrFromI16, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromI16)], &DirectHandler<&rt_str_i16_alloc, rt_string, int16_t>::invoke, featureLowering(RuntimeFeature::StrFromI16), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_i32_alloc", RtSig::StrFromI32, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromI32)], &DirectHandler<&rt_str_i32_alloc, rt_string, int32_t>::invoke, featureLowering(RuntimeFeature::StrFromI32), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_f_alloc", RtSig::StrFromSingle, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromSingle)], &invokeRtStrFAlloc, featureLowering(RuntimeFeature::StrFromSingle), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_d_alloc", RtSig::StrFromDouble, data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromDouble)], &DirectHandler<&rt_str_d_alloc, rt_string, double>::invoke, featureLowering(RuntimeFeature::StrFromDouble), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_cint_from_double", std::nullopt, "i64(f64,ptr)", &invokeRtCintFromDouble, featureLowering(RuntimeFeature::CintFromDouble), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_clng_from_double", std::nullopt, "i64(f64,ptr)", &invokeRtClngFromDouble, featureLowering(RuntimeFeature::ClngFromDouble), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_csng_from_double", std::nullopt, "f64(f64,ptr)", &invokeRtCsngFromDouble, featureLowering(RuntimeFeature::CsngFromDouble), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_cdbl_from_any", std::nullopt, "f64(f64)", &DirectHandler<&rt_cdbl_from_any, double, double>::invoke, featureLowering(RuntimeFeature::CdblFromAny), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_int_floor", std::nullopt, "f64(f64)", &DirectHandler<&rt_int_floor, double, double>::invoke, featureLowering(RuntimeFeature::IntFloor), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_fix_trunc", std::nullopt, "f64(f64)", &DirectHandler<&rt_fix_trunc, double, double>::invoke, featureLowering(RuntimeFeature::FixTrunc), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_round_even", std::nullopt, "f64(f64,i32)", &invokeRtRoundEven, featureLowering(RuntimeFeature::RoundEven), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_alloc", std::nullopt, "ptr(i64)", &DirectHandler<&rt_alloc, void *, int64_t>::invoke, featureLowering(RuntimeFeature::Alloc), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_new", std::nullopt, "ptr(i64)", &invokeRtArrI32New, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_retain", std::nullopt, "void(ptr)", &DirectHandler<&rt_arr_i32_retain, void, int32_t *>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_release", std::nullopt, "void(ptr)", &DirectHandler<&rt_arr_i32_release, void, int32_t *>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_len", std::nullopt, "i64(ptr)", &invokeRtArrI32Len, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_get", std::nullopt, "i64(ptr,i64)", &invokeRtArrI32Get, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_set", std::nullopt, "void(ptr,i64,i64)", &invokeRtArrI32Set, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_i32_resize", std::nullopt, "ptr(ptr,i64)", &invokeRtArrI32Resize, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_arr_oob_panic", std::nullopt, "void(i64,i64)", &invokeRtArrOobPanic, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_left", std::nullopt, "string(string,i64)", &DirectHandler<&rt_left, rt_string, rt_string, int64_t>::invoke, featureLowering(RuntimeFeature::Left), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_right", std::nullopt, "string(string,i64)", &DirectHandler<&rt_right, rt_string, rt_string, int64_t>::invoke, featureLowering(RuntimeFeature::Right), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_mid2", std::nullopt, "string(string,i64)", &DirectHandler<&rt_mid2, rt_string, rt_string, int64_t>::invoke, featureLowering(RuntimeFeature::Mid2), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_mid3", std::nullopt, "string(string,i64,i64)", &DirectHandler<&rt_mid3, rt_string, rt_string, int64_t, int64_t>::invoke, featureLowering(RuntimeFeature::Mid3), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_instr2", std::nullopt, "i64(string,string)", &DirectHandler<&rt_instr2, int64_t, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Instr2), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_instr3", std::nullopt, "i64(i64,string,string)", &DirectHandler<&rt_instr3, int64_t, int64_t, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Instr3), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_ltrim", std::nullopt, "string(string)", &DirectHandler<&rt_ltrim, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Ltrim), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_rtrim", std::nullopt, "string(string)", &DirectHandler<&rt_rtrim, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Rtrim), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_trim", std::nullopt, "string(string)", &DirectHandler<&rt_trim, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Trim), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_ucase", std::nullopt, "string(string)", &DirectHandler<&rt_ucase, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Ucase), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_lcase", std::nullopt, "string(string)", &DirectHandler<&rt_lcase, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::Lcase), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_chr", std::nullopt, "string(i64)", &DirectHandler<&rt_chr, rt_string, int64_t>::invoke, featureLowering(RuntimeFeature::Chr), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_asc", std::nullopt, "i64(string)", &DirectHandler<&rt_asc, int64_t, rt_string>::invoke, featureLowering(RuntimeFeature::Asc), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_eq", std::nullopt, "i1(string,string)", &DirectHandler<&rt_str_eq, int64_t, rt_string, rt_string>::invoke, featureLowering(RuntimeFeature::StrEq), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_val", std::nullopt, "f64(string)", &DirectHandler<&rt_val, double, rt_string>::invoke, featureLowering(RuntimeFeature::Val), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_val_to_double", std::nullopt, "f64(ptr,ptr)", &DirectHandler<&rt_val_to_double, double, const char *, bool *>::invoke, featureLowering(RuntimeFeature::Val), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_string_cstr", std::nullopt, "ptr(string)", &DirectHandler<&rt_string_cstr, const char *, rt_string>::invoke, featureLowering(RuntimeFeature::Val), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_sqrt", std::nullopt, "f64(f64)", &DirectHandler<&rt_sqrt, double, double>::invoke, featureLowering(RuntimeFeature::Sqrt, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_abs_i64", std::nullopt, "i64(i64)", &DirectHandler<&rt_abs_i64, long long, long long>::invoke, featureLowering(RuntimeFeature::AbsI64, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_abs_f64", std::nullopt, "f64(f64)", &DirectHandler<&rt_abs_f64, double, double>::invoke, featureLowering(RuntimeFeature::AbsF64, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_floor", std::nullopt, "f64(f64)", &DirectHandler<&rt_floor, double, double>::invoke, featureLowering(RuntimeFeature::Floor, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_ceil", std::nullopt, "f64(f64)", &DirectHandler<&rt_ceil, double, double>::invoke, featureLowering(RuntimeFeature::Ceil, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_sin", std::nullopt, "f64(f64)", &DirectHandler<&rt_sin, double, double>::invoke, featureLowering(RuntimeFeature::Sin, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_cos", std::nullopt, "f64(f64)", &DirectHandler<&rt_cos, double, double>::invoke, featureLowering(RuntimeFeature::Cos, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_pow_f64_chkdom", std::nullopt, "f64(f64,f64)", &invokeRtPowF64Chkdom, featureLowering(RuntimeFeature::Pow, true), kPowHidden.data(), kPowHidden.size(), RuntimeTrapClass::PowDomainOverflow},
    DescriptorRow{"rt_randomize_i64", std::nullopt, "void(i64)", &DirectHandler<&rt_randomize_i64, void, long long>::invoke, featureLowering(RuntimeFeature::RandomizeI64, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_rnd", std::nullopt, "f64()", &DirectHandler<&rt_rnd, double>::invoke, featureLowering(RuntimeFeature::Rnd, true), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_open_err_vstr", std::nullopt, "i32(string,i32,i32)", &DirectHandler<&rt_open_err_vstr, int32_t, ViperString *, int32_t, int32_t>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_close_err", std::nullopt, "i32(i32)", &DirectHandler<&rt_close_err, int32_t, int32_t>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_write_ch_err", std::nullopt, "i32(i32,string)", &DirectHandler<&rt_write_ch_err, int32_t, int32_t, ViperString *>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_println_ch_err", std::nullopt, "i32(i32,string)", &DirectHandler<&rt_println_ch_err, int32_t, int32_t, ViperString *>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_line_input_ch_err", std::nullopt, "i32(i32,ptr)", &DirectHandler<&rt_line_input_ch_err, int32_t, int32_t, ViperString **>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_eof_ch", std::nullopt, "i32(i32)", &DirectHandler<&rt_eof_ch, int32_t, int32_t>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_lof_ch", std::nullopt, "i64(i32)", &DirectHandler<&rt_lof_ch, int64_t, int32_t>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_loc_ch", std::nullopt, "i64(i32)", &DirectHandler<&rt_loc_ch, int64_t, int32_t>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_seek_ch_err", std::nullopt, "i32(i32,i64)", &DirectHandler<&rt_seek_ch_err, int32_t, int32_t, int64_t>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_empty", std::nullopt, "string()", &DirectHandler<&rt_str_empty, rt_string>::invoke, kAlwaysLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_const_cstr", std::nullopt, "string(ptr)", &DirectHandler<&rt_const_cstr, rt_string, const char *>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_retain_maybe", std::nullopt, "void(string)", &DirectHandler<&rt_str_retain_maybe, void, rt_string>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_str_release_maybe", std::nullopt, "void(string)", &DirectHandler<&rt_str_release_maybe, void, rt_string>::invoke, kManualLowering, nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_new_i64", std::nullopt, "ptr(i64,i64)", &DirectHandler<&rt_obj_new_i64, void *, int64_t, int64_t>::invoke, featureLowering(RuntimeFeature::ObjNew), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_retain_maybe", std::nullopt, "void(ptr)", &DirectHandler<&rt_obj_retain_maybe, void, void *>::invoke, featureLowering(RuntimeFeature::ObjRetainMaybe), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_release_check0", std::nullopt, "i1(ptr)", &DirectHandler<&rt_obj_release_check0, int32_t, void *>::invoke, featureLowering(RuntimeFeature::ObjReleaseChk0), nullptr, 0, RuntimeTrapClass::None},
    DescriptorRow{"rt_obj_free", std::nullopt, "void(ptr)", &DirectHandler<&rt_obj_free, void, void *>::invoke, featureLowering(RuntimeFeature::ObjFree), nullptr, 0, RuntimeTrapClass::None},
}};

RuntimeSignature buildSignature(const DescriptorRow &row)
{
    RuntimeSignature signature = row.signatureId ? signatureFor(*row.signatureId) : parseSignatureSpec(row.spec);
    signature.hiddenParams.assign(row.hidden, row.hidden + row.hiddenCount);
    signature.trapClass = row.trapClass;
    return signature;
}

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

} // namespace

const std::vector<RuntimeDescriptor> &runtimeRegistry()
{
    static const std::vector<RuntimeDescriptor> registry = []
    {
        std::vector<RuntimeDescriptor> entries;
        entries.reserve(kDescriptorRows.size());
        for (const auto &row : kDescriptorRows)
            entries.push_back(buildDescriptor(row));
        return entries;
    }();
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
    return it == index.end() ? nullptr : it->second;
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
    return it == index.end() ? nullptr : it->second;
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
