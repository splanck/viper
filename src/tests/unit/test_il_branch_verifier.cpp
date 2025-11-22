//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_branch_verifier.cpp
// Purpose: Validate branch verifier helpers catch structural issues and accept correct inputs. 
// Key invariants: Branch argument types, condition operands, and return values are enforced.
// Ownership/Lifetime: Constructs temporary IL functions for each scenario.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/verify/BranchVerifier.hpp"
#include "il/verify/TypeInference.hpp"

#include <cassert>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

int main()
{
    using namespace il::core;
    using il::verify::TypeInference;
    using il::verify::verifyBr_E;
    using il::verify::verifyCBr_E;
    using il::verify::verifyRet_E;
    using il::verify::verifySwitchI32_E;

    Function fn;
    fn.name = "f";

    BasicBlock source;
    source.label = "entry";

    BasicBlock target;
    target.label = "dest";
    Param destParam;
    destParam.name = "x";
    destParam.type = Type(Type::Kind::I64);
    destParam.id = 10u;
    target.params.push_back(destParam);

    std::unordered_map<std::string, const BasicBlock *> blockMap;
    blockMap[target.label] = &target;

    std::unordered_map<unsigned, Type> temps;
    temps[5] = Type(Type::Kind::I1);
    std::unordered_set<unsigned> defined = {5};
    TypeInference types(temps, defined);

    Instr br;
    br.op = Opcode::Br;
    br.labels.push_back(target.label);
    br.brArgs.push_back({Value::temp(5)});
    auto brResult = verifyBr_E(fn, source, br, blockMap, types);
    assert(!brResult);
    const std::string brMessage = brResult.error().message;
    const bool brMentionsArg = brMessage.find("arg") != std::string::npos;
    const bool brMentionsMismatch = brMessage.find("mismatch") != std::string::npos;
    assert(brMentionsArg && brMentionsMismatch);

    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.operands.push_back(Value::temp(5));
    cbr.labels = {target.label, target.label};
    temps[5] = Type(Type::Kind::I64);
    TypeInference cbrTypes(temps, defined);
    auto cbrResult = verifyCBr_E(fn, source, cbr, blockMap, cbrTypes);
    assert(!cbrResult);
    assert(cbrResult.error().message.find("conditional branch mismatch") != std::string::npos);

    Function retFn;
    retFn.name = "r";
    retFn.retType = Type(Type::Kind::I64);
    BasicBlock retBlock;
    retBlock.label = "entry";
    std::unordered_map<unsigned, Type> retTemps;
    retTemps[1] = Type(Type::Kind::I64);
    std::unordered_set<unsigned> retDefined = {1};
    TypeInference retTypes(retTemps, retDefined);
    Instr retInstr;
    retInstr.op = Opcode::Ret;
    auto retMissing = verifyRet_E(retFn, retBlock, retInstr, retTypes);
    assert(!retMissing);
    const std::string retMessage = retMissing.error().message;
    const bool retMentionsRet = retMessage.find("ret") != std::string::npos;
    const bool retMentionsMismatch = retMessage.find("mismatch") != std::string::npos;
    assert(retMentionsRet && retMentionsMismatch);

    retInstr.operands.push_back(Value::temp(1));
    auto retOk = verifyRet_E(retFn, retBlock, retInstr, retTypes);
    assert(retOk);

    Function switchFn;
    switchFn.name = "s";

    BasicBlock switchBlock;
    switchBlock.label = "entry";

    BasicBlock defaultBlock;
    defaultBlock.label = "fallback";

    BasicBlock caseBlock;
    caseBlock.label = "case0";

    std::unordered_map<std::string, const BasicBlock *> manualSwitchMap;
    manualSwitchMap[defaultBlock.label] = &defaultBlock;
    manualSwitchMap[caseBlock.label] = &caseBlock;

    std::unordered_map<unsigned, Type> switchTemps;
    switchTemps[7] = Type(Type::Kind::I32);
    std::unordered_set<unsigned> switchDefined = {7};
    TypeInference switchTypes(switchTemps, switchDefined);

    Instr badSwitch;
    badSwitch.op = Opcode::SwitchI32;
    badSwitch.type = Type(Type::Kind::Void);
    badSwitch.operands.push_back(Value::temp(7));
    badSwitch.operands.push_back(Value::constInt(0));
    badSwitch.labels = {defaultBlock.label, caseBlock.label};

    auto badSwitchResult =
        verifySwitchI32_E(switchFn, switchBlock, badSwitch, manualSwitchMap, switchTypes);
    assert(!badSwitchResult);
    const std::string switchMessage = badSwitchResult.error().message;
    assert(switchMessage.find("branch argument vector count mismatch") != std::string::npos);

    badSwitch.brArgs = std::vector<std::vector<Value>>{std::vector<Value>{}, std::vector<Value>{}};
    auto goodSwitch =
        verifySwitchI32_E(switchFn, switchBlock, badSwitch, manualSwitchMap, switchTypes);
    assert(goodSwitch);

#ifdef NEGATIVE_DIR
    const std::string fixturePath = std::string(NEGATIVE_DIR) + "/switch_missing_brargs.il";
    std::ifstream fixtureStream(fixturePath);
    assert(fixtureStream && "failed to open switch_missing_brargs.il fixture");

    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(fixtureStream, module);
    assert(parsed);
    assert(module.functions.size() == 1);

    il::core::Function &fixtureFn = module.functions.front();
    assert(!fixtureFn.blocks.empty());
    il::core::BasicBlock &fixtureEntry = fixtureFn.blocks.front();
    assert(!fixtureEntry.instructions.empty());
    const il::core::Instr &fixtureSwitch = fixtureEntry.instructions.back();
    assert(fixtureSwitch.op == il::core::Opcode::SwitchI32);

    std::unordered_map<std::string, const il::core::BasicBlock *> fixtureMap;
    for (const auto &block : fixtureFn.blocks)
        fixtureMap.emplace(block.label, &block);

    std::unordered_map<unsigned, Type> fixtureTemps;
    std::unordered_set<unsigned> fixtureDefined;
    for (const auto &param : fixtureEntry.params)
    {
        fixtureTemps[param.id] = param.type;
        fixtureDefined.insert(param.id);
    }
    TypeInference fixtureTypes(fixtureTemps, fixtureDefined);

    auto fixtureOk =
        verifySwitchI32_E(fixtureFn, fixtureEntry, fixtureSwitch, fixtureMap, fixtureTypes);
    assert(fixtureOk);

    Instr mutatedSwitch = fixtureSwitch;
    mutatedSwitch.brArgs.clear();
    auto mutatedResult =
        verifySwitchI32_E(fixtureFn, fixtureEntry, mutatedSwitch, fixtureMap, fixtureTypes);
    assert(!mutatedResult);
    assert(mutatedResult.error().message.find("branch argument vector count mismatch") !=
           std::string::npos);
#endif

    return 0;
}
