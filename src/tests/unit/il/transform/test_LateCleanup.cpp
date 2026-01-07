//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_LateCleanup.cpp
// Purpose: Validate that LateCleanup removes unreachable blocks, dead
//          instructions, and simplifies CFG noise created by earlier passes.
// Key invariants: Unreachable blocks removed; dead temps eliminated; trivial
//                 branches folded.
// Ownership/Lifetime: Builds a transient module per test invocation.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/LateCleanup.hpp"
#include "il/transform/SimplifyCFG.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "il/transform/analysis/Liveness.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace il::core;

namespace
{

/// @brief Find a block by label in a function.
BasicBlock *findBlock(Function &function, const std::string &label)
{
    for (auto &block : function.blocks)
    {
        if (block.label == label)
            return &block;
    }
    return nullptr;
}

/// @brief Count instructions with a given opcode in a function.
size_t countOpcode(const Function &function, Opcode op)
{
    size_t count = 0;
    for (const auto &block : function.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == op)
                ++count;
        }
    }
    return count;
}

il::transform::AnalysisRegistry createRegistry()
{
    il::transform::AnalysisRegistry registry;
    registry.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg", [](Module &mod, Function &fnRef) { return il::transform::buildCFG(mod, fnRef); });
    return registry;
}

/// Test 1: SimplifyCFG removes unreachable blocks
/// Run SimplifyCFG directly (without module verification) to test basic cleanup.
void test_simplifycfg_unreachable_block_removal()
{
    Module module;
    Function fn;
    fn.name = "test_unreachable";
    fn.retType = Type(Type::Kind::Void);

    // entry: ret.void
    BasicBlock entry;
    entry.label = "entry";

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    // No operands for void return

    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    // unreachable: ret (void)  (no predecessors, should be removed)
    BasicBlock unreachable;
    unreachable.label = "unreachable";

    Instr ret2;
    ret2.op = Opcode::Ret;
    ret2.type = Type(Type::Kind::Void);
    // No operands for void return

    unreachable.instructions.push_back(std::move(ret2));
    unreachable.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(unreachable));

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    // Verify we start with 2 blocks
    assert(function.blocks.size() == 2);

    // Run SimplifyCFG directly without module verification
    il::transform::SimplifyCFG cfgPass(/*aggressive=*/true);
    // Don't set module - this skips verification
    il::transform::SimplifyCFG::Stats stats;
    bool changed = cfgPass.run(function, &stats);

    // After cleanup, should have only 1 block (unreachable removed)
    assert(changed);
    assert(function.blocks.size() == 1);
    assert(function.blocks[0].label == "entry");
}

/// Test 2: DCE removes dead loads
/// Note: The current DCE is "trivial" - it only removes dead loads/stores/allocas
/// and unused block parameters, not general dead instructions like adds.
void test_dce_dead_load_elimination()
{
    Module module;
    Function fn;
    fn.name = "test_dead_load";
    fn.retType = Type(Type::Kind::Void);

    unsigned nextId = 0;

    // entry:
    //   %ptr = alloca i64
    //   %dead = load %ptr   (unused, should be removed)
    //   ret
    BasicBlock entry;
    entry.label = "entry";

    Instr alloca;
    alloca.result = nextId++;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    unsigned allocaId = *alloca.result;

    Instr deadLoad;
    deadLoad.result = nextId++;
    deadLoad.op = Opcode::Load;
    deadLoad.type = Type(Type::Kind::I64);
    deadLoad.operands.push_back(Value::temp(allocaId));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    // No operands for void return

    entry.instructions.push_back(std::move(alloca));
    entry.instructions.push_back(std::move(deadLoad));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    // Verify we start with 3 instructions (alloca + load + ret)
    assert(function.blocks[0].instructions.size() == 3);
    assert(countOpcode(function, Opcode::Load) == 1);
    assert(countOpcode(function, Opcode::Alloca) == 1);

    // Run DCE directly
    il::transform::dce(module);

    // After DCE, the dead load should be removed
    // Note: The alloca remains because hasLoad was set to true (the load existed)
    // before DCE ran - it doesn't iterate. This is expected behavior for trivial DCE.
    assert(function.blocks[0].instructions.size() == 2);
    assert(countOpcode(function, Opcode::Load) == 0);
    assert(countOpcode(function, Opcode::Alloca) == 1);
}

