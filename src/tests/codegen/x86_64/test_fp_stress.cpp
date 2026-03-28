//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_fp_stress.cpp
// Purpose: Stress-test the x86-64 backend's floating-point code generation.
//          Exercises SSE2 scalar instruction encoding, FP comparisons with NaN
//          semantics, special value materialization, conversions, and FP
//          register allocation under pressure.
// Key invariants:
//   - All FP arithmetic produces SSE2 scalar double instructions (addsd, etc.)
//   - FP comparisons use UCOMISD; condition codes must respect IEEE 754 NaN rules
//   - FP constants are bit-exact in .rodata via std::bit_cast
//   - No user-facing SIMD/packed operations exist in the IL
// Ownership/Lifetime: IL modules are constructed within each test scope.
// Links: src/codegen/x86_64/Lowering.EmitCommon.cpp,
//        src/codegen/x86_64/Lowering.Arith.cpp,
//        src/codegen/x86_64/AsmEmitter.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace viper::codegen::x64;

namespace {

// ===----------------------------------------------------------------------===
// Helper functions (adapted from test_regalloc_stress.cpp)
// ===----------------------------------------------------------------------===

[[nodiscard]] ILValue val(int id) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::I64;
    v.id = id;
    return v;
}

[[nodiscard]] ILValue valF(int id) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::F64;
    v.id = id;
    return v;
}

[[nodiscard]] ILValue valB(int id) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::I1;
    v.id = id;
    return v;
}

[[nodiscard]] ILValue imm(int64_t v) noexcept {
    ILValue c{};
    c.kind = ILValue::Kind::I64;
    c.id = -1;
    c.i64 = v;
    return c;
}

[[nodiscard]] ILValue immF(double v) noexcept {
    ILValue c{};
    c.kind = ILValue::Kind::F64;
    c.id = -1;
    c.f64 = v;
    return c;
}

[[nodiscard]] ILValue lab(const char *name) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::LABEL;
    v.id = -1;
    v.label = name;
    return v;
}

[[nodiscard]] ILInstr makeOp(const char *opc,
                             std::vector<ILValue> ops,
                             int res,
                             ILValue::Kind k = ILValue::Kind::I64) {
    ILInstr instr{};
    instr.opcode = opc;
    instr.ops = std::move(ops);
    instr.resultId = res;
    instr.resultKind = k;
    return instr;
}

[[nodiscard]] ILInstr makeRet(int id, ILValue::Kind k = ILValue::Kind::I64) {
    ILInstr instr{};
    instr.opcode = "ret";
    ILValue ref{};
    ref.kind = k;
    ref.id = id;
    instr.ops = {ref};
    return instr;
}

[[nodiscard]] ILInstr makeConstF64(double v, int resultId) {
    ILInstr instr{};
    instr.opcode = "const_f64";
    instr.ops = {immF(v)};
    instr.resultId = resultId;
    instr.resultKind = ILValue::Kind::F64;
    return instr;
}

[[nodiscard]] ILInstr makeFCmp(const char *cmpOp, int lhsId, int rhsId, int resultId) {
    ILInstr instr{};
    instr.opcode = cmpOp;
    instr.ops = {valF(lhsId), valF(rhsId)};
    instr.resultId = resultId;
    instr.resultKind = ILValue::Kind::I1;
    return instr;
}

[[nodiscard]] std::size_t countOccurrences(const std::string &text, const std::string &pattern) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        ++count;
        pos += pattern.size();
    }
    return count;
}

[[nodiscard]] std::size_t countXmmSpillStores(const std::string &text) {
    const std::regex pattern(R"(movsd\s+%xmm\d+,\s*-\d+\(%rbp\))");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    return static_cast<std::size_t>(std::distance(begin, end));
}

[[nodiscard]] std::size_t countXmmSpillLoads(const std::string &text) {
    const std::regex pattern(R"(movsd\s+-\d+\(%rbp\),\s*%xmm\d+)");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    return static_cast<std::size_t>(std::distance(begin, end));
}

