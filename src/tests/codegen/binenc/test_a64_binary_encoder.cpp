//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/binenc/test_a64_binary_encoder.cpp
// Purpose: Unit tests for A64BinaryEncoder — verifies that AArch64 MIR
//          instructions produce the correct 32-bit machine code words and
//          relocations.
// Key invariants:
//   - Every instruction is exactly 4 bytes
//   - Templates match ARM Architecture Reference Manual encodings
//   - Branch resolution produces correct signed offsets (imm26/imm19)
//   - External BL generates A64Call26 relocations
//   - Prologue/epilogue synthesis matches AsmEmitter behaviour
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/aarch64/binenc/A64BinaryEncoder.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/binenc/A64BinaryEncoder.hpp"
#include "codegen/aarch64/binenc/A64Encoding.hpp"
#include "codegen/common/objfile/CodeSection.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace viper::codegen::aarch64;
using namespace viper::codegen::aarch64::binenc;
using namespace viper::codegen::objfile;

namespace viper::codegen::aarch64::binenc {

struct A64BinaryEncoderTestAccess {
    static size_t measureWithKnownTarget(const MInstr &mi, size_t currentOffset, size_t targetOffset) {
        A64BinaryEncoder enc;
        MFunction fn;
        fn.name = "test_func";
        fn.isLeaf = true;
        enc.currentFn_ = &fn;
        enc.skipFrame_ = true;
        enc.usePlan_ = false;
        enc.labelOffsets_["target"] = targetOffset;
        return enc.measureInstructionSize(mi, currentOffset, enc.labelOffsets_);
    }

    static std::vector<uint8_t> encodeWithKnownTarget(const MInstr &mi,
                                                      size_t currentOffset,
                                                      size_t targetOffset) {
        A64BinaryEncoder enc;
        MFunction fn;
        fn.name = "test_func";
        fn.isLeaf = true;
        enc.currentFn_ = &fn;
        enc.skipFrame_ = true;
        enc.usePlan_ = false;
        enc.labelOffsets_["target"] = targetOffset;

        CodeSection text;
        text.emitZeros(currentOffset);
        enc.encodeInstruction(mi, text);
        return text.bytes();
    }
};

} // namespace viper::codegen::aarch64::binenc

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// Helper: read a 32-bit LE word from a byte vector at offset.
static uint32_t readWord(const std::vector<uint8_t> &bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

// Helper: create a physical GPR register operand.
static MOperand gpr(PhysReg r) {
    return MOperand::regOp(r);
}

// Helper: create a physical FPR register operand.
static MOperand fpr(PhysReg r) {
    return MOperand::regOp(r);
}

// Helper: create an immediate operand.
static MOperand imm(long long val) {
    return MOperand::immOp(val);
}

// Helper: create a condition code operand.
static MOperand cond(const char *c) {
    return MOperand::condOp(c);
}

// Helper: create a label operand.
static MOperand label(const std::string &name) {
    return MOperand::labelOp(name);
}

// Encode a single-block leaf function with given instructions.
// Returns the word at instruction index `idx` (skipping BTI prefix).
static uint32_t encodeSingleInstr(const std::vector<MInstr> &instrs, size_t idx = 0) {
    MFunction fn;
    fn.name = "test_func";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs = instrs;
    // Add a Ret at the end.
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    rodata.defineSymbol("my_global", SymbolBinding::Local, SymbolSection::Rodata);
    rodata.emit8(0);
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // Leaf function with no saved regs and no frame → no prologue.
    // First word is BTI C (always emitted); instructions start at offset 4.
    constexpr size_t kBtiPrefixWords = 1;
    return readWord(text.bytes(), (idx + kBtiPrefixWords) * 4);
}

// Encode a single-block leaf function and return all bytes.
// Includes BTI prefix (always emitted).
static std::vector<uint8_t> encodeInstrBytes(const std::vector<MInstr> &instrs) {
    MFunction fn;
    fn.name = "test_func";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs = instrs;
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    rodata.defineSymbol("my_global", SymbolBinding::Local, SymbolSection::Rodata);
    rodata.emit8(0);
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);
    return text.bytes();
}

// Count how many 4-byte words are emitted for a set of instructions
// (excluding the trailing Ret and the BTI prefix).
static size_t countWords(const std::vector<MInstr> &instrs) {
    auto bytes = encodeInstrBytes(instrs);
    constexpr size_t kOverhead = 2; // BTI prefix + Ret
    return (bytes.size() / 4) - kOverhead;
}

// =============================================================================
// Tests
// =============================================================================

static void testAddRRR() {
    // add x0, x1, x2 → 0x8B020020
    MInstr mi{MOpcode::AddRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x8B020020);
}

static void testSubRRR() {
    // sub x3, x4, x5 → 0xCB050083
    MInstr mi{MOpcode::SubRRR, {gpr(PhysReg::X3), gpr(PhysReg::X4), gpr(PhysReg::X5)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0xCB050083);
}

static void testAndRRR() {
    // and x0, x1, x2 → 0x8A020020
    MInstr mi{MOpcode::AndRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x8A020020);
}

static void testOrrRRR() {
    // orr x0, x1, x2 → 0xAA020020
    MInstr mi{MOpcode::OrrRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0xAA020020);
}

static void testEorRRR() {
    // eor x0, x1, x2 → 0xCA020020
    MInstr mi{MOpcode::EorRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0xCA020020);
}

static void testMulRRR() {
    // mul x0, x1, x2 → madd x0, x1, x2, xzr → 0x9B027C20
    MInstr mi{MOpcode::MulRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x9B027C20);
}

static void testSDivRRR() {
    // sdiv x0, x1, x2 → 0x9AC20C20
    MInstr mi{MOpcode::SDivRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x9AC20C20);
}

static void testMSubRRRR() {
    // msub x0, x1, x2, x3 → 0x9B028060
    MInstr mi{MOpcode::MSubRRRR,
              {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2), gpr(PhysReg::X3)}};
    uint32_t word = encodeSingleInstr({mi});
    // template 0x9B008000 | (Rm=2 << 16) | (Ra=3 << 10) | (Rn=1 << 5) | Rd=0
    // = 0x9B008000 | 0x00020000 | 0x00000C00 | 0x00000020 | 0
    CHECK(word == (0x9B008000u | (2u << 16) | (3u << 10) | (1u << 5) | 0u));
}

