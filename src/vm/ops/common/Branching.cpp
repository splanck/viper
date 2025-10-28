//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/vm/ops/common/Branching.cpp
// Purpose: Implement shared helpers for VM branching mechanics.
// Key invariants: Switch dispatch honours cache selection and linear fallbacks.
// Ownership/Lifetime: Helpers access VM state through the active instance.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "vm/ops/common/Branching.hpp"

#include "vm/OpHandlerAccess.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VMContext.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <variant>

namespace il::vm::detail::ops::common
{
namespace
{
int32_t lookupDense(const viper::vm::DenseJumpTable &table, int32_t sel, int32_t defIdx)
{
    const int64_t offset = static_cast<int64_t>(sel) - static_cast<int64_t>(table.base);
    if (offset < 0 || offset >= static_cast<int64_t>(table.targets.size()))
        return defIdx;
    const int32_t target = table.targets[static_cast<size_t>(offset)];
    return (target < 0) ? defIdx : target;
}

int32_t lookupSorted(const viper::vm::SortedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = std::lower_bound(cases.keys.begin(), cases.keys.end(), sel);
    if (it == cases.keys.end() || *it != sel)
        return defIdx;
    const size_t idx = static_cast<size_t>(it - cases.keys.begin());
    return cases.targetIdx[idx];
}

int32_t lookupHashed(const viper::vm::HashedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = cases.map.find(sel);
    return (it == cases.map.end()) ? defIdx : it->second;
}

[[noreturn]] void reportBranchArgMismatch(const il::core::BasicBlock &target,
                                          const std::string &sourceLabel,
                                          size_t expected,
                                          size_t provided,
                                          const il::core::Instr &in,
                                          const std::string &function)
{
    std::ostringstream os;
    os << "branch argument count mismatch targeting '" << target.label << '\'';
    if (!sourceLabel.empty())
        os << " from '" << sourceLabel << '\'';
    os << ": expected " << expected << ", got " << provided;
    RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), in.loc, function, sourceLabel);
    std::_Exit(1);
}

Target invalidTarget(const Target &fallback)
{
    Target out = fallback;
    out.valid = false;
    out.block = nullptr;
    return out;
}

Target linearSelect(Scalar scrutinee, std::span<const Case> table, Target default_tgt)
{
    for (const auto &entry : table)
    {
        if (scrutinee >= entry.lower && scrutinee <= entry.upper && entry.target.valid)
            return entry.target;
    }
    return default_tgt;
}

} // namespace

Target select_case(Scalar scrutinee, std::span<const Case> table, Target default_tgt)
{
    const viper::vm::SwitchCacheEntry *cache = default_tgt.cache;
    if (!cache && !table.empty())
        cache = table.front().target.cache;

    if (!cache)
        return linearSelect(scrutinee, table, default_tgt);

    const bool forceLinear = (cache->kind == viper::vm::SwitchCacheEntry::Linear);
#if defined(VIPER_VM_DEBUG_SWITCH_LINEAR)
    (void)forceLinear;
    return linearSelect(scrutinee, table, default_tgt);
#else
    if (forceLinear)
        return linearSelect(scrutinee, table, default_tgt);

    int32_t idx = cache->defaultIdx;
    std::visit(
        [&](auto &&backend)
        {
            using BackendT = std::decay_t<decltype(backend)>;
            if constexpr (std::is_same_v<BackendT, viper::vm::DenseJumpTable>)
                idx = lookupDense(backend, scrutinee, cache->defaultIdx);
            else if constexpr (std::is_same_v<BackendT, viper::vm::SortedCases>)
                idx = lookupSorted(backend, scrutinee, cache->defaultIdx);
            else if constexpr (std::is_same_v<BackendT, viper::vm::HashedCases>)
                idx = lookupHashed(backend, scrutinee, cache->defaultIdx);
        },
        cache->backend);

    if (idx < 0)
        return invalidTarget(default_tgt);

    if (idx == static_cast<int32_t>(default_tgt.labelIndex))
        return default_tgt;

    for (const auto &entry : table)
    {
        if (static_cast<int32_t>(entry.target.labelIndex) == idx)
            return entry.target;
    }

    return invalidTarget(default_tgt);
#endif
}

void jump(Frame &fr, Target target)
{
    assert(target.cursor && target.ip && "branch target requires control pointers");
    assert(target.instr && "branch target requires originating instruction");

    if (!target.valid || target.block == nullptr)
        return;

    VM *vm = activeVMInstance();
    assert(vm && "active VM instance required for branching");

    const il::core::BasicBlock *sourceBlock = *target.cursor;
    const std::string sourceLabel = sourceBlock ? sourceBlock->label : std::string();
    const std::string functionName = fr.func ? fr.func->name : std::string();

    const size_t expected = target.block->params.size();
    const size_t provided = target.args.size();
    if (provided != expected)
        reportBranchArgMismatch(*target.block, sourceLabel, expected, provided, *target.instr, functionName);

    for (size_t i = 0; i < provided; ++i)
    {
        const auto &param = target.block->params[i];
        const auto id = param.id;
        assert(id < fr.params.size());
        Slot incoming = VMAccess::eval(*vm, fr, target.args[i]);
        auto &dest = fr.params[id];
        if (param.type.kind == il::core::Type::Kind::Str)
        {
            if (dest)
                rt_str_release_maybe(dest->str);
            rt_str_retain_maybe(incoming.str);
            dest = incoming;
            continue;
        }
        dest = incoming;
    }

    *target.cursor = target.block;
    *target.ip = 0;
}

Scalar eval_scrutinee(Frame &fr, const il::core::Instr &instr)
{
    VM *vm = activeVMInstance();
    assert(vm && "eval_scrutinee requires active VM instance");
    Slot slot = VMAccess::eval(*vm, fr, il::core::switchScrutinee(instr));
    return static_cast<Scalar>(slot.i64);
}

Target make_target(const il::core::Instr &instr,
                   size_t labelIndex,
                   const VM::BlockMap &blocks,
                   const il::core::BasicBlock *&bb,
                   size_t &ip,
                   const viper::vm::SwitchCacheEntry *cacheEntry)
{
    Target target{};
    target.instr = &instr;
    target.labelIndex = labelIndex;
    target.cursor = &bb;
    target.ip = &ip;
    target.cache = cacheEntry;

    if (labelIndex < instr.labels.size())
    {
        const auto &label = instr.labels[labelIndex];
        auto it = blocks.find(label);
        assert(it != blocks.end() && "branch target label must exist");
        target.block = it->second;
        target.valid = target.block != nullptr;
    }

    if (labelIndex < instr.brArgs.size())
        target.args = std::span<const il::core::Value>(instr.brArgs[labelIndex]);

    return target;
}

} // namespace il::vm::detail::ops::common

