//===----------------------------------------------------------------------===//
//
// This file is part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/vm/ops/Op_BranchSwitch.cpp
// Purpose: Provide control-flow opcode handlers including conditional branches
//          and switch dispatch.
// Key invariants: Switch cache entries are keyed on instruction identity and the
//                 handler always validates branch argument counts before jumping.
// Ownership/Lifetime: Handlers borrow VM state and never assume ownership of
//                     frames, blocks, or cached data structures.
// Links: docs/runtime-vm.md#dispatch
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

SwitchMode g_switchMode = SwitchMode::Auto; ///< Global override for switch backend selection.

/// @brief Determine whether verbose VM debug logging has been requested.
///
/// @details Reads the `VIPER_DEBUG_VM` environment variable once and caches the
///          boolean result in a static local so subsequent calls are cheap.  The
///          helper is used by switch caching to emit backend selection traces.
///
/// @return @c true when `VIPER_DEBUG_VM` is non-empty.
bool isVmDebugLoggingEnabled()
{
    static const bool enabled = [] {
        if (const char *flag = std::getenv("VIPER_DEBUG_VM"))
            return flag[0] != '\0';
        return false;
    }();
    return enabled;
}

/// @brief Convert a switch cache backend enumerator into a human-readable label.
///
/// @details Used exclusively for logging decisions about backend selection so
///          developers can confirm whether auto-selection matched expectations.
///
/// @param kind Cache backend that will service switch lookups.
/// @return Printable backend name.
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
/// @brief Retrieve the current switch backend selection policy.
///
/// @details The policy defaults to automatic selection but can be overridden by
///          tests to force a particular backend.  The value controls how
///          @ref getOrBuildSwitchCache constructs cache entries.
///
/// @return The active switch mode.
SwitchMode getSwitchMode()
{
    return g_switchMode;
}

/// @brief Override the switch backend selection policy used by handlers.
///
/// @param mode New selection policy.
void setSwitchMode(SwitchMode mode)
{
    g_switchMode = mode;
}
} // namespace viper::vm

