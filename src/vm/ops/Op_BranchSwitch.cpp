//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Contains control-flow branching and switch opcode handlers.  The helpers keep
// branch parameter propagation and switch backend selection isolated so that the
// dispatcher in OpHandlers_Control.hpp remains concise.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Control.hpp"

#include "vm/RuntimeBridge.hpp"
#include "vm/control_flow.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace
{
using viper::vm::DenseJumpTable;
using viper::vm::HashedCases;
using viper::vm::SortedCases;
using viper::vm::SwitchCache;
using viper::vm::SwitchCacheEntry;
using viper::vm::SwitchMode;

SwitchMode g_switchMode = SwitchMode::Auto;

bool isVmDebugLoggingEnabled()
{
    static const bool enabled = [] {
        if (const char *flag = std::getenv("VIPER_DEBUG_VM"))
            return flag[0] != '\0';
        return false;
    }();
    return enabled;
}

const char *switchCacheKindName(SwitchCacheEntry::Kind kind)
{
    switch (kind)
    {
        case SwitchCacheEntry::Dense:
            return "Dense";
        case SwitchCacheEntry::Sorted:
            return "Sorted";
        case SwitchCacheEntry::Hashed:
            return "Hashed";
        case SwitchCacheEntry::Linear:
            return "Linear";
    }
    return "Unknown";
}
} // namespace

namespace viper::vm
{
SwitchMode getSwitchMode()
{
    return g_switchMode;
}

void setSwitchMode(SwitchMode mode)
{
    g_switchMode = mode;
}
} // namespace viper::vm

namespace il::vm::detail::control
{

namespace
{
struct SwitchMeta
{
    const void *key = nullptr;
    std::vector<int32_t> values;
    std::vector<int32_t> succIdx;
    int32_t defaultIdx = -1;
};

SwitchMeta collectSwitchMeta(const il::core::Instr &in)
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
        assert(value.kind == il::core::Value::Kind::ConstInt && "switch case requires integer literal");
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

int32_t lookupDense(const DenseJumpTable &table, int32_t sel, int32_t defIdx)
{
    const int64_t offset = static_cast<int64_t>(sel) - static_cast<int64_t>(table.base);
    if (offset < 0 || offset >= static_cast<int64_t>(table.targets.size()))
        return defIdx;
    const int32_t target = table.targets[static_cast<size_t>(offset)];
    return (target < 0) ? defIdx : target;
}

int32_t lookupSorted(const SortedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = std::lower_bound(cases.keys.begin(), cases.keys.end(), sel);
    if (it == cases.keys.end() || *it != sel)
        return defIdx;
    const size_t idx = static_cast<size_t>(it - cases.keys.begin());
    return cases.targetIdx[idx];
}

int32_t lookupHashed(const HashedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = cases.map.find(sel);
    return (it == cases.map.end()) ? defIdx : it->second;
}

SwitchCacheEntry::Kind chooseBackend(const SwitchMeta &meta)
{
    if (meta.values.empty())
        return SwitchCacheEntry::Sorted;

    const auto [minIt, maxIt] = std::minmax_element(meta.values.begin(), meta.values.end());
    const int64_t minv = *minIt;
    const int64_t maxv = *maxIt;
    const int64_t range = maxv - minv + 1;
    const double density = static_cast<double>(meta.values.size()) / static_cast<double>(range);

    if (range <= 4096 && density >= 0.60)
        return SwitchCacheEntry::Dense;
    if (meta.values.size() >= 64 && density < 0.15)
        return SwitchCacheEntry::Hashed;
    return SwitchCacheEntry::Sorted;
}

DenseJumpTable buildDense(const SwitchMeta &meta)
{
    DenseJumpTable table;
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

HashedCases buildHashed(const SwitchMeta &meta)
{
    HashedCases hashed;
    hashed.map.reserve(meta.values.size() * 2);
    for (size_t i = 0; i < meta.values.size(); ++i)
        hashed.map.emplace(meta.values[i], meta.succIdx[i]);
    return hashed;
}

SortedCases buildSorted(const SwitchMeta &meta)
{
    std::vector<size_t> order(meta.values.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return meta.values[a] < meta.values[b];
    });
    SortedCases sorted;
    sorted.keys.reserve(order.size());
    sorted.targetIdx.reserve(order.size());
    for (size_t idx : order)
    {
        sorted.keys.push_back(meta.values[idx]);
        sorted.targetIdx.push_back(meta.succIdx[idx]);
    }
    return sorted;
}

SwitchCacheEntry &getOrBuildSwitchCache(SwitchCache &cache, const il::core::Instr &in)
{
    SwitchMeta meta = collectSwitchMeta(in);
    auto it = cache.entries.find(meta.key);
    if (it != cache.entries.end())
        return it->second;

    SwitchCacheEntry entry{};
    entry.defaultIdx = meta.defaultIdx;
    const SwitchMode mode = viper::vm::getSwitchMode();
    const char *selectedKindName = nullptr;
    if (mode != SwitchMode::Auto)
    {
        switch (mode)
        {
            case SwitchMode::Dense:
                entry.kind = SwitchCacheEntry::Dense;
                entry.backend = buildDense(meta);
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Sorted:
                entry.kind = SwitchCacheEntry::Sorted;
                entry.backend = buildSorted(meta);
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Hashed:
                entry.kind = SwitchCacheEntry::Hashed;
                entry.backend = buildHashed(meta);
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Linear:
                entry.kind = SwitchCacheEntry::Linear;
                entry.backend = std::monostate{};
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Auto:
                break;
        }
    }
    else
    {
        SwitchCacheEntry::Kind kind = chooseBackend(meta);
        entry.kind = kind;
        selectedKindName = switchCacheKindName(kind);
        switch (kind)
        {
            case SwitchCacheEntry::Dense:
                entry.backend = buildDense(meta);
                break;
            case SwitchCacheEntry::Sorted:
                entry.backend = buildSorted(meta);
                break;
            case SwitchCacheEntry::Hashed:
                entry.backend = buildHashed(meta);
                break;
            case SwitchCacheEntry::Linear:
                entry.backend = std::monostate{};
                break;
        }
    }

    if (isVmDebugLoggingEnabled() && selectedKindName)
    {
        std::fprintf(stderr,
                     "[DEBUG][VM] switch backend: %s (cases=%zu)\n",
                     selectedKindName,
                     meta.values.size());
    }

    auto [pos, _] = cache.entries.emplace(meta.key, std::move(entry));
    return pos->second;
}
} // namespace

VM::ExecResult branchToTarget(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              size_t idx,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip)
{
    const auto &label = in.labels[idx];
    auto it = blocks.find(label);
    assert(it != blocks.end() && "invalid branch target");
    const il::core::BasicBlock *target = it->second;
    const il::core::BasicBlock *sourceBlock = bb;
    const std::string sourceLabel = sourceBlock ? sourceBlock->label : std::string{};
    const std::string functionName = fr.func ? fr.func->name : std::string{};

    const size_t expected = target->params.size();
    const size_t provided = idx < in.brArgs.size() ? in.brArgs[idx].size() : 0;
    if (provided != expected)
    {
        std::ostringstream os;
        os << "branch argument count mismatch targeting '" << target->label << '\'';
        if (!sourceLabel.empty())
            os << " from '" << sourceLabel << '\'';
        os << ": expected " << expected << ", got " << provided;
        RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), in.loc, functionName, sourceLabel);
        std::exit(1);
        return {};
    }

    if (provided > 0)
    {
        const auto &args = in.brArgs[idx];
        for (size_t i = 0; i < provided; ++i)
        {
            const auto id = target->params[i].id;
            assert(id < fr.params.size());
            fr.params[id] = VMAccess::eval(vm, fr, args[i]);
        }
    }

    bb = target;
    ip = 0;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