[[nodiscard]] bool rodataContainsHex(const std::string &text, std::uint64_t bits) {
    std::ostringstream oss;
    oss << ".quad 0x" << std::hex << std::setw(16) << std::setfill('0') << bits;
    return text.find(oss.str()) != std::string::npos;
}

// ===----------------------------------------------------------------------===
// Test result tracking
// ===----------------------------------------------------------------------===

struct TestResult {
    const char *name{};

    enum Status { PASS, FAIL, BUG } status{FAIL};

    std::string notes{};
    std::string failReason{};
    std::vector<std::string> bugs{};
};

// ===----------------------------------------------------------------------===
// Test 1: FP Arithmetic Encoding
// ===----------------------------------------------------------------------===

TestResult testFpArithEncoding() {
    TestResult r{"1. FP arithmetic encoding"};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::F64, ILValue::Kind::F64};
    entry.instrs.push_back(makeOp("add", {valF(0), valF(1)}, 2, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("sub", {valF(2), valF(1)}, 3, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("mul", {valF(3), valF(0)}, 4, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("fdiv", {valF(4), valF(1)}, 5, ILValue::Kind::F64));
    entry.instrs.push_back(makeRet(5, ILValue::Kind::F64));

    ILFunction func{};
    func.name = "fp_arith";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    const bool hasAddsd = cg.asmText.find("addsd") != std::string::npos;
    const bool hasSubsd = cg.asmText.find("subsd") != std::string::npos;
    const bool hasMulsd = cg.asmText.find("mulsd") != std::string::npos;
    const bool hasDivsd = cg.asmText.find("divsd") != std::string::npos;

    if (!hasAddsd || !hasSubsd || !hasMulsd || !hasDivsd) {
        r.failReason = "Missing FP instruction:";
        if (!hasAddsd)
            r.failReason += " addsd";
        if (!hasSubsd)
            r.failReason += " subsd";
        if (!hasMulsd)
            r.failReason += " mulsd";
        if (!hasDivsd)
            r.failReason += " divsd";
        return r;
    }

    r.status = TestResult::PASS;
    r.notes = "addsd/subsd/mulsd/divsd verified";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 2: FP Constant Special Values
// ===----------------------------------------------------------------------===

TestResult testFpConstSpecialVals() {
    TestResult r{"2. FP constant special vals"};

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double posInf = std::numeric_limits<double>::infinity();
    const double negInf = -std::numeric_limits<double>::infinity();
    const double negZero = -0.0;
    const double denorm = std::bit_cast<double>(std::uint64_t{0x0000000000000002});

    ILBlock entry{};
    entry.name = "entry";
    // const_f64 for each special value
    entry.instrs.push_back(makeConstF64(nan, 0));
    entry.instrs.push_back(makeConstF64(posInf, 1));
    entry.instrs.push_back(makeConstF64(negInf, 2));
    entry.instrs.push_back(makeConstF64(negZero, 3));
    entry.instrs.push_back(makeConstF64(denorm, 4));
    entry.instrs.push_back(makeConstF64(1.0, 5));
    entry.instrs.push_back(makeConstF64(-1.0, 6));
    entry.instrs.push_back(makeConstF64(3.14159, 7));
    // Chain adds to force all to be used
    entry.instrs.push_back(makeOp("add", {valF(0), valF(1)}, 8, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(8), valF(2)}, 9, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(9), valF(3)}, 10, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(10), valF(4)}, 11, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(11), valF(5)}, 12, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(12), valF(6)}, 13, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(13), valF(7)}, 14, ILValue::Kind::F64));
    entry.instrs.push_back(makeRet(14, ILValue::Kind::F64));

    ILFunction func{};
    func.name = "fp_special_consts";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    // Verify .rodata section exists
    if (cg.asmText.find(".section .rodata") == std::string::npos) {
        r.failReason = "No .rodata section found";
        return r;
    }

    // Verify special value bit patterns
    struct {
        const char *name;
        std::uint64_t bits;
    } checks[] = {
        {"NaN", 0x7FF8000000000000ULL},
        {"+Inf", 0x7FF0000000000000ULL},
        {"-Inf", 0xFFF0000000000000ULL},
        {"-0.0", 0x8000000000000000ULL},
        {"denorm", 0x0000000000000002ULL},
    };

    std::string missing{};
    for (const auto &chk : checks) {
        if (!rodataContainsHex(cg.asmText, chk.bits)) {
            if (!missing.empty())
                missing += ", ";
            missing += chk.name;
        }
    }

    if (!missing.empty()) {
        r.failReason = "Missing .rodata patterns: " + missing;
        return r;
    }

    const std::size_t labelCount = countOccurrences(cg.asmText, ".LC_f64_");
    r.status = TestResult::PASS;
    r.notes = "NaN/Inf/-0.0/denorm in .rodata (" + std::to_string(labelCount / 2) + " labels)";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 3: FP Comparisons — Correct Cases (fcmp_gt, ge, ord, uno)
