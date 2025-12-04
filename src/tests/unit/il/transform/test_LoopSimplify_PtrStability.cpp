//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_LoopSimplify_PtrStability.cpp
// Purpose: Regression test for pointer invalidation in LoopSimplify.
// Key invariants: The pass must use indices or stable references when modifying
//                 function.blocks to avoid use-after-reallocation bugs.
// Ownership/Lifetime: Builds a local module for the duration of the test run.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/LoopSimplify.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace il::core;

namespace
{

BasicBlock *findBlock(Function &function, const std::string &label)
{
    for (auto &block : function.blocks)
    {
        if (block.label == label)
            return &block;
    }
    return nullptr;
}

/// @brief Creates a simple block with a branch to the given target.
BasicBlock makeSimpleBlock(const std::string &label,
                           const std::string &target,
                           unsigned &nextId,
                           Function &fn)
{
    BasicBlock block;
    block.label = label;

    Instr branch;
    branch.op = Opcode::Br;
    branch.type = Type(Type::Kind::Void);
    branch.labels.push_back(target);
    branch.brArgs.emplace_back();
    block.instructions.push_back(std::move(branch));
    block.terminated = true;

    return block;
}

/// @brief Creates a block with a conditional branch.
BasicBlock makeCondBlock(const std::string &label,
                         unsigned condId,
                         const std::string &trueTarget,
                         const std::string &falseTarget,
                         unsigned &nextId,
                         Function &fn)
{
    BasicBlock block;
    block.label = label;

    Instr branch;
    branch.op = Opcode::CBr;
    branch.type = Type(Type::Kind::Void);
    branch.operands.push_back(Value::temp(condId));
    branch.labels.push_back(trueTarget);
    branch.labels.push_back(falseTarget);
    branch.brArgs.emplace_back();
    branch.brArgs.emplace_back();
    block.instructions.push_back(std::move(branch));
    block.terminated = true;

    return block;
}

} // namespace

