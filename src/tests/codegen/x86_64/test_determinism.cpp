//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_determinism.cpp
// Purpose: Verify that the x86-64 backend is fully deterministic — compiling
//          the same ILModule produces byte-identical assembly every time.
//          Exercises all potential non-determinism sources: hash map iteration
//          order in ISel/regalloc, rodata pool dedup ordering, multi-function
//          emission order, block layout stability, and pointer-address
//          independence.
// Key invariants:
//   - Same ILModule compiled N times → identical assembly each time
//   - Module constructed separately (different heap addresses) → same output
//   - No pointer values, timestamps, or RNG leak into generated assembly
//   - Register allocator spill decisions are deterministic under pressure
// Ownership/Lifetime: IL modules constructed within each test scope.
// Links: src/codegen/x86_64/Backend.hpp,
//        src/codegen/x86_64/ra/Allocator.cpp,
//        src/codegen/x86_64/ISel.cpp,
//        src/codegen/x86_64/AsmEmitter.cpp
//
// Cross-compilation note: The x86-64 backend is pure computation on in-memory
// data structures with no #ifdef'd codegen logic.  Assembly output depends only
// on the ILModule content and the target ABI (SysV vs Win64), not on the host
// architecture.  Cross-arch determinism is guaranteed by construction and cannot
// be tested within a single binary.  The separate-construction test (Test 6)
// validates that no host-specific runtime state (pointer addresses, ASLR) leaks
// into output.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Backend.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace viper::codegen::x64;