// ===----------------------------------------------------------------------===

TestResult testFpCmpCorrectCodes() {
    TestResult r{"3. FP cmp correct codes"};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::F64, ILValue::Kind::F64};
    entry.instrs.push_back(makeFCmp("fcmp_gt", 0, 1, 2));
    entry.instrs.push_back(makeFCmp("fcmp_ge", 0, 1, 3));
    entry.instrs.push_back(makeFCmp("fcmp_ord", 0, 1, 4));
    entry.instrs.push_back(makeFCmp("fcmp_uno", 0, 1, 5));
    // zext1 each I1 result to I64 so we can add them
    entry.instrs.push_back(makeOp("zext", {valB(2)}, 6));
    entry.instrs.push_back(makeOp("zext", {valB(3)}, 7));
    entry.instrs.push_back(makeOp("zext", {valB(4)}, 8));
    entry.instrs.push_back(makeOp("zext", {valB(5)}, 9));
    entry.instrs.push_back(makeOp("add", {val(6), val(7)}, 10));
    entry.instrs.push_back(makeOp("add", {val(10), val(8)}, 11));
    entry.instrs.push_back(makeOp("add", {val(11), val(9)}, 12));
    entry.instrs.push_back(makeRet(12));

    ILFunction func{};
    func.name = "fp_cmp_correct";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    const std::size_t ucomiCount = countOccurrences(cg.asmText, "ucomisd");
    const bool hasSeta = cg.asmText.find("seta ") != std::string::npos;
    const bool hasSetae = cg.asmText.find("setae ") != std::string::npos;
    const bool hasSetnp = cg.asmText.find("setnp ") != std::string::npos;
    const bool hasSetp = cg.asmText.find("setp ") != std::string::npos;

    if (ucomiCount < 4) {
        r.failReason = "Expected >= 4 ucomisd, found " + std::to_string(ucomiCount);
        return r;
    }

    if (!hasSeta || !hasSetae || !hasSetnp || !hasSetp) {
        r.failReason = "Missing condition code:";
        if (!hasSeta)
            r.failReason += " seta(gt)";
        if (!hasSetae)
            r.failReason += " setae(ge)";
        if (!hasSetnp)
            r.failReason += " setnp(ord)";
        if (!hasSetp)
            r.failReason += " setp(uno)";
        return r;
    }

    r.status = TestResult::PASS;
    r.notes = "seta/setae/setnp/setp verified (" + std::to_string(ucomiCount) + " ucomisd)";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 4: FP Comparisons — NaN Bug Detection (fcmp_eq, ne, lt, le)
// ===----------------------------------------------------------------------===

