// File: tests/unit/test_runtime_registry.cpp
// Purpose: Validate runtime registry metadata coverage.
// License: MIT License.
// Key invariants: Every descriptor publishes a handler and signature mapping.
// Links: docs/codemap.md

#include "il/runtime/RuntimeSignatures.hpp"
#include <cassert>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

int main()
{
    const auto &registry = il::runtime::runtimeRegistry();
    assert(!registry.empty());

    std::unordered_set<std::string_view> names;
    std::unordered_map<il::runtime::RuntimeFeature, const il::runtime::RuntimeDescriptor *> featureOwners;
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

    const auto &signatureMap = il::runtime::runtimeSignatures();
    assert(signatureMap.size() == registry.size());
    return 0;
}

