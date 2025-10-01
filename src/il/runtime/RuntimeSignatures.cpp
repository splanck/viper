// File: src/il/runtime/RuntimeSignatures.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Defines the shared runtime descriptor registry for IL consumers.
// Key invariants: Registry contents reflect the runtime C ABI signatures.
// Ownership/Lifetime: Uses static storage duration; entries are immutable after construction.
// Links: docs/il-guide.md#reference

#include "il/runtime/RuntimeSignatures.hpp"

#include "rt.hpp"
#include "rt_debug.h"
#include "rt_fp.h"
#include "rt_internal.h"
#include "rt_math.h"
#include "rt_numeric.h"
#include "rt_random.h"
#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace il::runtime
{
namespace
{
using Kind = il::core::Type::Kind;

/// @brief Placeholder handler for rt_pow_f64_chkdom, actual logic lives in the VM bridge.
void trapPowHandler(void **, void *)
{
    rt_trap("rt_pow_f64_chkdom handler invoked from registry");
}

/// @brief Construct a runtime signature from the provided type kinds.
RuntimeSignature makeSignature(Kind ret, std::initializer_list<Kind> params)
{
    RuntimeSignature sig;
    sig.retType = il::core::Type(ret);
    sig.paramTypes.reserve(params.size());
    for (Kind p : params)
        sig.paramTypes.emplace_back(p);
    return sig;
}

/// @brief Generic adapter that invokes a runtime function with direct mapping.
template <auto Fn, typename Ret, typename... Args>
struct DirectHandler
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

RuntimeLowering makeLowering(RuntimeLoweringKind kind,
                             RuntimeFeature feature = RuntimeFeature::Count,
                             bool ordered = false)
{
    RuntimeLowering lowering;
    lowering.kind = kind;
    lowering.feature = feature;
    lowering.ordered = ordered;
    return lowering;
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

/// @brief Populate the runtime descriptor registry with known helper declarations.
std::vector<RuntimeDescriptor> buildRegistry()
{
    std::vector<RuntimeDescriptor> entries;
    entries.reserve(50);
    auto add = [&](std::string_view name,
                   Kind ret,
                   std::initializer_list<Kind> params,
                   RuntimeHandler handler,
                   RuntimeLowering lowering)
    {
        RuntimeDescriptor desc;
        desc.name = name;
        desc.signature = makeSignature(ret, params);
        desc.handler = handler;
        desc.lowering = lowering;
        entries.push_back(std::move(desc));
    };

    const auto always = [] { return makeLowering(RuntimeLoweringKind::Always); };
    const auto bounds = [] { return makeLowering(RuntimeLoweringKind::BoundsChecked); };
    const auto manual = [] { return makeLowering(RuntimeLoweringKind::Manual); };
    const auto feature = [](RuntimeFeature f, bool ordered = false)
    { return makeLowering(RuntimeLoweringKind::Feature, f, ordered); };

    add("rt_abort",
        Kind::Void,
        {Kind::Ptr},
        &DirectHandler<&rt_abort, void, const char *>::invoke,
        manual());
    add("rt_print_str",
        Kind::Void,
        {Kind::Str},
        &DirectHandler<&rt_print_str, void, rt_string>::invoke,
        always());
    add("rt_print_i64",
        Kind::Void,
        {Kind::I64},
        &DirectHandler<&rt_print_i64, void, int64_t>::invoke,
        always());
    add("rt_print_f64",
        Kind::Void,
        {Kind::F64},
        &DirectHandler<&rt_print_f64, void, double>::invoke,
        always());
    add("rt_println_i32",
        Kind::Void,
        {Kind::I32},
        &DirectHandler<&rt_println_i32, void, int32_t>::invoke,
        manual());
    add("rt_println_str",
        Kind::Void,
        {Kind::Ptr},
        &DirectHandler<&rt_println_str, void, const char *>::invoke,
        manual());
    add("rt_len",
        Kind::I64,
        {Kind::Str},
        &DirectHandler<&rt_len, int64_t, rt_string>::invoke,
        always());
    add("rt_substr",
        Kind::Str,
        {Kind::Str, Kind::I64, Kind::I64},
        &DirectHandler<&rt_substr, rt_string, rt_string, int64_t, int64_t>::invoke,
        always());
    add("rt_trap",
        Kind::Void,
        {Kind::Str},
        &trapFromRuntimeString,
        bounds());
    add("rt_concat",
        Kind::Str,
        {Kind::Str, Kind::Str},
        &DirectHandler<&rt_concat, rt_string, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Concat));
    add("rt_input_line",
        Kind::Str,
        {},
        &DirectHandler<&rt_input_line, rt_string>::invoke,
        feature(RuntimeFeature::InputLine));
    add("rt_to_int",
        Kind::I64,
        {Kind::Str},
        &DirectHandler<&rt_to_int, int64_t, rt_string>::invoke,
        feature(RuntimeFeature::ToInt));
    add("rt_int_to_str",
        Kind::Str,
        {Kind::I64},
        &DirectHandler<&rt_int_to_str, rt_string, int64_t>::invoke,
        feature(RuntimeFeature::IntToStr));
    add("rt_f64_to_str",
        Kind::Str,
        {Kind::F64},
        &DirectHandler<&rt_f64_to_str, rt_string, double>::invoke,
        feature(RuntimeFeature::F64ToStr));
    add("rt_str_i16_alloc",
        Kind::Str,
        {Kind::I16},
        &DirectHandler<&rt_str_i16_alloc, rt_string, int16_t>::invoke,
        feature(RuntimeFeature::StrFromI16));
    add("rt_str_i32_alloc",
        Kind::Str,
        {Kind::I32},
        &DirectHandler<&rt_str_i32_alloc, rt_string, int32_t>::invoke,
        feature(RuntimeFeature::StrFromI32));
    add("rt_str_f_alloc",
        Kind::Str,
        {Kind::F64},
        &invokeRtStrFAlloc,
        feature(RuntimeFeature::StrFromSingle));
    add("rt_str_d_alloc",
        Kind::Str,
        {Kind::F64},
        &DirectHandler<&rt_str_d_alloc, rt_string, double>::invoke,
        feature(RuntimeFeature::StrFromDouble));
    add("rt_cint_from_double",
        Kind::I64,
        {Kind::F64, Kind::Ptr},
        &invokeRtCintFromDouble,
        feature(RuntimeFeature::CintFromDouble));
    add("rt_clng_from_double",
        Kind::I64,
        {Kind::F64, Kind::Ptr},
        &invokeRtClngFromDouble,
        feature(RuntimeFeature::ClngFromDouble));
    add("rt_csng_from_double",
        Kind::F64,
        {Kind::F64, Kind::Ptr},
        &invokeRtCsngFromDouble,
        feature(RuntimeFeature::CsngFromDouble));
    add("rt_cdbl_from_any",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_cdbl_from_any, double, double>::invoke,
        feature(RuntimeFeature::CdblFromAny));
    add("rt_int_floor",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_int_floor, double, double>::invoke,
        feature(RuntimeFeature::IntFloor));
    add("rt_fix_trunc",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_fix_trunc, double, double>::invoke,
        feature(RuntimeFeature::FixTrunc));
    add("rt_round_even",
        Kind::F64,
        {Kind::F64, Kind::I32},
        &invokeRtRoundEven,
        feature(RuntimeFeature::RoundEven));
    add("rt_alloc",
        Kind::Ptr,
        {Kind::I64},
        &DirectHandler<&rt_alloc, void *, int64_t>::invoke,
        feature(RuntimeFeature::Alloc));
    add("rt_arr_i32_new",
        Kind::Ptr,
        {Kind::I64},
        &invokeRtArrI32New,
        manual());
    add("rt_arr_i32_retain",
        Kind::Void,
        {Kind::Ptr},
        &DirectHandler<&rt_arr_i32_retain, void, int32_t *>::invoke,
        manual());
    add("rt_arr_i32_release",
        Kind::Void,
        {Kind::Ptr},
        &DirectHandler<&rt_arr_i32_release, void, int32_t *>::invoke,
        manual());
    add("rt_arr_i32_len",
        Kind::I64,
        {Kind::Ptr},
        &invokeRtArrI32Len,
        manual());
    add("rt_arr_i32_get",
        Kind::I64,
        {Kind::Ptr, Kind::I64},
        &invokeRtArrI32Get,
        manual());
    add("rt_arr_i32_set",
        Kind::Void,
        {Kind::Ptr, Kind::I64, Kind::I64},
        &invokeRtArrI32Set,
        manual());
    add("rt_arr_i32_resize",
        Kind::Ptr,
        {Kind::Ptr, Kind::I64},
        &invokeRtArrI32Resize,
        manual());
    add("rt_arr_oob_panic",
        Kind::Void,
        {Kind::I64, Kind::I64},
        &invokeRtArrOobPanic,
        manual());
    add("rt_left",
        Kind::Str,
        {Kind::Str, Kind::I64},
        &DirectHandler<&rt_left, rt_string, rt_string, int64_t>::invoke,
        feature(RuntimeFeature::Left));
    add("rt_right",
        Kind::Str,
        {Kind::Str, Kind::I64},
        &DirectHandler<&rt_right, rt_string, rt_string, int64_t>::invoke,
        feature(RuntimeFeature::Right));
    add("rt_mid2",
        Kind::Str,
        {Kind::Str, Kind::I64},
        &DirectHandler<&rt_mid2, rt_string, rt_string, int64_t>::invoke,
        feature(RuntimeFeature::Mid2));
    add("rt_mid3",
        Kind::Str,
        {Kind::Str, Kind::I64, Kind::I64},
        &DirectHandler<&rt_mid3, rt_string, rt_string, int64_t, int64_t>::invoke,
        feature(RuntimeFeature::Mid3));
    add("rt_instr2",
        Kind::I64,
        {Kind::Str, Kind::Str},
        &DirectHandler<&rt_instr2, int64_t, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Instr2));
    add("rt_instr3",
        Kind::I64,
        {Kind::I64, Kind::Str, Kind::Str},
        &DirectHandler<&rt_instr3, int64_t, int64_t, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Instr3));
    add("rt_ltrim",
        Kind::Str,
        {Kind::Str},
        &DirectHandler<&rt_ltrim, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Ltrim));
    add("rt_rtrim",
        Kind::Str,
        {Kind::Str},
        &DirectHandler<&rt_rtrim, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Rtrim));
    add("rt_trim",
        Kind::Str,
        {Kind::Str},
        &DirectHandler<&rt_trim, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Trim));
    add("rt_ucase",
        Kind::Str,
        {Kind::Str},
        &DirectHandler<&rt_ucase, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Ucase));
    add("rt_lcase",
        Kind::Str,
        {Kind::Str},
        &DirectHandler<&rt_lcase, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::Lcase));
    add("rt_chr",
        Kind::Str,
        {Kind::I64},
        &DirectHandler<&rt_chr, rt_string, int64_t>::invoke,
        feature(RuntimeFeature::Chr));
    add("rt_asc",
        Kind::I64,
        {Kind::Str},
        &DirectHandler<&rt_asc, int64_t, rt_string>::invoke,
        feature(RuntimeFeature::Asc));
    add("rt_str_eq",
        Kind::I1,
        {Kind::Str, Kind::Str},
        &DirectHandler<&rt_str_eq, int64_t, rt_string, rt_string>::invoke,
        feature(RuntimeFeature::StrEq));
    add("rt_val",
        Kind::F64,
        {Kind::Str},
        &DirectHandler<&rt_val, double, rt_string>::invoke,
        feature(RuntimeFeature::Val));
    add("rt_val_to_double",
        Kind::F64,
        {Kind::Ptr, Kind::Ptr},
        &DirectHandler<&rt_val_to_double, double, const char *, bool *>::invoke,
        feature(RuntimeFeature::Val));
    add("rt_string_cstr",
        Kind::Ptr,
        {Kind::Str},
        &DirectHandler<&rt_string_cstr, const char *, rt_string>::invoke,
        feature(RuntimeFeature::Val));
    add("rt_sqrt",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_sqrt, double, double>::invoke,
        feature(RuntimeFeature::Sqrt, true));
    add("rt_abs_i64",
        Kind::I64,
        {Kind::I64},
        &DirectHandler<&rt_abs_i64, long long, long long>::invoke,
        feature(RuntimeFeature::AbsI64, true));
    add("rt_abs_f64",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_abs_f64, double, double>::invoke,
        feature(RuntimeFeature::AbsF64, true));
    add("rt_floor",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_floor, double, double>::invoke,
        feature(RuntimeFeature::Floor, true));
    add("rt_ceil",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_ceil, double, double>::invoke,
        feature(RuntimeFeature::Ceil, true));
    add("rt_sin",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_sin, double, double>::invoke,
        feature(RuntimeFeature::Sin, true));
    add("rt_cos",
        Kind::F64,
        {Kind::F64},
        &DirectHandler<&rt_cos, double, double>::invoke,
        feature(RuntimeFeature::Cos, true));
    add("rt_pow_f64_chkdom",
        Kind::F64,
        {Kind::F64, Kind::F64},
        &trapPowHandler,
        feature(RuntimeFeature::Pow, true));
    add("rt_randomize_i64",
        Kind::Void,
        {Kind::I64},
        &DirectHandler<&rt_randomize_i64, void, long long>::invoke,
        feature(RuntimeFeature::RandomizeI64, true));
    add("rt_rnd",
        Kind::F64,
        {},
        &DirectHandler<&rt_rnd, double>::invoke,
        feature(RuntimeFeature::Rnd, true));
    add("rt_open_err_vstr",
        Kind::I32,
        {Kind::Str, Kind::I32, Kind::I32},
        &DirectHandler<&rt_open_err_vstr, int32_t, ViperString *, int32_t, int32_t>::invoke,
        manual());
    add("rt_close_err",
        Kind::I32,
        {Kind::I32},
        &DirectHandler<&rt_close_err, int32_t, int32_t>::invoke,
        manual());
    add("rt_write_ch_err",
        Kind::I32,
        {Kind::I32, Kind::Str},
        &DirectHandler<&rt_write_ch_err, int32_t, int32_t, ViperString *>::invoke,
        manual());
    add("rt_println_ch_err",
        Kind::I32,
        {Kind::I32, Kind::Str},
        &DirectHandler<&rt_println_ch_err, int32_t, int32_t, ViperString *>::invoke,
        manual());
    add("rt_line_input_ch_err",
        Kind::I32,
        {Kind::I32, Kind::Ptr},
        &DirectHandler<&rt_line_input_ch_err, int32_t, int32_t, ViperString **>::invoke,
        manual());
    add("rt_const_cstr",
        Kind::Str,
        {Kind::Ptr},
        &DirectHandler<&rt_const_cstr, rt_string, const char *>::invoke,
        manual());

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

const RuntimeSignature *findRuntimeSignature(std::string_view name)
{
    if (const auto *desc = findRuntimeDescriptor(name))
        return &desc->signature;
    return nullptr;
}

} // namespace il::runtime

