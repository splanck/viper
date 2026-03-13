//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_asm_encoding.cpp
// Purpose: Exhaustively verify that every MOpcode the x86-64 backend can emit
//          produces correct AT&T assembly text. For each instruction type, we
//          construct a MInstr with physical register operands, emit it through
//          AsmEmitter, and check that the output string contains the expected
//          mnemonic and operand formatting.
// Key invariants:
//   - Every emittable MOpcode has at least one test case.
//   - Condition codes 0-13 are tested for JCC and SETcc.
//   - Hi-register (R8-R15, XMM8-XMM15) variants catch REX prefix issues.
//   - All addressing modes (base+disp, SIB, RIP-relative) are covered.
// Ownership/Lifetime: Standalone test binary; all MIR is stack-allocated.
// Links: codegen/x86_64/AsmEmitter.hpp, codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/AsmEmitter.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/TargetX64.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace viper::codegen::x64;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

/// Shorthand for a physical GPR operand.
[[nodiscard]] Operand gpr(PhysReg r)
{
    return makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(r));
}

/// Shorthand for a physical XMM operand.
[[nodiscard]] Operand xmm(PhysReg r)
{
    return makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(r));
}

/// Shorthand for an immediate operand.
[[nodiscard]] Operand imm(int64_t v)
{
    return makeImmOperand(v);
}

/// Shorthand for a base+disp memory operand.
[[nodiscard]] Operand mem(PhysReg base, int32_t disp)
{
    return makeMemOperand(makePhysReg(RegClass::GPR, static_cast<uint16_t>(base)), disp);
}

/// Shorthand for a base+index*scale+disp memory operand.
[[nodiscard]] Operand idxmem(PhysReg base, PhysReg idx, uint8_t scale, int32_t disp)
{
    return makeMemOperand(makePhysReg(RegClass::GPR, static_cast<uint16_t>(base)),
                          makePhysReg(RegClass::GPR, static_cast<uint16_t>(idx)),
                          scale,
                          disp);
}

/// Shorthand for a label operand.
[[nodiscard]] Operand lab(const std::string &name)
{
    return makeLabelOperand(name);
}

/// Shorthand for a RIP-relative label operand.
[[nodiscard]] Operand rip(const std::string &name)
{
    return makeRipLabelOperand(name);
}

/// Emit a single instruction by wrapping it in an MFunction with one block.
/// Returns the emitted assembly text (including .text/.globl boilerplate).
[[nodiscard]] std::string emitSingle(MInstr instr)
{
    MBasicBlock block;
    block.label = "test_func";
    block.instructions.push_back(std::move(instr));

    MFunction func;
    func.name = "test_func";
    func.blocks.push_back(std::move(block));

    AsmEmitter::RoDataPool pool;
    AsmEmitter emitter(pool);

    std::ostringstream os;
    emitter.emitFunction(os, func, hostTarget());
    return os.str();
}

// Test bookkeeping
struct CategoryStats
{
    const char *name;
    int total{0};
    int pass{0};
    int fail{0};
};

struct TestContext
{
    std::vector<CategoryStats> categories;
    int currentCat{-1};
    int globalFail{0};

    void beginCategory(const char *name)
    {
        categories.push_back({name, 0, 0, 0});
        currentCat = static_cast<int>(categories.size()) - 1;
    }

    void check(const char *caseName, const MInstr &instr, const std::string &expected)
    {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        const std::string text = emitSingle(MInstr{instr});
        if (text.find(expected) != std::string::npos)
        {
            ++cat.pass;
        }
        else
        {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName << "\n"
                      << "  expected substring: \"" << expected << "\"\n"
                      << "  actual output:\n"
                      << text << "\n";
        }
    }

    /// Check that find_encoding returns nullptr for a pseudo-op.
    void checkNoEncoding(const char *caseName, MOpcode opc, std::vector<Operand> ops = {})
    {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        const auto *row = find_encoding(opc, std::span<const Operand>{ops});
        if (row == nullptr)
        {
            ++cat.pass;
        }
        else
        {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName
                      << ": expected no encoding row, but found one\n";
        }
    }

