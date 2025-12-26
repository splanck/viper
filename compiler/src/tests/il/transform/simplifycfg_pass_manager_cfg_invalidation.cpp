//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_pass_manager_cfg_invalidation.cpp
// Purpose: Ensure PassManager reruns CFG analysis after SimplifyCFG mutates the IR.
// Key invariants: Cached CFG summaries should be recomputed once the pass modifies control flow.
// Ownership/Lifetime: Test builds a module locally and executes passes via PassManager.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Instr.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/analysis/Liveness.hpp"

#include <cassert>
#include <optional>

int main()
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("cfg-invalidation", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "A");
    builder.createBlock(fn, "B");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &blockA = fn.blocks[1];
    BasicBlock &blockB = fn.blocks[2];

    builder.setInsertPoint(entry);
    builder.cbr(Value::constBool(true), blockA, {}, blockB, {});

    builder.setInsertPoint(blockA);
    builder.emitRet(std::nullopt, {});

    builder.setInsertPoint(blockB);
    builder.emitRet(std::nullopt, {});

    il::transform::PassManager pm;

    int cfgComputeCount = 0;
    pm.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg",
        [&cfgComputeCount](Module &moduleRef, Function &fnRef)
        {
            ++cfgComputeCount;
            return il::transform::buildCFG(moduleRef, fnRef);
        });

    bool seedRan = false;
    pm.registerFunctionPass(
        "seed-cfg-cache",
        [&seedRan, &cfgComputeCount](Function &function, il::transform::AnalysisManager &analysis)
        {
            il::transform::CFGInfo &first =
                analysis.getFunctionResult<il::transform::CFGInfo>("cfg", function);
            il::transform::CFGInfo &second =
                analysis.getFunctionResult<il::transform::CFGInfo>("cfg", function);
            assert(&first == &second);
            seedRan = true;
            assert(cfgComputeCount == 1);
            return il::transform::PreservedAnalyses::all();
        });

    pm.addSimplifyCFG();

    bool checkRan = false;
    pm.registerFunctionPass(
        "verify-cfg-recomputed",
        [&checkRan, &cfgComputeCount](Function &function, il::transform::AnalysisManager &analysis)
        {
            il::transform::CFGInfo &cfg =
                analysis.getFunctionResult<il::transform::CFGInfo>("cfg", function);
            (void)cfg;
            checkRan = true;
            assert(cfgComputeCount == 2);
            return il::transform::PreservedAnalyses::all();
        });

    pm.registerPipeline("simplifycfg-cfg-invalidation",
                        {"seed-cfg-cache", "simplify-cfg", "verify-cfg-recomputed"});

    bool ran = pm.runPipeline(module, "simplifycfg-cfg-invalidation");
    assert(ran);
    assert(seedRan);
    assert(checkRan);
    assert(cfgComputeCount == 2);

    const il::core::Instr &terminator = module.functions[0].blocks[0].instructions.back();
    assert(terminator.op != il::core::Opcode::CBr);

    return 0;
}
