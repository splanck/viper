//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Per-pass isolation correctness tests: apply each of the 17 optimization
// passes individually and verify the resulting module is still valid IL.
//
//===----------------------------------------------------------------------===//

#include "il/transform/PassManager.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "tests/TestHarness.hpp"

#include <iostream>

using namespace il::core;

namespace
{

// Verify a module passes the IL verifier
void verifyOrDie(const Module &module)
{
    auto result = il::verify::Verifier::verify(module);
    if (!result)
    {
        il::support::printDiag(result.error(), std::cerr);
        ASSERT_TRUE(false);
    }
}

// Count total instructions across all functions
size_t countInstructions(const Module &module)
{
    size_t count = 0;
    for (const auto &fn : module.functions)
        for (const auto &bb : fn.blocks)
            count += bb.instructions.size();
    return count;
}

// Build a canonical module with diverse IL constructs for pass testing.
// Contains two functions: "callee" (simple) and "main_fn" (complex with loops,
// branches, allocas, checked arithmetic, block params, etc.)
Module buildCanonicalModule()
{
    Module module;

    // Add an extern declaration so calls to it are valid
    Extern ext;
    ext.name = "rt_print_i64";
    ext.retType = Type(Type::Kind::Void);
    ext.params = {Type(Type::Kind::I64)};
    module.externs.push_back(ext);

    // --- callee function: simple function that adds 2 to its parameter ---
    {
        unsigned nextId = 0;
        Function fn;
        fn.name = "callee";
        fn.retType = Type(Type::Kind::I64);

        Param p{"x", Type(Type::Kind::I64), nextId++}; // %0
        fn.params.push_back(p);

        BasicBlock entry;
        entry.label = "entry";

        // %1 = iadd.ovf i64 %0, 2
        Instr add;
        add.result = nextId++; // %1
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands = {Value::temp(0), Value::constInt(2)};
        entry.instructions.push_back(std::move(add));

        // ret %1
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(1));
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;

        fn.blocks.push_back(std::move(entry));
        fn.valueNames.resize(nextId);
        fn.valueNames[0] = "x";
        fn.valueNames[1] = "add";
        module.functions.push_back(std::move(fn));
    }

    // --- main_fn: complex function with loops, branches, allocas ---
    {
        unsigned nextId = 0;
        Function fn;
        fn.name = "main_fn";
        fn.retType = Type(Type::Kind::I64);

        Param p{"n", Type(Type::Kind::I64), nextId++}; // %0
        fn.params.push_back(p);

        // entry block: alloca, store, comparison, conditional branch
        BasicBlock entry;
        entry.label = "entry";

        // %1 = alloca 8
        Instr alloca_i;
        alloca_i.result = nextId++; // %1
        alloca_i.op = Opcode::Alloca;
        alloca_i.type = Type(Type::Kind::Ptr);
        alloca_i.operands = {Value::constInt(8)};
        entry.instructions.push_back(std::move(alloca_i));

        // store i64 %1, 0  (initialize counter to 0)
        Instr store_init;
        store_init.op = Opcode::Store;
        store_init.type = Type(Type::Kind::I64);
        store_init.operands = {Value::temp(1), Value::constInt(0)};
        entry.instructions.push_back(std::move(store_init));

        // %2 = scmp_lt %0, 1
        Instr cmp;
        cmp.result = nextId++; // %2
        cmp.op = Opcode::SCmpLT;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands = {Value::temp(0), Value::constInt(1)};
        entry.instructions.push_back(std::move(cmp));

        // cbr %2, early_exit(), loop_header(0)
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::temp(2)};
        cbr.labels = {"early_exit", "loop_header"};
        cbr.brArgs = {{}, {Value::constInt(0)}};
        entry.instructions.push_back(std::move(cbr));
        entry.terminated = true;

        // early_exit block: return 0
        BasicBlock earlyExit;
        earlyExit.label = "early_exit";
        Instr ret0;
        ret0.op = Opcode::Ret;
        ret0.type = Type(Type::Kind::Void);
        ret0.operands = {Value::constInt(0)};
        earlyExit.instructions.push_back(std::move(ret0));
        earlyExit.terminated = true;

        // loop_header block with block parameter (loop counter %3 = i)
        BasicBlock loopHeader;
        loopHeader.label = "loop_header";

        Param bp{"i", Type(Type::Kind::I64), nextId++}; // %3
        loopHeader.params.push_back(bp);