    void printSummary() const
    {
        std::cout << "\n=== x86-64 Assembly Encoding Test Matrix ===\n";
        std::cout << "Category             Total  Pass  Fail\n";
        int totalAll = 0, passAll = 0, failAll = 0;
        for (const auto &cat : categories)
        {
            // Right-pad name to 20 chars
            std::string padded = cat.name;
            while (padded.size() < 20)
                padded.push_back(' ');
            std::cout << padded << " " << cat.total << "     " << cat.pass << "     " << cat.fail
                      << "\n";
            totalAll += cat.total;
            passAll += cat.pass;
            failAll += cat.fail;
        }
        std::cout << "--------------------------------------------\n";
        std::cout << "TOTAL                " << totalAll << "    " << passAll << "     " << failAll
                  << "\n\n";
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Test categories
// ---------------------------------------------------------------------------

static void testMovFamily(TestContext &ctx)
{
    ctx.beginCategory("MOV family");

    // MOVrr — low-low
    ctx.check("MOVrr_lo_lo",
              MInstr::make(MOpcode::MOVrr, {gpr(PhysReg::RAX), gpr(PhysReg::RDI)}),
              "movq %rdi, %rax");

    // MOVrr — low-high
    ctx.check("MOVrr_lo_hi",
              MInstr::make(MOpcode::MOVrr, {gpr(PhysReg::RAX), gpr(PhysReg::R8)}),
              "movq %r8, %rax");

    // MOVrr — high-low
    ctx.check("MOVrr_hi_lo",
              MInstr::make(MOpcode::MOVrr, {gpr(PhysReg::R9), gpr(PhysReg::RCX)}),
              "movq %rcx, %r9");

    // MOVrr — high-high
    ctx.check("MOVrr_hi_hi",
              MInstr::make(MOpcode::MOVrr, {gpr(PhysReg::R10), gpr(PhysReg::R15)}),
              "movq %r15, %r10");

    // MOVrr — special regs
    ctx.check("MOVrr_special",
              MInstr::make(MOpcode::MOVrr, {gpr(PhysReg::RSP), gpr(PhysReg::RBP)}),
              "movq %rbp, %rsp");

    // MOVrm — base+disp
    ctx.check("MOVrm_base_disp",
              MInstr::make(MOpcode::MOVrm, {mem(PhysReg::RBP, -8), gpr(PhysReg::RAX)}),
              "movq %rax, -8(%rbp)");

    // MOVrm — zero disp
    ctx.check("MOVrm_zero_disp",
              MInstr::make(MOpcode::MOVrm, {mem(PhysReg::RDI, 0), gpr(PhysReg::RSI)}),
              "movq %rsi, (%rdi)");

    // MOVrm — hi base
    ctx.check("MOVrm_hi_base",
              MInstr::make(MOpcode::MOVrm, {mem(PhysReg::R12, 16), gpr(PhysReg::R8)}),
              "movq %r8, 16(%r12)");

    // MOVrm — indexed SIB
    ctx.check(
        "MOVrm_indexed",
        MInstr::make(MOpcode::MOVrm, {idxmem(PhysReg::RDI, PhysReg::RSI, 4, 8), gpr(PhysReg::RAX)}),
        "movq %rax, 8(%rdi,%rsi,4)");

    // MOVmr — base+disp
    ctx.check("MOVmr_base_disp",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RCX), mem(PhysReg::RBP, -16)}),
              "movq -16(%rbp), %rcx");

    // MOVmr — indexed
    ctx.check(
        "MOVmr_indexed",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::RDI, PhysReg::R8, 8, 0)}),
        "(%rdi,%r8,8)");

    // MOVmr — RIP-relative
    ctx.check(
        "MOVmr_rip", MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), rip("data")}), "data(%rip)");

    // MOVri — zero
    ctx.check(
        "MOVri_zero", MInstr::make(MOpcode::MOVri, {gpr(PhysReg::RAX), imm(0)}), "movq $0, %rax");

    // MOVri — positive
    ctx.check("MOVri_positive",
              MInstr::make(MOpcode::MOVri, {gpr(PhysReg::R15), imm(42)}),
              "movq $42, %r15");

    // MOVri — negative
    ctx.check("MOVri_negative",
              MInstr::make(MOpcode::MOVri, {gpr(PhysReg::RDX), imm(-1)}),
              "movq $-1, %rdx");

    // MOVri — large
    ctx.check("MOVri_large",
              MInstr::make(MOpcode::MOVri, {gpr(PhysReg::RAX), imm(2147483647)}),
              "movq $2147483647");

    // CMOVNErr — low regs (64-bit: uses REX.W, 'q' suffix)
    ctx.check("CMOVNErr",
              MInstr::make(MOpcode::CMOVNErr, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}),
              "cmovneq %rcx, %rax");

    // CMOVNErr — high regs
    ctx.check("CMOVNErr_hi",
              MInstr::make(MOpcode::CMOVNErr, {gpr(PhysReg::R8), gpr(PhysReg::R15)}),
              "cmovneq %r15, %r8");
}

