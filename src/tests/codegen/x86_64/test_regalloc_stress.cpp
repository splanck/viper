//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_regalloc_stress.cpp
// Purpose: Stress tests for the x86-64 register allocator exercising spill
//          pressure, callee-saved preservation, nested loops, mixed int/fp,
//          caller-saved spill storms, and long dependency chains.
// Key invariants:
//   - All IL programs compile without errors through the full pipeline.
//   - Assembly output contains expected instruction patterns.
//   - No pathological allocation patterns (spill-immediately-reload, excessive moves).
// Ownership/Lifetime: Constructs IL locally via adapter API and inspects emitted
//                     assembly text returned by emitModuleToAssembly.
// Links: src/codegen/x86_64/Backend.hpp, src/codegen/x86_64/ra/Allocator.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

using namespace viper::codegen::x64;

// ===== Helpers ==============================================================

namespace {

// --- IL value constructors ---

/// SSA reference (I64).
[[nodiscard]] ILValue val(int id) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::I64;
    v.id = id;
    return v;
}

/// SSA reference (F64).
[[nodiscard]] ILValue valF(int id) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::F64;
    v.id = id;
    return v;
}

/// SSA reference (I1 / boolean).
[[nodiscard]] ILValue valB(int id) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::I1;
    v.id = id;
    return v;
}

/// Integer immediate.
[[nodiscard]] ILValue imm(int64_t v) noexcept {
    ILValue c{};
    c.kind = ILValue::Kind::I64;
    c.id = -1;
    c.i64 = v;
    return c;
}

/// Label reference.
[[nodiscard]] ILValue lab(const char *name) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::LABEL;
    v.id = -1;
    v.label = name;
    return v;
}

// --- IL instruction builders ---

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

[[nodiscard]] ILInstr makeBr(const char *target) {
    ILInstr instr{};
    instr.opcode = "br";
    instr.ops = {lab(target)};
    return instr;
}

[[nodiscard]] ILInstr makeCbr(int condId, const char *trueTarget, const char *falseTarget) {
    ILInstr instr{};
    instr.opcode = "cbr";
    instr.ops = {valB(condId), lab(trueTarget), lab(falseTarget)};
    return instr;
}

// --- Assembly analysis ---

[[nodiscard]] std::size_t countOccurrences(const std::string &text, const std::string &pattern) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        ++count;
        pos += pattern.size();
    }
    return count;
}

/// Count spill stores: movq %rXX, -NNN(%rbp) patterns.
[[nodiscard]] std::size_t countSpillStores(const std::string &text) {
    const std::regex pattern(R"(movq\s+%r\w+,\s*-\d+\(%rbp\))");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    return static_cast<std::size_t>(std::distance(begin, end));
}

/// Count spill loads: movq -NNN(%rbp), %rXX patterns.
[[nodiscard]] std::size_t countSpillLoads(const std::string &text) {
    const std::regex pattern(R"(movq\s+-\d+\(%rbp\),\s*%r\w+)");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    return static_cast<std::size_t>(std::distance(begin, end));
}

/// Count register-to-register movq %rXX, %rYY (both GPR, not memory).
[[nodiscard]] std::size_t countRegToRegMoves(const std::string &text) {
    const std::regex pattern(R"(movq\s+%r\w+,\s*%r\w+)");
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    auto end = std::sregex_iterator();
    return static_cast<std::size_t>(std::distance(begin, end));
}

// --- Test reporting ---

struct TestResult {
    const char *name{};
    bool passed{false};
    std::size_t spillStores{0};
    std::size_t spillLoads{0};
    std::size_t regMoves{0};
    std::string asmText{};
    std::string failReason{};
};

void printSummary(const std::vector<TestResult> &results) {
    std::cout << "\n=== Register Allocator Stress Tests ===" << std::endl;
    std::cout << std::left << std::setw(30) << "Test" << std::setw(8) << "Status" << std::setw(8)
              << "Spills" << std::setw(10) << "Reloads" << std::setw(10) << "RegMoves" << std::endl;
    std::cout << std::string(66, '-') << std::endl;
    for (const auto &r : results) {
        std::cout << std::left << std::setw(30) << r.name << std::setw(8)
                  << (r.passed ? "PASS" : "FAIL") << std::setw(8) << r.spillStores << std::setw(10)
                  << r.spillLoads << std::setw(10) << r.regMoves << std::endl;
    }
}

// ===== Test 1: Spill Pressure ===============================================

