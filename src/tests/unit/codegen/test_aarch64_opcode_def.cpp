//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_opcode_def.cpp
// Purpose: Verify that the MOpcodeDef.inc X-macro generates consistent
//          enum values and name table entries. Catches desync between the
//          .inc file and any manual additions.
//
// Key invariants:
//   - Every MOpcode enum value has a non-empty name from opcodeName()
//   - opcodeName never returns "<unknown>" for any valid enum value
//   - The total opcode count matches expectations
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/aarch64/MOpcodeDef.inc
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/MachineIR.hpp"

#include <cstring>
#include <string>
#include <unordered_set>

using namespace viper::codegen::aarch64;

namespace {

/// Collect all opcode enum values into a vector for iteration.
std::vector<MOpcode> allOpcodes() {
    std::vector<MOpcode> opcodes;
#define VIPER_MIR_OPCODE(name) opcodes.push_back(MOpcode::name);
#include "codegen/aarch64/MOpcodeDef.inc"
    return opcodes;
}

} // namespace

// ---------------------------------------------------------------------------
// Test: Every opcode has a name (not "<unknown>")
// ---------------------------------------------------------------------------
TEST(AArch64OpcodeDef, AllOpcodesHaveNames) {
    auto opcodes = allOpcodes();
    for (MOpcode opc : opcodes) {
        const char *name = opcodeName(opc);
        EXPECT_NE(std::strcmp(name, "<unknown>"), 0);
        EXPECT_GT(std::strlen(name), 0u);
    }
}

// ---------------------------------------------------------------------------
// Test: No duplicate names
// ---------------------------------------------------------------------------
TEST(AArch64OpcodeDef, NoDuplicateNames) {
    auto opcodes = allOpcodes();
    std::unordered_set<std::string> names;
    for (MOpcode opc : opcodes) {
        std::string name = opcodeName(opc);
        EXPECT_TRUE(names.insert(name).second);
    }
}

// ---------------------------------------------------------------------------
// Test: Opcode count matches expected value
// ---------------------------------------------------------------------------
TEST(AArch64OpcodeDef, OpcodeCount) {
    auto opcodes = allOpcodes();
    // 79 opcodes as of the MOpcodeDef.inc creation. Update this when adding opcodes.
    EXPECT_EQ(opcodes.size(), 79u);
}

// ---------------------------------------------------------------------------
// Test: First and last opcodes are correct (ordering sanity check)
// ---------------------------------------------------------------------------
TEST(AArch64OpcodeDef, FirstAndLastOpcode) {
    auto opcodes = allOpcodes();
    EXPECT_EQ(opcodes.front(), MOpcode::MovRR);
    EXPECT_EQ(opcodes.back(), MOpcode::MulOvfRRR);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
