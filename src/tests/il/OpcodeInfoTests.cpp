//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/OpcodeInfoTests.cpp
// Purpose: Exercise opcode metadata enumeration helpers for stability.
// Key invariants: Enumeration covers every opcode exactly once in declaration order.
// Ownership/Lifetime: Uses read-only metadata from il::core.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/OpcodeInfo.hpp"

#include "tests/TestHarness.hpp"

TEST(IL, OpcodeInfoTests)
{
    using namespace il::core;

    const auto ops = all_opcodes();
    ASSERT_FALSE(ops.empty());

    const auto again = all_opcodes();
    ASSERT_EQ(ops, again);

    for (size_t index = 0; index < ops.size(); ++index)
    {
        const Opcode op = ops[index];
        ASSERT_EQ(static_cast<size_t>(op), index);

        const auto mnemonic = opcode_mnemonic(op);
        ASSERT_FALSE(mnemonic.empty());
        ASSERT_EQ(mnemonic, getOpcodeInfo(op).name);
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