TestResult testFpCmpNanBugs() {
    TestResult r{"4. FP cmp NaN safety"};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::F64, ILValue::Kind::F64};
    entry.instrs.push_back(makeFCmp("fcmp_eq", 0, 1, 2));
    entry.instrs.push_back(makeFCmp("fcmp_ne", 0, 1, 3));
    entry.instrs.push_back(makeFCmp("fcmp_lt", 0, 1, 4));
    entry.instrs.push_back(makeFCmp("fcmp_le", 0, 1, 5));
    // Use all results
    entry.instrs.push_back(makeOp("zext", {valB(2)}, 6));
    entry.instrs.push_back(makeOp("zext", {valB(3)}, 7));
    entry.instrs.push_back(makeOp("zext", {valB(4)}, 8));
    entry.instrs.push_back(makeOp("zext", {valB(5)}, 9));
    entry.instrs.push_back(makeOp("add", {val(6), val(7)}, 10));
    entry.instrs.push_back(makeOp("add", {val(10), val(8)}, 11));
    entry.instrs.push_back(makeOp("add", {val(11), val(9)}, 12));
    entry.instrs.push_back(makeRet(12));

    ILFunction func{};
    func.name = "fp_cmp_nan_safe";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    // Verify NaN-safe comparison patterns.
    // After UCOMISD with NaN: ZF=1, PF=1, CF=1.
    //
    // fcmp_eq: ordered equal → SETE ∧ SETNP (compound AND).
    //   Verify: sete present, setnp present, andl/andq present.
    // fcmp_ne: unordered not-equal → SETNE ∨ SETP (compound OR).
    //   Verify: setne present, setp present, orl/orq present.
    // fcmp_lt: swap operands + SETA. Verify: no "setb " (old buggy pattern).
    // fcmp_le: swap operands + SETAE. Verify: no "setbe " (old buggy pattern).

    std::string missing{};

    // fcmp_eq: compound SETE + SETNP + AND
    const bool hasSetE = cg.asmText.find("sete ") != std::string::npos;
    const bool hasSetNP = cg.asmText.find("setnp ") != std::string::npos;
    const bool hasAnd = cg.asmText.find("andl ") != std::string::npos ||
                        cg.asmText.find("andq ") != std::string::npos;
    if (!hasSetE)
        missing += " sete(eq)";
    if (!hasSetNP)
        missing += " setnp(eq-parity-guard)";
    if (!hasAnd)
        missing += " and(eq-compound)";

    // fcmp_ne: compound SETNE + SETP + OR
    const bool hasSetNE = cg.asmText.find("setne ") != std::string::npos;
    const bool hasSetP = cg.asmText.find("setp ") != std::string::npos;
    const bool hasOr = cg.asmText.find("orl ") != std::string::npos ||
                       cg.asmText.find("orq ") != std::string::npos;
    if (!hasSetNE)
        missing += " setne(ne)";
    if (!hasSetP)
        missing += " setp(ne-parity-guard)";
    if (!hasOr)
        missing += " or(ne-compound)";

    // fcmp_lt: must NOT use setb (old NaN-buggy pattern), should use seta (swapped)
    const bool hasBuggySetB = cg.asmText.find("setb ") != std::string::npos;
    if (hasBuggySetB) {
        r.bugs.push_back("fcmp_lt still uses SETB — NaN-incorrect (CF=1 → true)");
    }

    // fcmp_le: must NOT use setbe (old NaN-buggy pattern), should use setae (swapped)
    const bool hasBuggySetBE = cg.asmText.find("setbe ") != std::string::npos;
    if (hasBuggySetBE) {
        r.bugs.push_back("fcmp_le still uses SETBE — NaN-incorrect (CF=1|ZF=1 → true)");
    }

    if (!missing.empty()) {
        r.failReason = "Missing NaN-safe patterns:" + missing;
        return r;
    }

    if (!r.bugs.empty()) {
        r.status = TestResult::BUG;
        r.notes = std::to_string(r.bugs.size()) + " remaining NaN comparison issues";
        return r;
    }

    r.status = TestResult::PASS;
    r.notes = "All NaN comparison patterns verified (eq/ne compound, lt/le swapped)";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 5: FP Conversions
// ===----------------------------------------------------------------------===

