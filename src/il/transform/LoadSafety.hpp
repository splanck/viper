//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/LoadSafety.hpp
// Purpose: Shared helpers for deciding whether an IL load can be optimized
//          without changing documented trap behaviour.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/analysis/BasicAA.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace il::transform {

namespace detail {

inline std::unordered_map<unsigned, const core::Instr *> buildTempDefMap(const core::Function &fn) {
    std::unordered_map<unsigned, const core::Instr *> defs;
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            if (instr.result)
                defs.emplace(*instr.result, &instr);
    return defs;
}

inline std::optional<unsigned> constNonNegativeOffset(const core::Value &value) {
    if (value.kind != core::Value::Kind::ConstInt || value.i64 < 0)
        return std::nullopt;
    if (value.i64 > static_cast<long long>(std::numeric_limits<unsigned>::max()))
        return std::nullopt;
    return static_cast<unsigned>(value.i64);
}

inline bool loadBaseWithinAlloca(const core::Instr &allocaInstr,
                                 unsigned offset,
                                 unsigned accessSize) {
    if (allocaInstr.operands.empty())
        return false;
    auto allocSize = constNonNegativeOffset(allocaInstr.operands[0]);
    if (!allocSize)
        return false;
    return offset <= *allocSize && accessSize <= (*allocSize - offset);
}

inline bool isPointerKnownDereferenceable(
    const core::Value &ptr,
    unsigned accessSize,
    const std::unordered_map<unsigned, const core::Instr *> &defs,
    std::unordered_set<unsigned> &visiting,
    unsigned offset = 0) {
    if (ptr.kind != core::Value::Kind::Temp)
        return false;
    if (!visiting.insert(ptr.id).second)
        return false;

    auto defIt = defs.find(ptr.id);
    if (defIt == defs.end()) {
        visiting.erase(ptr.id);
        return false;
    }

    const core::Instr &def = *defIt->second;
    bool safe = false;
    if (def.op == core::Opcode::Alloca) {
        safe = loadBaseWithinAlloca(def, offset, accessSize);
    } else if (def.op == core::Opcode::GEP && def.operands.size() >= 2) {
        auto gepOffset = constNonNegativeOffset(def.operands[1]);
        if (gepOffset) {
            const unsigned nextOffset = offset + *gepOffset;
            if (nextOffset >= offset) {
                safe = isPointerKnownDereferenceable(
                    def.operands[0], accessSize, defs, visiting, nextOffset);
            }
        }
    }

    visiting.erase(ptr.id);
    return safe;
}

} // namespace detail

/// @brief Return true when removing, reusing, or hoisting @p load cannot remove
///        a null, bounds, or alignment trap under the current IL semantics.
inline bool isLoadKnownNonTrapping(const core::Function &fn, const core::Instr &load) {
    if (load.op != core::Opcode::Load || load.operands.empty())
        return false;

    auto accessSize = viper::analysis::BasicAA::typeSizeBytes(load.type);
    if (!accessSize)
        return false;

    auto defs = detail::buildTempDefMap(fn);
    std::unordered_set<unsigned> visiting;
    return detail::isPointerKnownDereferenceable(load.operands[0], *accessSize, defs, visiting);
}

} // namespace il::transform
