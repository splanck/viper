// File: src/il/runtime/RuntimeSignatures.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Defines the shared runtime descriptor registry for IL consumers.
// Key invariants: Registry contents reflect the runtime C ABI signatures.
// Ownership/Lifetime: Uses static storage duration; entries are immutable after construction.
// Links: docs/il-guide.md#reference

#include "il/runtime/RuntimeSignatures.hpp"

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_math.h"
#include "rt_random.h"
#include <initializer_list>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace il::runtime
{
namespace
{
using Kind = il::core::Type::Kind;

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

/// @brief Populate the runtime descriptor registry with known helper declarations.
std::vector<RuntimeDescriptor> buildRegistry()
{
    std::vector<RuntimeDescriptor> entries;
    entries.reserve(40);
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
    add("rt_alloc",
        Kind::Ptr,
        {Kind::I64},
        &DirectHandler<&rt_alloc, void *, int64_t>::invoke,
        feature(RuntimeFeature::Alloc));
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
        manual());
    add("rt_str",
        Kind::Str,
        {Kind::F64},
        &DirectHandler<&rt_str, rt_string, double>::invoke,
        manual());
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
    add("rt_pow",
        Kind::F64,
        {Kind::F64, Kind::F64},
        &DirectHandler<&rt_pow, double, double, double>::invoke,
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

