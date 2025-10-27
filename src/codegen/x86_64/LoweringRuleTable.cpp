//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lookup helpers that translate declarative lowering tables into
// efficient opcode dispatch.  The dispatch indirection keeps LoweringRules.cpp
// focused on emission logic while this unit manages operand classification and
// table probing.
//
//===----------------------------------------------------------------------===//

#include "LoweringRuleTable.hpp"

#include "LowerILToMIR.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

using lowering::hasFlag;
using lowering::OperandKindPattern;
using lowering::RuleFlags;
using lowering::RuleSpec;

OperandKindPattern classifyOperand(const ILValue &value) noexcept
{
    if (value.kind == ILValue::Kind::LABEL)
    {
        return OperandKindPattern::Label;
    }
    if (value.id < 0)
    {
        return OperandKindPattern::Immediate;
    }
    return OperandKindPattern::Value;
}

bool matchesOperandPattern(const lowering::OperandShape &shape, const ILInstr &instr) noexcept
{
    const std::size_t arity = instr.ops.size();
    if (arity < shape.minArity)
    {
        return false;
    }
    if (shape.maxArity != std::numeric_limits<std::uint8_t>::max() &&
        arity > shape.maxArity)
    {
        return false;
    }

    const std::size_t checkCount = std::min<std::size_t>(shape.kindCount, arity);
    for (std::size_t idx = 0; idx < checkCount; ++idx)
    {
        const OperandKindPattern expected = shape.kinds[idx];
        if (expected == OperandKindPattern::Any)
        {
            continue;
        }

        const OperandKindPattern actual = classifyOperand(instr.ops[idx]);
        if (expected == OperandKindPattern::Value)
        {
            if (actual == OperandKindPattern::Label)
            {
                return false;
            }
            continue;
        }

        if (expected != actual)
        {
            return false;
        }
    }

    return true;
}

bool opcodeMatches(const RuleSpec &spec, std::string_view opcode) noexcept
{
    if (hasFlag(spec.flags, RuleFlags::Prefix))
    {
        return opcode.starts_with(spec.opcode);
    }
    return opcode == spec.opcode;
}

struct DispatchTables
{
    std::unordered_map<std::string_view, std::vector<const RuleSpec *>> exact{};
    std::vector<const RuleSpec *> prefix{};
};

DispatchTables buildDispatchTables()
{
    DispatchTables tables{};
    for (const auto &spec : lowering::kLoweringRuleTable)
    {
        if (hasFlag(spec.flags, RuleFlags::Prefix))
        {
            tables.prefix.push_back(&spec);
        }
        else
        {
            tables.exact[spec.opcode].push_back(&spec);
        }
    }
    return tables;
}

const DispatchTables &dispatchTables()
{
    static const DispatchTables tables = buildDispatchTables();
    return tables;
}

} // namespace

bool matchesRuleSpec(const lowering::RuleSpec &spec, const ILInstr &instr)
{
    if (!opcodeMatches(spec, instr.opcode))
    {
        return false;
    }
    return matchesOperandPattern(spec.operands, instr);
}

const lowering::RuleSpec *lookupRuleSpec(const ILInstr &instr)
{
    const auto &tables = dispatchTables();

    if (const auto it = tables.exact.find(instr.opcode); it != tables.exact.end())
    {
        for (const auto *candidate : it->second)
        {
            if (matchesOperandPattern(candidate->operands, instr))
            {
                return candidate;
            }
        }
    }

    for (const auto *candidate : tables.prefix)
    {
        if (opcodeMatches(*candidate, instr.opcode) &&
            matchesOperandPattern(candidate->operands, instr))
        {
            return candidate;
        }
    }

    return nullptr;
}

} // namespace viper::codegen::x64
