// File: tests/unit/test_il_branch_verifier.cpp
// Purpose: Validate branch verifier helpers catch structural issues and accept correct inputs.
// Key invariants: Branch argument types, condition operands, and return values are enforced.
// Ownership/Lifetime: Constructs temporary IL functions for each scenario.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/verify/BranchVerifier.hpp"
#include "il/verify/TypeInference.hpp"

#include <cassert>
#include <unordered_map>
#include <unordered_set>

int main()
{
    using namespace il::core;
    using il::verify::TypeInference;
    using il::verify::verifyBr_E;
    using il::verify::verifyCBr_E;
    using il::verify::verifyRet_E;

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
    assert(brResult.error().message.find("arg type mismatch") != std::string::npos);

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
    assert(retMissing.error().message.find("ret value type mismatch") != std::string::npos);

    retInstr.operands.push_back(Value::temp(1));
    auto retOk = verifyRet_E(retFn, retBlock, retInstr, retTypes);
    assert(retOk);

    return 0;
}