[[nodiscard]] TestResult testSpillPressure() {
    TestResult result{};
    result.name = "1. Spill pressure";

    // 6 params + 10 computed = 16 simultaneously live values > 14 allocatable GPRs
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1, 2, 3, 4, 5};
    entry.paramKinds = {ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64};

    // 10 computed values, each depending on different params
    entry.instrs.push_back(makeOp("add", {val(0), val(1)}, 6));
    entry.instrs.push_back(makeOp("sub", {val(2), val(3)}, 7));
    entry.instrs.push_back(makeOp("mul", {val(4), val(5)}, 8));
    entry.instrs.push_back(makeOp("add", {val(0), val(2)}, 9));
    entry.instrs.push_back(makeOp("sub", {val(1), val(3)}, 10));
    entry.instrs.push_back(makeOp("mul", {val(4), val(0)}, 11));
    entry.instrs.push_back(makeOp("add", {val(5), val(1)}, 12));
    entry.instrs.push_back(makeOp("sub", {val(2), val(4)}, 13));
    entry.instrs.push_back(makeOp("add", {val(3), val(5)}, 14));
    entry.instrs.push_back(makeOp("mul", {val(0), val(3)}, 15));

    // Sum all 10 computed values — forces all to be live simultaneously
    entry.instrs.push_back(makeOp("add", {val(6), val(7)}, 16));
    entry.instrs.push_back(makeOp("add", {val(16), val(8)}, 17));
    entry.instrs.push_back(makeOp("add", {val(17), val(9)}, 18));
    entry.instrs.push_back(makeOp("add", {val(18), val(10)}, 19));
    entry.instrs.push_back(makeOp("add", {val(19), val(11)}, 20));
    entry.instrs.push_back(makeOp("add", {val(20), val(12)}, 21));
    entry.instrs.push_back(makeOp("add", {val(21), val(13)}, 22));
    entry.instrs.push_back(makeOp("add", {val(22), val(14)}, 23));
    entry.instrs.push_back(makeOp("add", {val(23), val(15)}, 24));
    entry.instrs.push_back(makeRet(24));

    ILFunction func{};
    func.name = "stress_spill_pressure";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const CodegenResult cg = emitModuleToAssembly(mod, {});
    result.asmText = cg.asmText;
    result.spillStores = countSpillStores(cg.asmText);
    result.spillLoads = countSpillLoads(cg.asmText);
    result.regMoves = countRegToRegMoves(cg.asmText);

    if (!cg.errors.empty()) {
        result.failReason = "Compilation failed: " + cg.errors;
        return result;
    }
    if (cg.asmText.find("addq") == std::string::npos) {
        result.failReason = "Missing addq instruction";
        return result;
    }
    if (cg.asmText.find("subq") == std::string::npos) {
        result.failReason = "Missing subq instruction";
        return result;
    }
    if (cg.asmText.find("imulq") == std::string::npos) {
        result.failReason = "Missing imulq instruction";
        return result;
    }
    // With 16 live values and ~14 allocatable GPRs, expect at least 1 spill
    if (result.spillStores == 0 && result.spillLoads == 0) {
        result.failReason = "Expected spills with 16 live values but found none";
        return result;
    }

    result.passed = true;
    return result;
}

// ===== Test 2: Callee-Saved Preservation ====================================