static void testLea(TestContext &ctx)
{
    ctx.beginCategory("LEA");

    // LEA — memory operand
    ctx.check("LEA_mem",
              MInstr::make(MOpcode::LEA, {gpr(PhysReg::RAX), mem(PhysReg::RBP, -8)}),
              "leaq -8(%rbp), %rax");

    // LEA — indexed
    ctx.check(
        "LEA_indexed",
        MInstr::make(MOpcode::LEA, {gpr(PhysReg::RDX), idxmem(PhysReg::RDI, PhysReg::RSI, 4, 16)}),
        "leaq 16(%rdi,%rsi,4), %rdx");

    // LEA — label (becomes RIP-relative)
    ctx.check("LEA_label",
              MInstr::make(MOpcode::LEA, {gpr(PhysReg::RAX), lab("sym")}),
              "sym(%rip), %rax");

    // LEA — RIP label
    ctx.check(
        "LEA_rip", MInstr::make(MOpcode::LEA, {gpr(PhysReg::R8), rip("data")}), "data(%rip), %r8");

    // LEA — hi base + hi index
    ctx.check(
        "LEA_hi_base",
        MInstr::make(MOpcode::LEA, {gpr(PhysReg::R12), idxmem(PhysReg::R13, PhysReg::R14, 8, 32)}),
        "leaq 32(%r13,%r14,8), %r12");
}

static void testIntegerAlu(TestContext &ctx)
{
    ctx.beginCategory("Integer ALU");

    ctx.check("ADDrr",
              MInstr::make(MOpcode::ADDrr, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}),
              "addq %rcx, %rax");

    ctx.check("ADDrr_hi",
              MInstr::make(MOpcode::ADDrr, {gpr(PhysReg::R8), gpr(PhysReg::R12)}),
              "addq %r12, %r8");

    ctx.check(
        "ADDri", MInstr::make(MOpcode::ADDri, {gpr(PhysReg::RSI), imm(100)}), "addq $100, %rsi");

    ctx.check(
        "ADDri_neg", MInstr::make(MOpcode::ADDri, {gpr(PhysReg::RAX), imm(-1)}), "addq $-1, %rax");

    ctx.check("ANDrr",
              MInstr::make(MOpcode::ANDrr, {gpr(PhysReg::RDI), gpr(PhysReg::RSI)}),
              "andq %rsi, %rdi");

    ctx.check(
        "ANDri", MInstr::make(MOpcode::ANDri, {gpr(PhysReg::RAX), imm(255)}), "andq $255, %rax");

    ctx.check(
        "ORrr", MInstr::make(MOpcode::ORrr, {gpr(PhysReg::R8), gpr(PhysReg::R9)}), "orq %r9, %r8");

    ctx.check("ORri", MInstr::make(MOpcode::ORri, {gpr(PhysReg::RAX), imm(1)}), "orq $1, %rax");

    ctx.check("XORrr",
              MInstr::make(MOpcode::XORrr, {gpr(PhysReg::RCX), gpr(PhysReg::RDX)}),
              "xorq %rdx, %rcx");

    ctx.check(
        "XORri", MInstr::make(MOpcode::XORri, {gpr(PhysReg::RAX), imm(-1)}), "xorq $-1, %rax");

    ctx.check("SUBrr",
              MInstr::make(MOpcode::SUBrr, {gpr(PhysReg::RAX), gpr(PhysReg::RBX)}),
              "subq %rbx, %rax");

    ctx.check("SUBrr_hi",
              MInstr::make(MOpcode::SUBrr, {gpr(PhysReg::R13), gpr(PhysReg::R14)}),
              "subq %r14, %r13");

    ctx.check("IMULrr",
              MInstr::make(MOpcode::IMULrr, {gpr(PhysReg::RAX), gpr(PhysReg::RDI)}),
              "imulq %rdi, %rax");

    ctx.check("IMULrr_hi",
              MInstr::make(MOpcode::IMULrr, {gpr(PhysReg::R13), gpr(PhysReg::R14)}),
              "imulq %r14, %r13");
}

