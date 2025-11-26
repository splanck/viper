//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the simple function inliner. Verifies that a tiny callee is inlined
// and that SCCP + DCE can fold constants across the former call boundary. Also
// checks that recursive functions are not inlined.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Inline.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/SCCP.hpp"
#include "il/transform/DCE.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>

using namespace il::core;

static Function makeAdd2()
{
    Function f;
    f.name = "add2";
    f.retType = Type(Type::Kind::I64);
    unsigned id = 0;
    Param x{"x", Type(Type::Kind::I64), id++};
    f.params.push_back(x);
    f.valueNames.resize(id);
    BasicBlock b;
    b.label = "entry";
    Instr add;
    add.result = id++;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::temp(x.id));
    add.operands.push_back(Value::constInt(2));
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*add.result));
    b.instructions.push_back(std::move(add));
    b.instructions.push_back(std::move(ret));
    b.terminated = true;
    f.blocks.push_back(std::move(b));
    return f;
}

static Function makeCaller()
{
    Function f;
    f.name = "caller";
    f.retType = Type(Type::Kind::I64);
    BasicBlock b;
    b.label = "entry";
    Instr call;
    call.result = 0; // temp 0
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "add2";
    call.operands.push_back(Value::constInt(40));
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*call.result));
    b.instructions.push_back(std::move(call));
    b.instructions.push_back(std::move(ret));
    b.terminated = true;
    f.blocks.push_back(std::move(b));
    return f;
}

static void test_inline_and_fold()
{
    Module M;
    M.functions.push_back(makeAdd2());
    M.functions.push_back(makeCaller());

    il::transform::Inliner inl;
    il::transform::AnalysisRegistry reg; // unused for inliner
    il::transform::AnalysisManager AM(M, reg);
    auto preserved = inl.run(M, AM);
    (void)preserved;

    // After inlining, caller should not contain a call
    Function &caller = M.functions[1];
    bool hasCall = false;
    for (const auto &I : caller.blocks.front().instructions)
        if (I.op == Opcode::Call)
            hasCall = true;
    assert(!hasCall);

    // Run SCCP + DCE to fold constants
    il::transform::sccp(M);
    il::transform::dce(M);

    // Caller ret should be a constant 42
    const Instr &ret = caller.blocks.front().instructions.back();
    assert(ret.op == Opcode::Ret);
    assert(!ret.operands.empty());
    assert(ret.operands.front().kind == Value::Kind::ConstInt);
    assert(ret.operands.front().i64 == 42);
}

static void test_no_inline_recursive()
{
    Module M;
    Function f;
    f.name = "self";
    f.retType = Type(Type::Kind::I64);
    BasicBlock b;
    b.label = "entry";
    Instr call;
    call.result = 0;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "self";
    call.operands.push_back(Value::constInt(1));
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*call.result));
    b.instructions.push_back(std::move(call));
    b.instructions.push_back(std::move(ret));
    b.terminated = true;
    f.blocks.push_back(std::move(b));
    M.functions.push_back(std::move(f));

    il::transform::Inliner inl;
    il::transform::AnalysisRegistry reg; // unused
    il::transform::AnalysisManager AM(M, reg);
    (void)inl.run(M, AM);

    // Call should still be present
    const Function &self = M.functions.front();
    bool hasCall = false;
    for (const auto &I : self.blocks.front().instructions)
        if (I.op == Opcode::Call)
            hasCall = true;
    assert(hasCall);
}

int main()
{
    test_inline_and_fold();
    test_no_inline_recursive();
    return 0;
}