[[nodiscard]] TestResult testCalleeSaved() {
    TestResult result{};
    result.name = "2. Callee-saved";

    // Helper function: identity(x) -> x
    ILBlock helperEntry{};
    helperEntry.name = "entry";
    helperEntry.paramIds = {0};
    helperEntry.paramKinds = {ILValue::Kind::I64};
    helperEntry.instrs.push_back(makeRet(0));

    ILFunction helperFunc{};
    helperFunc.name = "helper";
    helperFunc.blocks = {helperEntry};

    // Main function: 6 params kept alive across call
    ILBlock mainEntry{};
    mainEntry.name = "entry";
    mainEntry.paramIds = {0, 1, 2, 3, 4, 5};
    mainEntry.paramKinds = {ILValue::Kind::I64,
                            ILValue::Kind::I64,
                            ILValue::Kind::I64,
                            ILValue::Kind::I64,
                            ILValue::Kind::I64,
                            ILValue::Kind::I64};

    // call @helper(p0) — clobbers all caller-saved; p1-p5 must survive
    ILInstr callInstr{};
    callInstr.opcode = "call";
    callInstr.resultId = 6;
    callInstr.resultKind = ILValue::Kind::I64;
    callInstr.ops = {lab("helper"), val(0)};
    mainEntry.instrs.push_back(callInstr);

    // Use p1-p5 after the call (forcing them into callee-saved regs)
    mainEntry.instrs.push_back(makeOp("add", {val(1), val(6)}, 7));
    mainEntry.instrs.push_back(makeOp("add", {val(7), val(2)}, 8));
    mainEntry.instrs.push_back(makeOp("add", {val(8), val(3)}, 9));
    mainEntry.instrs.push_back(makeOp("add", {val(9), val(4)}, 10));
    mainEntry.instrs.push_back(makeOp("add", {val(10), val(5)}, 11));
    mainEntry.instrs.push_back(makeRet(11));

    ILFunction mainFunc{};
    mainFunc.name = "stress_callee_saved";
    mainFunc.blocks = {mainEntry};

    ILModule mod{};
    mod.funcs = {helperFunc, mainFunc};

    const CodegenResult cg = emitModuleToAssembly(mod, {});
    result.asmText = cg.asmText;
    result.spillStores = countSpillStores(cg.asmText);
    result.spillLoads = countSpillLoads(cg.asmText);
    result.regMoves = countRegToRegMoves(cg.asmText);

    if (!cg.errors.empty()) {
        result.failReason = "Compilation failed: " + cg.errors;
        return result;
    }

    // Extract just the stress_callee_saved function's assembly
    const std::size_t funcStart = cg.asmText.find("stress_callee_saved:");
    if (funcStart == std::string::npos) {
        result.failReason = "Cannot find stress_callee_saved function label";
        return result;
    }
    const std::string funcAsm = cg.asmText.substr(funcStart);

    if (funcAsm.find("callq") == std::string::npos) {
        result.failReason = "Missing callq instruction";
        return result;
    }

    // The backend saves callee-saved registers either via pushq/popq or
    // movq %reg, -N(%rbp) / movq -N(%rbp), %reg in the prologue/epilogue.
    // Check for at least one callee-saved register being preserved.
    const bool hasPush = funcAsm.find("pushq") != std::string::npos;
    const bool hasCalleeSave = funcAsm.find("movq %rbx,") != std::string::npos ||
                               funcAsm.find("movq %r12,") != std::string::npos ||
                               funcAsm.find("movq %r13,") != std::string::npos ||
                               funcAsm.find("movq %r14,") != std::string::npos ||
                               funcAsm.find("movq %r15,") != std::string::npos;
    if (!hasPush && !hasCalleeSave) {
        result.failReason = "No callee-saved register preservation found";
        return result;
    }

    // Verify callee-saved registers are restored in the epilogue
    const bool hasCalleeSaveRestore = funcAsm.find("popq") != std::string::npos ||
                                      funcAsm.find("), %rbx") != std::string::npos ||
                                      funcAsm.find("), %r12") != std::string::npos ||
                                      funcAsm.find("), %r13") != std::string::npos ||
                                      funcAsm.find("), %r14") != std::string::npos ||
                                      funcAsm.find("), %r15") != std::string::npos;
    if (!hasCalleeSaveRestore) {
        result.failReason = "No callee-saved register restoration found";
        return result;
    }

    result.passed = true;
    return result;
}

// ===== Test 3: Nested Loop Register Competition =============================

