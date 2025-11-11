// File: tests/unit/VM_StepTcoTests.cpp
// Purpose: Verify stepping across a tail-call lands in the callee entry and
//          triggers a source-line breakpoint there on the next step.

#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include "viper/vm/VM.hpp"

#include <cassert>

using namespace il::core;

static Module build_tco_module(uint32_t fileId, uint32_t calleeLine)
{
    Module m;
    il::build::IRBuilder b(m);

    // callee() -> i64 { t0 = add 1, 1 @loc; ret t0 }
    Function &callee = b.startFunction("callee", Type(Type::Kind::I64), {});
    BasicBlock &cb = b.addBlock(callee, "entry");
    b.setInsertPoint(cb);
    Instr acc;
    acc.result = b.reserveTempId();
    acc.op = Opcode::Add;
    acc.type = Type(Type::Kind::I64);
    acc.operands.push_back(Value::constInt(1));
    acc.operands.push_back(Value::constInt(1));
    acc.loc.file_id = fileId;
    acc.loc.line = calleeLine;
    acc.loc.column = 1;
    cb.instructions.push_back(acc);
    Instr rret;
    rret.op = Opcode::Ret;
    rret.type = Type(Type::Kind::Void);
    rret.operands.push_back(Value::temp(*acc.result));
    cb.instructions.push_back(rret);
    cb.terminated = true;

    // main() -> i64 { dst = call callee(); ret dst }  (tail position)
    Function &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &mb = b.addBlock(mainFn, "entry");
    b.setInsertPoint(mb);
    unsigned dst = b.reserveTempId();
    b.emitCall("callee", {}, Value::temp(dst), {1});
    b.emitRet(Value::temp(dst), {1});
    return m;
}

int main()
{
    il::support::SourceManager sm;
    const auto fileId = sm.addFile("/tmp/examples/tco.il");

    Module m = build_tco_module(fileId, /*calleeLine=*/42);

    il::vm::RunConfig cfg;
    cfg.trace.sm = &sm;
    il::vm::Runner runner(m, cfg);

    // Set a breakpoint on the first instruction of the callee.
    il::support::SourceLoc brk;
    brk.file_id = fileId;
    brk.line = 42;
    brk.column = 1;
    runner.setBreakpoint(brk);

    // Step once to execute the tail-call in main.
    auto s1 = runner.step();
    assert(s1.status == il::vm::Runner::StepStatus::Advanced);

    // The next step should land in callee entry and trigger the breakpoint.
    auto s2 = runner.step();
    assert(s2.status == il::vm::Runner::StepStatus::BreakpointHit);

    // Continue to program halt to complete execution.
    runner.clearBreakpoints();
    auto rs = runner.continueRun();
    assert(rs == il::vm::Runner::RunStatus::Halted);
    return 0;
}

