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

#include "codegen/aarch64/binenc/A64BinaryEncoder.hpp"
#include "codegen/aarch64/binenc/A64Encoding.hpp"
#include "codegen/common/objfile/CodeSection.hpp"
#include "codegen/aarch64/MachineIR.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace viper::codegen::aarch64;
using namespace viper::codegen::aarch64::binenc;
using namespace viper::codegen::objfile;

static int gFail = 0;

static void check(bool cond, const char *msg, int line)
{
    if (!cond)
    {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// Helper: read a 32-bit LE word from a byte vector at offset.
static uint32_t readWord(const std::vector<uint8_t> &bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

// Helper: create a physical GPR register operand.
static MOperand gpr(PhysReg r) { return MOperand::regOp(r); }

// Helper: create a physical FPR register operand.
static MOperand fpr(PhysReg r) { return MOperand::regOp(r); }

// Helper: create an immediate operand.
static MOperand imm(long long val) { return MOperand::immOp(val); }

// Helper: create a condition code operand.
static MOperand cond(const char *c) { return MOperand::condOp(c); }

// Helper: create a label operand.
static MOperand label(const std::string &name) { return MOperand::labelOp(name); }

// Encode a single-block leaf function with given instructions.
// Returns the word at instruction index `idx` (skipping any prologue).
static uint32_t encodeSingleInstr(const std::vector<MInstr> &instrs, size_t idx = 0)
{
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
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // Leaf function with no saved regs and no frame → no prologue.
    // First word should be instruction 0.
    return readWord(text.bytes(), idx * 4);
}

// Encode a single-block leaf function and return all bytes (no prologue for leaf).
static std::vector<uint8_t> encodeInstrBytes(const std::vector<MInstr> &instrs)
{
    MFunction fn;
    fn.name = "test_func";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs = instrs;
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);
    return text.bytes();
}

// Count how many 4-byte words are emitted for a set of instructions
// (excluding the trailing Ret).
static size_t countWords(const std::vector<MInstr> &instrs)
{
    auto bytes = encodeInstrBytes(instrs);
    return (bytes.size() / 4) - 1; // Subtract 1 for the Ret.
}

// =============================================================================
// Tests
// =============================================================================

static void testAddRRR()
{
    // add x0, x1, x2 → 0x8B020020
    MInstr mi{MOpcode::AddRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x8B020020);
}

static void testSubRRR()
{
    // sub x3, x4, x5 → 0xCB050083
    MInstr mi{MOpcode::SubRRR, {gpr(PhysReg::X3), gpr(PhysReg::X4), gpr(PhysReg::X5)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0xCB050083);
}

static void testAndRRR()
{
    // and x0, x1, x2 → 0x8A020020
    MInstr mi{MOpcode::AndRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x8A020020);
}

static void testOrrRRR()
{
    // orr x0, x1, x2 → 0xAA020020
    MInstr mi{MOpcode::OrrRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0xAA020020);
}

static void testEorRRR()
{
    // eor x0, x1, x2 → 0xCA020020
    MInstr mi{MOpcode::EorRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0xCA020020);
}

static void testMulRRR()
{
    // mul x0, x1, x2 → madd x0, x1, x2, xzr → 0x9B027C20
    MInstr mi{MOpcode::MulRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x9B027C20);
}

static void testSDivRRR()
{
    // sdiv x0, x1, x2 → 0x9AC20C20
    MInstr mi{MOpcode::SDivRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == 0x9AC20C20);
}

static void testMSubRRRR()
{
    // msub x0, x1, x2, x3 → 0x9B028060
    MInstr mi{MOpcode::MSubRRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2), gpr(PhysReg::X3)}};
    uint32_t word = encodeSingleInstr({mi});
    // template 0x9B008000 | (Rm=2 << 16) | (Ra=3 << 10) | (Rn=1 << 5) | Rd=0
    // = 0x9B008000 | 0x00020000 | 0x00000C00 | 0x00000020 | 0
    CHECK(word == (0x9B008000u | (2u << 16) | (3u << 10) | (1u << 5) | 0u));
}

static void testAddRI()
{
    // add x0, x1, #42 → 0x9100A820
    MInstr mi{MOpcode::AddRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(42)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x91000000u | (42u << 10) | (1u << 5) | 0u));
}