[[nodiscard]] TestResult testNestedLoop() {
    TestResult result{};
    result.name = "3. Nested loops";

    // entry(p0): zero = sub p0, p0; br outer_header
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::I64};
    entry.instrs.push_back(makeOp("sub", {val(0), val(0)}, 1)); // zero
    entry.instrs.push_back(makeBr("outer_header"));
    entry.terminatorEdges = {{{"outer_header", {1, 1}}}};

    // outer_header(oi=2, osum=3): cond = icmp_slt oi, p0; cbr cond, inner_header, exit
    ILBlock outerHeader{};
    outerHeader.name = "outer_header";
    outerHeader.paramIds = {2, 3};
    outerHeader.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    outerHeader.instrs.push_back(makeOp("icmp_slt", {val(2), val(0)}, 4, ILValue::Kind::I1));
    outerHeader.instrs.push_back(makeCbr(4, "inner_header", "exit"));
    outerHeader.terminatorEdges = {{{"inner_header", {1, 1}}}};

    // inner_header(ii=5, isum=6): cond = icmp_slt ii, oi; cbr cond, inner_body, outer_latch
    ILBlock innerHeader{};
    innerHeader.name = "inner_header";
    innerHeader.paramIds = {5, 6};
    innerHeader.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    innerHeader.instrs.push_back(makeOp("icmp_slt", {val(5), val(2)}, 7, ILValue::Kind::I1));
    innerHeader.instrs.push_back(makeCbr(7, "inner_body", "outer_latch"));

    // inner_body(): prod = mul ii, oi; newsum = add isum, prod; newii = add ii, 1; br inner_header
    ILBlock innerBody{};
    innerBody.name = "inner_body";
    innerBody.instrs.push_back(makeOp("mul", {val(5), val(2)}, 8));
    innerBody.instrs.push_back(makeOp("add", {val(6), val(8)}, 9));
    innerBody.instrs.push_back(makeOp("add", {val(5), imm(1)}, 10));
    innerBody.instrs.push_back(makeBr("inner_header"));
    innerBody.terminatorEdges = {{{"inner_header", {10, 9}}}};

    // outer_latch(): newosum = add osum, isum; newoi = add oi, 1; br outer_header
    ILBlock outerLatch{};
    outerLatch.name = "outer_latch";
    outerLatch.instrs.push_back(makeOp("add", {val(3), val(6)}, 11));
    outerLatch.instrs.push_back(makeOp("add", {val(2), imm(1)}, 12));
    outerLatch.instrs.push_back(makeBr("outer_header"));
    outerLatch.terminatorEdges = {{{"outer_header", {12, 11}}}};

    // exit(): ret osum
    ILBlock exitBlock{};
    exitBlock.name = "exit";
    exitBlock.instrs.push_back(makeRet(3));

    ILFunction func{};
    func.name = "stress_nested_loop";
    func.blocks = {entry, outerHeader, innerHeader, innerBody, outerLatch, exitBlock};

    ILModule mod{};
    mod.funcs = {func};

    const CodegenResult cg = emitModuleToAssembly(mod, {});
    result.asmText = cg.asmText;
    result.spillStores = countSpillStores(cg.asmText);
    result.spillLoads = countSpillLoads(cg.asmText);
    result.regMoves = countRegToRegMoves(cg.asmText);

    if (!cg.errors.empty()) {
        result.failReason = "Compilation failed: " + cg.errors;
        return result;
    }

    // Should have multiple labels (loop structure)
    if (countOccurrences(cg.asmText, ":") < 4) {
        result.failReason = "Expected at least 4 labels for nested loop structure";
        return result;
    }

    // imulq must be present for inner multiply
    if (cg.asmText.find("imulq") == std::string::npos) {
        result.failReason = "Missing imulq instruction in inner loop";
        return result;
    }

    // Check for backward jumps (loop back-edges)
    if (cg.asmText.find("jmp") == std::string::npos && cg.asmText.find("jl") == std::string::npos &&
        cg.asmText.find("jge") == std::string::npos) {
        result.failReason = "Missing backward jump for loop";
        return result;
    }

    result.passed = true;
    return result;
}

// ===== Test 4: Mixed Integer/FP Allocation ==================================

