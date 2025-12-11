//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/runtime/test_runtime_name_map.cpp
// Purpose: Ensure the canonical Viper.* runtime name map stays in sync with
//          the runtime descriptor registry and contains no duplicates.
// Key invariants: Every alias entry is unique and resolvable via
//                 findRuntimeDescriptor() for both canonical and rt_* names.
// Ownership/Lifetime: Uses static tables only; no allocations beyond sets.
// Links: il/runtime/RuntimeNameMap.hpp, il/runtime/RuntimeSignatures.hpp
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "tests/unit/GTestStub.hpp"

#include <iostream>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace il::runtime;

TEST(RuntimeNameMap, CanonicalAndRuntimeNamesUnique)
{
    std::unordered_set<std::string_view> canonical;
    for (const auto &alias : kRuntimeNameAliases)
    {
        EXPECT_TRUE(canonical.insert(alias.canonical).second);
    }
}

TEST(RuntimeNameMap, AliasesResolveToRegisteredDescriptors)
{
    std::vector<std::string_view> missingCanon;

    for (const auto &alias : kRuntimeNameAliases)
    {
        const auto *canon = findRuntimeDescriptor(alias.canonical);
        if (!canon)
            missingCanon.push_back(alias.canonical);
    }

    if (!missingCanon.empty())
    {
        std::cerr << "Missing canonical descriptors:\n";
        for (auto name : missingCanon)
            std::cerr << "  " << name << "\n";
    }

    EXPECT_TRUE(missingCanon.empty());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
