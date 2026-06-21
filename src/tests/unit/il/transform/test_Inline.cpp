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

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/Inline.hpp"
#include "il/transform/SCCP.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>

using namespace il::core;

static Function makeAdd2() {
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

static Function makeCaller() {
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

static void test_inline_and_fold() {
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
    for (const auto &B : caller.blocks)
        for (const auto &I : B.instructions)
            if (I.op == Opcode::Call)
                hasCall = true;
    assert(!hasCall);

    // Run SCCP + DCE to fold constants
    il::transform::sccp(M);
    il::transform::dce(M);

    // Caller ret should be a single constant 42 (now potentially in a continuation block)
    bool foundConstRet = false;
    for (const auto &B : caller.blocks) {
        for (const auto &I : B.instructions) {
            if (I.op != Opcode::Ret)
                continue;
            assert(!foundConstRet);
            assert(!I.operands.empty());
            assert(I.operands.front().kind == Value::Kind::ConstInt);
            assert(I.operands.front().i64 == 42);
            foundConstRet = true;
        }
    }
    assert(foundConstRet);
}

static void test_no_inline_recursive() {
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

// Callee whose return value is an overflow-checked add. Like the frontend, the
// iadd.ovf result carries no explicit type (Instr.type stays Void); the verifier
// infers i64.
static Function makeOvfHelper() {
    Function f;
    f.name = "ovf_helper";
    f.retType = Type(Type::Kind::I64);
    unsigned id = 0;
    Param x{"x", Type(Type::Kind::I64), id++};
    f.params.push_back(x);
    f.valueNames.resize(id);
    BasicBlock b;
    b.label = "entry";
    Instr add;
    add.result = id++;
    add.op = Opcode::IAddOvf; // result type left Void on purpose
    add.operands.push_back(Value::temp(x.id));
    add.operands.push_back(Value::constInt(1));
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

// Caller in which an iadd.ovf value (%a, Void result type) is live across the
// inlined call, so it becomes an escaped continuation-block parameter.
static Function makeOvfCaller() {
    Function f;
    f.name = "ovf_caller";
    f.retType = Type(Type::Kind::I64);
    unsigned id = 0;
    Param p{"p", Type(Type::Kind::I64), id++};
    f.params.push_back(p);
    f.valueNames.resize(id);
    BasicBlock b;
    b.label = "entry";

    Instr a; // %a = iadd.ovf %p, 5  (escapes; Void result type)
    a.result = id++;
    a.op = Opcode::IAddOvf;
    a.operands.push_back(Value::temp(p.id));
    a.operands.push_back(Value::constInt(5));
    const unsigned aId = *a.result;

    Instr call; // %b = call @ovf_helper(%a)
    call.result = id++;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "ovf_helper";
    call.operands.push_back(Value::temp(aId));
    const unsigned bId = *call.result;

    Instr c; // %c = iadd.ovf %a, %b  (uses escaped %a and the return value %b)
    c.result = id++;
    c.op = Opcode::IAddOvf;
    c.operands.push_back(Value::temp(aId));
    c.operands.push_back(Value::temp(bId));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*c.result));

    b.instructions.push_back(std::move(a));
    b.instructions.push_back(std::move(call));
    b.instructions.push_back(std::move(c));
    b.instructions.push_back(std::move(ret));
    b.terminated = true;
    f.blocks.push_back(std::move(b));
    return f;
}

// Regression: an escaped iadd.ovf value (Void Instr.type) must be typed via
// inference, not the raw void field. Before the fix the escaped continuation
// parameter was typed void and the branch-arg/param check failed.
static void test_inline_escaped_ovf_type() {
    Module M;
    M.functions.push_back(makeOvfHelper());
    M.functions.push_back(makeOvfCaller());

    assert(static_cast<bool>(il::verify::Verifier::verify(M)));

    il::transform::Inliner inl;
    il::transform::AnalysisRegistry reg;
    il::transform::AnalysisManager AM(M, reg);
    (void)inl.run(M, AM);

    assert(static_cast<bool>(il::verify::Verifier::verify(M)));
}

/// @brief Main.
int main() {
    test_inline_and_fold();
    test_no_inline_recursive();
    test_inline_escaped_ovf_type();
    return 0;
}
