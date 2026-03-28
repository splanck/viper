//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_addr_stress.cpp
// Purpose: Exhaustively test every x86-64 memory addressing mode the backend
//          can generate.  Covers two levels:
//          Part A — MIR-level: construct OpMem directly with every base register,
//                   displacement range, scale factor, and index combination, then
//                   verify the AsmEmitter produces correct AT&T text.
//          Part B — IL-level: build IL programs exercising memory access patterns
//                   through the full pipeline (lowering → regalloc → asm) and verify
//                   the output assembly contains expected addressing syntax.
// Key invariants:
//   - Every GPR can serve as base register in AT&T output.
//   - Displacements from -2^31 to 2^31-1 format correctly.
//   - Scale factors 1/2/4/8 with index registers format correctly.
//   - RSP/R12 as base (SIB-requiring) and RBP/R13 (no zero-disp shortform)
//     produce valid AT&T text the external assembler can encode.
//   - RIP-relative labels format as symbol(%rip).
// Ownership/Lifetime: All MIR and IL are stack-allocated within each test.
// Links: src/codegen/x86_64/AsmEmitter.cpp, src/codegen/x86_64/asmfmt/Format.cpp,
//        src/codegen/x86_64/Lowering.EmitCommon.cpp (tryMakeIndexedMem)
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/AsmEmitter.hpp"
#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/TargetX64.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace viper::codegen::x64;

// ===----------------------------------------------------------------------===
// Part A helpers: MIR-level (from test_asm_encoding.cpp pattern)
// ===----------------------------------------------------------------------===

namespace {

[[nodiscard]] Operand gpr(PhysReg r) {
    return makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(r));
}

[[nodiscard]] Operand xmm(PhysReg r) {
    return makePhysRegOperand(RegClass::XMM, static_cast<uint16_t>(r));
}

[[nodiscard]] Operand mirImm(int64_t v) {
    return makeImmOperand(v);
}

[[nodiscard]] Operand mem(PhysReg base, int32_t disp) {
    return makeMemOperand(makePhysReg(RegClass::GPR, static_cast<uint16_t>(base)), disp);
}

[[nodiscard]] Operand idxmem(PhysReg base, PhysReg idx, uint8_t scale, int32_t disp) {
    return makeMemOperand(makePhysReg(RegClass::GPR, static_cast<uint16_t>(base)),
                          makePhysReg(RegClass::GPR, static_cast<uint16_t>(idx)),
                          scale,
                          disp);
}

[[nodiscard]] Operand rip(const std::string &name) {
    return makeRipLabelOperand(name);
}

