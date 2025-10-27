//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_FileIO.cpp
// Purpose: Registers runtime helper descriptors for file and console I/O.
// Key invariants: Descriptors mirror the C runtime ABI and are appended in a
//                 deterministic order.
// Ownership/Lifetime: Registered descriptors and debug signatures are stored
//                     with static duration.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include "rt.hpp"
#include "rt_debug.h"
#include "rt_internal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

    constexpr RuntimeLowering makeLowering(RuntimeLoweringKind kind,
                                           RuntimeFeature feature = RuntimeFeature::Count,
                                           bool ordered = false)
    {
        return RuntimeLowering{kind, feature, ordered};
    }

    constexpr RuntimeLowering kAlwaysLowering = makeLowering(RuntimeLoweringKind::Always);
    constexpr RuntimeLowering kManualLowering = makeLowering(RuntimeLoweringKind::Manual);

    constexpr RuntimeLowering featureLowering(RuntimeFeature feature, bool ordered = false)
    {
        return makeLowering(RuntimeLoweringKind::Feature, feature, ordered);
    }

    int32_t clampRuntimeOffset(int64_t value)
    {
        if (value > std::numeric_limits<int32_t>::max())
            return std::numeric_limits<int32_t>::max();
        if (value < std::numeric_limits<int32_t>::min())
            return std::numeric_limits<int32_t>::min();
        return static_cast<int32_t>(value);
    }

    int32_t rt_lof_ch_i32(int32_t channel)
    {
        return clampRuntimeOffset(rt_lof_ch(channel));
    }

    int32_t rt_loc_ch_i32(int32_t channel)
    {
        return clampRuntimeOffset(rt_loc_ch(channel));
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

    constexpr std::array<DescriptorDefinition, 20> kDefinitions{{
        DescriptorDefinition{{"rt_abort",
                               std::nullopt,
                               "void(ptr)",
                               &DirectHandler<&rt_abort, void, const char *>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_abort", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_print_str",
                               RtSig::PrintS,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintS)],
                               &DirectHandler<&rt_print_str, void, rt_string>::invoke,
                               kAlwaysLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_print_str", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_print_i64",
                               RtSig::PrintI,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintI)],
                               &DirectHandler<&rt_print_i64, void, int64_t>::invoke,
                               kAlwaysLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_print_i64", {SigParam{SigParam::I64}}, {}}},
        DescriptorDefinition{{"rt_print_f64",
                               RtSig::PrintF,
                               data::kRtSigSpecs[static_cast<std::size_t>(RtSig::PrintF)],
                               &DirectHandler<&rt_print_f64, void, double>::invoke,
                               kAlwaysLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_print_f64", {SigParam{SigParam::F64}}, {}}},
        DescriptorDefinition{{"rt_println_i32",
                               std::nullopt,
                               "void(i32)",
                               &DirectHandler<&rt_println_i32, void, int32_t>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_println_i32", {SigParam{SigParam::I32}}, {}}},
        DescriptorDefinition{{"rt_println_str",
                               std::nullopt,
                               "void(ptr)",
                               &DirectHandler<&rt_println_str, void, const char *>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_println_str", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_term_cls",
                               std::nullopt,
                               "void()",
                               &DirectHandler<&rt_term_cls, void>::invoke,
                               featureLowering(RuntimeFeature::TermCls),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_term_cls", {}, {}}},
        DescriptorDefinition{{"rt_term_color_i32",
                               std::nullopt,
                               "void(i32,i32)",
                               &DirectHandler<&rt_term_color_i32, void, int32_t, int32_t>::invoke,
                               featureLowering(RuntimeFeature::TermColor),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_term_color_i32",
                                        {SigParam{SigParam::I32}, SigParam{SigParam::I32}},
                                        {}}},
        DescriptorDefinition{{"rt_term_locate_i32",
                               std::nullopt,
                               "void(i32,i32)",
                               &DirectHandler<&rt_term_locate_i32, void, int32_t, int32_t>::invoke,
                               featureLowering(RuntimeFeature::TermLocate),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_term_locate_i32",
                                        {SigParam{SigParam::I32}, SigParam{SigParam::I32}},
                                        {}}},
        DescriptorDefinition{{"rt_getkey_str",
                               std::nullopt,
                               "string()",
                               &DirectHandler<&rt_getkey_str, rt_string>::invoke,
                               featureLowering(RuntimeFeature::GetKey),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_getkey_str", {}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_inkey_str",
                               std::nullopt,
                               "string()",
                               &DirectHandler<&rt_inkey_str, rt_string>::invoke,
                               featureLowering(RuntimeFeature::InKey),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_inkey_str", {}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_open_err_vstr",
                               std::nullopt,
                               "i32(string,i32,i32)",
                               &DirectHandler<&rt_open_err_vstr, int32_t, ViperString *, int32_t, int32_t>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_open_err_vstr",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I32}, SigParam{SigParam::I32}},
                                        {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_close_err",
                               std::nullopt,
                               "i32(i32)",
                               &DirectHandler<&rt_close_err, int32_t, int32_t>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_close_err", {SigParam{SigParam::I32}}, {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_write_ch_err",
                               std::nullopt,
                               "i32(i32,string)",
                               &DirectHandler<&rt_write_ch_err, int32_t, int32_t, ViperString *>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_write_ch_err",
                                        {SigParam{SigParam::I32}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_println_ch_err",
                               std::nullopt,
                               "i32(i32,string)",
                               &DirectHandler<&rt_println_ch_err, int32_t, int32_t, ViperString *>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_println_ch_err",
                                        {SigParam{SigParam::I32}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_line_input_ch_err",
                               std::nullopt,
                               "i32(i32,ptr)",
                               &DirectHandler<&rt_line_input_ch_err, int32_t, int32_t, ViperString **>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_line_input_ch_err",
                                        {SigParam{SigParam::I32}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_eof_ch",
                               std::nullopt,
                               "i32(i32)",
                               &DirectHandler<&rt_eof_ch, int32_t, int32_t>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_eof_ch", {SigParam{SigParam::I32}}, {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_lof_ch",
                               std::nullopt,
                               "i32(i32)",
                               &DirectHandler<&rt_lof_ch_i32, int32_t, int32_t>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_lof_ch", {SigParam{SigParam::I32}}, {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_loc_ch",
                               std::nullopt,
                               "i32(i32)",
                               &DirectHandler<&rt_loc_ch_i32, int32_t, int32_t>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_loc_ch", {SigParam{SigParam::I32}}, {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_seek_ch_err",
                               std::nullopt,
                               "i32(i32,i64)",
                               &DirectHandler<&rt_seek_ch_err, int32_t, int32_t, int64_t>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_seek_ch_err",
                                        {SigParam{SigParam::I32}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::I32}}}},
    }};
} // namespace

void register_fileio(std::vector<RuntimeDescriptor> &out)
{
    out.reserve(out.size() + kDefinitions.size());
    for (const auto &def : kDefinitions)
    {
        out.push_back(buildDescriptor(def.row));
        register_signature(def.expected);
    }
}

} // namespace il::runtime::signatures

