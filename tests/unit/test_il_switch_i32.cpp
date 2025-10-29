// File: tests/unit/test_il_switch_i32.cpp
// Purpose: Confirm SwitchI32 opcode metadata, parsing, and passes handle multi-way control flow.
// Key invariants: SwitchI32 must expose correct operands/successors and analyses recognise all
// edges. Ownership/Lifetime: Test owns parsed module and mutates it locally. Links:
// docs/il-guide.md#reference

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/api/expected_api.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/Mem2Reg.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/analysis/Liveness.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>
#include <string>
#include <unordered_set>

using namespace il;

namespace
{
const char *kSwitchProgram = R"(il 0.1
func @main(%x:i32) -> i32 {
entry(%x:i32):
  switch.i32 %x, ^default(%x, %x), 0 -> ^case0(%x, %x), 1 -> ^case1(%x, %x)
case0(%v0:i32, %unused0:i32):
  ret %v0
case1(%v1:i32, %unused1:i32):
  ret %v1
default(%v2:i32, %unused2:i32):
  ret %v2
}
)";
}

int main()
{
    core::Module module;
    std::istringstream input(kSwitchProgram);
    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(parsed);
    assert(module.functions.size() == 1);

    core::Function &fn = module.functions[0];
    assert(fn.blocks.size() == 4);

    core::BasicBlock &entry = fn.blocks[0];
    assert(!entry.instructions.empty());
    const core::Instr &switchInstr = entry.instructions.back();
    assert(switchInstr.op == core::Opcode::SwitchI32);

    const core::OpcodeInfo &info = core::getOpcodeInfo(core::Opcode::SwitchI32);
    assert(std::string(info.name) == "switch.i32");
    assert(info.resultArity == core::ResultArity::None);
    assert(info.resultType == core::TypeCategory::None);
    assert(info.numOperandsMin == 1);
    assert(info.numOperandsMax == core::kVariadicOperandCount);
    assert(info.operandTypes[0] == core::TypeCategory::I32);
    assert(info.operandTypes[1] == core::TypeCategory::I32);
    assert(info.numSuccessors == core::kVariadicOperandCount);
    assert(info.isTerminator);
    assert(info.parse[0].kind == core::OperandParseKind::Value);
    assert(info.parse[1].kind == core::OperandParseKind::Switch);

    assert(core::switchCaseCount(switchInstr) == 2);
    assert(core::switchDefaultLabel(switchInstr) == "default");

    viper::analysis::CFGContext directCtx(module);
    auto directSucc = viper::analysis::successors(directCtx, entry);
    assert(directSucc.size() == 3);
    auto rpo = viper::analysis::reversePostOrder(directCtx, fn);
    assert(rpo.size() == fn.blocks.size());
    assert(!rpo.empty() && rpo.front() == &entry);
    viper::analysis::DomTree dt = viper::analysis::computeDominatorTree(directCtx, fn);
    assert(dt.immediateDominator(&fn.blocks[0]) == nullptr);
    for (std::size_t idx = 1; idx < fn.blocks.size(); ++idx)
        assert(dt.immediateDominator(&fn.blocks[idx]) == &fn.blocks[0]);

    transform::PassManager pm;
    bool checkedCfg = false;
    pm.registerFunctionPass(
        "check-switch-cfg",
        [&checkedCfg](core::Function &function, transform::AnalysisManager &analysis)
        {
            transform::CFGInfo &cfg =
                analysis.getFunctionResult<transform::CFGInfo>("cfg", function);
            assert(!function.blocks.empty());
            core::BasicBlock &curEntry = function.blocks[0];
            auto succIt = cfg.successors.find(&curEntry);
            assert(succIt != cfg.successors.end());
            const auto &succList = succIt->second;
            assert(succList.size() == 3);
            std::unordered_set<const core::BasicBlock *> succSet(succList.begin(), succList.end());
            for (const std::string &label : curEntry.instructions.back().labels)
            {
                auto targetIt = std::find_if(function.blocks.begin(),
                                             function.blocks.end(),
                                             [&label](const core::BasicBlock &bb)
                                             { return bb.label == label; });
                assert(targetIt != function.blocks.end());
                assert(succSet.count(&*targetIt));
            }
            checkedCfg = true;
            return transform::PreservedAnalyses::all();
        });

    pm.registerPipeline("switch", {"check-switch-cfg"});
    bool pipelineRan = pm.runPipeline(module, "switch");
    assert(pipelineRan);
    assert(checkedCfg);

    viper::passes::mem2reg(module);
    transform::dce(module);

    core::BasicBlock &entryAfter = module.functions[0].blocks[0];
    const core::Instr &switchAfter = entryAfter.instructions.back();
    assert(switchAfter.labels.size() == 3);
    for (const auto &args : switchAfter.brArgs)
        assert(args.size() == 1);

    for (const std::string &label : switchAfter.labels)
    {
        auto blockIt =
            std::find_if(fn.blocks.begin(),
                         fn.blocks.end(),
                         [&label](const core::BasicBlock &bb) { return bb.label == label; });
        assert(blockIt != fn.blocks.end());
        assert(blockIt->params.size() == 1);
    }

    return 0;
}