int main()
{
    // Test 1: Many blocks to force reallocation during preheader insertion
    {
        Module module;
        Function fn;
        fn.name = "test_many_blocks_preheader";
        fn.retType = Type(Type::Kind::I64);

        unsigned nextId = 0;

        // Add a condition parameter for branching
        Param condParam{"cond", Type(Type::Kind::I1), nextId++};
        fn.params.push_back(condParam);
        fn.valueNames.resize(nextId);
        fn.valueNames[condParam.id] = condParam.name;

        // Create many dummy blocks to fill up the vector and make reallocation likely.
        // We'll create 100 blocks, then a loop, then run LoopSimplify.
        // The preheader insertion will add a new block, potentially triggering reallocation.
        const size_t numDummyBlocks = 100;

        // Entry block conditionally branches to loop header or skip block.
        // Using CBr means entry is not a dedicated preheader, so one must be created.
        BasicBlock entry;
        entry.label = "entry";
        {
            Instr branch;
            branch.op = Opcode::CBr;
            branch.type = Type(Type::Kind::Void);
            branch.operands.push_back(Value::temp(condParam.id));
            branch.labels.push_back("loop_header");
            branch.labels.push_back("skip");
            branch.brArgs.emplace_back(std::vector<Value>{Value::constInt(0)});
            branch.brArgs.emplace_back();
            entry.instructions.push_back(std::move(branch));
            entry.terminated = true;
        }
        fn.blocks.push_back(std::move(entry));

        // Skip block jumps to exit
        BasicBlock skipBlock;
        skipBlock.label = "skip";
        {
            Instr branch;
            branch.op = Opcode::Br;
            branch.type = Type(Type::Kind::Void);
            branch.labels.push_back("exit");
            branch.brArgs.emplace_back(std::vector<Value>{Value::constInt(99)});
            skipBlock.instructions.push_back(std::move(branch));
            skipBlock.terminated = true;
        }
        fn.blocks.push_back(std::move(skipBlock));

        // Loop header with a parameter
        BasicBlock loopHeader;
        loopHeader.label = "loop_header";
        Param loopParam{"acc", Type(Type::Kind::I64), nextId++};
        loopHeader.params.push_back(loopParam);
        fn.valueNames.resize(nextId);
        fn.valueNames[loopParam.id] = loopParam.name;
        {
            Instr branch;
            branch.op = Opcode::CBr;
            branch.type = Type(Type::Kind::Void);
            branch.operands.push_back(Value::temp(condParam.id));
            branch.labels.push_back("loop_body");
            branch.labels.push_back("exit");
            branch.brArgs.emplace_back();
            branch.brArgs.emplace_back(std::vector<Value>{Value::temp(loopParam.id)});
            loopHeader.instructions.push_back(std::move(branch));
            loopHeader.terminated = true;
        }
        fn.blocks.push_back(std::move(loopHeader));

        // Loop body branches back to header (latch)
        BasicBlock loopBody;
        loopBody.label = "loop_body";
        {
            Instr branch;
            branch.op = Opcode::Br;
            branch.type = Type(Type::Kind::Void);
            branch.labels.push_back("loop_header");
            branch.brArgs.emplace_back(std::vector<Value>{Value::constInt(1)});
            loopBody.instructions.push_back(std::move(branch));
            loopBody.terminated = true;
        }
        fn.blocks.push_back(std::move(loopBody));

        // Exit block
        BasicBlock exitBlock;
        exitBlock.label = "exit";
        Param exitParam{"result", Type(Type::Kind::I64), nextId++};
        exitBlock.params.push_back(exitParam);
        fn.valueNames.resize(nextId);
        fn.valueNames[exitParam.id] = exitParam.name;
        {
            Instr ret;
            ret.op = Opcode::Ret;
            ret.type = Type(Type::Kind::Void);
            ret.operands.push_back(Value::temp(exitParam.id));
            exitBlock.instructions.push_back(std::move(ret));
            exitBlock.terminated = true;
        }
        fn.blocks.push_back(std::move(exitBlock));

        // Add many dummy blocks that aren't part of the loop.
        // These fill up the vector to make reallocation more likely.
        for (size_t i = 0; i < numDummyBlocks; ++i)
        {
            BasicBlock dummy;
            dummy.label = "dummy_" + std::to_string(i);
            // These blocks are unreachable but fill up the vector
            Instr trap;
            trap.op = Opcode::Trap;
            trap.type = Type(Type::Kind::Void);
            trap.operands.push_back(Value::constInt(0)); // DivideByZero trap kind
            dummy.instructions.push_back(std::move(trap));
            dummy.terminated = true;
            fn.blocks.push_back(std::move(dummy));
        }

        module.functions.push_back(std::move(fn));
        Function &function = module.functions.back();

        // Run LoopSimplify - this should not crash even with reallocation
        il::transform::AnalysisRegistry registry;
        registry.registerFunctionAnalysis<il::transform::CFGInfo>(
            "cfg",
            [](Module &mod, Function &fnRef) { return il::transform::buildCFG(mod, fnRef); });
        registry.registerFunctionAnalysis<viper::analysis::DomTree>(
            "dominators",
            [](Module &mod, Function &fnRef)
            {
                viper::analysis::CFGContext ctx(mod);
                return viper::analysis::computeDominatorTree(ctx, fnRef);
            });
        registry.registerFunctionAnalysis<il::transform::LoopInfo>(
            "loop-info",
            [](Module &mod, Function &fnRef)
            { return il::transform::computeLoopInfo(mod, fnRef); });

        il::transform::AnalysisManager analysisManager(module, registry);

        il::transform::LoopSimplify pass;
        il::transform::PreservedAnalyses preserved = pass.run(function, analysisManager);
        (void)preserved;

        // Verify the preheader was created and terminators are valid
        const BasicBlock *preheader = findBlock(function, "loop_header.preheader");
        assert(preheader && "LoopSimplify must create a dedicated preheader block");
        assert(preheader->terminated && "Preheader must be terminated");
        assert(!preheader->instructions.empty() && "Preheader must have instructions");

        const Instr &preheaderTerm = preheader->instructions.back();
        assert(preheaderTerm.op == Opcode::Br && "Preheader must end with unconditional branch");
        assert(preheaderTerm.labels.size() == 1 && "Preheader branch must have one target");
        assert(preheaderTerm.labels.front() == "loop_header" &&
               "Preheader must branch to loop header");

        // Verify entry now branches to preheader
        const BasicBlock *entryBlock = findBlock(function, "entry");
        assert(entryBlock && "Entry block must exist");
        const Instr &entryTerm = entryBlock->instructions.back();
        assert(entryTerm.labels.front() == "loop_header.preheader" &&
               "Entry must branch to preheader");
    }

    // Test 2: Multiple trivial latches to test mergeTrivialLatches with reallocation
    {
        Module module;
        Function fn;
        fn.name = "test_multiple_latches";
        fn.retType = Type(Type::Kind::I64);

        unsigned nextId = 0;

        Param condParam{"cond", Type(Type::Kind::I1), nextId++};
        fn.params.push_back(condParam);
        fn.valueNames.resize(nextId);
        fn.valueNames[condParam.id] = condParam.name;

        // Entry block conditionally branches to loop header or skip.
        // Using CBr means entry is not a dedicated preheader.
        BasicBlock entry;
        entry.label = "entry";
        {
            Instr branch;
            branch.op = Opcode::CBr;
            branch.type = Type(Type::Kind::Void);
            branch.operands.push_back(Value::temp(condParam.id));
            branch.labels.push_back("loop_header");
            branch.labels.push_back("skip");
            branch.brArgs.emplace_back(std::vector<Value>{Value::constInt(0)});
            branch.brArgs.emplace_back();
            entry.instructions.push_back(std::move(branch));
            entry.terminated = true;
        }
        fn.blocks.push_back(std::move(entry));

        // Skip block jumps to exit
        BasicBlock skipBlock;
        skipBlock.label = "skip";
        {
            Instr branch;
            branch.op = Opcode::Br;
            branch.type = Type(Type::Kind::Void);
            branch.labels.push_back("exit");
            branch.brArgs.emplace_back(std::vector<Value>{Value::constInt(99)});
            skipBlock.instructions.push_back(std::move(branch));
            skipBlock.terminated = true;
        }
        fn.blocks.push_back(std::move(skipBlock));

        // Loop header
        BasicBlock loopHeader;
        loopHeader.label = "loop_header";
        Param loopParam{"acc", Type(Type::Kind::I64), nextId++};
        loopHeader.params.push_back(loopParam);
        fn.valueNames.resize(nextId);
        fn.valueNames[loopParam.id] = loopParam.name;
        {
            Instr branch;
            branch.op = Opcode::CBr;
            branch.type = Type(Type::Kind::Void);
            branch.operands.push_back(Value::temp(condParam.id));
            branch.labels.push_back("body");
            branch.labels.push_back("exit");
            branch.brArgs.emplace_back();
            branch.brArgs.emplace_back(std::vector<Value>{Value::temp(loopParam.id)});
            loopHeader.instructions.push_back(std::move(branch));
            loopHeader.terminated = true;
        }
        fn.blocks.push_back(std::move(loopHeader));

        // Body with conditional to two trivial latches
        BasicBlock body;
        body.label = "body";
        {
            Instr branch;
            branch.op = Opcode::CBr;
            branch.type = Type(Type::Kind::Void);
            branch.operands.push_back(Value::temp(condParam.id));
            branch.labels.push_back("latch1");
            branch.labels.push_back("latch2");
            branch.brArgs.emplace_back();
            branch.brArgs.emplace_back();
            body.instructions.push_back(std::move(branch));
            body.terminated = true;
        }
        fn.blocks.push_back(std::move(body));

        // Two trivial latches with identical arguments - these should be merged
        BasicBlock latch1;
        latch1.label = "latch1";
        {
            Instr branch;
            branch.op = Opcode::Br;
            branch.type = Type(Type::Kind::Void);
            branch.labels.push_back("loop_header");
            branch.brArgs.emplace_back(std::vector<Value>{Value::constInt(42)});
            latch1.instructions.push_back(std::move(branch));
            latch1.terminated = true;
        }
        fn.blocks.push_back(std::move(latch1));

        BasicBlock latch2;
        latch2.label = "latch2";
        {
            Instr branch;
            branch.op = Opcode::Br;
            branch.type = Type(Type::Kind::Void);
            branch.labels.push_back("loop_header");
            branch.brArgs.emplace_back(std::vector<Value>{Value::constInt(42)});
            latch2.instructions.push_back(std::move(branch));
            latch2.terminated = true;
        }
        fn.blocks.push_back(std::move(latch2));

        // Exit block
        BasicBlock exitBlock;
        exitBlock.label = "exit";
        Param exitParam{"result", Type(Type::Kind::I64), nextId++};
        exitBlock.params.push_back(exitParam);
        fn.valueNames.resize(nextId);
        fn.valueNames[exitParam.id] = exitParam.name;
        {
            Instr ret;
            ret.op = Opcode::Ret;
            ret.type = Type(Type::Kind::Void);
            ret.operands.push_back(Value::temp(exitParam.id));
            exitBlock.instructions.push_back(std::move(ret));
            exitBlock.terminated = true;
        }
        fn.blocks.push_back(std::move(exitBlock));

        // Add dummy blocks to make reallocation likely
        for (size_t i = 0; i < 100; ++i)
        {
            BasicBlock dummy;
            dummy.label = "dummy_" + std::to_string(i);
            Instr trap;
            trap.op = Opcode::Trap;
            trap.type = Type(Type::Kind::Void);
            trap.operands.push_back(Value::constInt(0));
            dummy.instructions.push_back(std::move(trap));
            dummy.terminated = true;
            fn.blocks.push_back(std::move(dummy));
        }

        module.functions.push_back(std::move(fn));
        Function &function = module.functions.back();

        il::transform::AnalysisRegistry registry;
        registry.registerFunctionAnalysis<il::transform::CFGInfo>(
            "cfg",
            [](Module &mod, Function &fnRef) { return il::transform::buildCFG(mod, fnRef); });
        registry.registerFunctionAnalysis<viper::analysis::DomTree>(
            "dominators",
            [](Module &mod, Function &fnRef)
            {
                viper::analysis::CFGContext ctx(mod);
                return viper::analysis::computeDominatorTree(ctx, fnRef);
            });
        registry.registerFunctionAnalysis<il::transform::LoopInfo>(
            "loop-info",
            [](Module &mod, Function &fnRef)
            { return il::transform::computeLoopInfo(mod, fnRef); });

        il::transform::AnalysisManager analysisManager(module, registry);

        il::transform::LoopSimplify pass;
        il::transform::PreservedAnalyses preserved = pass.run(function, analysisManager);
        (void)preserved;

        // Verify the preheader was created (entry branches to loop, needs preheader)
        const BasicBlock *preheader = findBlock(function, "loop_header.preheader");
        assert(preheader && "LoopSimplify must create a preheader");
        assert(preheader->terminated);

        // Verify terminators are not corrupted
        const BasicBlock *entryBlock = findBlock(function, "entry");
        assert(entryBlock && entryBlock->terminated);
        const Instr &entryTerm = entryBlock->instructions.back();
        assert(entryTerm.op == Opcode::CBr && "Entry should still be CBr");
        assert(entryTerm.labels.size() == 2 && "Entry should branch to preheader and skip");

        const BasicBlock *headerBlock = findBlock(function, "loop_header");
        assert(headerBlock && headerBlock->terminated);
        const Instr &headerTerm = headerBlock->instructions.back();
        assert(headerTerm.op == Opcode::CBr);
        assert(headerTerm.labels.size() == 2);
    }

    return 0;
}
