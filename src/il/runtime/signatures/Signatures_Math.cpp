//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_Math.cpp
// Purpose: Registers runtime helper descriptors for numeric conversions and
//          math utilities.
// Key invariants: Descriptors mirror the runtime ABI and manual signatures
//                 capture the expected argument shapes.
// Ownership/Lifetime: Registered descriptors live for the duration of the
//                     process and preserve registration order.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include "rt.hpp"
#include "rt_math.h"
#include "rt_numeric.h"
#include "rt_random.h"

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

    constexpr RuntimeLowering makeLowering(RuntimeLoweringKind kind,
                                           RuntimeFeature feature = RuntimeFeature::Count,
                                           bool ordered = false)
    {
        return RuntimeLowering{kind, feature, ordered};
    }

    constexpr RuntimeLowering featureLowering(RuntimeFeature feature, bool ordered = false)
    {
        return makeLowering(RuntimeLoweringKind::Feature, feature, ordered);
    }

    constexpr RuntimeLowering kManualLowering = makeLowering(RuntimeLoweringKind::Manual);

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

    RuntimeSignature buildSignature(const DescriptorRow &row)
    {
        RuntimeSignature signature = row.signatureId
            ? parseSignatureSpec(data::kRtSigSpecs[static_cast<std::size_t>(*row.signatureId)])
            : parseSignatureSpec(row.spec);
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

    struct DescriptorDefinition
    {
        DescriptorRow row;
        Signature expected;
    };

    constexpr std::array<DescriptorDefinition, 17> kDefinitions{{
        DescriptorDefinition{{"rt_cint_from_double",
                               std::nullopt,
                               "i64(f64,ptr)",
                               &invokeRtCintFromDouble,
                               featureLowering(RuntimeFeature::CintFromDouble),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_cint_from_double",
                                        {SigParam{SigParam::F64}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_clng_from_double",
                               std::nullopt,
                               "i64(f64,ptr)",
                               &invokeRtClngFromDouble,
                               featureLowering(RuntimeFeature::ClngFromDouble),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_clng_from_double",
                                        {SigParam{SigParam::F64}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_csng_from_double",
                               std::nullopt,
                               "f64(f64,ptr)",
                               &invokeRtCsngFromDouble,
                               featureLowering(RuntimeFeature::CsngFromDouble),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_csng_from_double",
                                        {SigParam{SigParam::F64}, SigParam{SigParam::Ptr}},
                                        {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_cdbl_from_any",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_cdbl_from_any, double, double>::invoke,
                               featureLowering(RuntimeFeature::CdblFromAny),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_cdbl_from_any", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_int_floor",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_int_floor, double, double>::invoke,
                               featureLowering(RuntimeFeature::IntFloor),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_int_floor", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_fix_trunc",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_fix_trunc, double, double>::invoke,
                               featureLowering(RuntimeFeature::FixTrunc),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_fix_trunc", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_round_even",
                               std::nullopt,
                               "f64(f64,i32)",
                               &invokeRtRoundEven,
                               featureLowering(RuntimeFeature::RoundEven),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_round_even",
                                        {SigParam{SigParam::F64}, SigParam{SigParam::I32}},
                                        {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_sqrt",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_sqrt, double, double>::invoke,
                               featureLowering(RuntimeFeature::Sqrt, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_sqrt", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_abs_i64",
                               std::nullopt,
                               "i64(i64)",
                               &DirectHandler<&rt_abs_i64, long long, long long>::invoke,
                               featureLowering(RuntimeFeature::AbsI64, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_abs_i64", {SigParam{SigParam::I64}}, {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_abs_f64",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_abs_f64, double, double>::invoke,
                               featureLowering(RuntimeFeature::AbsF64, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_abs_f64", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_floor",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_floor, double, double>::invoke,
                               featureLowering(RuntimeFeature::Floor, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_floor", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_ceil",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_ceil, double, double>::invoke,
                               featureLowering(RuntimeFeature::Ceil, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_ceil", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_sin",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_sin, double, double>::invoke,
                               featureLowering(RuntimeFeature::Sin, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_sin", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_cos",
                               std::nullopt,
                               "f64(f64)",
                               &DirectHandler<&rt_cos, double, double>::invoke,
                               featureLowering(RuntimeFeature::Cos, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_cos", {SigParam{SigParam::F64}}, {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_pow_f64_chkdom",
                               std::nullopt,
                               "f64(f64,f64)",
                               &invokeRtPowF64Chkdom,
                               featureLowering(RuntimeFeature::Pow, true),
                               kPowHidden.data(),
                               kPowHidden.size(),
                               RuntimeTrapClass::PowDomainOverflow},
                              Signature{"rt_pow_f64_chkdom",
                                        {SigParam{SigParam::F64}, SigParam{SigParam::F64}},
                                        {SigParam{SigParam::F64}}}},
        DescriptorDefinition{{"rt_randomize_i64",
                               std::nullopt,
                               "void(i64)",
                               &DirectHandler<&rt_randomize_i64, void, long long>::invoke,
                               featureLowering(RuntimeFeature::RandomizeI64, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_randomize_i64", {SigParam{SigParam::I64}}, {}}},
        DescriptorDefinition{{"rt_rnd",
                               std::nullopt,
                               "f64()",
                               &DirectHandler<&rt_rnd, double>::invoke,
                               featureLowering(RuntimeFeature::Rnd, true),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_rnd", {}, {SigParam{SigParam::F64}}}},
    }};
} // namespace

void register_math(std::vector<RuntimeDescriptor> &out)
{
    out.reserve(out.size() + kDefinitions.size());
    for (const auto &def : kDefinitions)
    {
        out.push_back(buildDescriptor(def.row));
        register_signature(def.expected);
    }
}

} // namespace il::runtime::signatures