static void testShifts(TestContext &ctx)
{
    ctx.beginCategory("Shifts");

    ctx.check(
        "SHLri_1", MInstr::make(MOpcode::SHLri, {gpr(PhysReg::RAX), imm(1)}), "shlq $1, %rax");

    ctx.check(
        "SHLri_63", MInstr::make(MOpcode::SHLri, {gpr(PhysReg::RAX), imm(63)}), "shlq $63, %rax");

    ctx.check("SHLri_hi", MInstr::make(MOpcode::SHLri, {gpr(PhysReg::R8), imm(4)}), "shlq $4, %r8");

    ctx.check("SHLrc",
              MInstr::make(MOpcode::SHLrc, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}),
              "shlq %cl, %rax");

    ctx.check("SHRri", MInstr::make(MOpcode::SHRri, {gpr(PhysReg::RDX), imm(4)}), "shrq $4, %rdx");

    ctx.check("SHRrc",
              MInstr::make(MOpcode::SHRrc, {gpr(PhysReg::RSI), gpr(PhysReg::RCX)}),
              "shrq %cl, %rsi");

    ctx.check("SARri", MInstr::make(MOpcode::SARri, {gpr(PhysReg::R8), imm(32)}), "sarq $32, %r8");

    ctx.check("SARrc",
              MInstr::make(MOpcode::SARrc, {gpr(PhysReg::R15), gpr(PhysReg::RCX)}),
              "sarq %cl, %r15");
}

static void testDivision(TestContext &ctx)
{
    ctx.beginCategory("Division");

    ctx.check("CQO", MInstr::make(MOpcode::CQO), "cqto");

    ctx.check("IDIVrm_reg", MInstr::make(MOpcode::IDIVrm, {gpr(PhysReg::RCX)}), "idivq %rcx");

    ctx.check("IDIVrm_hi", MInstr::make(MOpcode::IDIVrm, {gpr(PhysReg::R11)}), "idivq %r11");

    ctx.check(
        "IDIVrm_mem", MInstr::make(MOpcode::IDIVrm, {mem(PhysReg::RBP, -8)}), "idivq -8(%rbp)");

    ctx.check("DIVrm_reg", MInstr::make(MOpcode::DIVrm, {gpr(PhysReg::RSI)}), "divq %rsi");

    ctx.check("DIVrm_hi", MInstr::make(MOpcode::DIVrm, {gpr(PhysReg::R11)}), "divq %r11");

    ctx.check("XORrr32_lo",
              MInstr::make(MOpcode::XORrr32, {gpr(PhysReg::RAX), gpr(PhysReg::RAX)}),
              "xorl %eax, %eax");

    ctx.check("XORrr32_hi",
              MInstr::make(MOpcode::XORrr32, {gpr(PhysReg::R8), gpr(PhysReg::R8)}),
              "xorl %r8d, %r8d");
}

