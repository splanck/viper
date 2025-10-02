// File: src/il/core/Instr.cpp
// Purpose: Provides helper methods for IL instructions.
// Key invariants: Switch helpers assert opcode correctness.
// Ownership/Lifetime: Instructions stored by value in blocks.
// Links: docs/il-guide.md#reference

#include "il/core/Instr.hpp"

#include <cassert>

namespace il::core
{
namespace
{
void requireSwitch(const Instr &instr)
{
    assert(instr.op == Opcode::SwitchI32 && "expected switch instruction");
}

const std::vector<Value> &argsOrEmpty(const Instr &instr, size_t index)
{
    requireSwitch(instr);
    assert(index < instr.labels.size());
    assert(index < instr.brArgs.size());
    return instr.brArgs[index];
}
} // namespace

const Value &switchScrutinee(const Instr &instr)
{
    requireSwitch(instr);
    assert(!instr.operands.empty());
    return instr.operands.front();
}

const std::string &switchDefaultLabel(const Instr &instr)
{
    requireSwitch(instr);
    assert(!instr.labels.empty());
    return instr.labels.front();
}

const std::vector<Value> &switchDefaultArgs(const Instr &instr)
{
    return argsOrEmpty(instr, 0);
}

size_t switchCaseCount(const Instr &instr)
{
    requireSwitch(instr);
    if (instr.labels.empty())
        return 0;
    return instr.labels.size() - 1;
}

const Value &switchCaseValue(const Instr &instr, size_t index)
{
    requireSwitch(instr);
    assert(index < switchCaseCount(instr));
    assert(instr.operands.size() > index + 1);
    return instr.operands[index + 1];
}

const std::string &switchCaseLabel(const Instr &instr, size_t index)
{
    requireSwitch(instr);
    assert(index < switchCaseCount(instr));
    return instr.labels[index + 1];
}

const std::vector<Value> &switchCaseArgs(const Instr &instr, size_t index)
{
    return argsOrEmpty(instr, index + 1);
}

} // namespace il::core
