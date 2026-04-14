//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/utils/UseDefInfo.cpp
// Purpose: Implement temporary use-count tracking and safe SSA value
//          replacement for mutable IL.
// Links: docs/codemap.md#il-utils
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements use-count construction and safe replacement.
/// @details Counts are rebuilt from the function when replacements occur so the
///          helper remains correct even after instruction vectors are mutated.

#include "il/utils/UseDefInfo.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

namespace viper::il {

UseDefInfo::UseDefInfo(::il::core::Function &F) {
    build(F);
}

void UseDefInfo::build(::il::core::Function &F) {
    function_ = &F;
    useCounts_.clear();

    for (auto &B : F.blocks) {
        for (auto &I : B.instructions) {
            // Record uses in instruction operands
            for (auto &Op : I.operands) {
                recordUse(Op);
            }

            // Record uses in branch arguments
            for (auto &argList : I.brArgs) {
                for (auto &Arg : argList) {
                    recordUse(Arg);
                }
            }
        }
    }
}

void UseDefInfo::recordUse(::il::core::Value &v) {
    if (v.kind == ::il::core::Value::Kind::Temp) {
        ++useCounts_[v.id];
    }
}

std::size_t UseDefInfo::replaceAllUses(unsigned tempId, const ::il::core::Value &replacement) {
    if (!function_) {
        return 0;
    }

    std::size_t count = 0;
    for (auto &B : function_->blocks) {
        for (auto &I : B.instructions) {
            for (auto &Op : I.operands) {
                if (Op.kind == ::il::core::Value::Kind::Temp && Op.id == tempId) {
                    Op = replacement;
                    ++count;
                }
            }
            for (auto &argList : I.brArgs) {
                for (auto &Arg : argList) {
                    if (Arg.kind == ::il::core::Value::Kind::Temp && Arg.id == tempId) {
                        Arg = replacement;
                        ++count;
                    }
                }
            }
        }
    }

    // Rebuild observed counts from the mutated function so later queries are
    // coherent even after instruction insertion/erasure shifted storage.
    build(*function_);
    return count;
}

bool UseDefInfo::hasUses(unsigned tempId) const {
    auto it = useCounts_.find(tempId);
    return it != useCounts_.end() && it->second != 0;
}

std::size_t UseDefInfo::useCount(unsigned tempId) const {
    auto it = useCounts_.find(tempId);
    return it != useCounts_.end() ? it->second : 0;
}

} // namespace viper::il
