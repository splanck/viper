// File: tests/unit/test_il_type_inference.cpp
// Purpose: Exercise il::verify::TypeInference helper routines.
// Key invariants: TypeInference must track definitions and report operand issues.
// Ownership/Lifetime: Uses local modules and temporaries.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/verify/TypeInference.hpp"
#include <cassert>
#include <sstream>

int main()
{
    using namespace il::core;
    using il::verify::TypeInference;

    std::unordered_map<unsigned, Type> temps;
    std::unordered_set<unsigned> defined;
    TypeInference types(temps, defined);

    Instr def;
    def.result = 1u;
    types.recordResult(def, Type(Type::Kind::I64));
    assert(temps.at(1).kind == Type::Kind::I64);
    assert(types.isDefined(1));

    Value c = Value::constInt(42);
    assert(types.valueType(c).kind == Type::Kind::I64);

    Value bTrue = Value::constBool(true);
    assert(types.valueType(bTrue).kind == Type::Kind::I1);

    Value bFalse = Value::constBool(false);
    assert(types.valueType(bFalse).kind == Type::Kind::I1);

    Value missingVal = Value::temp(2);
    bool missing = false;
    Type missingType = types.valueType(missingVal, &missing);
    assert(missing && missingType.kind == Type::Kind::Void);

    Function fn;
    fn.name = "f";
    BasicBlock bb;
    bb.label = "entry";

    Instr useUnknown;
    useUnknown.op = Opcode::IAddOvf;
    useUnknown.operands.push_back(Value::temp(2));
    std::ostringstream err;
    bool ok = types.ensureOperandsDefined(fn, bb, useUnknown, err);
    assert(!ok);
    assert(!err.str().empty());

    temps[2] = Type(Type::Kind::I64);
    std::ostringstream errUse;
    ok = types.ensureOperandsDefined(fn, bb, useUnknown, errUse);
    assert(!ok);

    types.addTemp(2, Type(Type::Kind::I64));
    std::ostringstream errOk;
    ok = types.ensureOperandsDefined(fn, bb, useUnknown, errOk);
    assert(ok);
    assert(errOk.str().empty());

    types.removeTemp(2);
    assert(temps.find(2) == temps.end());
    assert(!types.isDefined(2));

    return 0;
}
