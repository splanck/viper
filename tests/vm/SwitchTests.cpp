// File: tests/vm/SwitchTests.cpp
// Purpose: Verify that VM switch.i32 selects the correct target and default blocks.
// Key invariants: Each switch arm returns its associated value and defaults when unmatched.
// Ownership/Lifetime: Modules are constructed per test and executed immediately.
// Links: docs/il-guide.md#reference#control-flow-terminators

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace il::core;

namespace
{
struct SwitchCase
{
    std::string label;
    int32_t match;
    int64_t result;
};

struct SwitchProgram
{
    std::string defaultLabel;
    int64_t defaultResult;
    std::vector<SwitchCase> cases;
};

Instr makeRet(int64_t value)
{
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(value));
    return ret;
}

Module buildSwitchModule(const SwitchProgram &program, int32_t scrutinee)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});

    builder.addBlock(fn, "entry");
    builder.addBlock(fn, program.defaultLabel);
    for (const auto &cs : program.cases)
        builder.addBlock(fn, cs.label);

    auto findBlock = [&fn](const std::string &label) -> BasicBlock & {
        for (auto &block : fn.blocks)
        {
            if (block.label == label)
                return block;
        }
        assert(false && "block label not found");
        std::abort();
    };

    BasicBlock &entry = findBlock("entry");
    BasicBlock &defaultBlock = findBlock(program.defaultLabel);

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(Value::constInt(scrutinee));
    sw.labels.push_back(program.defaultLabel);
    sw.brArgs.emplace_back();

    for (const auto &cs : program.cases)
    {
        sw.operands.push_back(Value::constInt(cs.match));
        sw.labels.push_back(cs.label);
        sw.brArgs.emplace_back();
    }

    entry.instructions.push_back(sw);
    entry.terminated = true;

    defaultBlock.instructions.push_back(makeRet(program.defaultResult));
    defaultBlock.terminated = true;

    for (const auto &cs : program.cases)
    {
        BasicBlock &block = findBlock(cs.label);
        block.instructions.push_back(makeRet(cs.result));
        block.terminated = true;
    }

    return module;
}

int64_t runSwitch(const SwitchProgram &program, int32_t scrutinee)
{
    Module module = buildSwitchModule(program, scrutinee);
    il::vm::VM vm(module);
    return vm.run();
}
}

int main()
{
    const SwitchProgram dense{
        "dense_default",
        99,
        {{"dense_case_0", 0, 10}, {"dense_case_1", 1, 20}, {"dense_case_2", 2, 30}},
    };

    assert(runSwitch(dense, 0) == 10);
    assert(runSwitch(dense, 1) == 20);
    assert(runSwitch(dense, 2) == 30);
    assert(runSwitch(dense, 7) == 99);

    const SwitchProgram sparse{
        "sparse_default",
        0,
        {{"sparse_case_0", 2, 200}, {"sparse_case_1", 10, 1000}, {"sparse_case_2", 42, 4200}},
    };

    assert(runSwitch(sparse, 2) == 200);
    assert(runSwitch(sparse, 10) == 1000);
    assert(runSwitch(sparse, 42) == 4200);
    assert(runSwitch(sparse, 7) == 0);

    return 0;
}