static void testSubRI()
{
    // sub x3, x4, #100 → template | (100 << 10) | (4 << 5) | 3
    MInstr mi{MOpcode::SubRI, {gpr(PhysReg::X3), gpr(PhysReg::X4), imm(100)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD1000000u | (100u << 10) | (4u << 5) | 3u));
}

static void testMovRR()
{
    // mov x0, x1 → orr x0, xzr, x1 → 0xAA0103E0
    MInstr mi{MOpcode::MovRR, {gpr(PhysReg::X0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xAA0003E0u | (1u << 16) | 0u));
}

static void testMovRI_small()
{
    // movz x0, #0x1234 → 0xD2824680
    MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(0x1234)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD2800000u | (0x1234u << 5) | 0u));
}

static void testLslRI()
{
    // lsl x0, x1, #4 → ubfm x0, x1, #60, #59
    MInstr mi{MOpcode::LslRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(4)}};
    uint32_t word = encodeSingleInstr({mi});
    uint32_t immr = (64 - 4) & 63; // 60
    uint32_t imms = 63 - 4;         // 59
    CHECK(word == (0xD3400000u | (immr << 16) | (imms << 10) | (1u << 5) | 0u));
}

static void testLsrRI()
{
    // lsr x0, x1, #8 → ubfm x0, x1, #8, #63
    MInstr mi{MOpcode::LsrRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(8)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD3400000u | (8u << 16) | (63u << 10) | (1u << 5) | 0u));
}

static void testAsrRI()
{
    // asr x0, x1, #8 → sbfm x0, x1, #8, #63
    MInstr mi{MOpcode::AsrRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(8)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x93400000u | (8u << 16) | (63u << 10) | (1u << 5) | 0u));
}