static void testAddRI() {
    // add x0, x1, #42 → 0x9100A820
    MInstr mi{MOpcode::AddRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(42)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x91000000u | (42u << 10) | (1u << 5) | 0u));
}

static void testSubRI() {
    // sub x3, x4, #100 → template | (100 << 10) | (4 << 5) | 3
    MInstr mi{MOpcode::SubRI, {gpr(PhysReg::X3), gpr(PhysReg::X4), imm(100)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD1000000u | (100u << 10) | (4u << 5) | 3u));
}

static void testMovRR() {
    // mov x0, x1 → orr x0, xzr, x1 → 0xAA0103E0
    MInstr mi{MOpcode::MovRR, {gpr(PhysReg::X0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xAA0003E0u | (1u << 16) | 0u));
}

static void testMovRI_small() {
    // movz x0, #0x1234 → 0xD2824680
    MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(0x1234)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD2800000u | (0x1234u << 5) | 0u));
}

static void testLslRI() {
    // lsl x0, x1, #4 → ubfm x0, x1, #60, #59
    MInstr mi{MOpcode::LslRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(4)}};
    uint32_t word = encodeSingleInstr({mi});
    uint32_t immr = (64 - 4) & 63; // 60
    uint32_t imms = 63 - 4;        // 59
    CHECK(word == (0xD3400000u | (immr << 16) | (imms << 10) | (1u << 5) | 0u));
}

static void testLsrRI() {
    // lsr x0, x1, #8 → ubfm x0, x1, #8, #63
    MInstr mi{MOpcode::LsrRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(8)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD3400000u | (8u << 16) | (63u << 10) | (1u << 5) | 0u));
}

static void testAsrRI() {
    // asr x0, x1, #8 → sbfm x0, x1, #8, #63
    MInstr mi{MOpcode::AsrRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(8)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x93400000u | (8u << 16) | (63u << 10) | (1u << 5) | 0u));
}

