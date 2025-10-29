//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/codegen/x86_64/LoweringRuleTable.cpp
// Purpose: Translate declarative lowering rules into efficient opcode
//          dispatch queries for the x86-64 backend.
// Key invariants: Operand classifications must match the IL operand encodings
//                 and dispatch tables remain immutable after initialisation.
// Ownership/Lifetime: Dispatch tables are computed on first use and cached for
//                     the lifetime of the process.
// Links: docs/codemap.md, src/codegen/x86_64/LoweringRules.cpp
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

/// @brief Categorise an IL operand for lowering rule matching.
/// @details Values tagged as labels represent control-flow targets, negative
///          identifiers denote immediates emitted during lowering, and all
///          remaining operands map to SSA temporaries.  The categories mirror
///          those referenced by the declarative lowering table.
/// @param value Operand extracted from an IL instruction.
/// @return Operand kind classification used by the rule engine.
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

/// @brief Check whether an instruction satisfies a declarative operand shape.
/// @details Validates arity constraints before comparing each operand against
///          the expected kind.  Patterns may mark elements as `Any` to bypass
///          matching and may allow extra operands when `maxArity` is `UINT8_MAX`.
/// @param shape Operand requirements captured in the lowering rule.
/// @param instr Instruction whose operands are being tested.
/// @return True when all mandatory constraints are met.
bool matchesOperandPattern(const lowering::OperandShape &shape, const ILInstr &instr) noexcept
{
    const std::size_t arity = instr.ops.size();
    if (arity < shape.minArity)
    {
        return false;
    }
    if (shape.maxArity != std::numeric_limits<std::uint8_t>::max() && arity > shape.maxArity)
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

/// @brief Determine whether a rule spec targets a given opcode.
/// @details Rules marked with the prefix flag treat their opcode string as a
///          prefix match; all other rules require an exact string match.
/// @param spec Rule specification to evaluate.
/// @param opcode Opcode mnemonic from the instruction.
/// @return True when the opcode falls within the rule's domain.
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

/// @brief Construct the cached dispatch tables for rule lookup.
/// @details Partitions the declarative lowering table into exact and prefix
///          groups so that hot-path lookups can avoid scanning unrelated rules.
///          The resulting structure is consumed by @ref dispatchTables().
/// @return Aggregated dispatch tables ready for reuse across lookups.
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

/// @brief Access the lazily constructed dispatch tables.
/// @details Builds the tables the first time the function is called and then
///          returns the cached instance on subsequent invocations.  Thread-safe
///          initialisation is guaranteed by the C++ static initialisation rules.
/// @return Reference to the cached dispatch tables.
const DispatchTables &dispatchTables()
{
    static const DispatchTables tables = buildDispatchTables();
    return tables;
}

} // namespace

/// @brief Determine whether a lowering rule applies to an instruction.
/// @details Combines opcode and operand checks, providing a convenient wrapper
///          for callers that already have a concrete rule candidate.
/// @param spec Candidate rule specification.
/// @param instr Instruction being lowered.
/// @return True when the rule constraints are satisfied.
bool matchesRuleSpec(const lowering::RuleSpec &spec, const ILInstr &instr)
{
    if (!opcodeMatches(spec, instr.opcode))
    {
        return false;
    }
    return matchesOperandPattern(spec.operands, instr);
}

/// @brief Find the first lowering rule that matches an instruction.
/// @details Consults the exact-match table before scanning prefix rules,
///          returning as soon as a compatible candidate is identified.
/// @param instr Instruction that requires a lowering rule.
/// @return Pointer to the matching rule or nullptr if none applies.
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
