//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/utils/UseDefInfo.cpp
// Purpose: Implement use-def chain tracking for efficient SSA value replacement.
// Links: docs/codemap.md#il-utils
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements use-def chain construction and replacement.
/// @details Pre-computing use locations allows O(uses) replacement instead of
///          O(instructions) full-function scans, significantly improving the
///          performance of optimization passes that frequently replace values.

#include "il/utils/UseDefInfo.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

namespace viper::il
{

UseDefInfo::UseDefInfo(::il::core::Function &F)
{
    build(F);
}

void UseDefInfo::build(::il::core::Function &F)
{
    uses_.clear();

    for (auto &B : F.blocks)
    {
        for (auto &I : B.instructions)
        {
            // Record uses in instruction operands
            for (auto &Op : I.operands)
            {
                recordUse(Op);
            }

            // Record uses in branch arguments
            for (auto &argList : I.brArgs)
            {
                for (auto &Arg : argList)
                {
                    recordUse(Arg);
                }
            }
        }
    }
}

void UseDefInfo::recordUse(::il::core::Value &v)
{
    if (v.kind == ::il::core::Value::Kind::Temp)
    {
        uses_[v.id].push_back(&v);
    }
}

std::size_t UseDefInfo::replaceAllUses(unsigned tempId, const ::il::core::Value &replacement)
{
    auto it = uses_.find(tempId);
    if (it == uses_.end())
    {
        return 0;
    }

    std::size_t count = 0;
    for (auto *usePtr : it->second)
    {
        *usePtr = replacement;
        ++count;
    }

    // Clear the use list since we've replaced all uses
    // If the replacement is also a temp, we should add these to that temp's uses
    if (replacement.kind == ::il::core::Value::Kind::Temp)
    {
        auto &newUses = uses_[replacement.id];
        newUses.insert(newUses.end(), it->second.begin(), it->second.end());
    }

    it->second.clear();
    return count;
}

bool UseDefInfo::hasUses(unsigned tempId) const
{
    auto it = uses_.find(tempId);
    return it != uses_.end() && !it->second.empty();
}

std::size_t UseDefInfo::useCount(unsigned tempId) const
{
    auto it = uses_.find(tempId);
    return it != uses_.end() ? it->second.size() : 0;
}

} // namespace viper::il
