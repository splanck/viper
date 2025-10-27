//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_Strings.cpp
// Purpose: Registers runtime helper descriptors for string manipulation and
//          conversion utilities.
// Key invariants: Descriptors match the runtime ABI and manual signatures track
//                 expected calling shapes.
// Ownership/Lifetime: Registered descriptors and signatures are stored with
//                     static duration.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include "rt.hpp"
#include "rt_fp.h"
#include "rt_internal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace il::runtime::signatures
{
namespace
{
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

    void trapFromRuntimeString(void **args, void * /*result*/)
    {
        rt_string str = args ? *reinterpret_cast<rt_string *>(args[0]) : nullptr;
        const char *msg = (str && str->data) ? str->data : "trap";
        rt_trap(msg);
    }

    void invokeRtStrFAlloc(void **args, void *result)
    {
        const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
        const float value = xPtr ? static_cast<float>(*xPtr) : 0.0f;
        rt_string str = rt_str_f_alloc(value);
        if (result)
            *reinterpret_cast<rt_string *>(result) = str;
    }

    constexpr RuntimeLowering makeLowering(RuntimeLoweringKind kind,
                                           RuntimeFeature feature = RuntimeFeature::Count,
                                           bool ordered = false)
    {
        return RuntimeLowering{kind, feature, ordered};
    }

    constexpr RuntimeLowering kAlwaysLowering = makeLowering(RuntimeLoweringKind::Always);
    constexpr RuntimeLowering kBoundsCheckedLowering =
        makeLowering(RuntimeLoweringKind::BoundsChecked);
    constexpr RuntimeLowering kManualLowering = makeLowering(RuntimeLoweringKind::Manual);

    constexpr RuntimeLowering featureLowering(RuntimeFeature feature, bool ordered = false)
    {
        return makeLowering(RuntimeLoweringKind::Feature, feature, ordered);
    }

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

    RuntimeSignature buildSignature(const DescriptorRow &row)
    {
        RuntimeSignature signature = row.signatureId
            ? parseSignatureSpec(data::kRtSigSpecs[static_cast<std::size_t>(*row.signatureId)])
            : parseSignatureSpec(row.spec);
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

    struct DescriptorDefinition
    {
        DescriptorRow row;
        Signature expected;
    };

    constexpr std::array<DescriptorDefinition, 36> kDefinitions{{
        DescriptorDefinition{{"rt_len",
                               RtSig::Len,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Len)],
                               &DirectHandler<&rt_len, int64_t, rt_string>::invoke,
                               kAlwaysLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_len", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_substr",
                               RtSig::Substr,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Substr)],
                               &DirectHandler<&rt_substr, rt_string, rt_string, int64_t, int64_t>::invoke,
                               kAlwaysLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_substr",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_trap",
                               RtSig::Trap,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Trap)],
                               &trapFromRuntimeString,
                               kBoundsCheckedLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_trap", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_concat",
                               RtSig::Concat,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::Concat)],
                               &ConsumingStringHandler<&rt_concat, rt_string, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Concat),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_concat",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_csv_quote_alloc",
                               std::nullopt,
                               "string(string)",
                               &DirectHandler<&rt_csv_quote_alloc, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::CsvQuote),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_csv_quote_alloc", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_input_line",
                               RtSig::InputLine,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::InputLine)],
                               &DirectHandler<&rt_input_line, rt_string>::invoke,
                               featureLowering(RuntimeFeature::InputLine),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_input_line", {}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_split_fields",
                               RtSig::SplitFields,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::SplitFields)],
                               &DirectHandler<&rt_split_fields, int64_t, rt_string, rt_string *, int64_t>::invoke,
                               featureLowering(RuntimeFeature::SplitFields),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_split_fields",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_to_int",
                               RtSig::ToInt,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ToInt)],
                               &DirectHandler<&rt_to_int, int64_t, rt_string>::invoke,
                               featureLowering(RuntimeFeature::ToInt),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_to_int", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_to_double",
                               RtSig::ToDouble,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ToDouble)],
                               &DirectHandler<&rt_to_double, double, rt_string>::invoke,
                               featureLowering(RuntimeFeature::ToDouble),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_to_double", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_parse_int64",
                               RtSig::ParseInt64,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ParseInt64)],
                               &DirectHandler<&rt_parse_int64, int32_t, const char *, int64_t *>::invoke,
                               featureLowering(RuntimeFeature::ParseInt64),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_parse_int64",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_parse_double",
                               RtSig::ParseDouble,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::ParseDouble)],
                               &DirectHandler<&rt_parse_double, int32_t, const char *, double *>::invoke,
                               featureLowering(RuntimeFeature::ParseDouble),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_parse_double",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_int_to_str",
                               RtSig::IntToStr,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::IntToStr)],
                               &DirectHandler<&rt_int_to_str, rt_string, int64_t>::invoke,
                               featureLowering(RuntimeFeature::IntToStr),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_int_to_str", {SigParam{SigParam::I64}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_f64_to_str",
                               RtSig::F64ToStr,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::F64ToStr)],
                               &DirectHandler<&rt_f64_to_str, rt_string, double>::invoke,
                               featureLowering(RuntimeFeature::F64ToStr),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_f64_to_str", {SigParam{SigParam::F64}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_str_i16_alloc",
                               RtSig::StrFromI16,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromI16)],
                               &DirectHandler<&rt_str_i16_alloc, rt_string, int16_t>::invoke,
                               featureLowering(RuntimeFeature::StrFromI16),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_i16_alloc", {SigParam{SigParam::I32}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_str_i32_alloc",
                               RtSig::StrFromI32,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromI32)],
                               &DirectHandler<&rt_str_i32_alloc, rt_string, int32_t>::invoke,
                               featureLowering(RuntimeFeature::StrFromI32),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_i32_alloc", {SigParam{SigParam::I32}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_str_f_alloc",
                               RtSig::StrFromSingle,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromSingle)],
                               &invokeRtStrFAlloc,
                               featureLowering(RuntimeFeature::StrFromSingle),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_f_alloc", {SigParam{SigParam::F64}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_str_d_alloc",
                               RtSig::StrFromDouble,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::StrFromDouble)],
                               &DirectHandler<&rt_str_d_alloc, rt_string, double>::invoke,
                               featureLowering(RuntimeFeature::StrFromDouble),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_d_alloc", {SigParam{SigParam::F64}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_left",
                               std::nullopt,
                               "string(string,i64)",
                               &DirectHandler<&rt_left, rt_string, rt_string, int64_t>::invoke,
                               featureLowering(RuntimeFeature::Left),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_left",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_right",
                               std::nullopt,
                               "string(string,i64)",
                               &DirectHandler<&rt_right, rt_string, rt_string, int64_t>::invoke,
                               featureLowering(RuntimeFeature::Right),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_right",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_mid2",
                               std::nullopt,
                               "string(string,i64)",
                               &DirectHandler<&rt_mid2, rt_string, rt_string, int64_t>::invoke,
                               featureLowering(RuntimeFeature::Mid2),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_mid2",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_mid3",
                               std::nullopt,
                               "string(string,i64,i64)",
                               &DirectHandler<&rt_mid3, rt_string, rt_string, int64_t, int64_t>::invoke,
                               featureLowering(RuntimeFeature::Mid3),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_mid3",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_instr2",
                               std::nullopt,
                               "i64(string,string)",
                               &DirectHandler<&rt_instr2, int64_t, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Instr2),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_instr2",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_instr3",
                               std::nullopt,
                               "i64(i64,string,string)",
                               &DirectHandler<&rt_instr3, int64_t, int64_t, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Instr3),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_instr3",
                                        {SigParam{SigParam::I64}, SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_ltrim",
                               std::nullopt,
                               "string(string)",
                               &DirectHandler<&rt_ltrim, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Ltrim),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_ltrim", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_rtrim",
                               std::nullopt,
                               "string(string)",
                               &DirectHandler<&rt_rtrim, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Rtrim),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_rtrim", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_trim",
                               std::nullopt,
                               "string(string)",
                               &DirectHandler<&rt_trim, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Trim),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_trim", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_ucase",
                               std::nullopt,
                               "string(string)",
                               &DirectHandler<&rt_ucase, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Ucase),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_ucase", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_lcase",
                               std::nullopt,
                               "string(string)",
                               &DirectHandler<&rt_lcase, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Lcase),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_lcase", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_chr",
                               std::nullopt,
                               "string(i64)",
                               &DirectHandler<&rt_chr, rt_string, int64_t>::invoke,
                               featureLowering(RuntimeFeature::Chr),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_chr", {SigParam{SigParam::I64}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_asc",
                               std::nullopt,
                               "i64(string)",
                               &DirectHandler<&rt_asc, int64_t, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Asc),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_asc", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_str_eq",
                               std::nullopt,
                               "i1(string,string)",
                               &DirectHandler<&rt_str_eq, int64_t, rt_string, rt_string>::invoke,
                               featureLowering(RuntimeFeature::StrEq),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_eq",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_val",
                               std::nullopt,
                               "f64(string)",
                               &DirectHandler<&rt_val, double, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Val),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_val", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_val_to_double",
                               std::nullopt,
                               "f64(ptr,ptr)",
                               &DirectHandler<&rt_val_to_double, double, const char *, bool *>::invoke,
                               featureLowering(RuntimeFeature::Val),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_val_to_double",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_string_cstr",
                               std::nullopt,
                               "ptr(string)",
                               &DirectHandler<&rt_string_cstr, const char *, rt_string>::invoke,
                               featureLowering(RuntimeFeature::Val),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_string_cstr", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_str_empty",
                               std::nullopt,
                               "string()",
                               &DirectHandler<&rt_str_empty, rt_string>::invoke,
                               kAlwaysLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_empty", {}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_const_cstr",
                               std::nullopt,
                               "string(ptr)",
                               &DirectHandler<&rt_const_cstr, rt_string, const char *>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_const_cstr", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_str_retain_maybe",
                               std::nullopt,
                               "void(string)",
                               &DirectHandler<&rt_str_retain_maybe, void, rt_string>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_retain_maybe", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_str_release_maybe",
                               std::nullopt,
                               "void(string)",
                               &DirectHandler<&rt_str_release_maybe, void, rt_string>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_str_release_maybe", {SigParam{SigParam::Ptr}}, {}}},
    }};
} // namespace

void register_strings(std::vector<RuntimeDescriptor> &out)
{
    out.reserve(out.size() + kDefinitions.size());
    for (const auto &def : kDefinitions)
    {
        out.push_back(buildDescriptor(def.row));
        register_signature(def.expected);
    }
}

} // namespace il::runtime::signatures