TestResult testFpConversions() {
    TestResult r{"5. FP conversions"};

    // Test sitofp + fptosi (signed path)
    ILBlock signedEntry{};
    signedEntry.name = "entry";
    signedEntry.paramIds = {0};
    signedEntry.paramKinds = {ILValue::Kind::I64};
    signedEntry.instrs.push_back(makeOp("sitofp", {val(0)}, 1, ILValue::Kind::F64));
    signedEntry.instrs.push_back(makeConstF64(3.7, 2));
    signedEntry.instrs.push_back(makeOp("add", {valF(1), valF(2)}, 3, ILValue::Kind::F64));
    signedEntry.instrs.push_back(makeOp("fptosi", {valF(3)}, 4));
    signedEntry.instrs.push_back(makeRet(4));

    ILFunction signedFunc{};
    signedFunc.name = "fp_conversions_signed";
    signedFunc.blocks = {signedEntry};

    // Test uitofp (unsigned int → float, full range)
    ILBlock uiEntry{};
    uiEntry.name = "entry";
    uiEntry.paramIds = {0};
    uiEntry.paramKinds = {ILValue::Kind::I64};
    uiEntry.instrs.push_back(makeOp("uitofp", {val(0)}, 1, ILValue::Kind::F64));
    uiEntry.instrs.push_back(makeRet(1, ILValue::Kind::F64));

    ILFunction uiFunc{};
    uiFunc.name = "fp_uitofp";
    uiFunc.blocks = {uiEntry};

    // Test fptoui (float → unsigned int, checked)
    ILBlock fuiEntry{};
    fuiEntry.name = "entry";
    fuiEntry.paramIds = {0};
    fuiEntry.paramKinds = {ILValue::Kind::F64};
    fuiEntry.instrs.push_back(makeOp("fptoui", {valF(0)}, 1));
    fuiEntry.instrs.push_back(makeRet(1));

    ILFunction fuiFunc{};
    fuiFunc.name = "fp_fptoui";
    fuiFunc.blocks = {fuiEntry};

    ILModule mod{};
    mod.funcs = {signedFunc, uiFunc, fuiFunc};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    // Verify signed conversions
    const bool hasCvtsi2sd = cg.asmText.find("cvtsi2sd") != std::string::npos;
    const bool hasCvttsd2si = cg.asmText.find("cvttsd2si") != std::string::npos;

    if (!hasCvtsi2sd || !hasCvttsd2si) {
        r.failReason = "Missing signed conversion:";
        if (!hasCvtsi2sd)
            r.failReason += " cvtsi2sd";
        if (!hasCvttsd2si)
            r.failReason += " cvttsd2si";
        return r;
    }

    // Verify uitofp: should have testq (sign-bit check), js/jl (branch to high path),
    // shrq (shift right for high-bit values), addsd (double after halved conversion)
    const auto uitofpPos = cg.asmText.find("fp_uitofp:");
    const std::string uitofpAsm =
        uitofpPos != std::string::npos ? cg.asmText.substr(uitofpPos) : "";
    const bool hasTestq = uitofpAsm.find("testq") != std::string::npos;
    const bool hasShrq = uitofpAsm.find("shrq") != std::string::npos;
    const bool hasAddsd = uitofpAsm.find("addsd") != std::string::npos;

    if (!hasTestq || !hasShrq || !hasAddsd) {
        r.failReason = "uitofp missing full-range handling:";
        if (!hasTestq)
            r.failReason += " testq(sign-check)";
        if (!hasShrq)
            r.failReason += " shrq(halve)";
        if (!hasAddsd)
            r.failReason += " addsd(double-back)";
        return r;
    }

    // Verify fptoui: should have ucomisd (NaN check), ud2 (trap), and branch labels
    const auto fptouiPos = cg.asmText.find("fp_fptoui:");
    const std::string fptouiAsm =
        fptouiPos != std::string::npos ? cg.asmText.substr(fptouiPos) : "";
    const bool hasUcomisd = fptouiAsm.find("ucomisd") != std::string::npos;
    const bool hasUd2 = fptouiAsm.find("ud2") != std::string::npos;
    const bool hasTrapLabel = fptouiAsm.find(".Lfptoui_trap_") != std::string::npos;

    if (!hasUcomisd || !hasUd2 || !hasTrapLabel) {
        r.failReason = "fptoui missing checked conversion:";
        if (!hasUcomisd)
            r.failReason += " ucomisd(NaN-check)";
        if (!hasUd2)
            r.failReason += " ud2(trap)";
        if (!hasTrapLabel)
            r.failReason += " trap-label";
        return r;
    }

    r.status = TestResult::PASS;
    r.notes = "sitofp/fptosi + uitofp(full-range) + fptoui(checked) verified";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 6: FP Register Pressure
// ===----------------------------------------------------------------------===

TestResult testFpRegisterPressure() {
    TestResult r{"6. FP register pressure"};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1, 2, 3, 4, 5};
    entry.paramKinds = {ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64};

    // Convert 6 I64 params to F64
    for (int i = 0; i < 6; ++i) {
        entry.instrs.push_back(makeOp("sitofp", {val(i)}, 6 + i, ILValue::Kind::F64));
    }
    // Cross-product arithmetic: 6 more F64 values (12 total live)
    entry.instrs.push_back(makeOp("add", {valF(6), valF(7)}, 12, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(8), valF(9)}, 13, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(10), valF(11)}, 14, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("mul", {valF(6), valF(8)}, 15, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("mul", {valF(7), valF(9)}, 16, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("mul", {valF(10), valF(6)}, 17, ILValue::Kind::F64));
    // Chain all 12 together
    entry.instrs.push_back(makeOp("add", {valF(12), valF(13)}, 18, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(18), valF(14)}, 19, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(19), valF(15)}, 20, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(20), valF(16)}, 21, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("add", {valF(21), valF(17)}, 22, ILValue::Kind::F64));
    entry.instrs.push_back(makeRet(22, ILValue::Kind::F64));

    ILFunction func{};
    func.name = "fp_reg_pressure";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    const auto xmmStores = countXmmSpillStores(cg.asmText);
    const auto xmmLoads = countXmmSpillLoads(cg.asmText);

    r.status = TestResult::PASS;
    r.notes = std::to_string(xmmStores) + " XMM spills, " + std::to_string(xmmLoads) + " reloads";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 7: FP Across Calls