static void testCmpRR()
{
    // cmp x0, x1 → subs xzr, x0, x1
    MInstr mi{MOpcode::CmpRR, {gpr(PhysReg::X0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    // kSubsRRR | (Rm=1 << 16) | (Rn=0 << 5) | Rd=31
    CHECK(word == (0xEB000000u | (1u << 16) | (0u << 5) | 31u));
}

static void testCmpRI()
{
    // cmp x0, #42 → subs xzr, x0, #42
    MInstr mi{MOpcode::CmpRI, {gpr(PhysReg::X0), imm(42)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xF1000000u | (42u << 10) | (0u << 5) | 31u));
}

static void testTstRR()
{
    // tst x0, x1 → ands xzr, x0, x1
    MInstr mi{MOpcode::TstRR, {gpr(PhysReg::X0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xEA000000u | (1u << 16) | (0u << 5) | 31u));
}

static void testCset()
{
    // cset x0, eq → csinc x0, xzr, xzr, ne (inverted: eq=0 → ne=1)
    MInstr mi{MOpcode::Cset, {gpr(PhysReg::X0), cond("eq")}};
    uint32_t word = encodeSingleInstr({mi});
    // kCset | (invertCond(0)=1 << 12) | Rd=0
    CHECK(word == (0x9A9F07E0u | (1u << 12) | 0u));
}

static void testCsel()
{
    // csel x0, x1, x2, lt → 0x9A82B020
    MInstr mi{MOpcode::Csel, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2), cond("lt")}};
    uint32_t word = encodeSingleInstr({mi});
    // kCsel | (Rm=2 << 16) | (cc=0xB << 12) | (Rn=1 << 5) | Rd=0
    CHECK(word == (0x9A800000u | (2u << 16) | (0xBu << 12) | (1u << 5) | 0u));
}

static void testFAddRRR()
{
    // fadd d0, d1, d2
    MInstr mi{MOpcode::FAddRRR, {fpr(PhysReg::V0), fpr(PhysReg::V1), fpr(PhysReg::V2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E602800u | (2u << 16) | (1u << 5) | 0u));
}

static void testFSubRRR()
{
    // fsub d0, d1, d2
    MInstr mi{MOpcode::FSubRRR, {fpr(PhysReg::V0), fpr(PhysReg::V1), fpr(PhysReg::V2)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E603800u | (2u << 16) | (1u << 5) | 0u));
}

static void testFCmpRR()
{
    // fcmp d0, d1 (Rd=0)
    MInstr mi{MOpcode::FCmpRR, {fpr(PhysReg::V0), fpr(PhysReg::V1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E602000u | (1u << 16) | (0u << 5)));
}

static void testSCvtF()
{
    // scvtf d0, x1
    MInstr mi{MOpcode::SCvtF, {fpr(PhysReg::V0), gpr(PhysReg::X1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x9E620000u | (1u << 5) | 0u));
}

static void testFCvtZS()
{
    // fcvtzs x0, d1
    MInstr mi{MOpcode::FCvtZS, {gpr(PhysReg::X0), fpr(PhysReg::V1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x9E780000u | (1u << 5) | 0u));
}

static void testRet()
{
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

    CHECK(text.bytes().size() == 4);
    CHECK(readWord(text.bytes(), 0) == 0xD65F03C0);
}

static void testBranchForwardBackward()
{
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

    // entry is at offset 0: b target → forward by 4 bytes → imm26 = 1
    // target is at offset 4: b entry → backward by 4 bytes → imm26 = -1
    CHECK(text.bytes().size() == 8);
    uint32_t fwd = readWord(text.bytes(), 0);
    uint32_t bwd = readWord(text.bytes(), 4);

    // Forward: kBr | (1 & 0x3FFFFFF) = 0x14000001
    CHECK(fwd == 0x14000001);
    // Backward: kBr | (-1 & 0x3FFFFFF) = 0x14000000 | 0x03FFFFFF = 0x17FFFFFF
    CHECK(bwd == (0x14000000u | (0x3FFFFFFu)));
}

static void testBCondForward()
{
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

    // entry: offset 0 = b.eq (forward 8 bytes = 2 instrs, imm19=2)
    //        offset 4 = mov x0, x0
    // target: offset 8 = ret
    uint32_t bcond = readWord(text.bytes(), 0);
    // kBCond | (imm19=2 << 5) | cc=0 → 0x54000000 | (2 << 5) | 0 = 0x54000040
    CHECK(bcond == 0x54000040);
}

static void testCbzForward()
{
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

    uint32_t cbz = readWord(text.bytes(), 0);
    // kCbz | (imm19=2 << 5) | Rt=0 → 0xB4000000 | (2 << 5) | 0 = 0xB4000040
    CHECK(cbz == 0xB4000040);
}

static void testExternalCall()
{
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

    // BL with relocation: instruction word = kBl with imm26=0 → 0x94000000
    uint32_t bl = readWord(text.bytes(), 0);
    CHECK(bl == 0x94000000);

    // Should have an A64Call26 relocation at offset 0.
    CHECK(text.relocations().size() >= 1);
    CHECK(text.relocations()[0].kind == RelocKind::A64Call26);
    CHECK(text.relocations()[0].offset == 0);
}

static void testPrologueEpilogue()
{
    // Non-leaf function: should get stp x29,x30,[sp,#-16]! ; mov x29,sp ; ... ; ldp x29,x30,[sp],#16 ; ret
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

    // Prologue: stp x29, x30, [sp, #-16]! then mov x29, sp
    // Epilogue: ldp x29, x30, [sp], #16 then ret
    CHECK(text.bytes().size() == 16); // 4 instructions

    // stp x29, x30, [sp, #-16]! (pre-indexed GPR pair)
    // imm7 = -16/8 = -2 → signed 7-bit → 0x7E
    uint32_t stp = readWord(text.bytes(), 0);
    uint32_t expected_stp = 0xA9800000u |
        ((static_cast<uint32_t>(-2) & 0x7F) << 15) |
        (hwGPR(PhysReg::X30) << 10) |
        (hwGPR(PhysReg::SP) << 5) |
        hwGPR(PhysReg::X29);
    CHECK(stp == expected_stp);

    // mov x29, sp → add x29, sp, #0
    uint32_t mov = readWord(text.bytes(), 4);
    CHECK(mov == (0x91000000u | (0u << 10) | (hwGPR(PhysReg::SP) << 5) | hwGPR(PhysReg::X29)));

    // ldp x29, x30, [sp], #16 (post-indexed)
    uint32_t ldp = readWord(text.bytes(), 8);
    uint32_t expected_ldp = 0xA8C00000u |
        ((static_cast<uint32_t>(2) & 0x7F) << 15) |
        (hwGPR(PhysReg::X30) << 10) |
        (hwGPR(PhysReg::SP) << 5) |
        hwGPR(PhysReg::X29);
    CHECK(ldp == expected_ldp);

    // ret
    CHECK(readWord(text.bytes(), 12) == 0xD65F03C0);
}

static void testMainInit()
{
    // main function should get two bl calls to runtime init after prologue.
    MFunction fn;
    fn.name = "main";
    fn.isLeaf = false;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // Prologue (2 instr) + 2 bl runtime init + epilogue (2 instr) + ret (included in epilogue) = 6
    // Wait — epilogue includes ldp + ret = 2 instrs, so total = 2 + 2 + 2 = 6
    CHECK(text.bytes().size() == 24); // 6 * 4 = 24

    // Instructions at offset 8 and 12 should be BL (0x94000000)
    CHECK(readWord(text.bytes(), 8) == 0x94000000);
    CHECK(readWord(text.bytes(), 12) == 0x94000000);

    // Should have at least 2 A64Call26 relocations for the runtime init calls.
    size_t callRelocs = 0;
    for (const auto &r : text.relocations())
        if (r.kind == RelocKind::A64Call26)
            ++callRelocs;
    CHECK(callRelocs >= 2);
}

static void testSubSpChunking()
{
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

    // Prologue: stp x29,x30,[sp,#-16]! | mov x29,sp | sub sp,sp,#1,lsl#12 | sub sp,sp,#904
    // offset 8: sub sp, sp, #1, lsl #12  (= 4096)
    uint32_t sub1 = readWord(text.bytes(), 8);
    CHECK(sub1 == (0xD1400000u | (1u << 10) | (hwGPR(PhysReg::SP) << 5) | hwGPR(PhysReg::SP)));

    // offset 12: sub sp, sp, #904
    uint32_t sub2 = readWord(text.bytes(), 12);
    CHECK(sub2 == (0xD1000000u | (904u << 10) | (hwGPR(PhysReg::SP) << 5) | hwGPR(PhysReg::SP)));
}

static void testCalleeSavedPair()
{
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

    // Prologue: stp x29,x30,[sp,#-16]!  mov x29,sp  stp x19,x20,[sp,#-16]!
    // Epilogue: ldp x19,x20,[sp],#16  ldp x29,x30,[sp],#16  ret
    // Total: 6 instructions = 24 bytes
    CHECK(text.bytes().size() == 24);

    // Third instruction (offset 8) = stp x19, x20, [sp, #-16]! (pre-indexed)
    uint32_t stp = readWord(text.bytes(), 8);
    uint32_t expected = 0xA9800000u |
        ((static_cast<uint32_t>(-2) & 0x7F) << 15) |
        (hwGPR(PhysReg::X20) << 10) |
        (hwGPR(PhysReg::SP) << 5) |
        hwGPR(PhysReg::X19);
    CHECK(stp == expected);
}

static void testCondCodeMapping()
{
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

static void testBlr()
{
    // blr x8
    MInstr mi{MOpcode::Blr, {gpr(PhysReg::X8)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xD63F0000u | (8u << 5)));
}

static void testAddsSubsRRR()
{
    // adds x0, x1, x2
    MInstr mi1{MOpcode::AddsRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    CHECK(encodeSingleInstr({mi1}) == (0xAB000000u | (2u << 16) | (1u << 5) | 0u));

    // subs x0, x1, x2
    MInstr mi2{MOpcode::SubsRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    CHECK(encodeSingleInstr({mi2}) == (0xEB000000u | (2u << 16) | (1u << 5) | 0u));
}

static void testLdpStpRegFpImm()
{
    // ldp x0, x1, [x29, #16]
    MInstr ldp{MOpcode::LdpRegFpImm, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(16)}};
    uint32_t w1 = encodeSingleInstr({ldp});
    // kLdpGpr | (imm7=16/8=2 << 15) | (Rt2=1 << 10) | (Rn=29 << 5) | Rt=0
    CHECK(w1 == (0xA9400000u | (2u << 15) | (1u << 10) | (29u << 5) | 0u));

    // stp x0, x1, [x29, #-16]
    MInstr stp{MOpcode::StpRegFpImm, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(-16)}};
    uint32_t w2 = encodeSingleInstr({stp});
    // imm7 = -16/8 = -2 → 0x7E
    CHECK(w2 == (0xA9000000u | ((static_cast<uint32_t>(-2) & 0x7F) << 15) | (1u << 10) | (29u << 5) | 0u));
}

static void testVariableShift()
{
    // lslv x0, x1, x2
    MInstr mi{MOpcode::LslvRRR, {gpr(PhysReg::X0), gpr(PhysReg::X1), gpr(PhysReg::X2)}};
    CHECK(encodeSingleInstr({mi}) == (0x9AC02000u | (2u << 16) | (1u << 5) | 0u));
}

static void testHighRegisters()
{
    // add x28, x19, x20 — tests registers with 5-bit encoding > 15.
    MInstr mi{MOpcode::AddRRR, {gpr(PhysReg::X28), gpr(PhysReg::X19), gpr(PhysReg::X20)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x8B000000u | (20u << 16) | (19u << 5) | 28u));
}

static void testFMovRR()
{
    // fmov d0, d1
    MInstr mi{MOpcode::FMovRR, {fpr(PhysReg::V0), fpr(PhysReg::V1)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E604000u | (1u << 5) | 0u));
}

static void testHighFPR()
{
    // fadd d16, d17, d18 — tests FPR encoding with values > 15.
    MInstr mi{MOpcode::FAddRRR, {fpr(PhysReg::V16), fpr(PhysReg::V17), fpr(PhysReg::V18)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0x1E602800u | (18u << 16) | (17u << 5) | 16u));
}

static void testStrRegSpImm()
{
    // str x0, [sp, #16] — scaled unsigned offset, offset/8 = 2
    MInstr mi{MOpcode::StrRegSpImm, {gpr(PhysReg::X0), imm(16)}};
    uint32_t word = encodeSingleInstr({mi});
    CHECK(word == (0xF9000000u | (2u << 10) | (hwGPR(PhysReg::SP) << 5) | 0u));
}

static void testAdrpAddPageOff()
{
    // adrp x0, sym → relocation
    // add x0, x0, sym@PAGEOFF → relocation
    MFunction fn;
    fn.name = "adrp_test";
    fn.isLeaf = true;
    MBasicBlock bb;
    bb.name = "entry";
    bb.instrs.push_back(MInstr{MOpcode::AdrPage, {gpr(PhysReg::X0), label("my_global")}});
    bb.instrs.push_back(MInstr{MOpcode::AddPageOff, {gpr(PhysReg::X0), gpr(PhysReg::X0), label("my_global")}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks.push_back(std::move(bb));

    CodeSection text, rodata;
    A64BinaryEncoder enc;
    enc.encodeFunction(fn, text, rodata, ABIFormat::Darwin);

    // adrp x0, ... → kAdrp | Rd=0 = 0x90000000
    CHECK(readWord(text.bytes(), 0) == 0x90000000);
    // add x0, x0, ... → kAddRI with imm12=0 = 0x91000000 | (0 << 5) | 0
    CHECK(readWord(text.bytes(), 4) == (0x91000000u | (0u << 10) | (0u << 5) | 0u));

    // Should have A64AdrpPage21 and A64AddPageOff12 relocations.
    bool hasAdrp = false, hasAdd = false;
    for (const auto &r : text.relocations())
    {
        if (r.kind == RelocKind::A64AdrpPage21) hasAdrp = true;
        if (r.kind == RelocKind::A64AddPageOff12) hasAdd = true;
    }
    CHECK(hasAdrp);
    CHECK(hasAdd);
}

static void testSymbolDefined()
{
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
    for (uint32_t i = 0; i < text.symbols().count(); ++i)
    {
        if (text.symbols().at(i).name == "my_func")
        {
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

static void testAndRI_logicalImm()
{
    // AND X0, X1, #0xFF → single instruction (logical immediate encodable).
    // enc(0xFF) = N=1, immr=0, imms=7 → 0x1007.
    // kAndImm | (0x1007 << 10) | (X1 << 5) | X0 = 0x92401C20
    MInstr mi{MOpcode::AndRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(0xFF)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0x92401C20);
}

static void testOrrRI_logicalImm()
{
    // ORR X0, X1, #0xFF → kOrrImm | (0x1007 << 10) | (X1 << 5) | X0 = 0xB2401C20
    MInstr mi{MOpcode::OrrRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(0xFF)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0xB2401C20);
}

static void testAndRI_nonEncodable()
{
    // AND X0, X1, #3 — value 3 (0b11) IS encodable as logical immediate.
    // For non-encodable, we'd need something like 5 (0b101) which isn't a contiguous run.
    // Test with 5: not encodable → fallback (MOVZ + AND, 2+ words).
    MInstr mi{MOpcode::AndRI, {gpr(PhysReg::X0), gpr(PhysReg::X1), imm(5)}};
    CHECK(countWords({mi}) > 1);
}

static void testMovRI_highOnly()
{
    // MovRI with 0x0000000100000000 → goes through encodeMovImm64.
    // Smart MOVZ start: chunks = [0, 0, 1, 0], first=2.
    // MOVZ X0, #1, lsl #32 → kMovZ32 | (1 << 5) | 0 = 0xD2C00020. Single instruction.
    MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(0x100000000LL)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0xD2C00020u);
}

static void testMovRI_movnPath()
{
    // MovRI with 0xFFFFFFFF00010000 → needs wide (not simple mov).
    // chunks = [0, 1, 0xFFFF, 0xFFFF], nzCount = 3.
    // invChunks = [0xFFFF, 0xFFFE, 0, 0], invNzCount = 2.
    // useMovn = (2 < 3) = true. src = invChunks, first = 0 (0xFFFF).
    // MOVN X0, #0xFFFF → then check chunks: [1]=1 (!=0xFFFF) → MOVK.
    // 2 instructions total (saved from 3 on MOVZ path).
    MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0),
        imm(static_cast<long long>(0xFFFFFFFF00010000ULL))}};
    CHECK(countWords({mi}) == 2);
}

static void testMovRI_zero()
{
    // MovRI with 0 → simple mov path: MOVZ X0, #0 (single instruction).
    MInstr mi{MOpcode::MovRI, {gpr(PhysReg::X0), imm(0)}};
    CHECK(countWords({mi}) == 1);
    CHECK(encodeSingleInstr({mi}) == 0xD2800000u);
}

static void testFMovRI_fp8()
{
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

static void testFMovRI_fallback()
{
    // FMOV D0, #0.3 → NOT FP8 encodable → MOVZ+MOVK+FMOV (multi-word).
    double val = 0.3;
    int64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    MInstr mi{MOpcode::FMovRI, {fpr(PhysReg::V0), imm(bits)}};
    CHECK(countWords({mi}) > 1);
}

int main()
{
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
    testFMovRI_fp8();
    testFMovRI_fallback();

    if (gFail == 0)
        std::cout << "All A64 binary encoder tests passed.\n";
    else
        std::cerr << gFail << " test(s) FAILED.\n";
    return gFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