[[nodiscard]] std::string emitSingle(MInstr instr) {
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

struct CategoryStats {
    const char *name;
    int total{0};
    int pass{0};
    int fail{0};
};

struct TestContext {
    std::vector<CategoryStats> categories;
    int currentCat{-1};
    int globalFail{0};

    void beginCategory(const char *name) {
        categories.push_back({name, 0, 0, 0});
        currentCat = static_cast<int>(categories.size()) - 1;
    }

    void check(const char *caseName, const MInstr &instr, const std::string &expected) {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        const std::string text = emitSingle(MInstr{instr});
        if (text.find(expected) != std::string::npos) {
            ++cat.pass;
        } else {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName << "\n"
                      << "  expected substring: \"" << expected << "\"\n"
                      << "  actual output:\n"
                      << text << "\n";
        }
    }

    /// Verify an assembly string (from IL pipeline) contains the expected pattern.
    void checkAsm(const char *caseName, const std::string &asmText, const std::string &expected) {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        if (asmText.find(expected) != std::string::npos) {
            ++cat.pass;
        } else {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName << "\n"
                      << "  expected substring: \"" << expected << "\"\n"
                      << "  actual output:\n"
                      << asmText << "\n";
        }
    }

    void printSummary() const {
        std::cout << "\n=== x86-64 Addressing Mode Stress Test ===\n";
        std::cout << "Category                  Total  Pass  Fail\n";
        int totalAll = 0, passAll = 0, failAll = 0;
        for (const auto &cat : categories) {
            std::string padded = cat.name;
            while (padded.size() < 26)
                padded.push_back(' ');
            std::cout << padded << " " << cat.total << "     " << cat.pass << "     " << cat.fail
                      << "\n";
            totalAll += cat.total;
            passAll += cat.pass;
            failAll += cat.fail;
        }
        std::cout << "------------------------------------------------\n";
        std::cout << "TOTAL                      " << totalAll << "    " << passAll << "     "
                  << failAll << "\n\n";
    }
};

// ===----------------------------------------------------------------------===
// Part A: MIR-level encoding matrix
// ===----------------------------------------------------------------------===

/// Category 1: Every GPR as base with zero displacement.
void testEveryGprBase(TestContext &ctx) {
    ctx.beginCategory("GPR base (zero disp)");

    struct Case {
        PhysReg reg;
        const char *name;
        const char *expected;
    };

    const Case cases[] = {
        {PhysReg::RAX, "RAX", "(%rax)"},
        {PhysReg::RBX, "RBX", "(%rbx)"},
        {PhysReg::RCX, "RCX", "(%rcx)"},
        {PhysReg::RDX, "RDX", "(%rdx)"},
        {PhysReg::RSI, "RSI", "(%rsi)"},
        {PhysReg::RDI, "RDI", "(%rdi)"},
        {PhysReg::R8, "R8", "(%r8)"},
        {PhysReg::R9, "R9", "(%r9)"},
        {PhysReg::R10, "R10", "(%r10)"},
        {PhysReg::R11, "R11", "(%r11)"},
        {PhysReg::R12, "R12", "(%r12)"},
        {PhysReg::R13, "R13", "(%r13)"},
        {PhysReg::R14, "R14", "(%r14)"},
        {PhysReg::R15, "R15", "(%r15)"},
        {PhysReg::RBP, "RBP", "(%rbp)"},
        {PhysReg::RSP, "RSP", "(%rsp)"},
    };

    for (const auto &c : cases) {
        ctx.check(
            c.name, MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(c.reg, 0)}), c.expected);
    }
}

/// Category 2: Displacement ranges with a normal base register.
void testDisplacementRanges(TestContext &ctx) {
    ctx.beginCategory("Displacement ranges");

    struct Case {
        int32_t disp;
        const char *name;
        const char *expected;
    };

    const Case cases[] = {
        {0, "zero", "(%rdi)"},
        {16, "small_pos_16", "16(%rdi)"},
        {127, "imm8_max", "127(%rdi)"},
        {-8, "small_neg_8", "-8(%rdi)"},
        {-128, "imm8_min", "-128(%rdi)"},
        {256, "imm32_pos_256", "256(%rdi)"},
        {2147483647, "imm32_max", "2147483647(%rdi)"},
        {-256, "imm32_neg_256", "-256(%rdi)"},
        // INT32_MIN = -2147483648
        {-2147483647 - 1, "imm32_min", "-2147483648(%rdi)"},
    };

    for (const auto &c : cases) {
        ctx.check(c.name,
                  MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(PhysReg::RDI, c.disp)}),
                  c.expected);
    }
}

/// Category 3: Special base registers (RSP, RBP, R12, R13) with various displacements.
void testSpecialBases(TestContext &ctx) {
    ctx.beginCategory("Special base regs");

    struct Case {
        PhysReg base;
        int32_t disp;
        const char *name;
        const char *expected;
    };

    const Case cases[] = {
        // RSP — requires SIB byte in binary encoding
        {PhysReg::RSP, 0, "RSP_disp0", "(%rsp)"},
        {PhysReg::RSP, 8, "RSP_disp8", "8(%rsp)"},
        {PhysReg::RSP, 256, "RSP_disp256", "256(%rsp)"},
        {PhysReg::RSP, -16, "RSP_neg16", "-16(%rsp)"},
        // RBP — no zero-displacement short form in binary
        {PhysReg::RBP, 0, "RBP_disp0", "(%rbp)"},
        {PhysReg::RBP, 8, "RBP_disp8", "8(%rbp)"},
        {PhysReg::RBP, -8, "RBP_neg8", "-8(%rbp)"},
        {PhysReg::RBP, 256, "RBP_disp256", "256(%rbp)"},
        // R12 — same bit pattern as RSP (needs SIB)
        {PhysReg::R12, 0, "R12_disp0", "(%r12)"},
        {PhysReg::R12, 8, "R12_disp8", "8(%r12)"},
        {PhysReg::R12, 256, "R12_disp256", "256(%r12)"},
        // R13 — same bit pattern as RBP (no zero-disp shortform)
        {PhysReg::R13, 0, "R13_disp0", "(%r13)"},
        {PhysReg::R13, 8, "R13_disp8", "8(%r13)"},
        {PhysReg::R13, 256, "R13_disp256", "256(%r13)"},
    };

    for (const auto &c : cases) {
        ctx.check(c.name,
                  MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(c.base, c.disp)}),
                  c.expected);
    }
}