static void testCmpTestSet(TestContext &ctx)
{
    ctx.beginCategory("Cmp/Test/Set");

    ctx.check("CMPrr",
              MInstr::make(MOpcode::CMPrr, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}),
              "cmpq %rcx, %rax");

    ctx.check("CMPri", MInstr::make(MOpcode::CMPri, {gpr(PhysReg::RDI), imm(0)}), "cmpq $0, %rdi");

    ctx.check(
        "CMPri_neg", MInstr::make(MOpcode::CMPri, {gpr(PhysReg::RAX), imm(-1)}), "cmpq $-1, %rax");

    ctx.check("TESTrr",
              MInstr::make(MOpcode::TESTrr, {gpr(PhysReg::RAX), gpr(PhysReg::RAX)}),
              "testq %rax, %rax");

    ctx.check("MOVZXrr32_lo",
              MInstr::make(MOpcode::MOVZXrr32, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}),
              "movzbq %cl, %rax");

    ctx.check("MOVZXrr32_hi",
              MInstr::make(MOpcode::MOVZXrr32, {gpr(PhysReg::R8), gpr(PhysReg::R9)}),
              "movzbq %r9b, %r8");

    // SETcc — all 14 condition codes
    // SETcc takes {imm(condCode), gpr(dest)} — operand order is imm first, reg second
    struct SetccCase
    {
        int code;
        const char *suffix;
        PhysReg reg;
        const char *reg8;
    };

    const SetccCase setccCases[] = {
        {0, "sete", PhysReg::RAX, "%al"},
        {1, "setne", PhysReg::RCX, "%cl"},
        {2, "setl", PhysReg::RDX, "%dl"},
        {3, "setle", PhysReg::RDI, "%dil"},
        {4, "setg", PhysReg::RSI, "%sil"},
        {5, "setge", PhysReg::RBP, "%bpl"},
        {6, "seta", PhysReg::R8, "%r8b"},
        {7, "setae", PhysReg::R9, "%r9b"},
        {8, "setb", PhysReg::R10, "%r10b"},
        {9, "setbe", PhysReg::R11, "%r11b"},
        {10, "setp", PhysReg::R12, "%r12b"},
        {11, "setnp", PhysReg::R13, "%r13b"},
        {12, "seto", PhysReg::R14, "%r14b"},
        {13, "setno", PhysReg::R15, "%r15b"},
    };

    for (const auto &sc : setccCases)
    {
        std::string caseName = std::string{"SETcc_"} + sc.suffix;
        std::string expected = std::string{sc.suffix} + " " + sc.reg8;
        ctx.check(
            caseName.c_str(), MInstr::make(MOpcode::SETcc, {imm(sc.code), gpr(sc.reg)}), expected);
    }
}

static void testControlFlow(TestContext &ctx)
{
    ctx.beginCategory("Control flow");

    // JMP — label
    ctx.check("JMP_label", MInstr::make(MOpcode::JMP, {lab("tgt")}), "jmp tgt");

    // JMP — register (indirect)
    ctx.check("JMP_reg", MInstr::make(MOpcode::JMP, {gpr(PhysReg::RAX)}), "jmp *%rax");

    // JMP — memory (indirect)
    ctx.check("JMP_mem", MInstr::make(MOpcode::JMP, {mem(PhysReg::RBP, -8)}), "jmp *-8(%rbp)");

    // CALL — label
    ctx.check("CALL_label", MInstr::make(MOpcode::CALL, {lab("func")}), "callq func");

    // CALL — register (indirect)
    ctx.check("CALL_reg", MInstr::make(MOpcode::CALL, {gpr(PhysReg::RAX)}), "callq *%rax");

    // CALL — memory (indirect)
    ctx.check("CALL_mem", MInstr::make(MOpcode::CALL, {mem(PhysReg::RBP, -8)}), "callq *-8(%rbp)");

    // UD2
    ctx.check("UD2", MInstr::make(MOpcode::UD2), "ud2");

    // RET
    ctx.check("RET", MInstr::make(MOpcode::RET), "ret");

    // JCC — all 14 condition codes
    struct JccCase
    {
        int code;
        const char *suffix;
    };

    const JccCase jccCases[] = {
        {0, "je"},
        {1, "jne"},
        {2, "jl"},
        {3, "jle"},
        {4, "jg"},
        {5, "jge"},
        {6, "ja"},
        {7, "jae"},
        {8, "jb"},
        {9, "jbe"},
        {10, "jp"},
        {11, "jnp"},
        {12, "jo"},
        {13, "jno"},
    };

    for (const auto &jc : jccCases)
    {
        std::string caseName = std::string{"JCC_"} + jc.suffix;
        std::string expected = std::string{jc.suffix} + " tgt";
        ctx.check(
            caseName.c_str(), MInstr::make(MOpcode::JCC, {imm(jc.code), lab("tgt")}), expected);
    }
}