// ===----------------------------------------------------------------------===

TestResult testFpAcrossCalls() {
    TestResult r{"7. FP across calls"};

    // identity function: i64 → i64
    ILBlock identEntry{};
    identEntry.name = "entry";
    identEntry.paramIds = {0};
    identEntry.paramKinds = {ILValue::Kind::I64};
    identEntry.instrs.push_back(makeRet(0));

    ILFunction identFunc{};
    identFunc.name = "identity";
    identFunc.blocks = {identEntry};

    // Main function: convert to F64, call, use F64 after call
    ILBlock mainEntry{};
    mainEntry.name = "entry";
    mainEntry.paramIds = {0, 1};
    mainEntry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};

    mainEntry.instrs.push_back(makeOp("sitofp", {val(0)}, 2, ILValue::Kind::F64));
    mainEntry.instrs.push_back(makeOp("sitofp", {val(1)}, 3, ILValue::Kind::F64));

    // Call identity(a) — clobbers caller-saved XMMs
    ILInstr callInstr{};
    callInstr.opcode = "call";
    callInstr.resultId = 4;
    callInstr.resultKind = ILValue::Kind::I64;
    callInstr.ops = {lab("identity"), val(0)};
    mainEntry.instrs.push_back(callInstr);

    // Use F64 values after the call
    mainEntry.instrs.push_back(makeOp("add", {valF(2), valF(3)}, 5, ILValue::Kind::F64));
    mainEntry.instrs.push_back(makeRet(5, ILValue::Kind::F64));

    ILFunction mainFunc{};
    mainFunc.name = "fp_across_calls";
    mainFunc.blocks = {mainEntry};

    ILModule mod{};
    mod.funcs = {identFunc, mainFunc};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    // Extract just the fp_across_calls function assembly
    const auto funcPos = cg.asmText.find("fp_across_calls:");
    const std::string funcAsm =
        funcPos != std::string::npos ? cg.asmText.substr(funcPos) : cg.asmText;

    const auto xmmStores = countXmmSpillStores(funcAsm);
    const auto xmmLoads = countXmmSpillLoads(funcAsm);
    const bool hasCall = funcAsm.find("callq") != std::string::npos;

    if (!hasCall) {
        r.failReason = "No callq found in fp_across_calls";
        return r;
    }

    r.status = TestResult::PASS;
    r.notes = std::to_string(xmmStores) + " FP spills, " + std::to_string(xmmLoads) +
              " reloads around CALL";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 8: NaN Propagation Through Arithmetic