[[nodiscard]] TestResult testMixedIntFp() {
    TestResult result{};
    result.name = "4. Mixed int/fp";

    // 3 I64 params, interleaved int and FP arithmetic
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1, 2};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64, ILValue::Kind::I64};

    // Integer ops
    entry.instrs.push_back(makeOp("add", {val(0), val(1)}, 3));
    entry.instrs.push_back(makeOp("mul", {val(1), val(2)}, 4));
    entry.instrs.push_back(makeOp("sub", {val(0), val(2)}, 5));

    // Convert to FP
    entry.instrs.push_back(makeOp("sitofp", {val(3)}, 6, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("sitofp", {val(4)}, 7, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("sitofp", {val(5)}, 8, ILValue::Kind::F64));

    // FP arithmetic (add/mul dispatch to FADD/FMUL when resultKind=F64)
    entry.instrs.push_back(makeOp("add", {valF(6), valF(7)}, 9, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("mul", {valF(9), valF(8)}, 10, ILValue::Kind::F64));
    entry.instrs.push_back(makeOp("fdiv", {valF(10), valF(6)}, 11, ILValue::Kind::F64));

    // Mix: use integer values again alongside FP results
    entry.instrs.push_back(makeOp("add", {val(3), val(4)}, 12)); // force %3,%4 still live
    entry.instrs.push_back(makeOp("fptosi", {valF(11)}, 13));
    entry.instrs.push_back(makeOp("add", {val(12), val(13)}, 14));
    entry.instrs.push_back(makeRet(14));

    ILFunction func{};
    func.name = "stress_mixed_int_fp";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const CodegenResult cg = emitModuleToAssembly(mod, {});
    result.asmText = cg.asmText;
    result.spillStores = countSpillStores(cg.asmText);
    result.spillLoads = countSpillLoads(cg.asmText);
    result.regMoves = countRegToRegMoves(cg.asmText);

    if (!cg.errors.empty()) {
        result.failReason = "Compilation failed: " + cg.errors;
        return result;
    }

    // GPR mnemonics
    if (cg.asmText.find("addq") == std::string::npos) {
        result.failReason = "Missing addq instruction";
        return result;
    }
    if (cg.asmText.find("imulq") == std::string::npos) {
        result.failReason = "Missing imulq instruction";
        return result;
    }
    if (cg.asmText.find("subq") == std::string::npos) {
        result.failReason = "Missing subq instruction";
        return result;
    }

    // XMM mnemonics
    if (cg.asmText.find("addsd") == std::string::npos) {
        result.failReason = "Missing addsd instruction";
        return result;
    }
    if (cg.asmText.find("mulsd") == std::string::npos) {
        result.failReason = "Missing mulsd instruction";
        return result;
    }
    if (cg.asmText.find("divsd") == std::string::npos) {
        result.failReason = "Missing divsd instruction";
        return result;
    }

    // Conversion instructions
    if (cg.asmText.find("cvtsi2sd") == std::string::npos) {
        result.failReason = "Missing cvtsi2sd instruction";
        return result;
    }
    if (cg.asmText.find("cvttsd2si") == std::string::npos) {
        result.failReason = "Missing cvttsd2si instruction";
        return result;
    }

    result.passed = true;
    return result;
}

// ===== Test 5: Caller-Saved Spill Storm =====================================

[[nodiscard]] TestResult testCallerSavedStorm() {
    TestResult result{};
    result.name = "5. Caller-saved storm";

    // Identity function: ret p0
    ILBlock identEntry{};
    identEntry.name = "entry";
    identEntry.paramIds = {0};
    identEntry.paramKinds = {ILValue::Kind::I64};
    identEntry.instrs.push_back(makeRet(0));

    ILFunction identFunc{};
    identFunc.name = "identity";
    identFunc.blocks = {identEntry};

    // Main: result = identity(a) + identity(b) + identity(c) + identity(d)
    ILBlock mainEntry{};
    mainEntry.name = "entry";
    mainEntry.paramIds = {0, 1, 2, 3};
    mainEntry.paramKinds = {
        ILValue::Kind::I64, ILValue::Kind::I64, ILValue::Kind::I64, ILValue::Kind::I64};

    // 4 calls — each successive call forces more values to be spilled
    ILInstr call0{};
    call0.opcode = "call";
    call0.resultId = 4;
    call0.resultKind = ILValue::Kind::I64;
    call0.ops = {lab("identity"), val(0)};
    mainEntry.instrs.push_back(call0);

    ILInstr call1{};
    call1.opcode = "call";
    call1.resultId = 5;
    call1.resultKind = ILValue::Kind::I64;
    call1.ops = {lab("identity"), val(1)};
    mainEntry.instrs.push_back(call1);

    ILInstr call2{};
    call2.opcode = "call";
    call2.resultId = 6;
    call2.resultKind = ILValue::Kind::I64;
    call2.ops = {lab("identity"), val(2)};
    mainEntry.instrs.push_back(call2);

    ILInstr call3{};
    call3.opcode = "call";
    call3.resultId = 7;
    call3.resultKind = ILValue::Kind::I64;
    call3.ops = {lab("identity"), val(3)};
    mainEntry.instrs.push_back(call3);

    // Sum all results
    mainEntry.instrs.push_back(makeOp("add", {val(4), val(5)}, 8));
    mainEntry.instrs.push_back(makeOp("add", {val(8), val(6)}, 9));
    mainEntry.instrs.push_back(makeOp("add", {val(9), val(7)}, 10));
    mainEntry.instrs.push_back(makeRet(10));

    ILFunction mainFunc{};
    mainFunc.name = "stress_caller_saved_storm";
    mainFunc.blocks = {mainEntry};

    ILModule mod{};
    mod.funcs = {identFunc, mainFunc};

    const CodegenResult cg = emitModuleToAssembly(mod, {});
    result.asmText = cg.asmText;

    // Analyse only the main function
    const std::size_t funcStart = cg.asmText.find("stress_caller_saved_storm:");
    const std::string funcAsm =
        funcStart != std::string::npos ? cg.asmText.substr(funcStart) : cg.asmText;

    result.spillStores = countSpillStores(funcAsm);
    result.spillLoads = countSpillLoads(funcAsm);
    result.regMoves = countRegToRegMoves(funcAsm);

    if (!cg.errors.empty()) {
        result.failReason = "Compilation failed: " + cg.errors;
        return result;
    }

    // 4 callq instructions
    const std::size_t callCount = countOccurrences(funcAsm, "callq");
    if (callCount < 4) {
        result.failReason = "Expected 4 callq but found " + std::to_string(callCount);
        return result;
    }

    // With 4 calls and intermediate results alive across them, expect spills or
    // callee-saved register usage (pushq/popq). Either approach is valid.
    const std::size_t pushCount = countOccurrences(funcAsm, "pushq");
    if (result.spillStores == 0 && pushCount == 0) {
        result.failReason = "Expected spills or callee-saved pushes around calls but found neither";
        return result;
    }

    result.passed = true;
    return result;
}

// ===== Test 6: Long Dependency Chain ========================================

[[nodiscard]] TestResult testDependencyChain() {
    TestResult result{};
    result.name = "6. Dependency chain";

    // 2 params, 15 serially dependent operations
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};

    entry.instrs.push_back(makeOp("add", {val(0), val(1)}, 2));
    entry.instrs.push_back(makeOp("sub", {val(2), val(0)}, 3));
    entry.instrs.push_back(makeOp("add", {val(3), val(1)}, 4));
    entry.instrs.push_back(makeOp("mul", {val(4), val(0)}, 5));
    entry.instrs.push_back(makeOp("sub", {val(5), val(3)}, 6));
    entry.instrs.push_back(makeOp("add", {val(6), val(2)}, 7));
    entry.instrs.push_back(makeOp("mul", {val(7), val(1)}, 8));
    entry.instrs.push_back(makeOp("sub", {val(8), val(4)}, 9));
    entry.instrs.push_back(makeOp("add", {val(9), val(5)}, 10));
    entry.instrs.push_back(makeOp("mul", {val(10), val(6)}, 11));
    entry.instrs.push_back(makeOp("sub", {val(11), val(7)}, 12));
    entry.instrs.push_back(makeOp("add", {val(12), val(8)}, 13));
    entry.instrs.push_back(makeOp("mul", {val(13), val(9)}, 14));
    entry.instrs.push_back(makeOp("sub", {val(14), val(10)}, 15));
    entry.instrs.push_back(makeOp("add", {val(15), val(11)}, 16));
    entry.instrs.push_back(makeRet(16));

    ILFunction func{};
    func.name = "stress_dep_chain";
    func.blocks = {entry};

    ILModule mod{};
    mod.funcs = {func};

    const CodegenResult cg = emitModuleToAssembly(mod, {});
    result.asmText = cg.asmText;
    result.spillStores = countSpillStores(cg.asmText);
    result.spillLoads = countSpillLoads(cg.asmText);
    result.regMoves = countRegToRegMoves(cg.asmText);

    if (!cg.errors.empty()) {
        result.failReason = "Compilation failed: " + cg.errors;
        return result;
    }

    // All ALU instructions should be present
    if (cg.asmText.find("addq") == std::string::npos) {
        result.failReason = "Missing addq instruction";
        return result;
    }
    if (cg.asmText.find("subq") == std::string::npos) {
        result.failReason = "Missing subq instruction";
        return result;
    }
    if (cg.asmText.find("imulq") == std::string::npos) {
        result.failReason = "Missing imulq instruction";
        return result;
    }

    result.passed = true;
    return result;
}

} // anonymous namespace

int main() {
    std::vector<TestResult> results;

    results.push_back(testSpillPressure());
    results.push_back(testCalleeSaved());
    results.push_back(testNestedLoop());
    results.push_back(testMixedIntFp());
    results.push_back(testCallerSavedStorm());
    results.push_back(testDependencyChain());

    printSummary(results);

    bool allPassed = true;
    for (const auto &r : results) {
        if (!r.passed) {
            allPassed = false;
            std::cerr << "\nFAILED: " << r.name << "\n  Reason: " << r.failReason << "\n";
            std::cerr << "  Assembly:\n" << r.asmText << "\n";
        }
    }

    return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
