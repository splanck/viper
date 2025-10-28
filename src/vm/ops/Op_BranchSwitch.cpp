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
#include "vm/ops/common/Branching.hpp"

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
using il::vm::detail::control::inline_impl::getOrBuildSwitchCache;
using il::vm::detail::control::inline_impl::lookupDense;
using il::vm::detail::control::inline_impl::lookupHashed;
using il::vm::detail::control::inline_impl::lookupSorted;

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
    static const bool enabled = []
    {
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
    assert(idx < in.labels.size() && "branch target index out of range");

    il::vm::ops::common::Target target{};
    target.vm = &vm;
    target.instr = &in;
    target.labelIndex = idx;
    target.blocks = &blocks;
    target.currentBlock = &bb;
    target.ip = &ip;

    il::vm::ops::common::jump(fr, target);

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
    const auto scrutineeScalar = il::vm::ops::common::eval_scrutinee(fr, in);
    const int32_t sel = scrutineeScalar.value;

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
            [&](auto &backend)
            {
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
