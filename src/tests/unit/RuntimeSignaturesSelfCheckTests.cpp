//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/RuntimeSignaturesSelfCheckTests.cpp
// Purpose: Test the runtime descriptor self-check API for embedders.
// Key invariants: Self-check passes in correctly built binaries, is idempotent.
// Ownership/Lifetime: Uses read-only runtime metadata.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include "il/runtime/RuntimeSignatures.hpp"

#include <unordered_set>

/// @brief Test that the self-check API passes under normal conditions.
/// @details This is the primary test that embedders rely on to verify
///          runtime integrity at startup. In a correctly built binary,
///          selfCheckRuntimeDescriptors() must always return true.
TEST(RuntimeSignaturesSelfCheck, HappyPathPasses)
{
    // The self-check should always pass in a correctly linked binary
    ASSERT_TRUE(il::runtime::selfCheckRuntimeDescriptors());
}

/// @brief Test that self-check is idempotent.
/// @details The self-check uses static initialization internally, so repeated
///          calls should return the same cached result without re-running checks.
TEST(RuntimeSignaturesSelfCheck, Idempotent)
{
    const bool first = il::runtime::selfCheckRuntimeDescriptors();
    const bool second = il::runtime::selfCheckRuntimeDescriptors();
    const bool third = il::runtime::selfCheckRuntimeDescriptors();

    ASSERT_EQ(first, second);
    ASSERT_EQ(second, third);
    ASSERT_TRUE(first);
}

/// @brief Test that the runtime registry is non-empty.
/// @details A valid runtime must have at least some descriptors registered.
///          This ensures the static initialization of the registry happened.
TEST(RuntimeSignaturesSelfCheck, RegistryNonEmpty)
{
    const auto &registry = il::runtime::runtimeRegistry();
    ASSERT_FALSE(registry.empty());
}

/// @brief Test that all descriptors have non-null handlers.
/// @details Every runtime descriptor must have a handler function pointer
///          that the VM can invoke at runtime.
TEST(RuntimeSignaturesSelfCheck, AllDescriptorsHaveHandlers)
{
    const auto &registry = il::runtime::runtimeRegistry();
    for (const auto &desc : registry)
    {
        ASSERT_NE(desc.handler, nullptr);
    }
}

/// @brief Test that all descriptor names are unique.
/// @details Duplicate names would cause lookup ambiguity at runtime.
TEST(RuntimeSignaturesSelfCheck, UniqueDescriptorNames)
{
    const auto &registry = il::runtime::runtimeRegistry();
    std::unordered_set<std::string_view> names;

    for (const auto &desc : registry)
    {
        auto [it, inserted] = names.insert(desc.name);
        ASSERT_TRUE(inserted);
    }
}

/// @brief Test that findRuntimeDescriptor returns correct descriptors.
/// @details Spot-check a few well-known runtime functions to ensure lookup works.
TEST(RuntimeSignaturesSelfCheck, LookupByNameWorks)
{
    // These are fundamental runtime functions that must exist
    const char *knownFunctions[] = {
        "rt_print_str",
        "rt_print_i64",
        "rt_concat",
        "rt_str_eq",
    };

    for (const char *name : knownFunctions)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(name);
        ASSERT_NE(desc, nullptr);
        ASSERT_EQ(desc->name, name);
    }
}

/// @brief Test that signature map has same size as registry.
/// @details Every descriptor should have a corresponding signature entry.
TEST(RuntimeSignaturesSelfCheck, SignatureMapMatchesRegistry)
{
    const auto &registry = il::runtime::runtimeRegistry();
    const auto &signatures = il::runtime::runtimeSignatures();

    ASSERT_EQ(registry.size(), signatures.size());
}

/// @brief Test parameter count bounds.
/// @details Runtime functions shouldn't have an unreasonable number of parameters.
///          This is one of the checks performed by selfCheckRuntimeDescriptors().
TEST(RuntimeSignaturesSelfCheck, ReasonableParameterCounts)
{
    constexpr std::size_t kMaxReasonableParams = 16;
    const auto &registry = il::runtime::runtimeRegistry();

    for (const auto &desc : registry)
    {
        ASSERT_TRUE(desc.signature.paramTypes.size() <= kMaxReasonableParams);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
