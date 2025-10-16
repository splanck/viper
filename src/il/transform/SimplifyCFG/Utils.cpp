// File: src/il/transform/SimplifyCFG/Utils.cpp
// License: MIT (see LICENSE for details).
// Purpose: Implements shared helper routines for SimplifyCFG transforms.
// Key invariants: Maintains structural correctness when mutating CFG constructs.
// Ownership/Lifetime: Manipulates caller-owned IL IR in place.
// Links: docs/codemap.md

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/ControlFlowChecker.hpp"

#include <cassert>
#include <cstdlib>

namespace il::transform::simplify_cfg
{

il::core::Instr *findTerminator(il::core::BasicBlock &block)
{
    for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it)
    {
        if (il::verify::isTerminator(it->op))
            return &*it;
    }
    return nullptr;
}

const il::core::Instr *findTerminator(const il::core::BasicBlock &block)
{
    return findTerminator(const_cast<il::core::BasicBlock &>(block));
}

bool valuesEqual(const il::core::Value &lhs, const il::core::Value &rhs)
{
    if (lhs.kind != rhs.kind)
        return false;

    switch (lhs.kind)
    {
        case il::core::Value::Kind::Temp:
            return lhs.id == rhs.id;
        case il::core::Value::Kind::ConstInt:
            return lhs.i64 == rhs.i64 && lhs.isBool == rhs.isBool;
        case il::core::Value::Kind::ConstFloat:
            return lhs.f64 == rhs.f64;
        case il::core::Value::Kind::ConstStr:
        case il::core::Value::Kind::GlobalAddr:
            return lhs.str == rhs.str;
        case il::core::Value::Kind::NullPtr:
            return true;
    }

    return false;
}

bool valueVectorsEqual(const std::vector<il::core::Value> &lhs,
                       const std::vector<il::core::Value> &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t index = 0; index < lhs.size(); ++index)
    {
        if (!valuesEqual(lhs[index], rhs[index]))
            return false;
    }

    return true;
}

il::core::Value substituteValue(
    const il::core::Value &value,
    const std::unordered_map<unsigned, il::core::Value> &mapping)
{
    if (value.kind != il::core::Value::Kind::Temp)
        return value;

    if (auto it = mapping.find(value.id); it != mapping.end())
        return it->second;

    return value;
}

size_t lookupBlockIndex(const std::unordered_map<std::string, size_t> &labelToIndex,
                        const std::string &label)
{
    if (auto it = labelToIndex.find(label); it != labelToIndex.end())
        return it->second;
    return static_cast<size_t>(-1);
}

void enqueueSuccessor(BitVector &reachable,
                      std::deque<size_t> &worklist,
                      size_t successor)
{
    if (successor == static_cast<size_t>(-1))
        return;
    if (successor < reachable.size() && !reachable.test(successor))
    {
        reachable.set(successor);
        worklist.push_back(successor);
    }
}

bool readDebugFlagFromEnv()
{
    if (const char *flag = std::getenv("VIPER_DEBUG_PASSES"))
        return flag[0] != '\0';
    return false;
}

bool hasSideEffects(const il::core::Instr &instr)
{
    return il::core::getOpcodeInfo(instr.op).hasSideEffects;
}

bool isEntryLabel(const std::string &label)
{
    return label == "entry" || label.rfind("entry_", 0) == 0;
}

bool isResumeOpcode(il::core::Opcode op)
{
    return op == il::core::Opcode::ResumeSame || op == il::core::Opcode::ResumeNext ||
           op == il::core::Opcode::ResumeLabel;
}

bool isEhStructuralOpcode(il::core::Opcode op)
{
    switch (op)
    {
        case il::core::Opcode::EhPush:
        case il::core::Opcode::EhPop:
        case il::core::Opcode::EhEntry:
            return true;
        default:
            return false;
    }
}

bool isEHSensitiveBlock(const il::core::BasicBlock &block)
{
    if (block.instructions.empty())
        return false;

    if (block.instructions.front().op == il::core::Opcode::EhEntry)
        return true;

    for (const auto &instr : block.instructions)
    {
        if (isEhStructuralOpcode(instr.op))
            return true;
    }

    const il::core::Instr *terminator = findTerminator(block);
    return terminator && isResumeOpcode(terminator->op);
}

} // namespace il::transform::simplify_cfg