static void testFpAlu(TestContext &ctx)
{
    ctx.beginCategory("FP ALU");

    ctx.check("FADD",
              MInstr::make(MOpcode::FADD, {xmm(PhysReg::XMM0), xmm(PhysReg::XMM1)}),
              "addsd %xmm1, %xmm0");

    ctx.check("FADD_hi",
              MInstr::make(MOpcode::FADD, {xmm(PhysReg::XMM8), xmm(PhysReg::XMM15)}),
              "addsd %xmm15, %xmm8");

    ctx.check("FSUB",
              MInstr::make(MOpcode::FSUB, {xmm(PhysReg::XMM0), xmm(PhysReg::XMM2)}),
              "subsd %xmm2, %xmm0");

    ctx.check("FMUL",
              MInstr::make(MOpcode::FMUL, {xmm(PhysReg::XMM3), xmm(PhysReg::XMM4)}),
              "mulsd %xmm4, %xmm3");

    ctx.check("FDIV",
              MInstr::make(MOpcode::FDIV, {xmm(PhysReg::XMM0), xmm(PhysReg::XMM1)}),
              "divsd %xmm1, %xmm0");

    ctx.check("UCOMIS",
              MInstr::make(MOpcode::UCOMIS, {xmm(PhysReg::XMM0), xmm(PhysReg::XMM1)}),
              "ucomisd %xmm1, %xmm0");

    ctx.check("CVTSI2SD",
              MInstr::make(MOpcode::CVTSI2SD, {xmm(PhysReg::XMM0), gpr(PhysReg::RAX)}),
              "cvtsi2sdq %rax, %xmm0");

    ctx.check("CVTSI2SD_hi",
              MInstr::make(MOpcode::CVTSI2SD, {xmm(PhysReg::XMM8), gpr(PhysReg::R15)}),
              "cvtsi2sdq %r15, %xmm8");

    ctx.check("CVTTSD2SI",
              MInstr::make(MOpcode::CVTTSD2SI, {gpr(PhysReg::RAX), xmm(PhysReg::XMM0)}),
              "cvttsd2siq %xmm0, %rax");

    ctx.check("CVTTSD2SI_hi",
              MInstr::make(MOpcode::CVTTSD2SI, {gpr(PhysReg::R15), xmm(PhysReg::XMM8)}),
              "cvttsd2siq %xmm8, %r15");
}

static void testFpDataMove(TestContext &ctx)
{
    ctx.beginCategory("FP data move");

    ctx.check("MOVQrx",
              MInstr::make(MOpcode::MOVQrx, {xmm(PhysReg::XMM0), gpr(PhysReg::RAX)}),
              "movq %rax, %xmm0");

    ctx.check("MOVQrx_hi",
              MInstr::make(MOpcode::MOVQrx, {xmm(PhysReg::XMM8), gpr(PhysReg::R15)}),
              "movq %r15, %xmm8");

    ctx.check("MOVSDrr",
              MInstr::make(MOpcode::MOVSDrr, {xmm(PhysReg::XMM0), xmm(PhysReg::XMM1)}),
              "movsd %xmm1, %xmm0");

    ctx.check("MOVSDrr_hi",
              MInstr::make(MOpcode::MOVSDrr, {xmm(PhysReg::XMM10), xmm(PhysReg::XMM11)}),
              "movsd %xmm11, %xmm10");

    ctx.check("MOVSDrm",
              MInstr::make(MOpcode::MOVSDrm, {mem(PhysReg::RBP, -8), xmm(PhysReg::XMM0)}),
              "movsd %xmm0, -8(%rbp)");

    ctx.check("MOVSDmr",
              MInstr::make(MOpcode::MOVSDmr, {xmm(PhysReg::XMM0), mem(PhysReg::RBP, -16)}),
              "movsd -16(%rbp), %xmm0");

    ctx.check("MOVUPSrm",
              MInstr::make(MOpcode::MOVUPSrm, {mem(PhysReg::RSP, 0), xmm(PhysReg::XMM6)}),
              "movups %xmm6, (%rsp)");

    ctx.check("MOVUPSmr",
              MInstr::make(MOpcode::MOVUPSmr, {xmm(PhysReg::XMM6), mem(PhysReg::RSP, 0)}),
              "movups (%rsp), %xmm6");

    ctx.check("MOVUPSrm_disp",
              MInstr::make(MOpcode::MOVUPSrm, {mem(PhysReg::RSP, 16), xmm(PhysReg::XMM15)}),
              "movups %xmm15, 16(%rsp)");
}

