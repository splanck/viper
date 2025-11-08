// File: tests/analysis/BasicAATests.cpp
// Purpose: Exercise the BasicAA alias and ModRef queries on synthetic IL.
// Key invariants: Alias classifications respect pointer identity, distinct allocas,
//                 and noalias parameters; ModRef classifications fold call and
//                 registry metadata.
// Ownership/Lifetime: Constructs temporary IL modules via IRBuilder.
// Links: docs/dev/analysis.md

#include "il/analysis/BasicAA.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include <cassert>

using namespace il::core;
using il::runtime::signatures::make_signature;
using il::runtime::signatures::register_signature;
using il::runtime::signatures::SigParam;
using viper::analysis::AliasResult;
using viper::analysis::BasicAA;
using viper::analysis::ModRefResult;

namespace
{

Instr makeAlloca(unsigned id)
{
    Instr instr;
    instr.result = id;
    instr.op = Opcode::Alloca;
    instr.type = Type(Type::Kind::Ptr);
    instr.operands.push_back(Value::constInt(8));
    return instr;
}

Instr makeCall(std::string callee)
{
    Instr instr;
    instr.op = Opcode::Call;
    instr.callee = std::move(callee);
    return instr;
}

} // namespace

int main()
{
    Module module;
    il::build::IRBuilder builder(module);

    Param first;
    first.name = "p";
    first.type = Type(Type::Kind::Ptr);
    first.setNoAlias(true);

    Param second;
    second.name = "q";
    second.type = Type(Type::Kind::Ptr);
    second.setNoAlias(true);

    Function &fn = builder.startFunction("callee", Type(Type::Kind::Void), {first, second});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks.front();
    builder.setInsertPoint(entry);

    const unsigned allocaA = builder.reserveTempId();
    entry.instructions.push_back(makeAlloca(allocaA));
    const unsigned allocaB = builder.reserveTempId();
    entry.instructions.push_back(makeAlloca(allocaB));

    BasicAA aa(module, fn);

    const Value allocaValA = Value::temp(allocaA);
    const Value allocaValB = Value::temp(allocaB);
    const Value firstParam = Value::temp(fn.params[0].id);
    const Value secondParam = Value::temp(fn.params[1].id);

    assert(aa.alias(allocaValA, allocaValA) == AliasResult::MustAlias);
    assert(aa.alias(allocaValA, allocaValB) == AliasResult::NoAlias);
    assert(aa.alias(firstParam, secondParam) == AliasResult::NoAlias);
    assert(aa.alias(allocaValA, Value::global("g")) == AliasResult::MayAlias);

    fn.attrs().readonly = true;

    Instr defaultCall = makeCall("callee");
    assert(aa.modRef(defaultCall) == ModRefResult::Ref);

    Instr pureCall = makeCall("callee");
    pureCall.CallAttr.pure = true;
    assert(aa.modRef(pureCall) == ModRefResult::NoModRef);

    Instr readonlyCall = makeCall("callee");
    readonlyCall.CallAttr.readonly = true;
    assert(aa.modRef(readonlyCall) == ModRefResult::Ref);

    register_signature(make_signature("rt_basicaa_pure", {}, {}, false, false, true));
    Instr runtimePure = makeCall("rt_basicaa_pure");
    assert(aa.modRef(runtimePure) == ModRefResult::NoModRef);

    register_signature(
        make_signature("rt_basicaa_readonly", {SigParam::Kind::Ptr}, {}, false, true, false));
    Instr runtimeReadonly = makeCall("rt_basicaa_readonly");
    assert(aa.modRef(runtimeReadonly) == ModRefResult::Ref);

    Instr otherOpcode;
    otherOpcode.op = Opcode::Load;
    assert(aa.modRef(otherOpcode) == ModRefResult::ModRef);

    return 0;
}
