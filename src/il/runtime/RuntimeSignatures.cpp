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

#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/RuntimeSignaturesData.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace il::runtime
{
namespace signatures
{
void register_fileio(std::vector<RuntimeDescriptor> &out);
void register_strings(std::vector<RuntimeDescriptor> &out);
void register_math(std::vector<RuntimeDescriptor> &out);
void register_arrays(std::vector<RuntimeDescriptor> &out);
} // namespace signatures

namespace
{
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

#ifndef NDEBUG
    signatures::SigParam::Kind toSimpleKind(il::core::Type::Kind kind)
    {
        using Kind = il::core::Type::Kind;
        switch (kind)
        {
            case Kind::I1:
            case Kind::I16:
            case Kind::I32:
                return signatures::SigParam::I32;
            case Kind::I64:
                return signatures::SigParam::I64;
            case Kind::F64:
                return signatures::SigParam::F64;
            case Kind::Ptr:
            case Kind::Str:
                return signatures::SigParam::Ptr;
            default:
                assert(false && "unsupported runtime signature kind");
                return signatures::SigParam::Ptr;
        }
    }

    void verifyRegistry(const std::vector<RuntimeDescriptor> &descriptors)
    {
        std::unordered_set<std::string_view> descriptorNames;
        descriptorNames.reserve(descriptors.size());
        for (const auto &desc : descriptors)
        {
            auto [_, inserted] = descriptorNames.emplace(desc.name);
            assert(inserted && "duplicate runtime descriptor registered");
        }

        const auto &expected = signatures::all_signatures();
        std::unordered_map<std::string_view, const signatures::Signature *> expectedMap;
        expectedMap.reserve(expected.size());
        for (const auto &sig : expected)
        {
            auto [it, inserted] = expectedMap.emplace(sig.name, &sig);
            assert(inserted && "duplicate runtime signature definition registered");
        }

        for (const auto &sig : expected)
        {
            auto nameIt = descriptorNames.find(sig.name);
            assert(nameIt != descriptorNames.end() && "debug signature missing runtime descriptor");
        }

        for (const auto &desc : descriptors)
        {
            auto it = expectedMap.find(desc.name);
            assert(it != expectedMap.end() && "runtime descriptor missing debug signature");
            const auto &sig = *it->second;

            std::vector<signatures::SigParam::Kind> actualParams;
            actualParams.reserve(desc.signature.paramTypes.size());
            for (const auto &param : desc.signature.paramTypes)
                actualParams.push_back(toSimpleKind(param.kind));

            std::vector<signatures::SigParam::Kind> actualRets;
            if (desc.signature.retType.kind != il::core::Type::Kind::Void)
                actualRets.push_back(toSimpleKind(desc.signature.retType.kind));

            assert(sig.params.size() == actualParams.size() &&
                   "runtime descriptor parameter count mismatch");
            for (std::size_t i = 0; i < actualParams.size(); ++i)
                assert(sig.params[i].kind == actualParams[i] &&
                       "runtime descriptor parameter type mismatch");

            assert(sig.rets.size() == actualRets.size() &&
                   "runtime descriptor return count mismatch");
            for (std::size_t i = 0; i < actualRets.size(); ++i)
                assert(sig.rets[i].kind == actualRets[i] &&
                       "runtime descriptor return type mismatch");
        }
    }
#endif
} // namespace

const std::vector<RuntimeDescriptor> &runtimeRegistry()
{
    static const std::vector<RuntimeDescriptor> registry = []
    {
        std::vector<RuntimeDescriptor> entries;
        entries.reserve(88);
        signatures::register_fileio(entries);
        signatures::register_strings(entries);
        signatures::register_math(entries);
        signatures::register_arrays(entries);
#ifndef NDEBUG
        verifyRegistry(entries);
#endif
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

