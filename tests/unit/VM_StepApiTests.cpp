// File: tests/unit/VM_StepApiTests.cpp
// Purpose: Validate Runner step/continue APIs and breakpoint behavior.
// Notes: Builds a tiny IL module with two instructions and a source breakpoint
//        on the second; verifies step then continue stops at the breakpoint.

#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include "viper/vm/VM.hpp"

#include <cassert>

using namespace il::core;

static Module build_simple_module(uint32_t fileId)
{
    Module m;
    il::build::IRBuilder b(m);

    // main() -> i64
    Function &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &entry = b.addBlock(mainFn, "entry");
    b.setInsertPoint(entry);

    // t0 = add 1, 2  (line 5)
    Instr add1;
    add1.result = b.reserveTempId();
    add1.op = Opcode::Add;
    add1.type = Type(Type::Kind::I64);
    add1.operands.push_back(Value::constInt(1));
    add1.operands.push_back(Value::constInt(2));
    add1.loc.file_id = fileId;
    add1.loc.line = 5;
    add1.loc.column = 1;
    entry.instructions.push_back(add1);

    // t1 = add t0, 3  (line 7) — place breakpoint here
    Instr add2;
    add2.result = b.reserveTempId();
    add2.op = Opcode::Add;
    add2.type = Type(Type::Kind::I64);
    add2.operands.push_back(Value::temp(*add1.result));
    add2.operands.push_back(Value::constInt(3));
    add2.loc.file_id = fileId;
    add2.loc.line = 7;
    add2.loc.column = 1;
    entry.instructions.push_back(add2);

    // ret t1
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*add2.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    return m;
}

int main()
{
    il::support::SourceManager sm;
    const auto fileId = sm.addFile("/tmp/examples/step.il");

    Module m = build_simple_module(fileId);

    // Configure runner with source manager so breakpoints resolve file ids.
    il::vm::RunConfig cfg;
    cfg.trace.sm = &sm;
    il::vm::Runner runner(m, cfg);

    // First step should advance one instruction (not at breakpoint yet).
    auto r1 = runner.step();
    assert(r1.status == il::vm::Runner::StepStatus::Advanced);

    // Set breakpoint on line 7 and continue; expect to stop at breakpoint.
    il::support::SourceLoc brkLoc;
    brkLoc.file_id = fileId;
    brkLoc.line = 7;
    brkLoc.column = 1;
    runner.setBreakpoint(brkLoc);

    auto rs = runner.continueRun();
    assert(rs == il::vm::Runner::RunStatus::BreakpointHit);

    // Clear breakpoints and continue to program halt.
    runner.clearBreakpoints();
    auto rs2 = runner.continueRun();
    assert(rs2 == il::vm::Runner::RunStatus::Halted);

    return 0;
}