/// Category 4: All scale factors with index register.
void testScaleFactors(TestContext &ctx) {
    ctx.beginCategory("Scale factors");

    struct Case {
        uint8_t scale;
        const char *name;
        const char *expected;
    };

    const Case cases[] = {
        {1, "scale1", "(%rdi,%rsi,1)"},
        {2, "scale2", "(%rdi,%rsi,2)"},
        {4, "scale4", "(%rdi,%rsi,4)"},
        {8, "scale8", "(%rdi,%rsi,8)"},
    };

    for (const auto &c : cases) {
        ctx.check(c.name,
                  MInstr::make(MOpcode::MOVmr,
                               {gpr(PhysReg::RAX), idxmem(PhysReg::RDI, PhysReg::RSI, c.scale, 0)}),
                  c.expected);
    }
}

/// Category 5: SIB with various displacements.
void testSibDisp(TestContext &ctx) {
    ctx.beginCategory("SIB + displacement");

    struct Case {
        int32_t disp;
        const char *name;
        const char *expected;
    };

    const Case cases[] = {
        {0, "sib_disp0", "(%rdi,%rsi,4)"},
        {8, "sib_disp8", "8(%rdi,%rsi,4)"},
        {256, "sib_disp256", "256(%rdi,%rsi,4)"},
        {-128, "sib_neg128", "-128(%rdi,%rsi,4)"},
        {-256, "sib_neg256", "-256(%rdi,%rsi,4)"},
        {2147483647, "sib_disp_max", "2147483647(%rdi,%rsi,4)"},
    };

    for (const auto &c : cases) {
        ctx.check(c.name,
                  MInstr::make(MOpcode::MOVmr,
                               {gpr(PhysReg::RAX), idxmem(PhysReg::RDI, PhysReg::RSI, 4, c.disp)}),
                  c.expected);
    }
}

/// Category 6: High register combinations in SIB.
void testHighRegSib(TestContext &ctx) {
    ctx.beginCategory("High-reg SIB");

    // R12 base + R13 index
    ctx.check(
        "R12_base_R13_idx",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::R12, PhysReg::R13, 4, 0)}),
        "(%r12,%r13,4)");

    // R13 base + R14 index
    ctx.check(
        "R13_base_R14_idx",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::R13, PhysReg::R14, 8, 0)}),
        "(%r13,%r14,8)");

    // R12 base + displacement (SIB required)
    ctx.check("R12_base_disp32",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(PhysReg::R12, 32)}),
              "32(%r12)");

    // R13 base + zero displacement (no short form — assembler adds disp8=0)
    ctx.check("R13_base_disp0",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), mem(PhysReg::R13, 0)}),
              "(%r13)");

    // RSP base + index register (SIB needed)
    ctx.check(
        "RSP_base_idx",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::RSP, PhysReg::RDI, 2, 0)}),
        "(%rsp,%rdi,2)");

    // RSP base + index + displacement
    ctx.check("RSP_base_idx_disp",
              MInstr::make(MOpcode::MOVmr,
                           {gpr(PhysReg::RAX), idxmem(PhysReg::RSP, PhysReg::RDI, 4, 16)}),
              "16(%rsp,%rdi,4)");

    // R12 base + R8 index + large displacement
    ctx.check("R12_R8_idx_disp512",
              MInstr::make(MOpcode::MOVmr,
                           {gpr(PhysReg::RAX), idxmem(PhysReg::R12, PhysReg::R8, 8, 512)}),
              "512(%r12,%r8,8)");

    // RBP base + index (RBP special encoding + SIB)
    ctx.check(
        "RBP_base_idx",
        MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), idxmem(PhysReg::RBP, PhysReg::RCX, 4, 0)}),
        "(%rbp,%rcx,4)");

    // R13 base + R15 index + displacement
    ctx.check("R13_R15_idx_disp24",
              MInstr::make(MOpcode::MOVmr,
                           {gpr(PhysReg::RAX), idxmem(PhysReg::R13, PhysReg::R15, 2, 24)}),
              "24(%r13,%r15,2)");
}

