//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/runtime/test_SignaturesPurity.cpp
// Purpose: Verify runtime signature registry seeds purity/read-only flags for optimisations. 
// Key invariants: Known math helpers report pure+nothrow; strlen-style helpers report
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "../../GTestStub.hpp"

#include "il/runtime/signatures/Registry.hpp"

#include <string_view>

namespace il::runtime::signatures
{
void register_math_signatures();
void register_string_signatures();
} // namespace il::runtime::signatures

namespace
{
using il::runtime::signatures::Signature;

const Signature *findSignature(std::string_view name)
{
    for (const auto &signature : il::runtime::signatures::all_signatures())
        if (signature.name == name)
            return &signature;
    return nullptr;
}
} // namespace

TEST(SignaturesPurity, MathHelpersArePure)
{
    il::runtime::signatures::register_math_signatures();
    const auto *roundEven = findSignature("rt_round_even");
    ASSERT_NE(roundEven, nullptr);
    EXPECT_TRUE(roundEven->pure);
    EXPECT_TRUE(roundEven->nothrow);
    EXPECT_FALSE(roundEven->readonly);

    const auto *sinSig = findSignature("rt_sin");
    ASSERT_NE(sinSig, nullptr);
    EXPECT_TRUE(sinSig->pure);
    EXPECT_TRUE(sinSig->nothrow);
}

TEST(SignaturesPurity, ReadonlyStringHelpers)
{
    il::runtime::signatures::register_string_signatures();
    const auto *lenSig = findSignature("rt_len");
    ASSERT_NE(lenSig, nullptr);
    EXPECT_TRUE(lenSig->readonly);
    EXPECT_TRUE(lenSig->nothrow);
    EXPECT_FALSE(lenSig->pure);

    const auto *instrSig = findSignature("rt_instr2");
    ASSERT_NE(instrSig, nullptr);
    EXPECT_TRUE(instrSig->readonly);
    EXPECT_TRUE(instrSig->nothrow);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
