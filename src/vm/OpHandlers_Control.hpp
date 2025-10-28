// File: src/vm/OpHandlers_Control.hpp
// Purpose: Declare control-flow opcode handlers and shared switch dispatch helpers.
// Key invariants: Handlers maintain VM block state, propagate parameters, and honor trap contracts.
// Ownership/Lifetime: Functions mutate the active VM frame without taking ownership of VM
// resources. Links: docs/il-guide.md#reference
#pragma once

#include "vm/OpHandlerAccess.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/ops/common/Branching.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace il::vm::detail::control
{
using ExecState = VMAccess::ExecState;

struct SwitchMeta
{
    const void *key = nullptr;
    std::vector<int32_t> values;
    std::vector<int32_t> succIdx;
    int32_t defaultIdx = -1;
};

namespace inline_impl
{
inline SwitchMeta collectSwitchMeta(const il::core::Instr &in)
{
    assert(in.op == il::core::Opcode::SwitchI32 && "expected switch.i32 instruction");

    SwitchMeta meta{};
    meta.key = static_cast<const void *>(&in);
    meta.defaultIdx = !in.labels.empty() ? 0 : -1;

    const size_t caseCount = switchCaseCount(in);
    meta.values.reserve(caseCount);
    meta.succIdx.reserve(caseCount);

    std::unordered_set<int32_t> seenValues;
    seenValues.reserve(caseCount);

    for (size_t idx = 0; idx < caseCount; ++idx)
    {
        const il::core::Value &value = switchCaseValue(in, idx);
        assert(value.kind == il::core::Value::Kind::ConstInt &&
               "switch case requires integer literal");
        const int32_t caseValue = static_cast<int32_t>(value.i64);
        const auto [_, inserted] = seenValues.insert(caseValue);
        if (!inserted)
            continue;
        meta.values.push_back(caseValue);
        meta.succIdx.push_back(static_cast<int32_t>(idx + 1));
    }

    assert(meta.values.size() == meta.succIdx.size());
    return meta;
}

inline int32_t lookupDense(const viper::vm::DenseJumpTable &table, int32_t sel, int32_t defIdx)
{
    const int64_t offset = static_cast<int64_t>(sel) - static_cast<int64_t>(table.base);
    if (offset < 0 || offset >= static_cast<int64_t>(table.targets.size()))
        return defIdx;
    const int32_t target = table.targets[static_cast<size_t>(offset)];
    return (target < 0) ? defIdx : target;
}

inline int32_t lookupSorted(const viper::vm::SortedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = std::lower_bound(cases.keys.begin(), cases.keys.end(), sel);
    if (it == cases.keys.end() || *it != sel)
        return defIdx;
    const size_t idx = static_cast<size_t>(it - cases.keys.begin());
    return cases.targetIdx[idx];
}

inline int32_t lookupHashed(const viper::vm::HashedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = cases.map.find(sel);
    return (it == cases.map.end()) ? defIdx : it->second;
}

inline viper::vm::SwitchCacheEntry::Kind chooseBackend(const SwitchMeta &meta)
{
    if (meta.values.empty())
        return viper::vm::SwitchCacheEntry::Sorted;

    const auto [minIt, maxIt] = std::minmax_element(meta.values.begin(), meta.values.end());
    const int64_t minv = *minIt;
    const int64_t maxv = *maxIt;
    const int64_t range = maxv - minv + 1;
    const double density = static_cast<double>(meta.values.size()) / static_cast<double>(range);

    if (range <= 4096 && density >= 0.60)
        return viper::vm::SwitchCacheEntry::Dense;
    if (meta.values.size() >= 64 && density < 0.15)
        return viper::vm::SwitchCacheEntry::Hashed;
    return viper::vm::SwitchCacheEntry::Sorted;
}

inline viper::vm::DenseJumpTable buildDense(const SwitchMeta &meta)
{
    viper::vm::DenseJumpTable table;
    if (meta.values.empty())
        return table;

    const int32_t minv = *std::min_element(meta.values.begin(), meta.values.end());
    const int32_t maxv = *std::max_element(meta.values.begin(), meta.values.end());
    table.base = minv;
    table.targets.assign(static_cast<size_t>(maxv - minv + 1), -1);
    for (size_t i = 0; i < meta.values.size(); ++i)
        table.targets[static_cast<size_t>(meta.values[i] - minv)] = meta.succIdx[i];
    return table;
}

inline viper::vm::HashedCases buildHashed(const SwitchMeta &meta)
{
    viper::vm::HashedCases hashed;
    hashed.map.reserve(meta.values.size() * 2);
    for (size_t i = 0; i < meta.values.size(); ++i)
        hashed.map.emplace(meta.values[i], meta.succIdx[i]);
    return hashed;
}

inline viper::vm::SortedCases buildSorted(const SwitchMeta &meta)
{
    std::vector<size_t> order(meta.values.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(),
              order.end(),
              [&](size_t a, size_t b) { return meta.values[a] < meta.values[b]; });
    viper::vm::SortedCases sorted;
    sorted.keys.reserve(order.size());
    sorted.targetIdx.reserve(order.size());
    for (size_t idx : order)
    {
        sorted.keys.push_back(meta.values[idx]);
        sorted.targetIdx.push_back(meta.succIdx[idx]);
    }
    return sorted;
}

inline viper::vm::SwitchCacheEntry &getOrBuildSwitchCache(viper::vm::SwitchCache &cache,
                                                          const il::core::Instr &in)
{
    SwitchMeta meta = collectSwitchMeta(in);
    auto it = cache.entries.find(meta.key);
    if (it != cache.entries.end())
        return it->second;

    viper::vm::SwitchCacheEntry entry{};
    entry.defaultIdx = meta.defaultIdx;
    const viper::vm::SwitchMode mode = viper::vm::getSwitchMode();
    if (mode != viper::vm::SwitchMode::Auto)
    {
        switch (mode)
        {
            case viper::vm::SwitchMode::Dense:
                entry.kind = viper::vm::SwitchCacheEntry::Dense;
                entry.backend = buildDense(meta);
                break;
            case viper::vm::SwitchMode::Sorted:
                entry.kind = viper::vm::SwitchCacheEntry::Sorted;
                entry.backend = buildSorted(meta);
                break;
            case viper::vm::SwitchMode::Hashed:
                entry.kind = viper::vm::SwitchCacheEntry::Hashed;
                entry.backend = buildHashed(meta);
                break;
            case viper::vm::SwitchMode::Linear:
                entry.kind = viper::vm::SwitchCacheEntry::Linear;
                entry.backend = std::monostate{};
                break;
            case viper::vm::SwitchMode::Auto:
                break;
        }
    }
    else
    {
        const auto kind = chooseBackend(meta);
        entry.kind = kind;
        switch (kind)
        {
            case viper::vm::SwitchCacheEntry::Dense:
                entry.backend = buildDense(meta);
                break;
            case viper::vm::SwitchCacheEntry::Sorted:
                entry.backend = buildSorted(meta);
                break;
            case viper::vm::SwitchCacheEntry::Hashed:
                entry.backend = buildHashed(meta);
                break;
            case viper::vm::SwitchCacheEntry::Linear:
                entry.backend = std::monostate{};
                break;
        }
    }

    auto [pos, _] = cache.entries.emplace(meta.key, std::move(entry));
    return pos->second;
}
} // namespace inline_impl

VM::ExecResult branchToTarget(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              size_t idx,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip);

inline VM::ExecResult handleBrImpl(VM &vm,
                                   ExecState *state,
                                   Frame &fr,
                                   const il::core::Instr &in,
                                   const VM::BlockMap &blocks,
                                   const il::core::BasicBlock *&bb,
                                   size_t &ip)
{
    (void)state;
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

inline VM::ExecResult handleCBrImpl(VM &vm,
                                    ExecState *state,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip)
{
    (void)state;
    Slot cond = VMAccess::eval(vm, fr, in.operands[0]);
    const size_t targetIdx = (cond.i64 != 0) ? 0 : 1;
    return branchToTarget(vm, fr, in, targetIdx, blocks, bb, ip);
}

inline VM::ExecResult handleSwitchI32Impl(VM &vm,
                                          ExecState *state,
                                          Frame &fr,
                                          const il::core::Instr &in,
                                          const VM::BlockMap &blocks,
                                          const il::core::BasicBlock *&bb,
                                          size_t &ip)
{
    const auto scrutineeScalar = il::vm::ops::common::eval_scrutinee(fr, in);
    const int32_t sel = scrutineeScalar.value;

    viper::vm::SwitchCache *cache = nullptr;
    if (state != nullptr)
    {
        cache = &state->switchCache;
    }
    else
    {
        static thread_local viper::vm::SwitchCache fallbackCache;
        cache = &fallbackCache;
    }

    auto &entry = inline_impl::getOrBuildSwitchCache(*cache, in);

    int32_t idx = entry.defaultIdx;

    const bool forceLinear = (entry.kind == viper::vm::SwitchCacheEntry::Linear);

#if defined(VIPER_VM_DEBUG_SWITCH_LINEAR)
    (void)forceLinear;
    const size_t caseCount = switchCaseCount(in);
    for (size_t caseIdx = 0; caseIdx < caseCount; ++caseIdx)
    {
        const il::core::Value &caseValue = switchCaseValue(in, caseIdx);
        const int32_t caseSel = static_cast<int32_t>(caseValue.i64);
        if (caseSel == sel)
        {
            idx = static_cast<int32_t>(caseIdx + 1);
            break;
        }
    }
#else
    if (forceLinear)
    {
        const size_t caseCount = switchCaseCount(in);
        for (size_t caseIdx = 0; caseIdx < caseCount; ++caseIdx)
        {
            const il::core::Value &caseValue = switchCaseValue(in, caseIdx);
            const int32_t caseSel = static_cast<int32_t>(caseValue.i64);
            if (caseSel == sel)
            {
                idx = static_cast<int32_t>(caseIdx + 1);
                break;
            }
        }
    }
    else
    {
        std::visit(
            [&](auto &backend)
            {
                using BackendT = std::decay_t<decltype(backend)>;
                if constexpr (std::is_same_v<BackendT, viper::vm::DenseJumpTable>)
                    idx = inline_impl::lookupDense(backend, sel, entry.defaultIdx);
                else if constexpr (std::is_same_v<BackendT, viper::vm::SortedCases>)
                    idx = inline_impl::lookupSorted(backend, sel, entry.defaultIdx);
                else if constexpr (std::is_same_v<BackendT, viper::vm::HashedCases>)
                    idx = inline_impl::lookupHashed(backend, sel, entry.defaultIdx);
            },
            entry.backend);
    }
#endif

    std::vector<il::vm::ops::common::Case> cases;
    cases.reserve(in.labels.size());
    auto makeTarget = [&](size_t labelIndex) {
        il::vm::ops::common::Target target{};
        target.vm = &vm;
        target.instr = &in;
        target.labelIndex = labelIndex;
        target.blocks = &blocks;
        target.currentBlock = &bb;
        target.ip = &ip;
        return target;
    };

    for (size_t labelIndex = 0; labelIndex < in.labels.size(); ++labelIndex)
    {
        cases.push_back(il::vm::ops::common::Case::exact(
            il::vm::ops::common::Scalar{static_cast<int32_t>(labelIndex)}, makeTarget(labelIndex)));
    }

    il::vm::ops::common::Target invalid{};
    auto selected = il::vm::ops::common::select_case(il::vm::ops::common::Scalar{idx}, cases, invalid);

    if (!selected.valid())
    {
        VM::ExecResult result{};
        result.returned = true;
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            "switch target out of range",
                            in.loc,
                            fr.func ? fr.func->name : std::string(),
                            bb ? bb->label : std::string());
        return result;
    }

    il::vm::ops::common::jump(fr, selected);

    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

VM::ExecResult handleSwitchI32(VM &vm,
                               Frame &fr,
                               const il::core::Instr &in,
                               const VM::BlockMap &blocks,
                               const il::core::BasicBlock *&bb,
                               size_t &ip);

VM::ExecResult handleBr(VM &vm,
                        Frame &fr,
                        const il::core::Instr &in,
                        const VM::BlockMap &blocks,
                        const il::core::BasicBlock *&bb,
                        size_t &ip);

VM::ExecResult handleCBr(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip);

VM::ExecResult handleRet(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip);

VM::ExecResult handleCall(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

VM::ExecResult handleErrGet(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleEhEntry(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
                             size_t &ip);

VM::ExecResult handleEhPush(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleEhPop(VM &vm,
                           Frame &fr,
                           const il::core::Instr &in,
                           const VM::BlockMap &blocks,
                           const il::core::BasicBlock *&bb,
                           size_t &ip);

VM::ExecResult handleResumeSame(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip);

VM::ExecResult handleResumeNext(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip);

VM::ExecResult handleResumeLabel(VM &vm,
                                 Frame &fr,
                                 const il::core::Instr &in,
                                 const VM::BlockMap &blocks,
                                 const il::core::BasicBlock *&bb,
                                 size_t &ip);

VM::ExecResult handleTrapKind(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip);

VM::ExecResult handleTrapErr(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
                             size_t &ip);

VM::ExecResult handleTrap(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

} // namespace il::vm::detail::control