VM::ExecResult handleSwitchI32(VM &vm,
                               Frame &fr,
                               const il::core::Instr &in,
                               const VM::BlockMap &blocks,
                               const il::core::BasicBlock *&bb,
                               size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    const Slot scrutineeSlot = VMAccess::eval(vm, fr, switchScrutinee(in));
    const int32_t sel = static_cast<int32_t>(scrutineeSlot.i64);

    SwitchCache *cache = nullptr;
    if (auto *state = VMAccess::currentExecState(vm))
    {
        cache = &state->switchCache;
    }
    else
    {
        static thread_local SwitchCache fallbackCache;
        cache = &fallbackCache;
    }

    auto &entry = getOrBuildSwitchCache(*cache, in);

    int32_t idx = entry.defaultIdx;

    const bool forceLinear = (entry.kind == SwitchCacheEntry::Linear);

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
            [&](auto &backend) {
                using BackendT = std::decay_t<decltype(backend)>;
                if constexpr (std::is_same_v<BackendT, DenseJumpTable>)
                    idx = lookupDense(backend, sel, entry.defaultIdx);
                else if constexpr (std::is_same_v<BackendT, SortedCases>)
                    idx = lookupSorted(backend, sel, entry.defaultIdx);
                else if constexpr (std::is_same_v<BackendT, HashedCases>)
                    idx = lookupHashed(backend, sel, entry.defaultIdx);
            },
            entry.backend);
    }
#endif

    if (idx < 0 || static_cast<size_t>(idx) >= in.labels.size())
    {
        VM::ExecResult result{};
        result.returned = true;
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            "switch target out of range",
                            in.loc,
                            fr.func ? fr.func->name : std::string(),
                            fr.func && !fr.func->blocks.empty() ? fr.func->blocks.front().label : "");
        return result;
    }

    return branchToTarget(vm, fr, in, static_cast<size_t>(idx), blocks, bb, ip);
}

VM::ExecResult handleBr(VM &vm,
                        Frame &fr,
                        const il::core::Instr &in,
                        const VM::BlockMap &blocks,
                        const il::core::BasicBlock *&bb,
                        size_t &ip)
{
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

VM::ExecResult handleCBr(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip)
{
    Slot cond = VMAccess::eval(vm, fr, in.operands[0]);
    const size_t targetIdx = (cond.i64 != 0) ? 0 : 1;
    return branchToTarget(vm, fr, in, targetIdx, blocks, bb, ip);
}

} // namespace il::vm::detail::control