/// Category 7: RIP-relative addressing.
void testRipRelative(TestContext &ctx) {
    ctx.beginCategory("RIP-relative");

    ctx.check(
        "rip_simple", MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), rip("data")}), "data(%rip)");

    ctx.check("rip_mangled",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), rip("_ZN5class6methodEv")}),
              "_ZN5class6methodEv(%rip)");

    ctx.check("rip_dotlabel",
              MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), rip(".LC_f64_0")}),
              ".LC_f64_0(%rip)");

    // LEA with RIP-relative
    ctx.check(
        "lea_rip", MInstr::make(MOpcode::LEA, {gpr(PhysReg::RDI), rip("symbol")}), "symbol(%rip)");
}

/// Category 8: Cross-opcode addressing — same memory operand across different instructions.
void testCrossOpcode(TestContext &ctx) {
    ctx.beginCategory("Cross-opcode addr");

    const auto m = mem(PhysReg::RBP, -24);
    const auto sibm = idxmem(PhysReg::RDI, PhysReg::RSI, 8, 16);

    // MOVmr (load from memory)
    ctx.check("MOVmr_base_disp", MInstr::make(MOpcode::MOVmr, {gpr(PhysReg::RAX), m}), "-24(%rbp)");

    // MOVrm (store to memory)
    ctx.check("MOVrm_base_disp", MInstr::make(MOpcode::MOVrm, {m, gpr(PhysReg::RAX)}), "-24(%rbp)");

    // LEA
    ctx.check("LEA_sib", MInstr::make(MOpcode::LEA, {gpr(PhysReg::RAX), sibm}), "16(%rdi,%rsi,8)");

    // MOVSDmr (FP load from memory)
    ctx.check(
        "MOVSDmr_base_disp", MInstr::make(MOpcode::MOVSDmr, {xmm(PhysReg::XMM0), m}), "-24(%rbp)");

    // MOVSDrm (FP store to memory)
    ctx.check(
        "MOVSDrm_base_disp", MInstr::make(MOpcode::MOVSDrm, {m, xmm(PhysReg::XMM0)}), "-24(%rbp)");

    // CALL memory indirect
    ctx.check("CALL_mem_sib", MInstr::make(MOpcode::CALL, {sibm}), "16(%rdi,%rsi,8)");

    // JMP memory indirect
    ctx.check("JMP_mem_base_disp", MInstr::make(MOpcode::JMP, {m}), "-24(%rbp)");

    // MOVUPSrm (128-bit store)
    ctx.check("MOVUPSrm_RSP",
              MInstr::make(MOpcode::MOVUPSrm, {mem(PhysReg::RSP, 32), xmm(PhysReg::XMM6)}),
              "32(%rsp)");

    // MOVUPSmr (128-bit load)
    ctx.check("MOVUPSmr_RSP",
              MInstr::make(MOpcode::MOVUPSmr, {xmm(PhysReg::XMM6), mem(PhysReg::RSP, 32)}),
              "32(%rsp)");

    // IDIVrm with memory operand
    ctx.check("IDIVrm_mem", MInstr::make(MOpcode::IDIVrm, {mem(PhysReg::RBP, -8)}), "-8(%rbp)");
}

// ===----------------------------------------------------------------------===
// Part B helpers: IL-level integration (from test_addressing_modes.cpp pattern)
// ===----------------------------------------------------------------------===

[[nodiscard]] ILValue makeParam(int id, ILValue::Kind kind) noexcept {
    ILValue v{};
    v.kind = kind;
    v.id = id;
    return v;
}

