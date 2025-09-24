// File: tests/unit/test_il_instruction_checker.cpp
// Purpose: Validate il::verify::verifyInstruction for representative opcodes.
// Key invariants: Checker records results on success and reports mismatches.
// Ownership/Lifetime: Uses locally constructed IL structures.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/verify/InstructionChecker.hpp"
#include <cassert>
#include <string>
#include <sstream>

int main()
{
    using namespace il::core;
    using il::verify::TypeInference;
    using il::verify::verifyInstruction;

    Function fn;
    fn.name = "f";
    BasicBlock bb;
    bb.label = "entry";

    std::unordered_map<unsigned, Type> temps;
    temps[1] = Type(Type::Kind::I64);
    temps[2] = Type(Type::Kind::I64);
    std::unordered_set<unsigned> defined = {1, 2};
    TypeInference types(temps, defined);

    Instr add;
    add.result = 3u;
    add.op = Opcode::Add;
    add.operands.push_back(Value::temp(1));
    add.operands.push_back(Value::temp(2));

    std::unordered_map<std::string, const Extern *> externs;
    std::unordered_map<std::string, const Function *> funcs;
    std::ostringstream err;
    bool ok = verifyInstruction(fn, bb, add, externs, funcs, types, err);
    assert(ok);
    assert(err.str().empty());
    assert(temps.at(3).kind == Type::Kind::I64);
    assert(types.isDefined(3));

    Instr cn;
    cn.result = 5u;
    cn.op = Opcode::ConstNull;

    ok = verifyInstruction(fn, bb, cn, externs, funcs, types, err);
    assert(ok);
    assert(err.str().empty());
    assert(temps.at(5).kind == Type::Kind::Ptr);
    assert(types.isDefined(5));

    std::unordered_map<unsigned, Type> tempsBad;
    tempsBad[1] = Type(Type::Kind::I64);
    tempsBad[2] = Type(Type::Kind::I64);
    std::unordered_set<unsigned> definedBad = {1, 2};
    TypeInference typesBad(tempsBad, definedBad);

    Instr fadd;
    fadd.result = 4u;
    fadd.op = Opcode::FAdd;
    fadd.operands.push_back(Value::temp(1));
    fadd.operands.push_back(Value::temp(2));

    std::ostringstream errBad;
    ok = verifyInstruction(fn, bb, fadd, externs, funcs, typesBad, errBad);
    assert(!ok);
    assert(!errBad.str().empty());

    Extern arrGet;
    arrGet.name = "rt_arr_i32_get";
    arrGet.retType = Type(Type::Kind::I64);
    arrGet.params = {Type(Type::Kind::Ptr), Type(Type::Kind::I64)};
    externs[arrGet.name] = &arrGet;

    std::unordered_map<unsigned, Type> arrTemps;
    arrTemps[10] = Type(Type::Kind::Ptr);
    arrTemps[11] = Type(Type::Kind::I64);
    std::unordered_set<unsigned> arrDefined = {10, 11};
    TypeInference arrTypes(arrTemps, arrDefined);

    Instr arrCall;
    arrCall.result = 12u;
    arrCall.op = Opcode::Call;
    arrCall.type = Type(Type::Kind::I64);
    arrCall.callee = arrGet.name;
    arrCall.operands.push_back(Value::temp(10));
    arrCall.operands.push_back(Value::temp(11));

    std::ostringstream arrErr;
    ok = verifyInstruction(fn, bb, arrCall, externs, funcs, arrTypes, arrErr);
    assert(ok);
    assert(arrErr.str().empty());
    assert(arrTemps.at(12).kind == Type::Kind::I64);

    std::unordered_map<unsigned, Type> arrTempsBad;
    arrTempsBad[10] = Type(Type::Kind::Ptr);
    std::unordered_set<unsigned> arrDefinedBad = {10};
    TypeInference arrTypesBad(arrTempsBad, arrDefinedBad);

    Instr arrCallBad;
    arrCallBad.result = 20u;
    arrCallBad.op = Opcode::Call;
    arrCallBad.type = Type(Type::Kind::I64);
    arrCallBad.callee = arrGet.name;
    arrCallBad.operands.push_back(Value::temp(10));
    arrCallBad.operands.push_back(Value::constFloat(1.0));

    std::ostringstream arrErrBad;
    ok = verifyInstruction(fn, bb, arrCallBad, externs, funcs, arrTypesBad, arrErrBad);
    assert(!ok);
    const std::string arrDiag = arrErrBad.str();
    assert(arrDiag.find("@rt_arr_i32_get index operand must be i64") != std::string::npos);

    return 0;
}
