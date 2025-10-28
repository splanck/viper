//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/LoweringRules.cpp
// Purpose: Assemble the lowering rule registry by bridging declarative table
//          entries to match and emit thunks.
// Key invariants: Rules are initialised lazily and remain immutable after the
//                 first access.  The generated registry mirrors the layout of
//                 the declarative table.
// Ownership/Lifetime: Returned registry is stored as a static vector whose
//                     lifetime spans the process.
//
//===----------------------------------------------------------------------===//

#include "LoweringRules.hpp"

#include "LoweringRuleTable.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

template <std::size_t Index>
bool matchRuleThunk(const IL::Instr &instr)
{
    return matchesRuleSpec(lowering::kLoweringRuleTable[Index], instr);
}

template <std::size_t Index>
void emitRuleThunk(const IL::Instr &instr, MIRBuilder &builder)
{
    lowering::kLoweringRuleTable[Index].emit(instr, builder);
}

template <std::size_t... Indices>
const std::vector<LoweringRule> &makeRules(std::index_sequence<Indices...>)
{
    static const std::vector<LoweringRule> rules{
        LoweringRule{&matchRuleThunk<Indices>, &emitRuleThunk<Indices>,
                     lowering::kLoweringRuleTable[Indices].name}...};
    return rules;
}

const std::vector<LoweringRule> &buildRules()
{
    static const auto &rules =
        makeRules(std::make_index_sequence<lowering::kLoweringRuleTable.size()>{});
    return rules;
}

} // namespace

const std::vector<LoweringRule> &viper_get_lowering_rules()
{
    return buildRules();
}

const LoweringRule *viper_select_rule(const IL::Instr &instr)
{
    const auto *spec = lookupRuleSpec(instr);
    if (!spec)
    {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(spec - lowering::kLoweringRuleTable.data());
    const auto &rules = buildRules();
    return index < rules.size() ? &rules[index] : nullptr;
}

} // namespace viper::codegen::x64

