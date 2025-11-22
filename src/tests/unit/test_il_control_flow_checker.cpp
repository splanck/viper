//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_control_flow_checker.cpp
// Purpose: Check il::verify control-flow helpers for common failure modes. 
// Key invariants: Functions emit diagnostics for invalid block structure.
// Ownership/Lifetime: Constructs temporary IL functions.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/TypeInference.hpp"
#include <cassert>
#include <sstream>

int main()
{
    using namespace il::core;
    using il::verify::checkBlockTerminators;
    using il::verify::TypeInference;
    using il::verify::validateBlockParams;
    using il::verify::verifyBr;

    Function fn;
    fn.name = "f";

    BasicBlock bb;
    bb.label = "entry";
    Param p1;
    p1.name = "x";
    p1.type = Type(Type::Kind::I64);
    p1.id = 1u;
    Param p2 = p1;
    p2.id = 2u;
    bb.params = {p1, p2};

    std::unordered_map<unsigned, Type> temps;
    std::unordered_set<unsigned> defined;
    TypeInference types(temps, defined);
    std::vector<unsigned> ids;
    std::ostringstream errParams;
    bool ok = validateBlockParams(fn, bb, types, ids, errParams);
    assert(!ok);
    assert(!errParams.str().empty());

    BasicBlock bbNoTerm;
    bbNoTerm.label = "body";
    Instr notTerm;
    notTerm.op = Opcode::IAddOvf;
    bbNoTerm.instructions.push_back(notTerm);
    std::ostringstream errTerm;
    ok = checkBlockTerminators(fn, bbNoTerm, errTerm);
    assert(!ok);
    assert(errTerm.str().find("missing terminator") != std::string::npos);

    BasicBlock dest;
    dest.label = "target";
    Param destParam;
    destParam.name = "v";
    destParam.type = Type(Type::Kind::I64);
    destParam.id = 10u;
    dest.params.push_back(destParam);

    std::unordered_map<std::string, const BasicBlock *> blockMap;
    blockMap[dest.label] = &dest;

    std::unordered_map<unsigned, Type> tempsBr;
    tempsBr[5] = Type(Type::Kind::I1);
    std::unordered_set<unsigned> definedBr = {5};
    TypeInference typesBr(tempsBr, definedBr);

    Instr br;
    br.op = Opcode::Br;
    br.labels.push_back(dest.label);
    br.brArgs.push_back({Value::temp(5)});

    std::ostringstream errBr;
    ok = verifyBr(fn, bb, br, blockMap, typesBr, errBr);
    assert(!ok);
    const std::string errBrText = errBr.str();
    const bool mentionsArg = errBrText.find("arg") != std::string::npos;
    const bool mentionsMismatch = errBrText.find("mismatch") != std::string::npos;
    assert(mentionsArg && mentionsMismatch);

    return 0;
}
