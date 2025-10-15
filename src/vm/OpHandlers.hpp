// File: src/vm/OpHandlers.hpp
// Purpose: Declares opcode handler helpers and dispatch table for the VM.
// Key invariants: Handlers implement IL opcode semantics according to docs/il-guide.md#reference.
// Ownership/Lifetime: Handlers operate on VM-owned frames and do not retain references.
// Links: docs/il-guide.md#reference
#pragma once

#include "vm/VM.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "vm/control_flow.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace il::vm::detail
{

/// @brief Collection of opcode handler entry points used by the VM dispatcher.
struct OpHandlers
{
    /// @brief Lightweight view that adapts legacy handler parameters to the
    ///        @ref VM::ExecState shape expected by inline handlers.
    struct LegacyExecState
    {
        Frame &fr;
        const VM::BlockMap &blocks;
        const il::core::BasicBlock *&bb;
        size_t &ip;
        viper::vm::SwitchCache *switchCache;
    };

    /// @brief Acquire a legacy execution state view backed by @p fr and
    ///        associated control flow references.
    static inline LegacyExecState makeLegacyState(VM &vm,
                                                  Frame &fr,
                                                  const VM::BlockMap &blocks,
                                                  const il::core::BasicBlock *&bb,
                                                  size_t &ip)
    {
        LegacyExecState state{fr, blocks, bb, ip, nullptr};
        if (!vm.execStack.empty())
        {
            state.switchCache = &vm.execStack.back()->switchCache;
        }
        else
        {
            static thread_local viper::vm::SwitchCache fallbackCache{};
            state.switchCache = &fallbackCache;
        }
        return state;
    }

    template <typename State>
    static inline viper::vm::SwitchCache &resolveSwitchCache(State &state)
    {
        if constexpr (std::is_pointer_v<std::decay_t<decltype(state.switchCache)>>)
        {
            assert(state.switchCache != nullptr && "switch cache pointer must be valid");
            return *state.switchCache;
        }
        else
        {
            return state.switchCache;
        }
    }

    static inline Slot loadSlotFromPtr(il::core::Type::Kind kind, void *ptr)
    {
        Slot out{};
        switch (kind)
        {
            case il::core::Type::Kind::I16:
                out.i64 = static_cast<int64_t>(*reinterpret_cast<int16_t *>(ptr));
                break;
            case il::core::Type::Kind::I32:
                out.i64 = static_cast<int64_t>(*reinterpret_cast<int32_t *>(ptr));
                break;
            case il::core::Type::Kind::I64:
                out.i64 = *reinterpret_cast<int64_t *>(ptr);
                break;
            case il::core::Type::Kind::I1:
                out.i64 = static_cast<int64_t>(*reinterpret_cast<uint8_t *>(ptr) & 1);
                break;
            case il::core::Type::Kind::F64:
                out.f64 = *reinterpret_cast<double *>(ptr);
                break;
            case il::core::Type::Kind::Str:
                out.str = *reinterpret_cast<rt_string *>(ptr);
                break;
            case il::core::Type::Kind::Ptr:
                out.ptr = *reinterpret_cast<void **>(ptr);
                break;
            case il::core::Type::Kind::Error:
            case il::core::Type::Kind::ResumeTok:
                out.ptr = nullptr;
                break;
            case il::core::Type::Kind::Void:
                out.i64 = 0;
                break;
        }

        return out;
    }

    static inline void storeSlotToPtr(il::core::Type::Kind kind, void *ptr, const Slot &value)
    {
        switch (kind)
        {
            case il::core::Type::Kind::I16:
                *reinterpret_cast<int16_t *>(ptr) = static_cast<int16_t>(value.i64);
                break;
            case il::core::Type::Kind::I32:
                *reinterpret_cast<int32_t *>(ptr) = static_cast<int32_t>(value.i64);
                break;
            case il::core::Type::Kind::I64:
                *reinterpret_cast<int64_t *>(ptr) = value.i64;
                break;
            case il::core::Type::Kind::I1:
                *reinterpret_cast<uint8_t *>(ptr) = static_cast<uint8_t>(value.i64 != 0);
                break;
            case il::core::Type::Kind::F64:
                *reinterpret_cast<double *>(ptr) = value.f64;
                break;
            case il::core::Type::Kind::Str:
                *reinterpret_cast<rt_string *>(ptr) = value.str;
                break;
            case il::core::Type::Kind::Ptr:
                *reinterpret_cast<void **>(ptr) = value.ptr;
                break;
            case il::core::Type::Kind::Error:
            case il::core::Type::Kind::ResumeTok:
            case il::core::Type::Kind::Void:
                break;
        }
    }

    static viper::vm::SwitchCacheEntry &ensureSwitchCacheEntry(viper::vm::SwitchCache &cache,
                                                               const il::core::Instr &in);

    template <typename State>
    static inline VM::ExecResult handleAddInline(VM &vm, State &state, const il::core::Instr &in)
    {
        return ops::applyBinary(vm,
                                state.fr,
                                in,
                                [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                { out.i64 = lhsVal.i64 + rhsVal.i64; });
    }

    template <typename State>
    static inline VM::ExecResult handleSubInline(VM &vm, State &state, const il::core::Instr &in)
    {
        return ops::applyBinary(vm,
                                state.fr,
                                in,
                                [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                { out.i64 = lhsVal.i64 - rhsVal.i64; });
    }

    template <typename State>
    static inline VM::ExecResult handleMulInline(VM &vm, State &state, const il::core::Instr &in)
    {
        return ops::applyBinary(vm,
                                state.fr,
                                in,
                                [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                { out.i64 = lhsVal.i64 * rhsVal.i64; });
    }

    template <typename State>
    static inline VM::ExecResult handleLoadInline(VM &vm, State &state, const il::core::Instr &in)
    {
        const auto &blocks [[maybe_unused]] = state.blocks;
        const il::core::BasicBlock *&bbRef = state.bb;
        size_t &ipRef [[maybe_unused]] = state.ip;

        void *ptr = vm.eval(state.fr, in.operands[0]).ptr;
        if (!ptr)
        {
            const std::string blockLabel = bbRef ? bbRef->label : std::string();
            const std::string functionName = state.fr.func ? state.fr.func->name : std::string();
            RuntimeBridge::trap(TrapKind::InvalidOperation, "null load", in.loc, functionName, blockLabel);
            VM::ExecResult result{};
            result.returned = true;
            return result;
        }

        ops::storeResult(state.fr, in, loadSlotFromPtr(in.type.kind, ptr));
        return {};
    }

    template <typename State>
    static inline VM::ExecResult handleStoreInline(VM &vm, State &state, const il::core::Instr &in)
    {
        const auto &blocks [[maybe_unused]] = state.blocks;
        const il::core::BasicBlock *&bbRef = state.bb;
        size_t &ipRef = state.ip;

        const std::string blockLabel = bbRef ? bbRef->label : std::string();
        const std::string functionName = state.fr.func ? state.fr.func->name : std::string();
        void *ptr = vm.eval(state.fr, in.operands[0]).ptr;
        if (!ptr)
        {
            RuntimeBridge::trap(TrapKind::InvalidOperation, "null store", in.loc, functionName, blockLabel);
            VM::ExecResult result{};
            result.returned = true;
            return result;
        }
        Slot value = vm.eval(state.fr, in.operands[1]);

        storeSlotToPtr(in.type.kind, ptr, value);

        if (in.operands[0].kind == il::core::Value::Kind::Temp)
        {
            const unsigned id = in.operands[0].id;
            if (id < state.fr.func->valueNames.size())
            {
                const std::string &nm = state.fr.func->valueNames[id];
                if (!nm.empty())
                    vm.debug.onStore(nm, in.type.kind, value.i64, value.f64, functionName, blockLabel, ipRef);
            }
        }

        return {};
    }

    template <typename State>
    static inline VM::ExecResult branchToTargetInline(VM &vm,
                                                      State &state,
                                                      const il::core::Instr &in,
                                                      size_t idx)
    {
        const auto &blocks = state.blocks;
        const auto &label = in.labels[idx];
        auto it = blocks.find(label);
        assert(it != blocks.end() && "invalid branch target");
        const il::core::BasicBlock *target = it->second;
        const il::core::BasicBlock *sourceBlock = state.bb;
        const std::string sourceLabel = sourceBlock ? sourceBlock->label : std::string{};
        const std::string functionName = state.fr.func ? state.fr.func->name : std::string{};

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
            return {};
        }

        if (provided > 0)
        {
            const auto &args = in.brArgs[idx];
            for (size_t i = 0; i < provided; ++i)
            {
                const auto id = target->params[i].id;
                assert(id < state.fr.params.size());
                state.fr.params[id] = vm.eval(state.fr, args[i]);
            }
        }

        state.bb = target;
        state.ip = 0;
        VM::ExecResult result{};
        result.jumped = true;
        return result;
    }

    template <typename State>
    static inline VM::ExecResult handleBrInline(VM &vm,
                                                State &state,
                                                const il::core::Instr &in)
    {
        return branchToTargetInline(vm, state, in, 0);
    }

    template <typename State>
    static inline VM::ExecResult handleCBrInline(VM &vm,
                                                 State &state,
                                                 const il::core::Instr &in)
    {
        Slot cond = vm.eval(state.fr, in.operands[0]);
        const size_t targetIdx = (cond.i64 != 0) ? 0 : 1;
        return branchToTargetInline(vm, state, in, targetIdx);
    }

    static inline int32_t dispatchDense(const viper::vm::DenseJumpTable &table,
                                        int32_t sel,
                                        int32_t defaultIdx)
    {
        const int64_t off = static_cast<int64_t>(sel) - static_cast<int64_t>(table.base);
        if (off < 0 || off >= static_cast<int64_t>(table.targets.size()))
            return defaultIdx;
        const int32_t t = table.targets[static_cast<size_t>(off)];
        return (t < 0) ? defaultIdx : t;
    }

    static inline int32_t dispatchSorted(const viper::vm::SortedCases &cases,
                                         int32_t sel,
                                         int32_t defaultIdx)
    {
        auto it = std::lower_bound(cases.keys.begin(), cases.keys.end(), sel);
        if (it == cases.keys.end() || *it != sel)
            return defaultIdx;
        const size_t idx = static_cast<size_t>(it - cases.keys.begin());
        return cases.targetIdx[idx];
    }

    static inline int32_t dispatchHashed(const viper::vm::HashedCases &cases,
                                         int32_t sel,
                                         int32_t defaultIdx)
    {
        auto it = cases.map.find(sel);
        return (it == cases.map.end()) ? defaultIdx : it->second;
    }

    template <typename State>
    static inline VM::ExecResult handleSwitchI32Inline(VM &vm,
                                                       State &state,
                                                       const il::core::Instr &in)
    {
        const Slot scrutineeSlot = vm.eval(state.fr, il::core::switchScrutinee(in));
        const int32_t sel = static_cast<int32_t>(scrutineeSlot.i64);

        viper::vm::SwitchCache &cache = resolveSwitchCache(state);
        auto &entry = OpHandlers::ensureSwitchCacheEntry(cache, in);

        int32_t idx = entry.defaultIdx;
        const bool forceLinear = (entry.kind == viper::vm::SwitchCacheEntry::Linear);

#if defined(VIPER_VM_DEBUG_SWITCH_LINEAR)
        (void)forceLinear;
        const size_t caseCount = il::core::switchCaseCount(in);
        for (size_t caseIdx = 0; caseIdx < caseCount; ++caseIdx)
        {
            const il::core::Value &caseValue = il::core::switchCaseValue(in, caseIdx);
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
            const size_t caseCount = il::core::switchCaseCount(in);
            for (size_t caseIdx = 0; caseIdx < caseCount; ++caseIdx)
            {
                const il::core::Value &caseValue = il::core::switchCaseValue(in, caseIdx);
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
                    using T = std::decay_t<decltype(backend)>;
                    if constexpr (std::is_same_v<T, viper::vm::DenseJumpTable>)
                        idx = dispatchDense(backend, sel, entry.defaultIdx);
                    else if constexpr (std::is_same_v<T, viper::vm::SortedCases>)
                        idx = dispatchSorted(backend, sel, entry.defaultIdx);
                    else if constexpr (std::is_same_v<T, viper::vm::HashedCases>)
                        idx = dispatchHashed(backend, sel, entry.defaultIdx);
                    else
                        idx = entry.defaultIdx;
                },
                entry.backend);
        }
#endif

        if (idx < 0)
            idx = entry.defaultIdx >= 0 ? entry.defaultIdx : 0;

        const size_t targetIdx = static_cast<size_t>(idx);

        assert(!in.labels.empty() && "switch must have a default successor");
        assert(targetIdx < in.labels.size() && "switch dispatch resolved invalid target");
        return branchToTargetInline(vm, state, in, targetIdx);
    }

    static VM::ExecResult handleAlloca(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleLoad(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleStore(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

    static VM::ExecResult handleAdd(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleSub(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleISub(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleMul(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleIAddOvf(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleISubOvf(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleIMulOvf(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleSDiv(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleUDiv(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleSRem(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleURem(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleSDivChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleUDivChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleSRemChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleURemChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleIdxChk(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFAdd(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleFSub(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleFMul(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleFDiv(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleAnd(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleOr(VM &vm,
                                   Frame &fr,
                                   const il::core::Instr &in,
                                   const VM::BlockMap &blocks,
                                   const il::core::BasicBlock *&bb,
                                   size_t &ip);

    static VM::ExecResult handleXor(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleShl(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleLShr(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleAShr(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleGEP(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleICmpEq(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleICmpNe(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpGT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpLT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpLE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpGE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleUCmpLT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleUCmpLE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleUCmpGT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleUCmpGE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpEQ(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpNE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpGT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpLT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpLE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpGE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSwitchI32(VM &vm,
                                          Frame &fr,
                                          const il::core::Instr &in,
                                          const VM::BlockMap &blocks,
                                          const il::core::BasicBlock *&bb,
                                          size_t &ip);

    static VM::ExecResult handleBr(VM &vm,
                                   Frame &fr,
                                   const il::core::Instr &in,
                                   const VM::BlockMap &blocks,
                                   const il::core::BasicBlock *&bb,
                                   size_t &ip);

    static VM::ExecResult handleCBr(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleRet(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleAddrOf(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleConstStr(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleConstNull(VM &vm,
                                          Frame &fr,
                                          const il::core::Instr &in,
                                          const VM::BlockMap &blocks,
                                          const il::core::BasicBlock *&bb,
                                          size_t &ip);

    static VM::ExecResult handleCall(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleSitofp(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFptosi(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleCastFpToSiRteChk(VM &vm,
                                                 Frame &fr,
                                                 const il::core::Instr &in,
                                                 const VM::BlockMap &blocks,
                                                 const il::core::BasicBlock *&bb,
                                                 size_t &ip);

    static VM::ExecResult handleCastFpToUiRteChk(VM &vm,
                                                 Frame &fr,
                                                 const il::core::Instr &in,
                                                 const VM::BlockMap &blocks,
                                                 const il::core::BasicBlock *&bb,
                                                 size_t &ip);

    static VM::ExecResult handleCastSiNarrowChk(VM &vm,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip);

    static VM::ExecResult handleCastUiNarrowChk(VM &vm,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip);

    static VM::ExecResult handleCastSiToFp(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleCastUiToFp(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleTruncOrZext1(VM &vm,
                                             Frame &fr,
                                             const il::core::Instr &in,
                                             const VM::BlockMap &blocks,
                                             const il::core::BasicBlock *&bb,
                                             size_t &ip);

    static VM::ExecResult handleErrGet(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleEhEntry(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleEhPush(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleEhPop(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

    static VM::ExecResult handleResumeSame(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleResumeNext(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleResumeLabel(VM &vm,
                                            Frame &fr,
                                            const il::core::Instr &in,
                                            const VM::BlockMap &blocks,
                                            const il::core::BasicBlock *&bb,
                                            size_t &ip);

    static VM::ExecResult handleTrapKind(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleTrapErr(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleTrap(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

  private:
    static VM::ExecResult branchToTarget(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         size_t idx,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);
};

/// @brief Access the lazily initialised opcode handler table.
const VM::OpcodeHandlerTable &getOpcodeHandlers();

} // namespace il::vm::detail
