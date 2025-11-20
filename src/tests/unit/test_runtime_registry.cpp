// File: tests/unit/test_runtime_registry.cpp
// Purpose: Validate runtime registry metadata coverage.
// License: GPL-3.0-only.
// Key invariants: Every descriptor publishes a handler and signature mapping.
// Links: docs/codemap.md

#include "il/runtime/RuntimeSignatures.hpp"
#include <cassert>
#include <initializer_list>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

int main()
{
    const auto &registry = il::runtime::runtimeRegistry();
    assert(!registry.empty());

    std::unordered_set<std::string_view> names;
    std::unordered_map<il::runtime::RuntimeFeature, const il::runtime::RuntimeDescriptor *>
        featureOwners;
    for (const auto &entry : registry)
    {
        assert(entry.handler && "runtime descriptor missing handler");
        assert(names.insert(entry.name).second && "duplicate runtime descriptor name");

        const auto *byName = il::runtime::findRuntimeDescriptor(entry.name);
        assert(byName == &entry && "descriptor lookup by name mismatch");

        if (entry.lowering.kind == il::runtime::RuntimeLoweringKind::Feature)
        {
            const auto *byFeature = il::runtime::findRuntimeDescriptor(entry.lowering.feature);
            auto [it, inserted] = featureOwners.emplace(entry.lowering.feature, &entry);
            if (inserted)
                assert(byFeature == &entry && "descriptor lookup by feature mismatch");
            else
                assert(byFeature == it->second && "descriptor lookup by feature mismatch");
        }
    }

    auto expectTermDescriptor = [](il::runtime::RuntimeFeature feature,
                                   std::string_view expectedName,
                                   std::initializer_list<il::core::Type::Kind> paramKinds)
    {
        const auto *descriptor = il::runtime::findRuntimeDescriptor(feature);
        assert(descriptor && "terminal runtime descriptor missing");
        assert(descriptor->name == expectedName && "terminal runtime descriptor name mismatch");
        assert(descriptor->signature.retType.kind == il::core::Type::Kind::Void &&
               "terminal runtime descriptor return type mismatch");
        assert(descriptor->signature.paramTypes.size() == paramKinds.size() &&
               "terminal runtime descriptor arity mismatch");
        auto kindIt = paramKinds.begin();
        for (const auto &param : descriptor->signature.paramTypes)
        {
            assert(param.kind == *kindIt++ && "terminal runtime descriptor parameter mismatch");
        }
    };

    expectTermDescriptor(il::runtime::RuntimeFeature::TermCls, "rt_term_cls", {});
    expectTermDescriptor(il::runtime::RuntimeFeature::TermColor,
                         "rt_term_color_i32",
                         {il::core::Type::Kind::I32, il::core::Type::Kind::I32});
    expectTermDescriptor(il::runtime::RuntimeFeature::TermLocate,
                         "rt_term_locate_i32",
                         {il::core::Type::Kind::I32, il::core::Type::Kind::I32});

    const auto *strEqDescriptor =
        il::runtime::findRuntimeDescriptor(il::runtime::RuntimeFeature::StrEq);
    assert(strEqDescriptor && "string equality runtime descriptor missing");
    assert(strEqDescriptor->name == "rt_str_eq" &&
           "string equality runtime descriptor name mismatch");
    assert(strEqDescriptor->signature.retType.kind == il::core::Type::Kind::I1 &&
           "string equality runtime descriptor return type mismatch");
    assert(strEqDescriptor->signature.paramTypes.size() == 2 &&
           "string equality runtime descriptor arity mismatch");
    assert(strEqDescriptor->signature.paramTypes[0].kind == il::core::Type::Kind::Str &&
           "string equality runtime descriptor first parameter mismatch");
    assert(strEqDescriptor->signature.paramTypes[1].kind == il::core::Type::Kind::Str &&
           "string equality runtime descriptor second parameter mismatch");

    const auto &signatureMap = il::runtime::runtimeSignatures();
    assert(signatureMap.size() == registry.size());
    return 0;
}