static void testPseudoOps(TestContext &ctx)
{
    ctx.beginCategory("Pseudo-ops");

    // LABEL
    ctx.check("LABEL", MInstr::make(MOpcode::LABEL, {lab("my_lbl")}), "my_lbl:");

    // PX_COPY with operands
    ctx.check("PX_COPY",
              MInstr::make(MOpcode::PX_COPY, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}),
              "# px_copy");

    // PX_COPY empty
    ctx.check("PX_COPY_empty", MInstr::make(MOpcode::PX_COPY), "# px_copy");

    // Negative tests: pseudo-ops that expand before emission should have no encoding
    ctx.checkNoEncoding("no_enc_DIVS64rr",
                        MOpcode::DIVS64rr,
                        {gpr(PhysReg::RAX), gpr(PhysReg::RCX), gpr(PhysReg::RDX)});

    ctx.checkNoEncoding("no_enc_DIVU64rr",
                        MOpcode::DIVU64rr,
                        {gpr(PhysReg::RAX), gpr(PhysReg::RCX), gpr(PhysReg::RDX)});

    ctx.checkNoEncoding("no_enc_REMS64rr",
                        MOpcode::REMS64rr,
                        {gpr(PhysReg::RAX), gpr(PhysReg::RCX), gpr(PhysReg::RDX)});

    ctx.checkNoEncoding("no_enc_REMU64rr",
                        MOpcode::REMU64rr,
                        {gpr(PhysReg::RAX), gpr(PhysReg::RCX), gpr(PhysReg::RDX)});

    ctx.checkNoEncoding("no_enc_ADDOvfrr",
                        MOpcode::ADDOvfrr,
                        {gpr(PhysReg::RAX), gpr(PhysReg::RCX), gpr(PhysReg::RDX)});

    ctx.checkNoEncoding("no_enc_SUBOvfrr",
                        MOpcode::SUBOvfrr,
                        {gpr(PhysReg::RAX), gpr(PhysReg::RCX), gpr(PhysReg::RDX)});

    ctx.checkNoEncoding("no_enc_IMULOvfrr",
                        MOpcode::IMULOvfrr,
                        {gpr(PhysReg::RAX), gpr(PhysReg::RCX), gpr(PhysReg::RDX)});
}

static void testAddressingModes(TestContext &ctx)
{
    ctx.beginCategory("Addr modes");

    // Base only (zero displacement)
    ctx.check("addr_base_only",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(PhysReg::RDI, 0)}),
              "(%rdi)");

    // Base + negative displacement
    ctx.check("addr_base_neg_disp",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(PhysReg::RBP, -8)}),
              "-8(%rbp)");

    // Base + positive displacement
    ctx.check("addr_base_pos_disp",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(PhysReg::RSP, 32)}),
              "32(%rsp)");

    // Base + Index*1
    ctx.check(
        "addr_idx_scale1",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::RDI, PhysReg::RSI, 1, 0)}),
        "(%rdi,%rsi,1)");

    // Base + Index*2
    ctx.check(
        "addr_idx_scale2",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::RDI, PhysReg::RSI, 2, 0)}),
        "(%rdi,%rsi,2)");

    // Base + Index*4 + disp
    ctx.check(
        "addr_idx_scale4_disp",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::RDI, PhysReg::RSI, 4, 8)}),
        "8(%rdi,%rsi,4)");

    // Base + Index*8 + disp
    ctx.check("addr_idx_scale8_disp",
              MInstr::make(MOpcode::MOVmr,
                           {gpr(PhysReg::RAX), idxmem(PhysReg::RDI, PhysReg::RSI, 8, 16)}),
              "16(%rdi,%rsi,8)");

    // Hi base + hi index
    ctx.check(
        "addr_hi_base_hi_idx",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::R12, PhysReg::R13, 4, 0)}),
        "(%r12,%r13,4)");

    // RIP-relative
    ctx.check("addr_rip_relative",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), rip("data")}),
              "data(%rip)");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    TestContext ctx;

    testMovFamily(ctx);
    testLea(ctx);
    testIntegerAlu(ctx);
    testShifts(ctx);
    testDivision(ctx);
    testCmpTestSet(ctx);
    testControlFlow(ctx);
    testFpAlu(ctx);
    testFpDataMove(ctx);
    testPseudoOps(ctx);
    testAddressingModes(ctx);

    ctx.printSummary();

    if (ctx.globalFail != 0)
    {
        std::cerr << ctx.globalFail << " test(s) FAILED\n";
        return EXIT_FAILURE;
    }
    std::cout << "All tests passed.\n";
    return EXIT_SUCCESS;
}
