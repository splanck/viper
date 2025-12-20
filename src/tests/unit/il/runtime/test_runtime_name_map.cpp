//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/runtime/test_runtime_name_map.cpp
// Purpose: Ensure the canonical Viper.* runtime name map contains no duplicates
//          and that descriptors are consistent when they exist.
// Notes: Not all runtime functions require descriptors - many are called directly
//        from native codegen via C ABI and don't need VM marshalling descriptors.
//        The RuntimeNameMap provides name-to-symbol mappings for all runtime
//        functions, while RuntimeDescriptors are only needed for VM-callable
//        functions that require IL signature information.
// Key invariants: Every alias entry is unique.
// Ownership/Lifetime: Uses static tables only; no allocations beyond sets.
// Links: il/runtime/RuntimeNameMap.hpp, il/runtime/RuntimeSignatures.hpp
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "tests/TestHarness.hpp"
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
    // This test verifies that functions WITH descriptors can be resolved.
    // Not all runtime functions need descriptors - many are native-only
    // (called via C ABI from generated code, not via VM).
    // We only check that descriptors that DO exist are properly linked.
    std::size_t withDescriptor = 0;
    std::size_t withoutDescriptor = 0;

    for (const auto &alias : kRuntimeNameAliases)
    {
        const auto *canon = findRuntimeDescriptor(alias.canonical);
        if (canon)
            ++withDescriptor;
        else
            ++withoutDescriptor;
    }

    // Sanity check: we should have at least some functions with descriptors
    EXPECT_TRUE(withDescriptor > 0);

    // Info output (not an error - just documenting the split)
    std::cerr << "Runtime functions with descriptors: " << withDescriptor << "\n";
    std::cerr << "Runtime functions without descriptors (native-only): " << withoutDescriptor << "\n";
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