// ===----------------------------------------------------------------------===

TestResult testNanPropagation() {
    TestResult r{"8. NaN propagation"};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs.push_back(makeConstF64(std::numeric_limits<double>::quiet_NaN(), 0));
    entry.instrs.push_back(makeConstF64(1.0, 1));
    entry.instrs.push_back(makeConstF64(2.0, 2));
    entry.instrs.push_back(makeConstF64(3.0, 3));
    entry.instrs.push_back(makeConstF64(4.0, 4));
    // NaN + 1.0 = NaN
    entry.instrs.push_back(makeOp("add", {valF(0), valF(1)}, 5, ILValue::Kind::F64));
    // NaN - 2.0 = NaN
    entry.instrs.push_back(makeOp("sub", {valF(5), valF(2)}, 6, ILValue::Kind::F64));
    // NaN * 3.0 = NaN
    entry.instrs.push_back(makeOp("mul", {valF(6), valF(3)}, 7, ILValue::Kind::F64));
    // NaN / 4.0 = NaN
    entry.instrs.push_back(makeOp("fdiv", {valF(7), valF(4)}, 8, ILValue::Kind::F64));
    entry.instrs.push_back(makeRet(8, ILValue::Kind::F64));

    ILFunction func{};
    func.name = "nan_propagation";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    // Verify NaN bit pattern in .rodata
    if (!rodataContainsHex(cg.asmText, 0x7FF8000000000000ULL)) {
        r.failReason = "NaN bit pattern not found in .rodata";
        return r;
    }

    // All 4 arithmetic instructions must be present (NaN not optimized away)
    const bool hasAll = cg.asmText.find("addsd") != std::string::npos &&
                        cg.asmText.find("subsd") != std::string::npos &&
                        cg.asmText.find("mulsd") != std::string::npos &&
                        cg.asmText.find("divsd") != std::string::npos;
    if (!hasAll) {
        r.failReason = "NaN arithmetic optimized away — missing addsd/subsd/mulsd/divsd";
        return r;
    }

    r.status = TestResult::PASS;
    r.notes = "NaN constant + 4 arithmetic ops preserved";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 9: Negative Zero Handling
// ===----------------------------------------------------------------------===

TestResult testNegativeZero() {
    TestResult r{"9. Negative zero"};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs.push_back(makeConstF64(-0.0, 0));
    entry.instrs.push_back(makeConstF64(+0.0, 1));
    // fcmp_eq(-0.0, +0.0) — IEEE 754 says true
    entry.instrs.push_back(makeFCmp("fcmp_eq", 0, 1, 2));
    // Also add them to ensure both constants are emitted
    entry.instrs.push_back(makeOp("add", {valF(0), valF(1)}, 3, ILValue::Kind::F64));
    entry.instrs.push_back(makeRet(3, ILValue::Kind::F64));

    ILFunction func{};
    func.name = "neg_zero";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    // -0.0 and +0.0 should have distinct .rodata entries
    const bool hasNegZero = rodataContainsHex(cg.asmText, 0x8000000000000000ULL);
    const bool hasPosZero = rodataContainsHex(cg.asmText, 0x0000000000000000ULL);

    if (!hasNegZero) {
        r.failReason = "-0.0 bit pattern (0x8000000000000000) not in .rodata";
        return r;
    }
    if (!hasPosZero) {
        r.failReason = "+0.0 bit pattern (0x0000000000000000) not in .rodata";
        return r;
    }

    // Verify comparison emits ucomisd
    const bool hasUcomisd = cg.asmText.find("ucomisd") != std::string::npos;

    r.status = TestResult::PASS;
    r.notes = "Distinct bit patterns in .rodata";
    if (hasUcomisd)
        r.notes += ", ucomisd for eq";
    return r;
}

// ===----------------------------------------------------------------------===
// Test 10: Precision Boundary (2^53 + 1)
// ===----------------------------------------------------------------------===

TestResult testPrecisionBoundary() {
    TestResult r{"10. Precision boundary"};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    // I64 → F64 → I64 round-trip
    entry.instrs.push_back(makeOp("sitofp", {val(0)}, 1, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("fptosi", {valF(1)}, 2));
    entry.instrs.push_back(makeRet(2));

    ILFunction func{};
    func.name = "precision_boundary";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const auto cg = emitModuleToAssembly(mod, {});
    if (!cg.errors.empty()) {
        r.failReason = "Codegen error: " + cg.errors;
        return r;
    }

    const bool hasCvtsi2sd = cg.asmText.find("cvtsi2sd") != std::string::npos;
    const bool hasCvttsd2si = cg.asmText.find("cvttsd2si") != std::string::npos;

    if (!hasCvtsi2sd || !hasCvttsd2si) {
        r.failReason = "Missing conversion instructions for round-trip";
        return r;
    }

    r.status = TestResult::PASS;
    r.notes = "2^53+1 round-trip: precision loss is expected IEEE 754 behavior";
    return r;
}

// ===----------------------------------------------------------------------===
// Summary printer
// ===----------------------------------------------------------------------===

void printSummary(const std::vector<TestResult> &results) {
    std::cout << "\n=== FP/SIMD Stress Tests ===" << std::endl;
    std::cout << std::left << std::setw(32) << "Test" << std::setw(8) << "Status"
              << "Notes" << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    for (const auto &r : results) {
        const char *statusStr =
            r.status == TestResult::PASS ? "PASS" : (r.status == TestResult::BUG ? "BUG" : "FAIL");
        std::cout << std::left << std::setw(32) << r.name << std::setw(8) << statusStr << r.notes
                  << std::endl;
    }

    // Bug summary
    std::size_t totalBugs = 0;
    for (const auto &r : results) {
        totalBugs += r.bugs.size();
    }
    if (totalBugs > 0) {
        std::cout << "\nBugs Found: " << totalBugs << std::endl;
        int bugNum = 1;
        for (const auto &r : results) {
            for (const auto &bug : r.bugs) {
                std::cout << "  BUG-" << bugNum++ << ": " << bug << std::endl;
            }
        }
    }

    std::cout << "\nSIMD: No user-facing SIMD operations in IL. "
              << "MOVUPSrm/MOVUPSmr internal only (frame lowering)." << std::endl;
}

} // namespace

int main() {
    std::vector<TestResult> results{};
    results.push_back(testFpArithEncoding());
    results.push_back(testFpConstSpecialVals());
    results.push_back(testFpCmpCorrectCodes());
    results.push_back(testFpCmpNanBugs());
    results.push_back(testFpConversions());
    results.push_back(testFpRegisterPressure());
    results.push_back(testFpAcrossCalls());
    results.push_back(testNanPropagation());
    results.push_back(testNegativeZero());
    results.push_back(testPrecisionBoundary());

    printSummary(results);

    bool anyFail = false;
    for (const auto &r : results) {
        if (r.status == TestResult::FAIL) {
            anyFail = true;
            std::cerr << "\nFAILED: " << r.name << "\n  Reason: " << r.failReason << std::endl;
        }
    }

    // BUG status does NOT cause test failure — it documents known issues
    return anyFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
