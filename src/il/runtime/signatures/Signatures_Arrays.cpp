//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_Arrays.cpp
// Purpose: Registers runtime helper descriptors for array and object support.
// Key invariants: Descriptors mirror the runtime ABI and manual signatures cover
//                 expected argument/return shapes.
// Ownership/Lifetime: Registered descriptors and manual signatures have static
//                     storage duration.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include "rt.hpp"
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

    constexpr RuntimeLowering makeLowering(RuntimeLoweringKind kind,
                                           RuntimeFeature feature = RuntimeFeature::Count,
                                           bool ordered = false)
    {
        return RuntimeLowering{kind, feature, ordered};
    }

    constexpr RuntimeLowering kManualLowering = makeLowering(RuntimeLoweringKind::Manual);
    constexpr RuntimeLowering kAlwaysLowering = makeLowering(RuntimeLoweringKind::Always);

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

    constexpr std::array<DescriptorDefinition, 13> kDefinitions{{
        DescriptorDefinition{{"rt_alloc",
                               std::nullopt,
                               "ptr(i64)",
                               &DirectHandler<&rt_alloc, void *, int64_t>::invoke,
                               featureLowering(RuntimeFeature::Alloc),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_alloc", {SigParam{SigParam::I64}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_arr_i32_new",
                               std::nullopt,
                               "ptr(i64)",
                               &invokeRtArrI32New,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_i32_new", {SigParam{SigParam::I64}}, {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_arr_i32_retain",
                               std::nullopt,
                               "void(ptr)",
                               &DirectHandler<&rt_arr_i32_retain, void, int32_t *>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_i32_retain", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_arr_i32_release",
                               std::nullopt,
                               "void(ptr)",
                               &DirectHandler<&rt_arr_i32_release, void, int32_t *>::invoke,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_i32_release", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_arr_i32_len",
                               std::nullopt,
                               "i64(ptr)",
                               &invokeRtArrI32Len,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_i32_len", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_arr_i32_get",
                               std::nullopt,
                               "i64(ptr,i64)",
                               &invokeRtArrI32Get,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_i32_get",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::I64}}}},
        DescriptorDefinition{{"rt_arr_i32_set",
                               std::nullopt,
                               "void(ptr,i64,i64)",
                               &invokeRtArrI32Set,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_i32_set",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}, SigParam{SigParam::I64}},
                                        {}}},
        DescriptorDefinition{{"rt_arr_i32_resize",
                               std::nullopt,
                               "ptr(ptr,i64)",
                               &invokeRtArrI32Resize,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_i32_resize",
                                        {SigParam{SigParam::Ptr}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_arr_oob_panic",
                               std::nullopt,
                               "void(i64,i64)",
                               &invokeRtArrOobPanic,
                               kManualLowering,
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_arr_oob_panic",
                                        {SigParam{SigParam::I64}, SigParam{SigParam::I64}},
                                        {}}},
        DescriptorDefinition{{"rt_obj_new_i64",
                               std::nullopt,
                               "ptr(i64,i64)",
                               &DirectHandler<&rt_obj_new_i64, void *, int64_t, int64_t>::invoke,
                               featureLowering(RuntimeFeature::ObjNew),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_obj_new_i64",
                                        {SigParam{SigParam::I64}, SigParam{SigParam::I64}},
                                        {SigParam{SigParam::Ptr}}}},
        DescriptorDefinition{{"rt_obj_retain_maybe",
                               std::nullopt,
                               "void(ptr)",
                               &DirectHandler<&rt_obj_retain_maybe, void, void *>::invoke,
                               featureLowering(RuntimeFeature::ObjRetainMaybe),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_obj_retain_maybe", {SigParam{SigParam::Ptr}}, {}}},
        DescriptorDefinition{{"rt_obj_release_check0",
                               std::nullopt,
                               "i1(ptr)",
                               &DirectHandler<&rt_obj_release_check0, int32_t, void *>::invoke,
                               featureLowering(RuntimeFeature::ObjReleaseChk0),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_obj_release_check0", {SigParam{SigParam::Ptr}}, {SigParam{SigParam::I32}}}},
        DescriptorDefinition{{"rt_obj_free",
                               std::nullopt,
                               "void(ptr)",
                               &DirectHandler<&rt_obj_free, void, void *>::invoke,
                               featureLowering(RuntimeFeature::ObjFree),
                               nullptr,
                               0,
                               RuntimeTrapClass::None},
                              Signature{"rt_obj_free", {SigParam{SigParam::Ptr}}, {}}},
    }};
} // namespace

void register_arrays(std::vector<RuntimeDescriptor> &out)
{
    out.reserve(out.size() + kDefinitions.size());
    for (const auto &def : kDefinitions)
    {
        out.push_back(buildDescriptor(def.row));
        register_signature(def.expected);
    }
}

} // namespace il::runtime::signatures