namespace il::vm::detail::control
{

namespace
{
/// @brief Metadata extracted from a switch instruction for cache construction.
struct SwitchMeta
{
    const void *key = nullptr;
    std::vector<int32_t> values;
    std::vector<int32_t> succIdx;
    int32_t defaultIdx = -1;
};

/// @brief Gather switch case data into a structure suitable for caching.
///
/// @details Records each unique integer case value together with the successor
///          index used by the interpreter.  Duplicate case values are ignored to
///          preserve the first occurrence, mirroring the front-end lowering
///          rules.  The instruction pointer itself serves as the cache key so
///          the VM reuses cached backends across executions of the same
///          instruction.
///
/// @param in Switch instruction under inspection.
/// @return Metadata describing case values and their corresponding successors.
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

/// @brief Resolve a switch case using the dense jump table backend.
///
/// @details Computes the offset from the table base to the selected value and
///          verifies it lies within the target vector.  Invalid lookups fall
///          back to the default successor index to match IL semantics.
///
/// @param table Dense jump table describing contiguous case values.
/// @param sel Integer selector evaluated from the switch scrutinee.
/// @param defIdx Default successor index for unmatched selectors.
/// @return Successor index determined by the table or @p defIdx.
int32_t lookupDense(const DenseJumpTable &table, int32_t sel, int32_t defIdx)
{
    const int64_t offset = static_cast<int64_t>(sel) - static_cast<int64_t>(table.base);
    if (offset < 0 || offset >= static_cast<int64_t>(table.targets.size()))
        return defIdx;
    const int32_t target = table.targets[static_cast<size_t>(offset)];
    return (target < 0) ? defIdx : target;
}

/// @brief Resolve a switch case using binary search over sorted case keys.
///
/// @details Performs a lower_bound search on the sorted key vector and returns
///          the associated successor index when found.  Otherwise the default
///          index is returned.
///
/// @param cases Sorted switch case metadata.
/// @param sel Evaluated switch selector.
/// @param defIdx Default successor index to use on miss.
/// @return Matching successor index or @p defIdx when not found.
int32_t lookupSorted(const SortedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = std::lower_bound(cases.keys.begin(), cases.keys.end(), sel);
    if (it == cases.keys.end() || *it != sel)
        return defIdx;
    const size_t idx = static_cast<size_t>(it - cases.keys.begin());
    return cases.targetIdx[idx];
}

/// @brief Resolve a switch case via hash table lookup.
///
/// @details The hashed backend stores case values directly in an unordered map.
///          A missing entry results in the default successor index, ensuring the
///          handler mirrors linear search behaviour.
///
/// @param cases Hash map describing sparse case values.
/// @param sel Evaluated selector.
/// @param defIdx Default successor index.
/// @return Successor index or the default when not found.
int32_t lookupHashed(const HashedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = cases.map.find(sel);
    return (it == cases.map.end()) ? defIdx : it->second;
}

/// @brief Select the most appropriate backend for the provided switch metadata.
///
/// @details Heuristics favour dense tables for compact value ranges, hash maps
///          for large sparse distributions, and sorted vectors otherwise.  Empty
///          switches default to the sorted representation to minimise storage.
///
/// @param meta Switch metadata computed by @ref collectSwitchMeta.
/// @return Backend kind to instantiate.
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

/// @brief Construct a dense jump table backend from switch metadata.
///
/// @details Allocates a contiguous vector sized to the value range and populates
///          it with successor indices, leaving gaps initialised to -1 so they
///          resolve to the default case at runtime.
///
/// @param meta Metadata describing switch case values.
/// @return Materialised dense jump table.
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

/// @brief Build the hashed switch backend storing key → successor mappings.
///
/// @param meta Switch metadata describing case values.
/// @return Hashed representation suitable for sparse distributions.
HashedCases buildHashed(const SwitchMeta &meta)
{
    HashedCases hashed;
    hashed.map.reserve(meta.values.size() * 2);
    for (size_t i = 0; i < meta.values.size(); ++i)
        hashed.map.emplace(meta.values[i], meta.succIdx[i]);
    return hashed;
}

/// @brief Build the sorted vector backend for switch dispatch.
///
/// @details Generates an index permutation that orders case values ascending,
///          then emits parallel vectors of keys and successor indices that
///          enable binary search lookups via @ref lookupSorted.
///
/// @param meta Switch metadata describing case values.
/// @return Sorted backend representation.
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

/// @brief Retrieve or construct the cached backend for a specific switch instruction.
///
/// @details Cache entries are keyed by the instruction pointer so repeated
///          execution reuses previously materialised backends.  When the entry
///          is absent the helper chooses or forces a backend according to the
///          global switch mode, logs the decision when debugging is enabled, and
///          stores the entry in the cache.
///
/// @param cache Switch cache associated with the current execution context.
/// @param in Switch instruction being executed.
/// @return Reference to the cached entry containing backend data.
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

/// @brief Transfer control to a branch target while propagating block parameters.
///
/// @details Validates the branch argument count against the destination block's
///          parameter list, evaluating arguments eagerly to honour IL semantics.
///          Successful jumps update the current basic block pointer and reset
///          the instruction pointer to zero so execution restarts at the first
///          instruction of the target block.  Mismatches trigger a runtime trap
///          through the bridge with a descriptive error message.
///
/// @param vm Virtual machine coordinating execution.
/// @param fr Active frame whose parameter vector receives propagated values.
/// @param in Branch instruction providing labels and arguments.
/// @param idx Index into @p in.labels selecting the target block.
/// @param blocks Mapping of block labels to concrete block pointers.
/// @param bb Reference to the current block pointer that will be updated.
/// @param ip Instruction pointer reference reset on jump.
/// @return Execution result with @ref VM::ExecResult::jumped set when control transfers.
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

/// @brief Execute an integer switch instruction and branch to the selected successor.
///
/// @details Evaluates the scrutinee, consults the per-instruction switch cache
///          to select a backend, and resolves the matching case.  When no case
///          matches the default successor index is used.  Out-of-range indices
///          trigger a runtime trap to guard against malformed IL.  Once the
///          destination index is known the helper delegates to @ref branchToTarget.
///
/// @param vm Running virtual machine.
/// @param fr Active frame.
/// @param in Switch instruction to execute.
/// @param blocks Mapping of block labels to block pointers.
/// @param bb Current block pointer reference.
/// @param ip Instruction pointer reference.
/// @return Execution result reporting whether control jumped.
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

/// @brief Execute an unconditional branch to the first successor label.
///
/// @details Simply forwards to @ref branchToTarget with successor index zero.
///          This keeps the common validation and parameter propagation logic in
///          one place.
VM::ExecResult handleBr(VM &vm,
                        Frame &fr,
                        const il::core::Instr &in,
                        const VM::BlockMap &blocks,
                        const il::core::BasicBlock *&bb,
                        size_t &ip)
{
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

/// @brief Execute a conditional branch using the first operand as the predicate.
///
/// @details Evaluates the operand and picks the first successor label when the
///          predicate is non-zero or the second label otherwise.  Control is
///          transferred through @ref branchToTarget so parameter handling remains
///          consistent with other branch forms.
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

