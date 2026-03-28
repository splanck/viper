//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_pac_bti.cpp
// Purpose: Verify that the AArch64 binary encoder emits PAC (Pointer
//          Authentication Code) and BTI (Branch Target Identification)
//          instructions in function prologues and epilogues.
//
// Key invariants:
//   - BTI C (HINT #34 = 0xD503245F) appears at function entry
//   - PACIASP (HINT #25 = 0xD503233F) appears before saving LR
//   - AUTIASP (HINT #29 = 0xD50323BF) appears before ret
//   - These execute as NOPs on pre-ARMv8.3/8.5 hardware
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/aarch64/binenc/A64BinaryEncoder.hpp,
//        plans/audit-04-aarch64-abi.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/binenc/A64BinaryEncoder.hpp"
#include "codegen/common/objfile/CodeSection.hpp"

using namespace viper::codegen::aarch64;
using namespace viper::codegen::aarch64::binenc;

namespace {

/// Read a little-endian 32-bit word from a byte buffer at the given offset.
uint32_t readWord(const std::vector<uint8_t> &bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

/// Check if a specific 32-bit instruction word appears anywhere in the code section.
bool containsWord(const viper::codegen::objfile::CodeSection &cs, uint32_t word) {
    const auto &bytes = cs.bytes();
    for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
        if (readWord(bytes, i) == word)
            return true;
    }
    return false;
}

/// Create a minimal non-leaf MIR function (has a Bl call → not leaf).
MFunction makeNonLeafFunc(const std::string &name) {
    MFunction fn{};
    fn.name = name;
    fn.isLeaf = false;
    fn.localFrameSize = 0;

    MBasicBlock entry{};
    entry.name = "entry";
    // bl some_func
    MInstr call{};
    call.opc = MOpcode::Bl;
    call.ops.push_back(MOperand::labelOp("some_func"));
    entry.instrs.push_back(std::move(call));
    // ret
    MInstr ret{};
    ret.opc = MOpcode::Ret;
    entry.instrs.push_back(std::move(ret));

    fn.blocks.push_back(std::move(entry));
    return fn;
}

/// Create a minimal leaf MIR function (no calls, no saves, no locals).
MFunction makeLeafFunc(const std::string &name) {
    MFunction fn{};
    fn.name = name;
    fn.isLeaf = true;
    fn.localFrameSize = 0;

    MBasicBlock entry{};
    entry.name = "entry";
    // ret
    MInstr ret{};
    ret.opc = MOpcode::Ret;
    entry.instrs.push_back(std::move(ret));

    fn.blocks.push_back(std::move(entry));
    return fn;
}

// PAC/BTI encoding constants
constexpr uint32_t kBtiC = 0xD503245F;
constexpr uint32_t kPaciasp = 0xD503233F;
constexpr uint32_t kAutiasp = 0xD50323BF;
constexpr uint32_t kRetInstr = 0xD65F03C0;

} // namespace

// ---------------------------------------------------------------------------
// Test: Non-leaf function gets BTI C at entry
// ---------------------------------------------------------------------------
TEST(AArch64PacBti, BtiAtFunctionEntry) {
    A64BinaryEncoder encoder;
    viper::codegen::objfile::CodeSection text;
    viper::codegen::objfile::CodeSection rodata;

    auto fn = makeNonLeafFunc("test_bti");
    encoder.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // First instruction should be BTI C
    EXPECT_GE(text.bytes().size(), 4u);
    uint32_t firstWord = readWord(text.bytes(), 0);
    EXPECT_EQ(firstWord, kBtiC);
}

// ---------------------------------------------------------------------------
// Test: Non-leaf function gets PACIASP in prologue
// ---------------------------------------------------------------------------
TEST(AArch64PacBti, PaciaspInPrologue) {
    A64BinaryEncoder encoder;
    viper::codegen::objfile::CodeSection text;
    viper::codegen::objfile::CodeSection rodata;

    auto fn = makeNonLeafFunc("test_pac");
    encoder.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // PACIASP should be present (after BTI C, before stp)
    EXPECT_TRUE(containsWord(text, kPaciasp));
}

// ---------------------------------------------------------------------------
// Test: Non-leaf function gets AUTIASP before ret
// ---------------------------------------------------------------------------
TEST(AArch64PacBti, AutoaspBeforeRet) {
    A64BinaryEncoder encoder;
    viper::codegen::objfile::CodeSection text;
    viper::codegen::objfile::CodeSection rodata;

    auto fn = makeNonLeafFunc("test_aut");
    encoder.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // AUTIASP should be present
    EXPECT_TRUE(containsWord(text, kAutiasp));

    // AUTIASP should appear before RET
    const auto &bytes = text.bytes();
    size_t autiaspPos = SIZE_MAX;
    size_t retPos = SIZE_MAX;
    for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
        uint32_t w = readWord(bytes, i);
        if (w == kAutiasp)
            autiaspPos = i;
        if (w == kRetInstr)
            retPos = i;
    }
    EXPECT_NE(autiaspPos, SIZE_MAX);
    EXPECT_NE(retPos, SIZE_MAX);
    if (autiaspPos != SIZE_MAX && retPos != SIZE_MAX) {
        EXPECT_LT(autiaspPos, retPos);
    }
}

// ---------------------------------------------------------------------------
// Test: Leaf function still gets BTI C (needed for indirect calls)
// ---------------------------------------------------------------------------
TEST(AArch64PacBti, LeafFuncGetsBti) {
    A64BinaryEncoder encoder;
    viper::codegen::objfile::CodeSection text;
    viper::codegen::objfile::CodeSection rodata;

    auto fn = makeLeafFunc("test_leaf_bti");
    encoder.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // Even leaf functions should have BTI C at entry
    EXPECT_GE(text.bytes().size(), 4u);
    uint32_t firstWord = readWord(text.bytes(), 0);
    EXPECT_EQ(firstWord, kBtiC);
}

// ---------------------------------------------------------------------------
// Test: Leaf function does NOT get PACIASP/AUTIASP (no LR to sign)
// ---------------------------------------------------------------------------
TEST(AArch64PacBti, LeafFuncNoPac) {
    A64BinaryEncoder encoder;
    viper::codegen::objfile::CodeSection text;
    viper::codegen::objfile::CodeSection rodata;

    auto fn = makeLeafFunc("test_leaf_nopac");
    encoder.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // Leaf functions skip prologue → no PACIASP or AUTIASP
    EXPECT_FALSE(containsWord(text, kPaciasp));
    EXPECT_FALSE(containsWord(text, kAutiasp));
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