        // %4 = load i64 %1
        Instr load;
        load.result = nextId++; // %4
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I64);
        load.operands = {Value::temp(1)};
        loopHeader.instructions.push_back(std::move(load));

        // %5 = iadd.ovf %4, %3 (checked add)
        Instr addOvf;
        addOvf.result = nextId++; // %5
        addOvf.op = Opcode::IAddOvf;
        addOvf.type = Type(Type::Kind::I64);
        addOvf.operands = {Value::temp(4), Value::temp(3)};
        loopHeader.instructions.push_back(std::move(addOvf));

        // store i64 %1, %5
        Instr storeSum;
        storeSum.op = Opcode::Store;
        storeSum.type = Type(Type::Kind::I64);
        storeSum.operands = {Value::temp(1), Value::temp(5)};
        loopHeader.instructions.push_back(std::move(storeSum));

        // %6 = iadd.ovf i64 %3, 1
        Instr incr;
        incr.result = nextId++; // %6
        incr.op = Opcode::IAddOvf;
        incr.type = Type(Type::Kind::I64);
        incr.operands = {Value::temp(3), Value::constInt(1)};
        loopHeader.instructions.push_back(std::move(incr));

        // %7 = scmp_ge %6, %0
        Instr doneCmp;
        doneCmp.result = nextId++; // %7
        doneCmp.op = Opcode::SCmpGE;
        doneCmp.type = Type(Type::Kind::I1);
        doneCmp.operands = {Value::temp(6), Value::temp(0)};
        loopHeader.instructions.push_back(std::move(doneCmp));

        // cbr %7, loop_exit(), loop_header(%6)
        Instr loopCbr;
        loopCbr.op = Opcode::CBr;
        loopCbr.type = Type(Type::Kind::Void);
        loopCbr.operands = {Value::temp(7)};
        loopCbr.labels = {"loop_exit", "loop_header"};
        loopCbr.brArgs = {{}, {Value::temp(6)}};
        loopHeader.instructions.push_back(std::move(loopCbr));
        loopHeader.terminated = true;

        // loop_exit block: load result and call callee, then return
        BasicBlock loopExit;
        loopExit.label = "loop_exit";

        // %8 = load i64 %1
        Instr loadResult;
        loadResult.result = nextId++; // %8
        loadResult.op = Opcode::Load;
        loadResult.type = Type(Type::Kind::I64);
        loadResult.operands = {Value::temp(1)};
        loopExit.instructions.push_back(std::move(loadResult));

        // %9 = call callee(%8)
        Instr call;
        call.result = nextId++; // %9
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I64);
        call.callee = "callee";
        call.operands = {Value::temp(8)};
        loopExit.instructions.push_back(std::move(call));

        // ret %9
        Instr retCall;
        retCall.op = Opcode::Ret;
        retCall.type = Type(Type::Kind::Void);
        retCall.operands = {Value::temp(9)};
        loopExit.instructions.push_back(std::move(retCall));
        loopExit.terminated = true;

        fn.blocks = {std::move(entry), std::move(earlyExit),
                     std::move(loopHeader), std::move(loopExit)};
        fn.valueNames.resize(nextId);
        fn.valueNames[0] = "n";
        fn.valueNames[1] = "alloca";
        fn.valueNames[2] = "cmp";
        fn.valueNames[3] = "i";
        fn.valueNames[4] = "load";
        fn.valueNames[5] = "sum";
        fn.valueNames[6] = "next_i";
        fn.valueNames[7] = "done";
        fn.valueNames[8] = "result";
        fn.valueNames[9] = "callresult";
        module.functions.push_back(std::move(fn));
    }

    return module;
}

// Run a single pass on a fresh copy of the canonical module and check validity
void testPassIsolation(const std::string &passId)
{
    Module module = buildCanonicalModule();
    verifyOrDie(module);

    size_t instrBefore = countInstructions(module);
    ASSERT_TRUE(instrBefore > 0u);

    // Apply single pass via PassManager
    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false); // We verify manually
    pm.run(module, {passId});

    // Module should still be valid
    verifyOrDie(module);

    // Each function should still have at least one block with at least one instruction
    for (const auto &fn : module.functions)
    {
        ASSERT_FALSE(fn.blocks.empty());
        bool hasInstr = false;
        for (const auto &bb : fn.blocks)
            hasInstr |= !bb.instructions.empty();
        ASSERT_TRUE(hasInstr);
    }
}

} // namespace

// --- One test per pass ---

TEST(PassIsolation, SimplifyCFG) { testPassIsolation("simplify-cfg"); }
TEST(PassIsolation, LoopSimplify) { testPassIsolation("loop-simplify"); }
TEST(PassIsolation, LICM) { testPassIsolation("licm"); }
TEST(PassIsolation, SCCP) { testPassIsolation("sccp"); }
TEST(PassIsolation, ConstFold) { testPassIsolation("constfold"); }
TEST(PassIsolation, Peephole) { testPassIsolation("peephole"); }
TEST(PassIsolation, DCE) { testPassIsolation("dce"); }
TEST(PassIsolation, Mem2Reg) { testPassIsolation("mem2reg"); }
TEST(PassIsolation, DSE) { testPassIsolation("dse"); }
TEST(PassIsolation, EarlyCSE) { testPassIsolation("earlycse"); }
TEST(PassIsolation, GVN) { testPassIsolation("gvn"); }
TEST(PassIsolation, IndVarSimplify) { testPassIsolation("indvars"); }
TEST(PassIsolation, LoopUnroll) { testPassIsolation("loop-unroll"); }
TEST(PassIsolation, Inline) { testPassIsolation("inline"); }
TEST(PassIsolation, CheckOpt) { testPassIsolation("check-opt"); }
TEST(PassIsolation, LateCleanup) { testPassIsolation("late-cleanup"); }
TEST(PassIsolation, SiblingRecursion) { testPassIsolation("sibling-recursion"); }

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