[[nodiscard]] ILValue makeImmI64(long long val) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::I64;
    v.id = -1;
    v.i64 = val;
    return v;
}

[[nodiscard]] ILValue makeValueRef(int id, ILValue::Kind kind) noexcept {
    ILValue v{};
    v.kind = kind;
    v.id = id;
    return v;
}

[[nodiscard]] ILValue makeImmF64(double fval) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::F64;
    v.id = -1;
    v.f64 = fval;
    return v;
}

/// Compile an IL module and return assembly text.
[[nodiscard]] std::string compileToAsm(ILModule &m) {
    const CodegenResult res = emitModuleToAssembly(m, {});
    return res.asmText;
}

// ===----------------------------------------------------------------------===
// Part B: IL-level integration tests
// ===----------------------------------------------------------------------===

/// Test 1: Simple pointer dereference — load(ptr) with zero offset.
void testILSimpleDeref(TestContext &ctx) {
    ctx.beginCategory("IL: simple deref");

    ILValue p = makeParam(0, ILValue::Kind::PTR);

    ILInstr ld{};
    ld.opcode = "load";
    ld.resultId = 1;
    ld.resultKind = ILValue::Kind::I64;
    ld.ops = {p};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(1, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id};
    entry.paramKinds = {p.kind};
    entry.instrs = {ld, ret};

    ILFunction fn{};
    fn.name = "simple_deref";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    // Should have a load with just base register, no displacement
    // Pattern: movq (%rXX), %rYY
    ctx.checkAsm("deref_compiles", text, "movq");
    // Verify we get a memory operand
    ctx.checkAsm("deref_has_mem", text, "(%r");
}

