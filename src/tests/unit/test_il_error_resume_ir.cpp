//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_error_resume_ir.cpp
// Purpose: Exercise construction of error-handling IL primitives in memory.
// Key invariants: Handler parameters adopt Error/ResumeTok nominal types.
// Ownership/Lifetime: Owns local module/function instances only.
// Links: docs/specs/errors.md
//
//===----------------------------------------------------------------------===//

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>

int main()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "trap_demo";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    Instr push;
    push.op = Opcode::EhPush;
    push.type = Type(Type::Kind::Void);
    push.labels.push_back("handler");
    entry.instructions.push_back(push);

    Instr trap;
    trap.op = Opcode::Trap;
    trap.type = Type(Type::Kind::Void);
    entry.instructions.push_back(trap);
    entry.terminated = true;

    BasicBlock handler;
    handler.label = "handler";
    handler.params.push_back({"err", Type(Type::Kind::Error), 0});
    handler.params.push_back({"tok", Type(Type::Kind::ResumeTok), 1});

    Instr entryMarker;
    entryMarker.op = Opcode::EhEntry;
    entryMarker.type = Type(Type::Kind::Void);
    handler.instructions.push_back(entryMarker);

    Instr resume;
    resume.op = Opcode::ResumeNext;
    resume.type = Type(Type::Kind::Void);
    resume.operands.push_back(Value::temp(handler.params[1].id));
    handler.instructions.push_back(resume);
    handler.terminated = true;

    fn.blocks.push_back(entry);
    fn.blocks.push_back(handler);
    m.functions.push_back(fn);

    assert(m.functions.size() == 1);
    const Function &built = m.functions.front();
    assert(built.blocks.size() == 2);

    const BasicBlock &builtEntry = built.blocks[0];
    assert(builtEntry.instructions.size() == 2);
    assert(builtEntry.instructions[0].op == Opcode::EhPush);
    assert(builtEntry.instructions[0].labels.size() == 1);
    assert(builtEntry.instructions[0].labels[0] == "handler");
    assert(builtEntry.instructions[1].op == Opcode::Trap);

    const BasicBlock &builtHandler = built.blocks[1];
    assert(builtHandler.params.size() == 2);
    assert(builtHandler.params[0].type.kind == Type::Kind::Error);
    assert(builtHandler.params[1].type.kind == Type::Kind::ResumeTok);
    assert(builtHandler.instructions.size() == 2);
    assert(builtHandler.instructions[0].op == Opcode::EhEntry);
    assert(builtHandler.instructions[1].op == Opcode::ResumeNext);
    assert(builtHandler.instructions[1].operands.size() == 1);
    assert(builtHandler.instructions[1].operands[0].kind == Value::Kind::Temp);
    assert(builtHandler.instructions[1].operands[0].id == handler.params[1].id);

    assert(Type(Type::Kind::Error).toString() == "error");
    assert(Type(Type::Kind::ResumeTok).toString() == "resume_tok");

    return 0;
}
