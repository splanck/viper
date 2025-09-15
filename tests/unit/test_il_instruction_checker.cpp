// File: tests/unit/test_il_instruction_checker.cpp
// Purpose: Validate il::verify::verifyInstruction for representative opcodes.
// Key invariants: Checker records results on success and reports mismatches.
// Ownership/Lifetime: Uses locally constructed IL structures.
// Links: docs/il-spec.md

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/verify/InstructionChecker.hpp"
#include <cassert>
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

    return 0;
}
