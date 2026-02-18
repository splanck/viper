//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_logical_imm.cpp
// Purpose: Verify AArch64 logical immediate detection and MIR opcode selection.
//
// AArch64 AND/ORR/EOR instructions encode immediates as "logical immediates" —
// values that consist of a replicated pattern of contiguous 1-bits at any
// element size (2, 4, 8, 16, 32, 64 bits), possibly rotated to wrap around.
//
// Tests:
//   1. Valid logical immediates are accepted.
//   2. Invalid immediates (non-repeating patterns, 0, ~0) are rejected.
//   3. All single-bit values (powers of two) are valid.
//   4. Common compiler-generated masks are encodable.
//   5. MIR opcode constants for RI-form bitwise ops are distinct.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

using namespace viper::codegen::aarch64;

// -------------------------------------------------------------------------
// Test 1: Valid logical immediates are accepted.
//
// Valid patterns:
//   - Any contiguous run of 1-bits within the 64-bit word
//   - Rotated runs (wrap around MSB↔LSB)
//   - Replicated N-bit elements where the element is a contiguous run
// -------------------------------------------------------------------------
TEST(LogicalImm, ValidImmediates)
{
    // Simple contiguous runs from bit 0 upward (no rotation)
    EXPECT_TRUE(isLogicalImmediate(0x1ULL));        // single bit
    EXPECT_TRUE(isLogicalImmediate(0x3ULL));        // 2 bits
    EXPECT_TRUE(isLogicalImmediate(0x7ULL));        // 3 bits
    EXPECT_TRUE(isLogicalImmediate(0xFULL));        // 4 bits
    EXPECT_TRUE(isLogicalImmediate(0xFFULL));       // 8 bits
    EXPECT_TRUE(isLogicalImmediate(0xFFFFULL));     // 16 bits
    EXPECT_TRUE(isLogicalImmediate(0xFFFFFFFFULL)); // 32 bits

    // Contiguous runs NOT starting at bit 0 (no wrapping needed at element level)
    EXPECT_TRUE(isLogicalImmediate(0x6ULL));        // bits 1-2 set
    EXPECT_TRUE(isLogicalImmediate(0xEULL));        // bits 1-3 set
    EXPECT_TRUE(isLogicalImmediate(0x0000FFFFULL)); // 16-bit run in bits 0-15

    // Replicated 2-bit elements: 0b01 and 0b10 both represent single 1-bit runs
    EXPECT_TRUE(isLogicalImmediate(0x5555555555555555ULL)); // N=2, elem=0b01
    EXPECT_TRUE(isLogicalImmediate(0xAAAAAAAAAAAAAAAAULL)); // N=2, elem=0b10 (rotated 0b01)

    // Replicated 8-bit elements
    EXPECT_TRUE(isLogicalImmediate(0x0F0F0F0F0F0F0F0FULL)); // N=8, elem=0b00001111
    EXPECT_TRUE(isLogicalImmediate(0xF0F0F0F0F0F0F0F0ULL)); // N=8, elem=0b11110000 (rotated)

    // Replicated 16-bit elements
    EXPECT_TRUE(isLogicalImmediate(0x00FF00FF00FF00FFULL)); // N=16, elem=0x00FF

    // Rotated 64-bit patterns: run wraps from bit 63 to bit 0
    EXPECT_TRUE(isLogicalImmediate(0x8000000000000001ULL)); // bits 0 and 63 set
    EXPECT_TRUE(isLogicalImmediate(0xC000000000000003ULL)); // bits 0-1 and 62-63 set
    EXPECT_TRUE(isLogicalImmediate(0xFFFFFFFF00000000ULL)); // upper 32 bits
}

// -------------------------------------------------------------------------
// Test 2: Invalid immediates are rejected.
//
// Invalid:
//   - 0 and ~0 (excluded by the AArch64 spec)
//   - Values where no replication of any element size works
//   - Values where the element has non-adjacent 1-bit runs
// -------------------------------------------------------------------------
TEST(LogicalImm, InvalidImmediates)
{
    // Boundary cases excluded by spec
    EXPECT_FALSE(isLogicalImmediate(0ULL));
    EXPECT_FALSE(isLogicalImmediate(~0ULL));

    // 0b1010 as a 64-bit value: bits 1 and 3 set (non-adjacent at N=64),
    // and no smaller element size replicates cleanly to 0b1010.
    EXPECT_FALSE(isLogicalImmediate(0xAULL));

    // 0b01010101 as a 64-bit value: 4 non-adjacent runs at N=64;
    // replicated 2-bit element would give 0x5555...5555, not 0x55.
    EXPECT_FALSE(isLogicalImmediate(0x55ULL));

    // Arbitrary non-structured values
    EXPECT_FALSE(isLogicalImmediate(0x1234567890ABCDEFULL));

    // Diagonal bits: each byte has exactly one bit at a unique position.
    // No element-size replication is consistent.
    EXPECT_FALSE(isLogicalImmediate(0x0102040810204080ULL));
}

// -------------------------------------------------------------------------
// Test 3: All single-bit values (powers of two) are valid logical immediates.
// -------------------------------------------------------------------------
TEST(LogicalImm, PowersOfTwo)
{
    for (int i = 0; i < 64; ++i)
    {
        const uint64_t val = uint64_t(1) << i;
        EXPECT_TRUE(isLogicalImmediate(val));
    }
}

// -------------------------------------------------------------------------
// Test 4: Common compiler-generated masks are encodable.
// -------------------------------------------------------------------------
TEST(LogicalImm, CommonMasks)
{
    EXPECT_TRUE(isLogicalImmediate(0xFFULL));               // byte mask (AND with 0xFF)
    EXPECT_TRUE(isLogicalImmediate(0xFFFFULL));             // 16-bit mask
    EXPECT_TRUE(isLogicalImmediate(0xFFFFFFFFULL));         // 32-bit mask (zext32)
    EXPECT_TRUE(isLogicalImmediate(0x7FFFFFFFFFFFFFFFULL)); // INT64_MAX
    EXPECT_TRUE(isLogicalImmediate(0x7FFFFFFFULL));         // INT32_MAX as 64-bit
    EXPECT_TRUE(isLogicalImmediate(0x1ULL));                // boolean (zext1)
}

// -------------------------------------------------------------------------
// Test 5: MIR opcode constants for RI-form bitwise ops are distinct.
// -------------------------------------------------------------------------
TEST(LogicalImm, OpcodeDistinct)
{
    EXPECT_TRUE(MOpcode::AndRI != MOpcode::AndRRR);
    EXPECT_TRUE(MOpcode::OrrRI != MOpcode::OrrRRR);
    EXPECT_TRUE(MOpcode::EorRI != MOpcode::EorRRR);
    EXPECT_TRUE(MOpcode::AndRI != MOpcode::OrrRI);
    EXPECT_TRUE(MOpcode::OrrRI != MOpcode::EorRI);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