static void testCmpRR() {
    // cmp x0, x1 → subs xzr, x0, x1
    MInstr mi{MOpcode::CmpRR, {gpr(PhysReg::X0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    // kSubsRRR | (Rm=1 << 16) | (Rn=0 << 5) | Rd=31
    CHECK(word == (0xEB000000u | (1u << 16) | (0u << 5) | 31u));
}

static void testCmpRI() {
    // cmp x0, #42 → subs xzr, x0, #42
    MInstr mi{MOpcode::CmpRI, {gpr(PhysReg::X0), imm(42)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xF1000000u | (42u << 10) | (0u << 5) | 31u));
}

static void testTstRR() {
    // tst x0, x1 → ands xzr, x0, x1
    MInstr mi{MOpcode::TstRR, {gpr(PhysReg::X0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xEA000000u | (1u << 16) | (0u << 5) | 31u));
}

static void testCset() {
    // cset x0, eq → csinc x0, xzr, xzr, ne (inverted: eq=0 → ne=1)
    MInstr mi{MOpcode::Cset, {gpr(PhysReg::X0), cond("eq")}};
    uint32_t word = encodeSingleInstr({mi});
    // kCset | (invertCond(0)=1 << 12) | Rd=0
    CHECK(word == (0x9A9F07E0u | (1u << 12) | 0u));
}

static void testCsel() {
    // csel x0, x1, x2, lt → 0x9A82B020
    MInstr mi{MOpcode::Csel, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2), cond("lt")}};
    uint32_t word = encodeSingleInstr({mi});
    // kCsel | (Rm=2 << 16) | (cc=0xB << 12) | (Rn=1 << 5) | Rd=0
    CHECK(word == (0x9A800000u | (2u << 16) | (0xBu << 12) | (1u << 5) | 0u));
}

static void testFAddRRR() {
    // fadd d0, d1, d2
    MInstr mi{MOpcode::FAddRRR, {fpr(PhysReg::V0), fpr(PhysReg::V1), fpr(PhysReg::V2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E602800u | (2u << 16) | (1u << 5) | 0u));
}

static void testFSubRRR() {
    // fsub d0, d1, d2
    MInstr mi{MOpcode::FSubRRR, {fpr(PhysReg::V0), fpr(PhysReg::V1), fpr(PhysReg::V2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E603800u | (2u << 16) | (1u << 5) | 0u));
}

static void testFCmpRR() {
    // fcmp d0, d1 (Rd=0)
    MInstr mi{MOpcode::FCmpRR, {fpr(PhysReg::V0), fpr(PhysReg::V1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E602000u | (1u << 16) | (0u << 5)));
}

static void testSCvtF() {
    // scvtf d0, x1
    MInstr mi{MOpcode::SCvtF, {fpr(PhysReg::V0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x9E620000u | (1u << 5) | 0u));
}

static void testFCvtZS() {
    // fcvtzs x0, d1
    MInstr mi{MOpcode::FCvtZS, {gpr(PhysReg::X0), fpr(PhysReg::V1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x9E780000u | (1u << 5) | 0u));
}

static void testRet() {
    // Leaf function with no frame → ret is just 0xD65F03C0.
    MFunction fn;
    fn.name = "leaf_ret";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI C (4 bytes) + Ret (4 bytes) = 8 bytes total
    CHECK(text.bytes().size() == 8);
    CHECK(readWord(text.bytes(), 0) == 0xD503245F); // BTI C
    CHECK(readWord(text.bytes(), 4) == 0xD65F03C0); // Ret
}

static void testBranchForwardBackward() {
    // Two blocks: entry branches to target, target branches back.
    MFunction fn;
    fn.name = "branch_test";
    fn.isLeaf = true;

    MBasicBlock entry;
    entry.name = "entry";
    entry.instrs.push_back(MInstr{MOpcode::Br, {label("target")}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock target;
    target.name = "target";
    target.instrs.push_back(MInstr{MOpcode::Br, {label("entry")}});
    fn.blocks.push_back(std::move(target));

    // No Ret needed — the branches form a loop.
    // Add Ret at the end of target to make it a valid function... but actually
    // the encoder doesn't require it. Let's just test with the loop.

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI C (4 bytes) + entry branch (4 bytes) + target branch (4 bytes) = 12 bytes
    CHECK(text.bytes().size() == 12);
    // Skip BTI at offset 0; entry at offset 4, target at offset 8
    uint32_t fwd = readWord(text.bytes(), 4);
    uint32_t bwd = readWord(text.bytes(), 8);

    // Forward: b target → +4 bytes → imm26 = 1
    CHECK(fwd == 0x14000001);
    // Backward: b entry → -4 bytes → imm26 = -1
    CHECK(bwd == (0x14000000u | (0x3FFFFFFu)));
}

static void testBCondForward() {
    MFunction fn;
    fn.name = "bcond_test";
    fn.isLeaf = true;

    MBasicBlock entry;
    entry.name = "entry";
    // b.eq target (cond=0x0, forward)
    entry.instrs.push_back(MInstr{MOpcode::BCond, {cond("eq"), label("target")}});
    // nop placeholder
    entry.instrs.push_back(MInstr{MOpcode::MovRR, {gpr(PhysReg::X0), gpr(PhysReg::X0)}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock target;
    target.name = "target";
    target.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI at offset 0; entry at offset 4 = b.eq; offset 8 = mov; target at offset 12 = ret
    // b.eq forward 8 bytes (2 instrs) → imm19=2
    uint32_t bcond = readWord(text.bytes(), 4);
    CHECK(bcond == 0x54000040);
}

static void testCbzForward() {
    MFunction fn;
    fn.name = "cbz_test";
    fn.isLeaf = true;

    MBasicBlock entry;
    entry.name = "entry";
    entry.instrs.push_back(MInstr{MOpcode::Cbz, {gpr(PhysReg::X0), label("target")}});
    entry.instrs.push_back(MInstr{MOpcode::MovRR, {gpr(PhysReg::X0), gpr(PhysReg::X0)}});
    fn.blocks.push_back(std::move(entry));

    MBasicBlock target;
    target.name = "target";
    target.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(target));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI at offset 0; cbz at offset 4
    uint32_t cbz = readWord(text.bytes(), 4);
    CHECK(cbz == 0xB4000040);
}

static void testFarConditionalBranchFallbacks() {
    constexpr size_t kBranchOffset = 4;
    constexpr size_t kFarTargetOffset = 1048584;

    const MInstr bcond{MOpcode::BCond, {cond("eq"), label("target")}};
    CHECK(A64BinaryEncoderTestAccess::measureWithKnownTarget(
              bcond, kBranchOffset, kFarTargetOffset) == 8);
    const auto bcondBytes =
        A64BinaryEncoderTestAccess::encodeWithKnownTarget(bcond, kBranchOffset, kFarTargetOffset);
    CHECK(readWord(bcondBytes, 4) == 0x54000041);
    CHECK(readWord(bcondBytes, 8) == 0x14040000);

    const MInstr cbz{MOpcode::Cbz, {gpr(PhysReg::X0), label("target")}};
    CHECK(A64BinaryEncoderTestAccess::measureWithKnownTarget(
              cbz, kBranchOffset, kFarTargetOffset) == 8);
    const auto cbzBytes =
        A64BinaryEncoderTestAccess::encodeWithKnownTarget(cbz, kBranchOffset, kFarTargetOffset);
    CHECK(readWord(cbzBytes, 4) == 0xB5000040);
    CHECK(readWord(cbzBytes, 8) == 0x14040000);

    const MInstr cbnz{MOpcode::Cbnz, {gpr(PhysReg::X0), label("target")}};
    CHECK(A64BinaryEncoderTestAccess::measureWithKnownTarget(
              cbnz, kBranchOffset, kFarTargetOffset) == 8);
    const auto cbnzBytes =
        A64BinaryEncoderTestAccess::encodeWithKnownTarget(cbnz, kBranchOffset, kFarTargetOffset);
    CHECK(readWord(cbnzBytes, 4) == 0xB4000040);
    CHECK(readWord(cbnzBytes, 8) == 0x14040000);
}

static void testExternalCall() {
    // bl to an external function should produce a relocation.
    MFunction fn;
    fn.name = "call_test";
    fn.isLeaf = true; // Still technically leaf in MIR...
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Bl, {label("rt_print_i64")}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI at offset 0; BL at offset 4
    uint32_t bl = readWord(text.bytes(), 4);
    CHECK(bl == 0x94000000);

    // Should have an A64Call26 relocation at offset 4 (after BTI).
    CHECK(text.relocations().size() >= 1);
    CHECK(text.relocations()[0].kind == RelocKind::A64Call26);
    CHECK(text.relocations()[0].offset == 4);
}

static void testPrologueEpilogue() {
    // Non-leaf function: should get stp x29,x30,[sp,#-16]! ; mov x29,sp ; ... ; ldp
    // x29,x30,[sp],#16 ; ret
    MFunction fn;
    fn.name = "nonleaf";
    fn.isLeaf = false; // has calls
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI C + PACIASP + stp x29,x30 + mov x29,sp + ldp x29,x30 + AUTIASP + ret = 7 instrs
    CHECK(text.bytes().size() == 28); // 7 * 4

    // Offset 0: BTI C
    CHECK(readWord(text.bytes(), 0) == 0xD503245F);
    // Offset 4: PACIASP
    CHECK(readWord(text.bytes(), 4) == 0xD503233F);

    // Offset 8: stp x29, x30, [sp, #-16]! (pre-indexed GPR pair)
    uint32_t stp = readWord(text.bytes(), 8);
    uint32_t expected_stp = 0xA9800000u | ((static_cast<uint32_t>(-2) & 0x7F) << 15) |
                            (hwGPR(PhysReg::X30) << 10) | (hwGPR(PhysReg::SP) << 5) |
                            hwGPR(PhysReg::X29);
    CHECK(stp == expected_stp);

    // Offset 12: mov x29, sp → add x29, sp, #0
    uint32_t mov = readWord(text.bytes(), 12);
    CHECK(mov == (0x91000000u | (0u << 10) | (hwGPR(PhysReg::SP) << 5) | hwGPR(PhysReg::X29)));

    // Offset 16: ldp x29, x30, [sp], #16 (post-indexed)
    uint32_t ldp = readWord(text.bytes(), 16);
    uint32_t expected_ldp = 0xA8C00000u | ((static_cast<uint32_t>(2) & 0x7F) << 15) |
                            (hwGPR(PhysReg::X30) << 10) | (hwGPR(PhysReg::SP) << 5) |
                            hwGPR(PhysReg::X29);
    CHECK(ldp == expected_ldp);

    // Offset 20: AUTIASP
    CHECK(readWord(text.bytes(), 20) == 0xD50323BF);
    // Offset 24: ret
    CHECK(readWord(text.bytes(), 24) == 0xD65F03C0);
}

static void testMainInit() {
    // The raw encoder must not inject runtime startup policy based on function name.
    MFunction fn;
    fn.name = "main";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    CHECK(text.bytes().size() == 8);
    CHECK(readWord(text.bytes(), 0) == 0xD503245F);
    CHECK(readWord(text.bytes(), 4) == 0xD65F03C0);

    size_t callRelocs = 0;
    for (const auto &r : text.relocations())
        if (r.kind == RelocKind::A64Call26)
            ++callRelocs;
    CHECK(callRelocs == 0);
}

static void testSubSpChunking() {
    // Large frame should produce multiple sub instructions.
    // 5000 = 4080 + 920, producing exactly 2 sub instructions.
    MFunction fn;
    fn.name = "large_frame";
    fn.isLeaf = false;
    fn.localFrameSize = 5000;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI + PACIASP + stp + mov + sub + sub + ...
    // offset 16: sub sp, sp, #1, lsl #12  (= 4096) — after BTI(0)+PACIASP(4)+stp(8)+mov(12)
    uint32_t sub1 = readWord(text.bytes(), 16);
    CHECK(sub1 == (0xD1400000u | (1u << 10) | (hwGPR(PhysReg::SP) << 5) | hwGPR(PhysReg::SP)));

    // offset 20: sub sp, sp, #904
    uint32_t sub2 = readWord(text.bytes(), 20);
    CHECK(sub2 == (0xD1000000u | (904u << 10) | (hwGPR(PhysReg::SP) << 5) | hwGPR(PhysReg::SP)));
}

static void testCalleeSavedPair() {
    // Function with 2 callee-saved GPRs should emit stp/ldp pair.
    MFunction fn;
    fn.name = "callee_saved";
    fn.isLeaf = false;
    fn.savedGPRs = {PhysReg::X19, PhysReg::X20};
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI + PACIASP + stp x29,x30 + mov x29,sp + stp x19,x20 +
    // ldp x19,x20 + ldp x29,x30 + AUTIASP + ret = 9 instrs = 36 bytes
    CHECK(text.bytes().size() == 36);

    // Fifth instruction (offset 16) = stp x19, x20, [sp, #-16]! (after BTI+PACIASP+stp+mov)
    uint32_t stp = readWord(text.bytes(), 16);
    uint32_t expected = 0xA9800000u | ((static_cast<uint32_t>(-2) & 0x7F) << 15) |
                        (hwGPR(PhysReg::X20) << 10) | (hwGPR(PhysReg::SP) << 5) |
                        hwGPR(PhysReg::X19);
    CHECK(stp == expected);
}

static void testCondCodeMapping() {
    // Test all condition code mappings.
    CHECK(condCode("eq") == 0x0);
    CHECK(condCode("ne") == 0x1);
    CHECK(condCode("hs") == 0x2);
    CHECK(condCode("cs") == 0x2);
    CHECK(condCode("lo") == 0x3);
    CHECK(condCode("cc") == 0x3);
    CHECK(condCode("mi") == 0x4);
    CHECK(condCode("pl") == 0x5);
    CHECK(condCode("vs") == 0x6);
    CHECK(condCode("vc") == 0x7);
    CHECK(condCode("hi") == 0x8);
    CHECK(condCode("ls") == 0x9);
    CHECK(condCode("ge") == 0xA);
    CHECK(condCode("lt") == 0xB);
    CHECK(condCode("gt") == 0xC);
    CHECK(condCode("le") == 0xD);
    CHECK(condCode("al") == 0xE);
    CHECK(condCode("nv") == 0xF);
}

static void testBlr() {
    // blr x8
    MInstr mi{MOpcode::Blr, {gpr(PhysReg::X8)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD63F0000u | (8u << 5)));
}

static void testAddsSubsRRR() {
    // adds x0, x1, x2
    MInstr mi1{MOpcode::AddsRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    CHECK(encodeSingleInstr({mi1}) == (0xAB000000u | (2u << 16) | (1u << 5) | 0u));

    // subs x0, x1, x2
    MInstr mi2{MOpcode::SubsRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    CHECK(encodeSingleInstr({mi2}) == (0xEB000000u | (2u << 16) | (1u << 5) | 0u));
}

static void testLdpStpRegFpImm() {
    // ldp x0, x1, [x29, #16]
    MInstr ldp{MOpcode::LdpRegFpImm, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(16)}};
    uint32_t w1 = encodeSingleInstr({ldp});
    // kLdpGpr | (imm7=16/8=2 << 15) | (Rt2=1 << 10) | (Rn=29 << 5) | Rt=0
    CHECK(w1 == (0xA9400000u | (2u << 15) | (1u << 10) | (29u << 5) | 0u));

    // stp x0, x1, [x29, #-16]
    MInstr stp{MOpcode::StpRegFpImm, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(-16)}};
    uint32_t w2 = encodeSingleInstr({stp});
    // imm7 = -16/8 = -2 → 0x7E
    CHECK(w2 == (0xA9000000u | ((static_cast<uint32_t>(-2) & 0x7F) << 15) | (1u << 10) |
                 (29u << 5) | 0u));
}

static void testVariableShift() {
    // lslv x0, x1, x2
    MInstr mi{MOpcode::LslvRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    CHECK(encodeSingleInstr({mi}) == (0x9AC02000u | (2u << 16) | (1u << 5) | 0u));
}

static void testHighRegisters() {
    // add x28, x19, x20 — tests registers with 5-bit encoding > 15.
    MInstr mi{MOpcode::AddRRR, {gpr(PhysReg::X28), gpr(PhysReg::X19), gpr(PhysReg::X20)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x8B000000u | (20u << 16) | (19u << 5) | 28u));
}

static void testFMovRR() {
    // fmov d0, d1
    MInstr mi{MOpcode::FMovRR, {fpr(PhysReg::V0), fpr(PhysReg::V1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E604000u | (1u << 5) | 0u));
}

static void testHighFPR() {
    // fadd d16, d17, d18 — tests FPR encoding with values > 15.
    MInstr mi{MOpcode::FAddRRR, {fpr(PhysReg::V16), fpr(PhysReg::V17), fpr(PhysReg::V18)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E602800u | (18u << 16) | (17u << 5) | 16u));
}

static void testStrRegSpImm() {
    // str x0, [sp, #16] — scaled unsigned offset, offset/8 = 2
    MInstr mi{MOpcode::StrRegSpImm, {gpr(PhysReg::X0), imm(16)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xF9000000u | (2u << 10) | (hwGPR(PhysReg::SP) << 5) | 0u));
}

static void testAdrpAddPageOff() {
    // adrp x0, sym → relocation
    // add x0, x0, sym@PAGEOFF → relocation
    MFunction fn;
    fn.name = "adrp_test";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::AdrPage, {gpr(PhysReg::X0), label("my_global")}});
    bb.instrs.push_back(
        MInstr{MOpcode::AddPageOff, {gpr(PhysReg::X0), gpr(PhysReg::X0), label("my_global")}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    rodata.defineSymbol("my_global", SymbolBinding::Local, SymbolSection::Rodata);
    rodata.emit8(0);
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // BTI at offset 0; adrp at offset 4; add at offset 8
    CHECK(readWord(text.bytes(), 4) == 0x90000000);
    CHECK(readWord(text.bytes(), 8) == (0x91000000u | (0u << 10) | (0u << 5) | 0u));

    // Should have A64AdrpPage21 and A64AddPageOff12 relocations.
    bool hasAdrp = false, hasAdd = false;
    for (const auto &r : text.relocations()) {
        if (r.kind == RelocKind::A64AdrpPage21) {
            hasAdrp = true;
            CHECK(r.targetSection == SymbolSection::Rodata);
        }
        if (r.kind == RelocKind::A64AddPageOff12) {
            hasAdd = true;
            CHECK(r.targetSection == SymbolSection::Rodata);
        }
    }
    CHECK(hasAdrp);
    CHECK(hasAdd);
}

static void testSymbolDefined() {
    // The function symbol should be defined in the text section's symbol table.
    MFunction fn;
    fn.name = "my_func";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // Check that "my_func" appears in the symbol table.
    bool found = false;
    for (uint32_t i = 0; i < text.symbols().count(); ++i) {
        if (text.symbols().at(i).name == "my_func") {
            found = true;
            CHECK(text.symbols().at(i).binding == SymbolBinding::Global);
            break;
        }
    }
    CHECK(found);
}

// =============================================================================
// Optimization tests
// =============================================================================

static void testAndRI_logicalImm() {
    // AND X0, X1, #0xFF → single instruction (logical immediate encodable).
    // enc(0xFF) = N=1, immr=0, imms=7 → 0x1007.
    // kAndImm | (0x1007 << 10) | (X1 << 5) | X0 = 0x92401C20
    MInstr mi{MOpcode::AndRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(0xFF)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0x92401C20);
}

static void testOrrRI_logicalImm() {
    // ORR X0, X1, #0xFF → kOrrImm | (0x1007 << 10) | (X1 << 5) | X0 = 0xB2401C20
    MInstr mi{MOpcode::OrrRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(0xFF)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0xB2401C20);
}

static void testAndRI_nonEncodable() {
    // AND X0, X1, #3 — value 3 (0b11) IS encodable as logical immediate.
    // For non-encodable, we'd need something like 5 (0b101) which isn't a contiguous run.
    // Test with 5: not encodable → fallback (MOVZ + AND, 2+ words).
    MInstr mi{MOpcode::AndRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(5)}};
    CHECK(countWords({mi}) > 1);
}

static void testMovRI_highOnly() {
    // MovRI with 0x0000000100000000 → goes through encodeMovImm64.
    // Smart MOVZ start: chunks = [0, 0, 1, 0], first=2.
    // MOVZ X0, #1, lsl #32 → kMovZ32 | (1 << 5) | 0 = 0xD2C00020. Single instruction.
    MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(0x100000000LL)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0xD2C00020u);
}

static void testMovRI_movnPath() {
    // MovRI with 0xFFFFFFFF00010000 → needs wide (not simple mov).
    // chunks = [0, 1, 0xFFFF, 0xFFFF], nzCount = 3.
    // invChunks = [0xFFFF, 0xFFFE, 0, 0], invNzCount = 2.
    // useMovn = (2 < 3) = true. src = invChunks, first = 0 (0xFFFF).
    // MOVN X0, #0xFFFF → then check chunks: [1]=1 (!=0xFFFF) → MOVK.
    // 2 instructions total (saved from 3 on MOVZ path).
    MInstr mi{MOpcode::MovRI,
              {gpr(PhysReg::X0), imm(static_cast<long long>(0xFFFFFFFF00010000ULL))}};
    CHECK(countWords({mi}) == 2);
}

static void testMovRI_zero() {
    // MovRI with 0 → simple mov path: MOVZ X0, #0 (single instruction).
    MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(0)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0xD2800000u);
}

static void testMovRI_negativeSimple() {
    // Negative values in [-65536, -1] must use MOVN, not MOVZ.
    // MOVN Xd, #(~imm & 0xFFFF) produces the correct sign-extended value.

    // -1: ~(-1)=0, MOVN X0, #0 → 0x92800000
    {
        MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(-1)}};
        CHECK(countWords({mi}) == 1);
        uint32_t word = encodeSingleInstr({mi});
        CHECK(word == 0x92800000u); // kMovN | (#0 << 5) | X0
    }
    // -1500 (PLAYER_JUMP): ~(-1500)=0x5DB, MOVN X0, #0x5DB → 0x9280BB60
    {
        MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(-1500)}};
        CHECK(countWords({mi}) == 1);
        uint32_t word = encodeSingleInstr({mi});
        CHECK(word == (0x92800000u | (0x5DBu << 5) | 0u));
    }
    // -65536: ~(-65536)=0xFFFF, MOVN X0, #0xFFFF → 0x929FFFE0
    {
        MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(-65536)}};
        CHECK(countWords({mi}) == 1);
        uint32_t word = encodeSingleInstr({mi});
        CHECK(word == (0x92800000u | (0xFFFFu << 5) | 0u));
    }
}

static void testFMovRI_fp8() {
    // FMOV D0, #1.0 → single instruction via FP8 encoding.
    // 1.0: sign=0, exp=0, frac=0 → fp8 = (0<<7)|(1<<6)|(3<<4)|0 = 0x70.
    // kFMovDImm | (0x70 << 13) | D0 = 0x1E601000 | 0xE0000 = 0x1E6E1000
    double val = 1.0;
    int64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    MInstr mi{MOpcode::FMovRI, {fpr(PhysReg::V0), imm(bits)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0x1E6E1000);
}

static void testFMovRI_fallback() {
    // FMOV D0, #0.3 → NOT FP8 encodable → MOVZ+MOVK+FMOV (multi-word).
    double val = 0.3;
    int64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    MInstr mi{MOpcode::FMovRI, {fpr(PhysReg::V0), imm(bits)}};
    CHECK(countWords({mi}) > 1);
}

static void testAddRI_shift12() {
    MInstr mi{MOpcode::AddRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(4096)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == encodeAddSubImmShift(kAddRI, hwGPR(PhysReg::X0), hwGPR(PhysReg::X1), 1));
}

static void testStrRegSpImm_largeOffsetFallback() {
    const std::vector<uint8_t> bytes =
        encodeInstrBytes({MInstr{MOpcode::StrRegSpImm, {gpr(PhysReg::X0), imm(40000)}}});
    // BTI + mov scratch,sp + chunked add + str + ret
    CHECK((bytes.size() / 4) > 4);
}

static void testAddRI_rejectsNegativeImmediate() {
    MFunction fn;
    fn.name = "bad_add_imm";
    fn.isLeaf = true;

    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::AddRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(-1)}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;

    bool threw = false;
    try {
        enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);
    } catch (const std::runtime_error &ex) {
        threw = std::string(ex.what()).find("without sign normalization") != std::string::npos;
    }
    CHECK(threw);
}

static void testBranchRejectsMissingTarget() {
    MFunction fn;
    fn.name = "missing_target";
    fn.isLeaf = true;

    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Br, {label("missing")}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;

    bool threw = false;
    try {
        enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);
    } catch (const std::runtime_error &ex) {
        threw = std::string(ex.what()).find("unresolved internal branch target 'missing'") !=
                std::string::npos;
    }
    CHECK(threw);
}

int main() {
    testAddRRR();
    testSubRRR();
    testAndRRR();
    testOrrRRR();
    testEorRRR();
    testMulRRR();
    testSDivRRR();
    testMSubRRRR();
    testAddRI();
    testSubRI();
    testMovRR();
    testMovRI_small();
    testLslRI();
    testLsrRI();
    testAsrRI();
    testCmpRR();
    testCmpRI();
    testTstRR();
    testCset();
    testCsel();
    testFAddRRR();
    testFSubRRR();
    testFCmpRR();
    testSCvtF();
    testFCvtZS();
    testRet();
    testBranchForwardBackward();
    testBCondForward();
    testCbzForward();
    testFarConditionalBranchFallbacks();
    testExternalCall();
    testPrologueEpilogue();
    testMainInit();
    testSubSpChunking();
    testCalleeSavedPair();
    testCondCodeMapping();
    testBlr();
    testAddsSubsRRR();
    testLdpStpRegFpImm();
    testVariableShift();
    testHighRegisters();
    testFMovRR();
    testHighFPR();
    testStrRegSpImm();
    testAdrpAddPageOff();
    testSymbolDefined();
    testAndRI_logicalImm();
    testOrrRI_logicalImm();
    testAndRI_nonEncodable();
    testMovRI_highOnly();
    testMovRI_movnPath();
    testMovRI_zero();
    testMovRI_negativeSimple();
    testFMovRI_fp8();
    testFMovRI_fallback();
    testAddRI_shift12();
    testStrRegSpImm_largeOffsetFallback();
    testAddRI_rejectsNegativeImmediate();
    testBranchRejectsMissingTarget();

    // --- Encoding coverage validation (W7 remediation) ---
    // Verify that every non-pseudo MOpcode can be encoded without crashing.
    // This catches the asymmetry where new opcodes get added to MachineIR.hpp
    // but are forgotten in the encoder's switch statement.
    {
        // Pseudo-opcodes that assert in the encoder (expanded before encoding).
        auto isPseudo = [](MOpcode opc) {
            return opc == MOpcode::AddOvfRRR || opc == MOpcode::SubOvfRRR ||
                   opc == MOpcode::AddOvfRI || opc == MOpcode::SubOvfRI ||
                   opc == MOpcode::MulOvfRRR;
        };

        // Build a minimal valid instruction for each opcode category.
        auto makeTestInstr = [&](MOpcode opc) -> MInstr {
            const auto x0 = gpr(PhysReg::X0);
            const auto x1 = gpr(PhysReg::X1);
            const auto x2 = gpr(PhysReg::X2);
            const auto x3 = gpr(PhysReg::X3);
            const auto d0 = fpr(PhysReg::V0);
            const auto d1 = fpr(PhysReg::V1);
            const auto d2 = fpr(PhysReg::V2);

            switch (opc) {
                // 2-reg GPR
                case MOpcode::MovRR:
                case MOpcode::CmpRR:
                case MOpcode::TstRR:
                    return MInstr{opc, {x0, x1}};

                // MovRI: reg + imm
                case MOpcode::MovRI:
                case MOpcode::AddFpImm:
                    return MInstr{opc, {x0, imm(0)}};

                // 3-reg GPR (dst, lhs, rhs)
                case MOpcode::AddRRR:
                case MOpcode::SubRRR:
                case MOpcode::MulRRR:
                case MOpcode::SmulhRRR:
                case MOpcode::SDivRRR:
                case MOpcode::UDivRRR:
                case MOpcode::AndRRR:
                case MOpcode::OrrRRR:
                case MOpcode::EorRRR:
                case MOpcode::AddsRRR:
                case MOpcode::SubsRRR:
                case MOpcode::LslvRRR:
                case MOpcode::LsrvRRR:
                case MOpcode::AsrvRRR:
                    return MInstr{opc, {x0, x1, x2}};

                // 4-reg GPR (dst, mul1, mul2, add/sub)
                case MOpcode::MSubRRRR:
                case MOpcode::MAddRRRR:
                    return MInstr{opc, {x0, x1, x2, x3}};

                // reg-imm arithmetic (3 operands: dst, src, imm)
                case MOpcode::AddRI:
                case MOpcode::SubRI:
                case MOpcode::AddsRI:
                case MOpcode::SubsRI:
                case MOpcode::LslRI:
                case MOpcode::LsrRI:
                case MOpcode::AsrRI:
                    return MInstr{opc, {x0, x1, imm(1)}};

                // CmpRI: 2 operands (reg, imm) — implicit XZR dest
                case MOpcode::CmpRI:
                    return MInstr{opc, {x0, imm(1)}};

                // Logical immediate (use 0xFFFF — encodable as bitmask)
                case MOpcode::AndRI:
                case MOpcode::OrrRI:
                case MOpcode::EorRI:
                    return MInstr{opc, {x0, x1, imm(0xFFFF)}};

                // Cset: dst, cond
                case MOpcode::Cset:
                    return MInstr{opc, {x0, cond("eq")}};

                // Csel: dst, trueReg, falseReg, cond
                case MOpcode::Csel:
                    return MInstr{opc, {x0, x1, x2, cond("eq")}};

                // Branches
                case MOpcode::Br:
                    return MInstr{opc, {label("target")}};
                case MOpcode::BCond:
                    return MInstr{opc, {cond("eq"), label("target")}};
                case MOpcode::Cbz:
                case MOpcode::Cbnz:
                    return MInstr{opc, {x0, label("target")}};
                case MOpcode::Bl:
                    return MInstr{opc, {label("target")}};
                case MOpcode::Blr:
                    return MInstr{opc, {x0}};
                case MOpcode::Ret:
                    return MInstr{opc, {}};

                // SP adjustment
                case MOpcode::SubSpImm:
                case MOpcode::AddSpImm:
                    return MInstr{opc, {imm(16)}};

                // FP-relative load/store
                case MOpcode::LdrRegFpImm:
                case MOpcode::StrRegFpImm:
                case MOpcode::PhiStoreGPR:
                    return MInstr{opc, {x0, imm(0)}};
                case MOpcode::LdrFprFpImm:
                case MOpcode::StrFprFpImm:
                case MOpcode::PhiStoreFPR:
                    return MInstr{opc, {d0, imm(0)}};

                // Base-register load/store
                case MOpcode::LdrRegBaseImm:
                case MOpcode::StrRegBaseImm:
                    return MInstr{opc, {x0, x1, imm(0)}};
                case MOpcode::LdrFprBaseImm:
                case MOpcode::StrFprBaseImm:
                    return MInstr{opc, {d0, x1, imm(0)}};

                // SP-relative store (for outgoing args)
                case MOpcode::StrRegSpImm:
                    return MInstr{opc, {x0, imm(0)}};
                case MOpcode::StrFprSpImm:
                    return MInstr{opc, {d0, imm(0)}};

                // Pair load/store (reg1, reg2, offset)
                case MOpcode::LdpRegFpImm:
                case MOpcode::StpRegFpImm:
                    return MInstr{opc, {x0, x1, imm(0)}};
                case MOpcode::LdpFprFpImm:
                case MOpcode::StpFprFpImm:
                    return MInstr{opc, {d0, d1, imm(0)}};

                // Address materialisation
                case MOpcode::AdrPage:
                    return MInstr{opc, {x0, label("sym")}};
                case MOpcode::AddPageOff:
                    return MInstr{opc, {x0, x1, label("sym")}};

                // FP 2-reg
                case MOpcode::FMovRR:
                case MOpcode::FCmpRR:
                    return MInstr{opc, {d0, d1}};
                case MOpcode::FMovGR:
                    return MInstr{opc, {d0, x0}};
                case MOpcode::FMovRI:
                    return MInstr{opc, {d0, imm(0)}};
                case MOpcode::FRintN:
                    return MInstr{opc, {d0, d1}};

                // FP 3-reg
                case MOpcode::FAddRRR:
                case MOpcode::FSubRRR:
                case MOpcode::FMulRRR:
                case MOpcode::FDivRRR:
                    return MInstr{opc, {d0, d1, d2}};

                // Conversions
                case MOpcode::SCvtF:
                case MOpcode::UCvtF:
                    return MInstr{opc, {d0, x0}};
                case MOpcode::FCvtZS:
                case MOpcode::FCvtZU:
                    return MInstr{opc, {x0, d0}};

                // Pseudo-opcodes (should never reach encoder)
                default:
                    return MInstr{opc, {}};
            }
        };

        // Iterate all opcodes from MovRR (0) to MulOvfRRR (last).
        constexpr int kFirstOpcode = static_cast<int>(MOpcode::MovRR);
        constexpr int kLastOpcode = static_cast<int>(MOpcode::MulOvfRRR);
        int encodedCount = 0;
        int pseudoCount = 0;

        for (int i = kFirstOpcode; i <= kLastOpcode; ++i) {
            const auto opc = static_cast<MOpcode>(i);
            if (isPseudo(opc)) {
                ++pseudoCount;
                continue;
            }

            MInstr mi = makeTestInstr(opc);

            // Encode in a leaf function — should not crash.
            MFunction fn;
            fn.name = "coverage_test";
            fn.isLeaf = true;
            MBasicBlock bb;
            bb.name = "entry";
            bb.instrs.push_back(std::move(mi));
            bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
            fn.blocks.push_back(std::move(bb));

            // Add a target block for branch instructions so labels resolve.
            MBasicBlock target;
            target.name = "target";
            target.instrs.push_back(MInstr{MOpcode::Ret, {}});
            fn.blocks.push_back(std::move(target));

            // Add a "sym" block for address materialisation opcodes.
            MBasicBlock sym;
            sym.name = "sym";
            sym.instrs.push_back(MInstr{MOpcode::Ret, {}});
            fn.blocks.push_back(std::move(sym));

            CodeSection text, rodata;
            A64BinaryEncoder enc;
            enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

            // Must have produced at least 8 bytes (test instr + ret).
            if (text.bytes().size() < 8) {
                std::cerr << "FAIL: opcode " << opcodeName(opc) << " produced only "
                          << text.bytes().size() << " bytes\n";
                ++gFail;
            } else {
                ++encodedCount;
            }
        }

        // Verify we covered the expected counts.
        CHECK(pseudoCount == 5);   // 5 pseudo-opcodes
        CHECK(encodedCount == 74); // 79 total - 5 pseudo = 74 real opcodes

        if (encodedCount == 74 && pseudoCount == 5)
            std::cout << "  Encoding coverage: " << encodedCount << "/74 opcodes OK, "
                      << pseudoCount << " pseudo-opcodes skipped.\n";
    }

    if (gFail == 0)
        std::cout << "All A64 binary encoder tests passed.\n";
    else
        std::cerr << gFail << " test(s) FAILED.\n";
    return gFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