/// Test 2: Load with small offset — struct field at offset 8.
void testILSmallOffset(TestContext &ctx) {
    ctx.beginCategory("IL: small offset");

    ILValue p = makeParam(0, ILValue::Kind::PTR);

    ILInstr ld{};
    ld.opcode = "load";
    ld.resultId = 1;
    ld.resultKind = ILValue::Kind::I64;
    ld.ops = {p, makeImmI64(8)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(1, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id};
    entry.paramKinds = {p.kind};
    entry.instrs = {ld, ret};

    ILFunction fn{};
    fn.name = "small_offset";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    ctx.checkAsm("offset8", text, "8(%r");
}

/// Test 3: Load with large offset — deep struct field at offset 256.
void testILLargeOffset(TestContext &ctx) {
    ctx.beginCategory("IL: large offset");

    ILValue p = makeParam(0, ILValue::Kind::PTR);

    ILInstr ld{};
    ld.opcode = "load";
    ld.resultId = 1;
    ld.resultKind = ILValue::Kind::I64;
    ld.ops = {p, makeImmI64(256)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(1, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id};
    entry.paramKinds = {p.kind};
    entry.instrs = {ld, ret};

    ILFunction fn{};
    fn.name = "large_offset";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    ctx.checkAsm("offset256", text, "256(%r");
}

/// Test 4: Array indexing with scale — shl(idx,3) + add(ptr,shifted) + load.
/// Tests tryMakeIndexedMem folding into SIB addressing.
void testILArrayScale(TestContext &ctx) {
    ctx.beginCategory("IL: array scale");

    ILValue p = makeParam(0, ILValue::Kind::PTR);
    ILValue i = makeParam(1, ILValue::Kind::I64);

    ILInstr shl{};
    shl.opcode = "shl";
    shl.resultId = 3;
    shl.resultKind = ILValue::Kind::I64;
    shl.ops = {i, makeImmI64(3)};

    ILInstr add{};
    add.opcode = "add";
    add.resultId = 4;
    add.resultKind = ILValue::Kind::PTR;
    add.ops = {p, makeValueRef(3, ILValue::Kind::I64)};

    ILInstr ld{};
    ld.opcode = "load";
    ld.resultId = 5;
    ld.resultKind = ILValue::Kind::I64;
    ld.ops = {makeValueRef(4, ILValue::Kind::PTR)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(5, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id, i.id};
    entry.paramKinds = {p.kind, i.kind};
    entry.instrs = {shl, add, ld, ret};

    ILFunction fn{};
    fn.name = "array_scale";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    // Should fold into SIB: (%rXX,%rYY,8)
    ctx.checkAsm("sib_scale8", text, ",8)");
}

/// Test 5: Array indexing with scale + displacement.
/// shl(idx,3) + add(ptr,shifted) + load(sum, 16).
void testILArrayScaleDisp(TestContext &ctx) {
    ctx.beginCategory("IL: array+disp");

    ILValue p = makeParam(0, ILValue::Kind::PTR);
    ILValue i = makeParam(1, ILValue::Kind::I64);

    ILInstr shl{};
    shl.opcode = "shl";
    shl.resultId = 3;
    shl.resultKind = ILValue::Kind::I64;
    shl.ops = {i, makeImmI64(3)};

    ILInstr add{};
    add.opcode = "add";
    add.resultId = 4;
    add.resultKind = ILValue::Kind::PTR;
    add.ops = {p, makeValueRef(3, ILValue::Kind::I64)};

    ILInstr ld{};
    ld.opcode = "load";
    ld.resultId = 5;
    ld.resultKind = ILValue::Kind::I64;
    ld.ops = {makeValueRef(4, ILValue::Kind::PTR), makeImmI64(16)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(5, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id, i.id};
    entry.paramKinds = {p.kind, i.kind};
    entry.instrs = {shl, add, ld, ret};

    ILFunction fn{};
    fn.name = "array_scale_disp";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    // Should fold into SIB: 16(%rXX,%rYY,8)
    ctx.checkAsm("sib_scale8_disp16", text, "16(");
    ctx.checkAsm("sib_scale8_disp16_scale", text, ",8)");
}

/// Test 6: Stack locals via alloca + store + load.
/// Exercises RSP/RBP-based addressing from the frame lowering.
void testILStackLocals(TestContext &ctx) {
    ctx.beginCategory("IL: stack locals");

    ILValue p = makeParam(0, ILValue::Kind::I64);

    // alloca 8 bytes → %2 (ptr)
    ILInstr alloc1{};
    alloc1.opcode = "alloca";
    alloc1.resultId = 2;
    alloc1.resultKind = ILValue::Kind::PTR;
    alloc1.ops = {makeImmI64(8)};

    // alloca 8 bytes → %3 (ptr)
    ILInstr alloc2{};
    alloc2.opcode = "alloca";
    alloc2.resultId = 3;
    alloc2.resultKind = ILValue::Kind::PTR;
    alloc2.ops = {makeImmI64(8)};

    // store param into first local
    ILInstr store1{};
    store1.opcode = "store";
    store1.resultId = -1;
    store1.ops = {makeValueRef(2, ILValue::Kind::PTR), p};

    // store param into second local
    ILInstr store2{};
    store2.opcode = "store";
    store2.resultId = -1;
    store2.ops = {makeValueRef(3, ILValue::Kind::PTR), p};

    // load from first local
    ILInstr ld1{};
    ld1.opcode = "load";
    ld1.resultId = 6;
    ld1.resultKind = ILValue::Kind::I64;
    ld1.ops = {makeValueRef(2, ILValue::Kind::PTR)};

    // load from second local
    ILInstr ld2{};
    ld2.opcode = "load";
    ld2.resultId = 7;
    ld2.resultKind = ILValue::Kind::I64;
    ld2.ops = {makeValueRef(3, ILValue::Kind::PTR)};

    // add loads together
    ILInstr add{};
    add.opcode = "add";
    add.resultId = 8;
    add.resultKind = ILValue::Kind::I64;
    add.ops = {makeValueRef(6, ILValue::Kind::I64), makeValueRef(7, ILValue::Kind::I64)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(8, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id};
    entry.paramKinds = {p.kind};
    entry.instrs = {alloc1, alloc2, store1, store2, ld1, ld2, add, ret};

    ILFunction fn{};
    fn.name = "stack_locals";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    // Should have RBP-based addressing from alloca lowering (LEA + stores/loads)
    ctx.checkAsm("has_rbp_addr", text, "%rbp");
    // Should have at least two distinct memory references with offsets
    ctx.checkAsm("has_leaq", text, "leaq");
}

/// Test 7: FP constant via RIP-relative — const_f64 uses .rodata pool.
void testILFpConstRip(TestContext &ctx) {
    ctx.beginCategory("IL: FP const RIP");

    // const_f64(3.14159) → %0
    ILInstr cf{};
    cf.opcode = "const_f64";
    cf.resultId = 0;
    cf.resultKind = ILValue::Kind::F64;
    cf.ops = {makeImmF64(3.14159)};

    // sitofp(I64 param) → %2
    ILValue p = makeParam(1, ILValue::Kind::I64);
    ILInstr conv{};
    conv.opcode = "sitofp";
    conv.resultId = 2;
    conv.resultKind = ILValue::Kind::F64;
    conv.ops = {p};

    // fadd(%0, %2) → %3
    ILInstr fa{};
    fa.opcode = "add";
    fa.resultId = 3;
    fa.resultKind = ILValue::Kind::F64;
    fa.ops = {makeValueRef(0, ILValue::Kind::F64), makeValueRef(2, ILValue::Kind::F64)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(3, ILValue::Kind::F64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id};
    entry.paramKinds = {p.kind};
    entry.instrs = {cf, conv, fa, ret};

    ILFunction fn{};
    fn.name = "fp_const_rip";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    // FP constant should be loaded via RIP-relative addressing
    ctx.checkAsm("rip_fp_const", text, "(%rip)");
    // Should be in .rodata section
    ctx.checkAsm("rodata_section", text, ".section");
}

/// Test 8: Store + load round-trip via alloca.
void testILStoreLoadRoundtrip(TestContext &ctx) {
    ctx.beginCategory("IL: store/load RT");

    ILValue p = makeParam(0, ILValue::Kind::I64);

    // alloca 8 bytes → %2 (ptr)
    ILInstr alloc{};
    alloc.opcode = "alloca";
    alloc.resultId = 2;
    alloc.resultKind = ILValue::Kind::PTR;
    alloc.ops = {makeImmI64(8)};

    // store param → alloca slot
    ILInstr store{};
    store.opcode = "store";
    store.resultId = -1;
    store.ops = {makeValueRef(2, ILValue::Kind::PTR), p};

    // load from the same slot
    ILInstr ld{};
    ld.opcode = "load";
    ld.resultId = 4;
    ld.resultKind = ILValue::Kind::I64;
    ld.ops = {makeValueRef(2, ILValue::Kind::PTR)};

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeValueRef(4, ILValue::Kind::I64)};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {p.id};
    entry.paramKinds = {p.kind};
    entry.instrs = {alloc, store, ld, ret};

    ILFunction fn{};
    fn.name = "store_load_rt";
    fn.blocks = {entry};

    ILModule m{};
    m.funcs = {fn};

    const auto text = compileToAsm(m);
    // Should have both store and load with frame-relative addressing
    ctx.checkAsm("has_store", text, "movq %r");
    ctx.checkAsm("has_load", text, "movq");
    ctx.checkAsm("has_frame_ref", text, "%rbp");
}

} // namespace

// ===----------------------------------------------------------------------===
// Main
// ===----------------------------------------------------------------------===

int main() {
    TestContext ctx;

    // Part A: MIR-level encoding matrix
    testEveryGprBase(ctx);
    testDisplacementRanges(ctx);
    testSpecialBases(ctx);
    testScaleFactors(ctx);
    testSibDisp(ctx);
    testHighRegSib(ctx);
    testRipRelative(ctx);
    testCrossOpcode(ctx);

    // Part B: IL-level integration
    testILSimpleDeref(ctx);
    testILSmallOffset(ctx);
    testILLargeOffset(ctx);
    testILArrayScale(ctx);
    testILArrayScaleDisp(ctx);
    testILStackLocals(ctx);
    testILFpConstRip(ctx);
    testILStoreLoadRoundtrip(ctx);

    ctx.printSummary();

    if (ctx.globalFail != 0) {
        std::cerr << ctx.globalFail << " test(s) FAILED\n";
        return EXIT_FAILURE;
    }
    std::cout << "All tests passed.\n";
    return EXIT_SUCCESS;
}