namespace {

// ===----------------------------------------------------------------------===
// Helper functions
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

[[nodiscard]] ILValue strLit(const char *s, std::size_t len) noexcept {
    ILValue v{};
    v.kind = ILValue::Kind::STR;
    v.id = -1;
    v.str = s;
    v.strLen = len;
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

[[nodiscard]] ILInstr makeRetImm(int64_t v) {
    ILInstr instr{};
    instr.opcode = "ret";
    instr.ops = {imm(v)};
    instr.resultId = -1;
    return instr;
}

[[nodiscard]] ILInstr makeBr(const char *target) {
    ILInstr instr{};
    instr.opcode = "br";
    instr.ops = {lab(target)};
    instr.resultId = -1;
    return instr;
}

[[nodiscard]] ILInstr makeCbr(int condId, const char *trueTarget, const char *falseTarget) {
    ILInstr instr{};
    instr.opcode = "cbr";
    instr.ops = {valB(condId), lab(trueTarget), lab(falseTarget)};
    instr.resultId = -1;
    return instr;
}

[[nodiscard]] ILInstr makeCmp(int lhsId, int rhsId, int condCode, int resId) {
    ILInstr instr{};
    instr.opcode = "cmp";
    instr.ops = {val(lhsId), val(rhsId), imm(condCode)};
    instr.resultId = resId;
    instr.resultKind = ILValue::Kind::I1;
    return instr;
}

[[nodiscard]] ILInstr makeSwitch(int scrutId,
                                 const std::vector<std::pair<int64_t, const char *>> &cases,
                                 const char *defaultLabel) {
    ILInstr instr{};
    instr.opcode = "switch_i32";
    instr.ops.push_back(val(scrutId));
    for (const auto &[caseVal, caseLab] : cases) {
        instr.ops.push_back(imm(caseVal));
        instr.ops.push_back(lab(caseLab));
    }
    instr.ops.push_back(lab(defaultLabel));
    instr.resultId = -1;
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

[[nodiscard]] std::string compileToAsm(const ILModule &m) {
    const CodegenResult res = emitModuleToAssembly(m, {});
    if (!res.errors.empty()) {
        std::cerr << "Compilation error: " << res.errors << "\n";
    }
    return res.asmText;
}

// ===----------------------------------------------------------------------===
// Label normalization for static counter labels
// ===----------------------------------------------------------------------===

/// Replace all occurrences of a pattern prefix followed by digits with a stable placeholder.
/// E.g., normalizePrefix(text, ".Lfptoui_trap_") replaces ".Lfptoui_trap_42" with
/// ".Lfptoui_trap_X".
void normalizePrefix(std::string &text, const std::string &prefix) {
    std::size_t pos = 0;
    while ((pos = text.find(prefix, pos)) != std::string::npos) {
        const std::size_t digitStart = pos + prefix.size();
        std::size_t digitEnd = digitStart;
        while (digitEnd < text.size() && text[digitEnd] >= '0' && text[digitEnd] <= '9') {
            ++digitEnd;
        }
        if (digitEnd > digitStart) {
            text.replace(digitStart, digitEnd - digitStart, "X");
        }
        pos = digitStart + 1;
    }
}

[[nodiscard]] std::string normalizeCounterLabels(const std::string &text) {
    std::string result = text;
    normalizePrefix(result, ".Lfptoui_trap_");
    normalizePrefix(result, ".Lfptoui_sm_");
    normalizePrefix(result, ".Lfptoui_done_");
    normalizePrefix(result, ".Luitofp_hi_");
    normalizePrefix(result, ".Luitofp_done_");
    normalizePrefix(result, ".Lidxchk_u_");
    normalizePrefix(result, ".Lidxchk_l_");
    return result;
}

// ===----------------------------------------------------------------------===
// First-difference diagnostic
// ===----------------------------------------------------------------------===

/// Find the line number and character position of the first difference between two strings.
void printFirstDifference(const std::string &a, const std::string &b) {
    int line = 1;
    std::size_t lineStart = 0;
    const std::size_t minLen = std::min(a.size(), b.size());

    for (std::size_t i = 0; i < minLen; ++i) {
        if (a[i] != b[i]) {
            // Find the full line in both strings
            std::size_t aEnd = a.find('\n', lineStart);
            if (aEnd == std::string::npos)
                aEnd = a.size();
            std::size_t bEnd = b.find('\n', lineStart);
            if (bEnd == std::string::npos)
                bEnd = b.size();

            std::cerr << "  first difference at line " << line << ", col " << (i - lineStart + 1)
                      << "\n";
            std::cerr << "  baseline: " << a.substr(lineStart, aEnd - lineStart) << "\n";
            std::cerr << "  current:  " << b.substr(lineStart, bEnd - lineStart) << "\n";
            return;
        }
        if (a[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }

    if (a.size() != b.size()) {
        std::cerr << "  strings have different lengths: " << a.size() << " vs " << b.size() << "\n";
    }
}

// ===----------------------------------------------------------------------===
// Test bookkeeping
// ===----------------------------------------------------------------------===

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

    void checkEqual(const char *caseName, const std::string &baseline, const std::string &current) {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        if (baseline == current) {
            ++cat.pass;
        } else {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName << "\n";
            printFirstDifference(baseline, current);
        }
    }

    void checkEqualNormalized(const char *caseName,
                              const std::string &baseline,
                              const std::string &current) {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        const auto normA = normalizeCounterLabels(baseline);
        const auto normB = normalizeCounterLabels(current);

        if (normA == normB) {
            ++cat.pass;
        } else {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName
                      << " (differs even after label normalization)\n";
            printFirstDifference(normA, normB);
        }
    }

    void checkAsm(const char *caseName, const std::string &asmText, const std::string &expected) {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        if (asmText.find(expected) != std::string::npos) {
            ++cat.pass;
        } else {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName << "\n"
                      << "  expected substring: \"" << expected << "\"\n";
        }
    }

    void checkOrder(const char *caseName,
                    const std::string &text,
                    const std::string &first,
                    const std::string &second) {
        auto &cat = categories[static_cast<std::size_t>(currentCat)];
        ++cat.total;

        const auto posA = text.find(first);
        const auto posB = text.find(second);
        if (posA != std::string::npos && posB != std::string::npos && posA < posB) {
            ++cat.pass;
        } else {
            ++cat.fail;
            ++globalFail;
            std::cerr << "FAIL [" << cat.name << "] " << caseName << "\n"
                      << "  expected \"" << first << "\" before \"" << second << "\"\n";
        }
    }

    void printSummary() const {
        std::cout << "\n=== x86-64 Determinism Stress Test ===\n";
        std::cout << "Category                   Total  Pass  Fail\n";
        int totalAll = 0, passAll = 0, failAll = 0;
        for (const auto &cat : categories) {
            std::string padded = cat.name;
            while (padded.size() < 27)
                padded.push_back(' ');
            std::cout << padded << " " << cat.total << "     " << cat.pass << "     " << cat.fail
                      << "\n";
            totalAll += cat.total;
            passAll += cat.pass;
            failAll += cat.fail;
        }
        std::cout << "------------------------------------------------\n";
        std::cout << "TOTAL                       " << totalAll << "    " << passAll << "     "
                  << failAll << "\n\n";
    }
};

// ===----------------------------------------------------------------------===
// Canonical module builder
// ===----------------------------------------------------------------------===

/// Build a non-trivial multi-function module from scratch.
/// Each call allocates fresh strings/vectors at different heap addresses.
/// Deliberately avoids fptoui/uitofp/idx_check (static counter issue).
[[nodiscard]] ILModule buildCanonicalModule() {
    // Function 1: canonical_arith(a, b) — arithmetic + diamond if/else
    ILBlock arithEntry{};
    arithEntry.name = "entry";
    arithEntry.paramIds = {0, 1};
    arithEntry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    arithEntry.instrs = {
        makeOp("add", {val(0), val(1)}, 2),
        makeOp("sub", {val(2), val(0)}, 3),
        makeOp("mul", {val(3), val(1)}, 4),
        makeCmp(4, 1, 4 /*sgt*/, 5),
        makeCbr(5, "arith_then", "arith_else"),
    };

    ILBlock arithThen{};
    arithThen.name = "arith_then";
    arithThen.instrs = {
        makeOp("sub", {val(4), val(1)}, 6),
        makeRet(6),
    };

    ILBlock arithElse{};
    arithElse.name = "arith_else";
    arithElse.instrs = {
        makeOp("add", {val(4), val(1)}, 7),
        makeRet(7),
    };

    ILFunction fnArith{};
    fnArith.name = "canonical_arith";
    fnArith.blocks = {arithEntry, arithThen, arithElse};

    // Function 2: canonical_helper(x) — identity
    ILBlock helperEntry{};
    helperEntry.name = "entry";
    helperEntry.paramIds = {0};
    helperEntry.paramKinds = {ILValue::Kind::I64};
    helperEntry.instrs = {makeRet(0)};

    ILFunction fnHelper{};
    fnHelper.name = "canonical_helper";
    fnHelper.blocks = {helperEntry};

    // Function 3: canonical_main(a, b, c, d) — calls + switch
    ILBlock mainEntry{};
    mainEntry.name = "entry";
    mainEntry.paramIds = {0, 1, 2, 3};
    mainEntry.paramKinds = {
        ILValue::Kind::I64, ILValue::Kind::I64, ILValue::Kind::I64, ILValue::Kind::I64};

    ILInstr callHelper{};
    callHelper.opcode = "call";
    callHelper.ops = {lab("canonical_helper"), val(0)};
    callHelper.resultId = 10;
    callHelper.resultKind = ILValue::Kind::I64;

    ILInstr callArith{};
    callArith.opcode = "call";
    callArith.ops = {lab("canonical_arith"), val(1), val(2)};
    callArith.resultId = 11;
    callArith.resultKind = ILValue::Kind::I64;

    mainEntry.instrs = {
        callHelper,
        callArith,
        makeOp("add", {val(10), val(11)}, 12),
        makeSwitch(3, {{1, "case1"}, {2, "case2"}, {3, "case3"}}, "default_blk"),
    };

    auto makeRetBlock = [](const char *name, int baseId, int64_t addend) {
        ILBlock blk{};
        blk.name = name;
        blk.instrs = {
            makeOp("add", {val(baseId), imm(addend)}, baseId + 100),
            makeRet(baseId + 100),
        };
        return blk;
    };

    ILFunction fnMain{};
    fnMain.name = "canonical_main";
    fnMain.blocks = {mainEntry,
                     makeRetBlock("case1", 12, 100),
                     makeRetBlock("case2", 12, 200),
                     makeRetBlock("case3", 12, 300),
                     makeRetBlock("default_blk", 12, 0)};

    ILModule mod{};
    mod.funcs = {fnArith, fnHelper, fnMain};
    return mod;
}

// ===----------------------------------------------------------------------===
// Test 1: Repeated compilation (N=100)
// ===----------------------------------------------------------------------===

void testRepeatedCompilation(TestContext &ctx) {
    ctx.beginCategory("Repeated (N=100)");

    const ILModule mod = buildCanonicalModule();
    const auto baseline = compileToAsm(mod);

    for (int i = 1; i < 100; ++i) {
        const auto current = compileToAsm(mod);
        ctx.checkEqual(("iter_" + std::to_string(i)).c_str(), baseline, current);
    }
}

// ===----------------------------------------------------------------------===
// Test 2: Register allocator pressure (N=50)
// ===----------------------------------------------------------------------===

void testRegAllocPressure(TestContext &ctx) {
    ctx.beginCategory("RegAlloc pressure (N=50)");

    // 6 params + 10 computed values = 16 simultaneously live, exceeding 14 GPRs.
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1, 2, 3, 4, 5};
    entry.paramKinds = {ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64,
                        ILValue::Kind::I64};

    // 10 computed values from different param combinations
    entry.instrs = {
        makeOp("add", {val(0), val(1)}, 10),
        makeOp("sub", {val(2), val(3)}, 11),
        makeOp("mul", {val(4), val(5)}, 12),
        makeOp("add", {val(0), val(3)}, 13),
        makeOp("sub", {val(1), val(4)}, 14),
        makeOp("mul", {val(2), val(5)}, 15),
        makeOp("add", {val(0), val(5)}, 16),
        makeOp("sub", {val(3), val(4)}, 17),
        makeOp("mul", {val(1), val(2)}, 18),
        makeOp("add", {val(4), val(0)}, 19),
    };

    // Sum all 10 to keep them all live
    entry.instrs.push_back(makeOp("add", {val(10), val(11)}, 20));
    entry.instrs.push_back(makeOp("add", {val(20), val(12)}, 21));
    entry.instrs.push_back(makeOp("add", {val(21), val(13)}, 22));
    entry.instrs.push_back(makeOp("add", {val(22), val(14)}, 23));
    entry.instrs.push_back(makeOp("add", {val(23), val(15)}, 24));
    entry.instrs.push_back(makeOp("add", {val(24), val(16)}, 25));
    entry.instrs.push_back(makeOp("add", {val(25), val(17)}, 26));
    entry.instrs.push_back(makeOp("add", {val(26), val(18)}, 27));
    entry.instrs.push_back(makeOp("add", {val(27), val(19)}, 28));
    entry.instrs.push_back(makeRet(28));

    ILFunction fn{};
    fn.name = "regpressure";
    fn.blocks = {entry};

    ILModule mod{};
    mod.funcs = {fn};

    const auto baseline = compileToAsm(mod);

    for (int i = 1; i < 50; ++i) {
        const auto current = compileToAsm(mod);
        ctx.checkEqual(("iter_" + std::to_string(i)).c_str(), baseline, current);
    }
}

// ===----------------------------------------------------------------------===
// Test 3: RoData pool stability (N=50)
// ===----------------------------------------------------------------------===

void testRodataPool(TestContext &ctx) {
    ctx.beginCategory("RoData pool (N=50)");

    // Function 1: returns string "Hello"
    ILBlock e1{};
    e1.name = "entry";
    ILInstr ret1{};
    ret1.opcode = "ret";
    ret1.ops = {strLit("Hello", 5)};
    ret1.resultId = -1;
    e1.instrs = {ret1};

    ILFunction f1{};
    f1.name = "str_hello";
    f1.blocks = {e1};

    // Function 2: returns string "World"
    ILBlock e2{};
    e2.name = "entry";
    ILInstr ret2{};
    ret2.opcode = "ret";
    ret2.ops = {strLit("World", 5)};
    ret2.resultId = -1;
    e2.instrs = {ret2};

    ILFunction f2{};
    f2.name = "str_world";
    f2.blocks = {e2};

    // Function 3: returns f64 3.14159
    ILBlock e3{};
    e3.name = "entry";
    e3.instrs = {
        makeConstF64(3.14159, 0),
        makeRet(0, ILValue::Kind::F64),
    };

    ILFunction f3{};
    f3.name = "fp_pi";
    f3.blocks = {e3};

    // Function 4: returns f64 2.71828
    ILBlock e4{};
    e4.name = "entry";
    e4.instrs = {
        makeConstF64(2.71828, 0),
        makeRet(0, ILValue::Kind::F64),
    };

    ILFunction f4{};
    f4.name = "fp_e";
    f4.blocks = {e4};

    // Function 5: returns "Hello" again (dedup test)
    ILBlock e5{};
    e5.name = "entry";
    ILInstr ret5{};
    ret5.opcode = "ret";
    ret5.ops = {strLit("Hello", 5)};
    ret5.resultId = -1;
    e5.instrs = {ret5};

    ILFunction f5{};
    f5.name = "str_hello_dup";
    f5.blocks = {e5};

    // Function 6: returns 3.14159 again (dedup test)
    ILBlock e6{};
    e6.name = "entry";
    e6.instrs = {
        makeConstF64(3.14159, 0),
        makeRet(0, ILValue::Kind::F64),
    };

    ILFunction f6{};
    f6.name = "fp_pi_dup";
    f6.blocks = {e6};

    ILModule mod{};
    mod.funcs = {f1, f2, f3, f4, f5, f6};

    const auto baseline = compileToAsm(mod);

    // Verify insertion-order labels
    ctx.checkOrder("str_0_before_1", baseline, ".LC_str_0", ".LC_str_1");
    ctx.checkOrder("f64_0_before_1", baseline, ".LC_f64_0", ".LC_f64_1");

    for (int i = 1; i < 50; ++i) {
        const auto current = compileToAsm(mod);
        ctx.checkEqual(("iter_" + std::to_string(i)).c_str(), baseline, current);
    }
}

// ===----------------------------------------------------------------------===
// Test 4: Multi-function ordering (N=50)
// ===----------------------------------------------------------------------===

void testMultiFunctionOrdering(TestContext &ctx) {
    ctx.beginCategory("Multi-function order (N=50)");

    ILModule mod{};
    for (int i = 0; i < 10; ++i) {
        std::string name = "fn_";
        if (i < 10)
            name += "0";
        name += std::to_string(i);

        ILBlock entry{};
        entry.name = "entry";
        entry.paramIds = {0};
        entry.paramKinds = {ILValue::Kind::I64};
        entry.instrs = {
            makeOp("add", {val(0), imm(i * 10)}, 1),
            makeRet(1),
        };

        ILFunction fn{};
        fn.name = name;
        fn.blocks = {entry};
        mod.funcs.push_back(fn);
    }

    const auto baseline = compileToAsm(mod);

    // Verify ordering: fn_00 before fn_01 before ... before fn_09
    for (int i = 0; i < 9; ++i) {
        std::string first = "fn_0" + std::to_string(i) + ":";
        std::string second = "fn_0" + std::to_string(i + 1) + ":";
        ctx.checkOrder(("order_" + std::to_string(i)).c_str(), baseline, first, second);
    }

    for (int i = 1; i < 50; ++i) {
        const auto current = compileToAsm(mod);
        ctx.checkEqual(("iter_" + std::to_string(i)).c_str(), baseline, current);
    }
}

// ===----------------------------------------------------------------------===
// Test 5: Complex CFG (N=50)
// ===----------------------------------------------------------------------===

void testComplexCfg(TestContext &ctx) {
    ctx.beginCategory("Complex CFG (N=50)");

    // entry: switch(param0) → 4 cases + default
    // case1: ret 10
    // case2: nested if/else on param1
    //   l2_t: ret 21
    //   l2_f: ret 22
    // case3: while loop (count down param1, ret result)
    // case4: ret 40
    // default: ret 0

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {
        makeSwitch(0, {{1, "case1"}, {2, "case2"}, {3, "case3"}, {4, "case4"}}, "default_blk"),
    };

    // case1: simple return
    ILBlock case1{};
    case1.name = "case1";
    case1.instrs = {makeRetImm(10)};

    // case2: nested if/else
    ILBlock case2{};
    case2.name = "case2";
    case2.instrs = {
        makeCmp(1, 0, 4 /*sgt*/, 10),
        makeCbr(10, "l2_t", "l2_f"),
    };

    ILBlock l2t{};
    l2t.name = "l2_t";
    l2t.instrs = {makeRetImm(21)};

    ILBlock l2f{};
    l2f.name = "l2_f";
    l2f.instrs = {makeRetImm(22)};

    // case3: while loop
    ILBlock case3{};
    case3.name = "case3";
    case3.instrs = {makeBr("loop_hdr")};

    ILBlock loopHdr{};
    loopHdr.name = "loop_hdr";
    loopHdr.instrs = {
        makeOp("cmp", {val(1), imm(0), imm(1) /*ne*/}, 20, ILValue::Kind::I1),
        makeCbr(20, "loop_body", "loop_exit"),
    };

    ILBlock loopBody{};
    loopBody.name = "loop_body";
    loopBody.instrs = {
        makeOp("add", {val(1), imm(-1)}, 21),
        makeBr("loop_hdr"),
    };

    ILBlock loopExit{};
    loopExit.name = "loop_exit";
    loopExit.instrs = {makeRet(1)};

    // case4: simple return
    ILBlock case4{};
    case4.name = "case4";
    case4.instrs = {makeRetImm(40)};

    // default
    ILBlock defaultBlk{};
    defaultBlk.name = "default_blk";
    defaultBlk.instrs = {makeRetImm(0)};

    ILFunction fn{};
    fn.name = "complex_cfg";
    fn.blocks = {
        entry, case1, case2, l2t, l2f, case3, loopHdr, loopBody, loopExit, case4, defaultBlk};

    ILModule mod{};
    mod.funcs = {fn};

    const auto baseline = compileToAsm(mod);

    for (int i = 1; i < 50; ++i) {
        const auto current = compileToAsm(mod);
        ctx.checkEqual(("iter_" + std::to_string(i)).c_str(), baseline, current);
    }
}

// ===----------------------------------------------------------------------===
// Test 6: Separate construction (pointer independence)
// ===----------------------------------------------------------------------===

void testSeparateConstruction(TestContext &ctx) {
    ctx.beginCategory("Separate construction");

    std::string asm1;
    std::string asm2;

    {
        ILModule mod = buildCanonicalModule();
        asm1 = compileToAsm(mod);
    } // mod destroyed, memory freed

    {
        ILModule mod = buildCanonicalModule();
        asm2 = compileToAsm(mod);
    } // mod destroyed

    ctx.checkEqual("ptr_independence", asm1, asm2);
}

// ===----------------------------------------------------------------------===
// Test 7: ISel pattern determinism (N=50)
// ===----------------------------------------------------------------------===

void testISelPatterns(TestContext &ctx) {
    ctx.beginCategory("ISel patterns (N=50)");

    // Exercise strength reduction: mul by power-of-2, udiv by power-of-2,
    // add/sub chains, multiple constants.
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1, 2, 3};
    entry.paramKinds = {
        ILValue::Kind::I64, ILValue::Kind::I64, ILValue::Kind::I64, ILValue::Kind::I64};
    entry.instrs = {
        // Multiply by various powers of 2 — triggers shlDefs map
        makeOp("mul", {val(0), imm(8)}, 10),
        makeOp("mul", {val(1), imm(16)}, 11),
        makeOp("mul", {val(2), imm(4)}, 12),
        makeOp("mul", {val(3), imm(32)}, 13),

        // Unsigned div by powers of 2 — triggers shift-based lowering
        makeOp("udiv", {val(0), imm(4)}, 14),
        makeOp("udiv", {val(1), imm(8)}, 15),

        // Add/sub chains
        makeOp("add", {val(10), val(11)}, 20),
        makeOp("add", {val(20), val(12)}, 21),
        makeOp("add", {val(21), val(13)}, 22),
        makeOp("sub", {val(22), val(14)}, 23),
        makeOp("sub", {val(23), val(15)}, 24),

        // More operations to populate ISel maps
        makeOp("add", {val(0), imm(42)}, 25),
        makeOp("add", {val(1), imm(100)}, 26),
        makeOp("add", {val(24), val(25)}, 27),
        makeOp("add", {val(27), val(26)}, 28),

        makeRet(28),
    };

    ILFunction fn{};
    fn.name = "isel_stress";
    fn.blocks = {entry};

    ILModule mod{};
    mod.funcs = {fn};

    const auto baseline = compileToAsm(mod);

    for (int i = 1; i < 50; ++i) {
        const auto current = compileToAsm(mod);
        ctx.checkEqual(("iter_" + std::to_string(i)).c_str(), baseline, current);
    }
}

// ===----------------------------------------------------------------------===
// Test 8: Static counter awareness
// ===----------------------------------------------------------------------===

void testPerFunctionLabelCounters(TestContext &ctx) {
    ctx.beginCategory("Per-function label ids");

    // Build a module that triggers fptoui label generation.
    // Labels are now per-function (reset to 0 each function), so repeated
    // compilations must produce identical output without normalization.
    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0};
    entry.paramKinds = {ILValue::Kind::F64};
    entry.instrs = {
        makeOp("fptoui", {valF(0)}, 1),
        makeRet(1),
    };

    ILFunction fn{};
    fn.name = "fptoui_label_test";
    fn.blocks = {entry};

    ILModule mod{};
    mod.funcs = {fn};

    const auto baseline = compileToAsm(mod);

    // Verify labels start at 0 (per-function reset)
    ctx.checkAsm("label_starts_at_0", baseline, ".Lfptoui_trap_0");

    // Compile again — must be byte-identical (no counter drift)
    for (int i = 1; i <= 10; ++i) {
        const auto current = compileToAsm(mod);
        ctx.checkEqual(("iter_" + std::to_string(i)).c_str(), baseline, current);
    }
}

} // namespace

// ===----------------------------------------------------------------------===
// Main
// ===----------------------------------------------------------------------===

int main() {
    TestContext ctx;

    testRepeatedCompilation(ctx);      // Test 1: N=100
    testRegAllocPressure(ctx);         // Test 2: N=50
    testRodataPool(ctx);               // Test 3: N=50
    testMultiFunctionOrdering(ctx);    // Test 4: N=50
    testComplexCfg(ctx);               // Test 5: N=50
    testSeparateConstruction(ctx);     // Test 6: pointer independence
    testISelPatterns(ctx);             // Test 7: N=50
    testPerFunctionLabelCounters(ctx); // Test 8: per-function label ids

    ctx.printSummary();

    if (ctx.globalFail != 0) {
        std::cerr << ctx.globalFail << " test(s) FAILED\n";
        return EXIT_FAILURE;
    }
    std::cout << "All determinism tests passed.\n";
    return EXIT_SUCCESS;
}