/// Test 3: Empty forwarding block elimination
/// A function with an empty block that just forwards to another.
void test_simplifycfg_empty_forwarding_block()
{
    Module module;
    Function fn;
    fn.name = "test_forward";
    fn.retType = Type(Type::Kind::Void);

    // entry: br forward
    BasicBlock entry;
    entry.label = "entry";

    Instr br1;
    br1.op = Opcode::Br;
    br1.type = Type(Type::Kind::Void);
    br1.labels.push_back("forward");
    br1.brArgs.emplace_back();

    entry.instructions.push_back(std::move(br1));
    entry.terminated = true;

    // forward: br exit (empty forwarding block)
    BasicBlock forward;
    forward.label = "forward";

    Instr br2;
    br2.op = Opcode::Br;
    br2.type = Type(Type::Kind::Void);
    br2.labels.push_back("exit");
    br2.brArgs.emplace_back();

    forward.instructions.push_back(std::move(br2));
    forward.terminated = true;

    // exit: ret (void)
    BasicBlock exit;
    exit.label = "exit";

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    // No operands for void return

    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(forward));
    fn.blocks.push_back(std::move(exit));

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    // Verify initial state: 3 blocks
    assert(function.blocks.size() == 3);

    // Run SimplifyCFG directly without module verification
    il::transform::SimplifyCFG cfgPass(/*aggressive=*/true);
    il::transform::SimplifyCFG::Stats stats;
    bool changed = cfgPass.run(function, &stats);
    (void)changed;

    // After cleanup: forwarding block should be eliminated or merged
    // (exact behavior depends on SimplifyCFG implementation)
    assert(function.blocks.size() <= 2);
}

/// Test 4: LateCleanup pass integration test
/// This test uses the full pass manager integration without module verification.
void test_late_cleanup_integration()
{
    Module module;
    Function fn;
    fn.name = "test_combined";
    fn.retType = Type(Type::Kind::Void);

    // entry: br then
    BasicBlock entry;
    entry.label = "entry";

    Instr brThen;
    brThen.op = Opcode::Br;
    brThen.type = Type(Type::Kind::Void);
    brThen.labels.push_back("then");
    brThen.brArgs.emplace_back();

    entry.instructions.push_back(std::move(brThen));
    entry.terminated = true;

    // then: ret (void)
    BasicBlock thenBlock;
    thenBlock.label = "then";

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    // No operands for void return

    thenBlock.instructions.push_back(std::move(ret));
    thenBlock.terminated = true;

    // unreachable: ret (void)
    BasicBlock unreachable;
    unreachable.label = "unreachable";

    Instr ret2;
    ret2.op = Opcode::Ret;
    ret2.type = Type(Type::Kind::Void);
    // No operands for void return

    unreachable.instructions.push_back(std::move(ret2));
    unreachable.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(thenBlock));
    fn.blocks.push_back(std::move(unreachable));

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    // Verify initial state: 3 blocks
    assert(function.blocks.size() == 3);

    auto registry = createRegistry();
    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::LateCleanup cleanup;
    auto preserved = cleanup.run(module, analysisManager);
    (void)preserved;

    // After cleanup:
    // - unreachable block should be removed
    // - entry may be merged with then (2 or fewer blocks)
    assert(function.blocks.size() <= 2);
    assert(findBlock(function, "unreachable") == nullptr);
}

} // namespace

int main()
{
    test_simplifycfg_unreachable_block_removal();
    test_dce_dead_load_elimination();
    test_simplifycfg_empty_forwarding_block();
    test_late_cleanup_integration();
    return 0;
}
