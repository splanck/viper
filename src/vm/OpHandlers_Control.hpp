//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/OpHandlers_Control.hpp
// Purpose: Declare control-flow opcode handlers and shared switch dispatch helpers.
// Key invariants: Handlers maintain VM block state, propagate parameters, and honor trap contracts.
// Ownership/Lifetime: Functions mutate the active VM frame without taking ownership of VM
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Control-flow opcode handlers and switch dispatch helpers for the VM.
/// @details Declares handlers for branching, calls, returns, traps, and
///          exception-handling opcodes. Inline helpers build efficient switch
///          dispatch tables and cache them in the execution state.

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
#include <cstdlib>
#include <numeric>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace il::vm::detail::control
{
/// @brief Execution state alias used by control handlers.
using ExecState = VMAccess::ExecState;

/// @brief Metadata extracted from a switch instruction.
/// @details Captures distinct case values, their successor indices, and the
///          default target index so the VM can build efficient dispatch tables.
struct SwitchMeta
{
    const void *key = nullptr;
    std::vector<int32_t> values;
    std::vector<int32_t> succIdx;
    int32_t defaultIdx = -1;
};

namespace inline_impl
{
/// @brief Extract switch case metadata from an instruction.
/// @details Builds a list of distinct case values and their successor indices.
///          Duplicate case values are ignored to preserve deterministic behavior.
/// @param in switch.i32 instruction to analyze.
/// @return Populated SwitchMeta describing cases and default index.
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

/// @brief Lookup a switch target in a dense jump table.
/// @details Converts the selector into an offset from the base value and returns
///          the target index when in range; otherwise returns @p defIdx.
/// @param table Dense jump table.
/// @param sel Switch selector value.
/// @param defIdx Default successor index.
/// @return Target successor index, or @p defIdx if out of range.
inline int32_t lookupDense(const viper::vm::DenseJumpTable &table, int32_t sel, int32_t defIdx)
{
    const int64_t offset = static_cast<int64_t>(sel) - static_cast<int64_t>(table.base);
    if (offset < 0 || offset >= static_cast<int64_t>(table.targets.size()))
        return defIdx;
    const int32_t target = table.targets[static_cast<size_t>(offset)];
    return (target < 0) ? defIdx : target;
}

/// @brief Lookup a switch target in a sorted case table.
/// @details Performs binary search over the sorted case values and returns the
///          corresponding target index, or @p defIdx when not found.
/// @param cases Sorted case table.
/// @param sel Switch selector value.
/// @param defIdx Default successor index.
/// @return Target successor index, or @p defIdx if no match.
inline int32_t lookupSorted(const viper::vm::SortedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = std::lower_bound(cases.keys.begin(), cases.keys.end(), sel);
    if (it == cases.keys.end() || *it != sel)
        return defIdx;
    const size_t idx = static_cast<size_t>(it - cases.keys.begin());
    return cases.targetIdx[idx];
}

/// @brief Lookup a switch target in a hashed case table.
/// @details Uses an unordered map to retrieve the target index in expected
///          constant time. Returns @p defIdx when no entry is present.
/// @param cases Hashed case table.
/// @param sel Switch selector value.
/// @param defIdx Default successor index.
/// @return Target successor index, or @p defIdx if no match.
inline int32_t lookupHashed(const viper::vm::HashedCases &cases, int32_t sel, int32_t defIdx)
{
    auto it = cases.map.find(sel);
    return (it == cases.map.end()) ? defIdx : it->second;
}

/// @brief Choose a switch backend strategy based on case density.
/// @details Uses tunables (possibly overridden by environment variables) to
///          decide between dense tables, sorted tables, hashed tables, or a
///          linear fallback. The heuristic favors dense tables for compact
///          ranges and hashes for sparse large ranges.
/// @param meta Switch metadata containing case values.
/// @return Selected backend kind.
inline viper::vm::SwitchCacheEntry::Kind chooseBackend(const SwitchMeta &meta)
{
    if (meta.values.empty())
        return viper::vm::SwitchCacheEntry::Sorted;

    struct Tunables
    {
        int64_t denseMaxRange = 4096;
        double denseMinDensity = 0.60;
        std::size_t hashMinCases = 64;
        double hashMaxDensity = 0.15;
    };

    static const Tunables t = []
    {
        Tunables tv{};
        if (const char *s = std::getenv("VIPER_SWITCH_DENSE_MAX_RANGE"))
        {
            char *end = nullptr;
            long long v = std::strtoll(s, &end, 10);
            if (end && *end == '\0' && v > 0)
                tv.denseMaxRange = static_cast<int64_t>(v);
        }
        if (const char *s = std::getenv("VIPER_SWITCH_DENSE_MIN_DENSITY"))
        {
            char *end = nullptr;
            double v = std::strtod(s, &end);
            if (end && *end == '\0' && v > 0.0 && v <= 1.0)
                tv.denseMinDensity = v;
        }
        if (const char *s = std::getenv("VIPER_SWITCH_HASH_MIN_CASES"))
        {
            char *end = nullptr;
            long v = std::strtol(s, &end, 10);
            if (end && *end == '\0' && v >= 0)
                tv.hashMinCases = static_cast<std::size_t>(v);
        }
        if (const char *s = std::getenv("VIPER_SWITCH_HASH_MAX_DENSITY"))
        {
            char *end = nullptr;
            double v = std::strtod(s, &end);
            if (end && *end == '\0' && v > 0.0 && v <= 1.0)
                tv.hashMaxDensity = v;
        }
        return tv;
    }();

    const auto [minIt, maxIt] = std::minmax_element(meta.values.begin(), meta.values.end());
    const int64_t minv = *minIt;
    const int64_t maxv = *maxIt;
    const int64_t range = maxv - minv + 1;
    const double density = static_cast<double>(meta.values.size()) / static_cast<double>(range);

    if (range <= t.denseMaxRange && density >= t.denseMinDensity)
        return viper::vm::SwitchCacheEntry::Dense;
    if (meta.values.size() >= t.hashMinCases && density < t.hashMaxDensity)
        return viper::vm::SwitchCacheEntry::Hashed;
    return viper::vm::SwitchCacheEntry::Sorted;
}

/// @brief Build a dense jump table from switch metadata.
/// @details Allocates a contiguous target array covering [min, max] and fills
///          entries with successor indices or -1 for missing values.
/// @param meta Switch metadata containing case values and successor indices.
/// @return Dense jump table representation.
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

/// @brief Build a hashed case table from switch metadata.
/// @details Inserts each distinct case value into an unordered map for fast
///          lookup when the selector range is sparse.
/// @param meta Switch metadata containing case values and successor indices.
/// @return Hashed case representation.
inline viper::vm::HashedCases buildHashed(const SwitchMeta &meta)
{
    viper::vm::HashedCases hashed;
    hashed.map.reserve(meta.values.size() * 2);
    for (size_t i = 0; i < meta.values.size(); ++i)
        hashed.map.emplace(meta.values[i], meta.succIdx[i]);
    return hashed;
}

/// @brief Build a sorted case table from switch metadata.
/// @details Orders case values and aligns them with successor indices so binary
///          search can be used during dispatch.
/// @param meta Switch metadata containing case values and successor indices.
/// @return Sorted case representation.
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

/// @brief Retrieve or construct a cached switch dispatch entry.
/// @details Looks up cached metadata by instruction address. If absent, it
///          builds the preferred backend according to the global switch mode
///          and inserts the entry into the cache.
/// @param cache Switch cache stored in the execution state.
/// @param in switch.i32 instruction to cache.
/// @return Reference to the cached entry for @p in.
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

/// @brief Branch to a successor label by index.
/// @details Resolves the target block, marshals branch arguments into the new
///          frame state, and updates the current block and instruction pointer.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Branch instruction being executed.
/// @param idx Index into @p in.labels identifying the target.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
VM::ExecResult branchToTarget(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              size_t idx,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip);

/// @brief Inline unconditional branch handler for fast dispatch.
/// @details Delegates to @ref branchToTarget with the first label as the target.
/// @param vm Active VM instance.
/// @param state Optional execution state (unused).
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
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

/// @brief Inline conditional branch handler for fast dispatch.
/// @details Evaluates the condition operand and branches to label 0 when true
///          or label 1 when false.
/// @param vm Active VM instance.
/// @param state Optional execution state (unused).
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
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

/// @brief Inline switch.i32 handler for fast dispatch.
/// @details Evaluates the scrutinee and chooses a successor using a cached
///          backend (dense, sorted, hashed, or linear). Traps if the selected
///          target index is out of range.
/// @param vm Active VM instance.
/// @param state Optional execution state containing a switch cache.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
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

    // Fast path: idx directly encodes the label index to jump to.
    if (idx < 0 || static_cast<size_t>(idx) >= in.labels.size())
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

    auto makeTarget = [&](size_t labelIndex)
    {
        il::vm::ops::common::Target target{};
        target.vm = &vm;
        target.instr = &in;
        target.labelIndex = labelIndex;
        target.blocks = &blocks;
        target.currentBlock = &bb;
        target.ip = &ip;
        return target;
    };

    il::vm::ops::common::Target selected = makeTarget(static_cast<size_t>(idx));
    il::vm::ops::common::jump(fr, selected);

    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Execute a switch.i32 instruction.
/// @details Resolves the target block based on the scrutinee and dispatch
///          policy, then updates the VM control-flow state.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
VM::ExecResult handleSwitchI32(VM &vm,
                               Frame &fr,
                               const il::core::Instr &in,
                               const VM::BlockMap &blocks,
                               const il::core::BasicBlock *&bb,
                               size_t &ip);

/// @brief Execute an unconditional branch (br).
/// @details Transfers control to the target block and forwards branch
///          arguments to the destination block parameters.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
VM::ExecResult handleBr(VM &vm,
                        Frame &fr,
                        const il::core::Instr &in,
                        const VM::BlockMap &blocks,
                        const il::core::BasicBlock *&bb,
                        size_t &ip);

/// @brief Execute a conditional branch (cbr).
/// @details Evaluates the condition and branches to the true or false target,
///          forwarding the corresponding argument list.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
VM::ExecResult handleCBr(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip);

/// @brief Execute a return instruction (ret).
/// @details Populates the VM return value (if any) and marks the frame as
///          completed so the caller can resume.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating return or trap status.
VM::ExecResult handleRet(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip);

/// @brief Execute a direct call instruction (call).
/// @details Resolves the callee, marshals arguments, and transfers control to
///          the target function, handling runtime externs as needed.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating call, return, or trap status.
VM::ExecResult handleCall(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

/// @brief Execute an indirect call instruction (call.indirect).
/// @details Resolves the callee pointer at runtime, marshals arguments, and
///          transfers control to the target function.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating call, return, or trap status.
VM::ExecResult handleCallIndirect(VM &vm,
                                  Frame &fr,
                                  const il::core::Instr &in,
                                  const VM::BlockMap &blocks,
                                  const il::core::BasicBlock *&bb,
                                  size_t &ip);

/// @brief Retrieve the current error object (err.get).
/// @details Reads the VM's active error slot and writes it to the destination.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue or trap status.
VM::ExecResult handleErrGet(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Enter an exception handler region (eh.entry).
/// @details Pushes a handler record so subsequent errors can be caught by the
///          current function's handler blocks.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue or trap status.
VM::ExecResult handleEhEntry(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
                             size_t &ip);

/// @brief Push a new exception handler (eh.push).
/// @details Adds a handler to the EH stack for nested try regions.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue or trap status.
VM::ExecResult handleEhPush(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Pop the most recent exception handler (eh.pop).
/// @details Removes the top handler from the EH stack.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue or trap status.
VM::ExecResult handleEhPop(VM &vm,
                           Frame &fr,
                           const il::core::Instr &in,
                           const VM::BlockMap &blocks,
                           const il::core::BasicBlock *&bb,
                           size_t &ip);

/// @brief Resume exception handling within the current handler (resume.same).
/// @details Transfers control to the current handler continuation without
///          unwinding to an outer handler.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
VM::ExecResult handleResumeSame(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip);

/// @brief Resume exception handling at the next enclosing handler (resume.next).
/// @details Pops the current handler and transfers control to the next one.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
VM::ExecResult handleResumeNext(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip);

/// @brief Resume exception handling at a specific handler label (resume.label).
/// @details Transfers control to the handler identified by the instruction's
///          label operand, preserving the active error payload.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating jump or trap status.
VM::ExecResult handleResumeLabel(VM &vm,
                                 Frame &fr,
                                 const il::core::Instr &in,
                                 const VM::BlockMap &blocks,
                                 const il::core::BasicBlock *&bb,
                                 size_t &ip);

/// @brief Trap with a specific trap kind (trap.kind).
/// @details Emits a runtime trap using the provided kind and optional message.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating trap status.
VM::ExecResult handleTrapKind(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip);

/// @brief Trap using the current error payload (trap.err).
/// @details Emits a trap based on the VM's active error slot.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating trap status.
VM::ExecResult handleTrapErr(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
                             size_t &ip);

/// @brief Trap with a default or constant message (trap).
/// @details Emits a runtime trap with the instruction's message operand or a
///          default string when none is provided.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating trap status.
VM::ExecResult handleTrap(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

} // namespace il::vm::detail::control
