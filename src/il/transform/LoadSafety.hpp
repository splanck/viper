//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

inline std::optional<int64_t> constSignedOffset(const core::Value &value) {
    if (value.kind != core::Value::Kind::ConstInt)
        return std::nullopt;
    return value.i64;
}

inline bool loadBaseWithinAlloca(const core::Instr &allocaInstr,
                                 int64_t offset,
                                 unsigned accessSize) {
    if (allocaInstr.operands.empty())
        return false;
    auto allocSize = constNonNegativeOffset(allocaInstr.operands[0]);
    if (!allocSize)
        return false;
    if (offset < 0)
        return false;
    const uint64_t unsignedOffset = static_cast<uint64_t>(offset);
    return unsignedOffset <= *allocSize && accessSize <= (*allocSize - unsignedOffset);
}

inline bool allocaSizeKnownNonTrapping(const core::Instr &allocaInstr) {
    if (allocaInstr.op != core::Opcode::Alloca || allocaInstr.operands.empty())
        return false;
    auto allocSize = constNonNegativeOffset(allocaInstr.operands[0]);
    if (!allocSize)
        return false;
    return true;
}

inline bool isPointerKnownDereferenceable(
    const core::Value &ptr,
    unsigned accessSize,
    const std::unordered_map<unsigned, const core::Instr *> &defs,
    std::unordered_set<unsigned> &visiting,
    int64_t offset = 0) {
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
        auto gepOffset = constSignedOffset(def.operands[1]);
        if (gepOffset) {
            const bool overflows = (*gepOffset > 0 &&
                                    offset > std::numeric_limits<int64_t>::max() - *gepOffset) ||
                                   (*gepOffset < 0 &&
                                    offset < std::numeric_limits<int64_t>::min() - *gepOffset);
            if (!overflows) {
                const int64_t nextOffset = offset + *gepOffset;
                safe = isPointerKnownDereferenceable(
                    def.operands[0], accessSize, defs, visiting, nextOffset);
            }
        }
    }

    visiting.erase(ptr.id);
    return safe;
}

} // namespace detail

/// @brief Reusable definition index for a stable function snapshot.
/// @details Construct once, issue any number of load/store safety queries, then
///          discard before inserting or erasing instructions.
class LoadSafetyContext {
  public:
    explicit LoadSafetyContext(const core::Function &fn) : defs_(detail::buildTempDefMap(fn)) {}

    [[nodiscard]] bool isPointerDereferenceable(const core::Value &pointer,
                                                unsigned accessSize) const {
        std::unordered_set<unsigned> visiting;
        return detail::isPointerKnownDereferenceable(
            pointer, accessSize, defs_, visiting);
    }

  private:
    std::unordered_map<unsigned, const core::Instr *> defs_;
};

/// @brief Return true when removing @p alloca cannot suppress an allocation trap.
inline bool isAllocaKnownNonTrapping(const core::Instr &allocaInstr) {
    return detail::allocaSizeKnownNonTrapping(allocaInstr);
}

/// @brief Return true when removing, reusing, or hoisting @p load cannot remove
///        a null, bounds, or alignment trap under the current IL semantics.
inline bool isLoadKnownNonTrapping(const core::Function &fn, const core::Instr &load) {
    if (load.op != core::Opcode::Load || load.operands.empty())
        return false;

    auto accessSize = zanna::analysis::BasicAA::typeSizeBytes(load.type);
    if (!accessSize)
        return false;

    auto defs = detail::buildTempDefMap(fn);
    std::unordered_set<unsigned> visiting;
    return detail::isPointerKnownDereferenceable(load.operands[0], *accessSize, defs, visiting);
}

inline bool isLoadKnownNonTrapping(const LoadSafetyContext &context,
                                   const core::Instr &load) {
    if (load.op != core::Opcode::Load || load.operands.empty())
        return false;
    auto accessSize = zanna::analysis::BasicAA::typeSizeBytes(load.type);
    return accessSize && context.isPointerDereferenceable(load.operands[0], *accessSize);
}

/// @brief Return true when removing @p store cannot suppress a memory trap.
inline bool isStoreKnownNonTrapping(const core::Function &fn, const core::Instr &store) {
    if (store.op != core::Opcode::Store || store.operands.empty())
        return false;

    auto accessSize = zanna::analysis::BasicAA::typeSizeBytes(store.type);
    if (!accessSize)
        return false;

    auto defs = detail::buildTempDefMap(fn);
    std::unordered_set<unsigned> visiting;
    return detail::isPointerKnownDereferenceable(store.operands[0], *accessSize, defs, visiting);
}


inline bool isStoreKnownNonTrapping(const LoadSafetyContext &context,
                                    const core::Instr &store) {
    if (store.op != core::Opcode::Store || store.operands.empty())
        return false;
    auto accessSize = zanna::analysis::BasicAA::typeSizeBytes(store.type);
    return accessSize && context.isPointerDereferenceable(store.operands[0], *accessSize);
}

} // namespace il::transform
