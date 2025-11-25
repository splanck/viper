//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/SwitchDispatchTests.cpp
// Purpose: Ensure the VM dispatch table executes SwitchI32 handlers correctly.
// Key invariants: Matching case transfers to correct block, default is taken otherwise.
// Ownership/Lifetime: Builds ephemeral modules per scenario and executes immediately.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdlib>
#include <utility>
#include <vector>

using namespace il::core;

namespace
{
struct CaseSpec
{
    std::string label;
    int32_t match;
    int64_t ret;
};

struct SwitchSpec
{
    int32_t scrutinee;
    std::string defaultLabel;
    int64_t defaultValue;
    std::vector<CaseSpec> cases;
};

Instr makeRet(int64_t value)
{
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(value));
    return ret;
}

Module buildSwitchModule(const SwitchSpec &spec)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    builder.addBlock(fn, "entry");
    builder.addBlock(fn, spec.defaultLabel);
    for (const auto &cs : spec.cases)
        builder.addBlock(fn, cs.label);

    auto findBlock = [&fn](const std::string &label) -> BasicBlock &
    {
        for (auto &block : fn.blocks)
        {
            if (block.label == label)
                return block;
        }
        assert(false && "block label not found");
        std::abort();
    };

    BasicBlock &entry = findBlock("entry");
    BasicBlock &defaultBlock = findBlock(spec.defaultLabel);

    std::vector<BasicBlock *> caseBlocks;
    caseBlocks.reserve(spec.cases.size());
    for (const auto &cs : spec.cases)
        caseBlocks.push_back(&findBlock(cs.label));

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(Value::constInt(spec.scrutinee));
    sw.labels.push_back(spec.defaultLabel);
    sw.brArgs.emplace_back();
    for (const auto &cs : spec.cases)
    {
        sw.operands.push_back(Value::constInt(cs.match));
        sw.labels.push_back(cs.label);
        sw.brArgs.emplace_back();
    }
    entry.instructions.push_back(sw);
    entry.terminated = true;

    defaultBlock.instructions.push_back(makeRet(spec.defaultValue));
    defaultBlock.terminated = true;
    for (size_t i = 0; i < caseBlocks.size(); ++i)
    {
        caseBlocks[i]->instructions.push_back(makeRet(spec.cases[i].ret));
        caseBlocks[i]->terminated = true;
    }

    return module;
}

int64_t runSwitch(SwitchSpec spec, int32_t scrutinee)
{
    spec.scrutinee = scrutinee;
    Module module = buildSwitchModule(spec);
    il::vm::VM vm(module);
    return vm.run();
}
} // namespace

int main()
{
    const auto &handlers = il::vm::VM::getOpcodeHandlers();
    const auto switchIndex = static_cast<size_t>(Opcode::SwitchI32);
    assert(handlers[switchIndex] != nullptr && "SwitchI32 handler must be registered");

    const SwitchSpec spec{7, "default_block", 99, {{"case_hit", 7, 77}}};
    assert(runSwitch(spec, 7) == 77);
    assert(runSwitch(spec, 0) == 99);

    return 0;
}
