// File: src/vm/OpHandlers.hpp
// Purpose: Declares opcode handler helpers and dispatch table for the VM.
// Key invariants: Handlers implement IL opcode semantics according to docs/il-guide.md#reference.
// Ownership/Lifetime: Handlers operate on VM-owned frames and do not retain references.
// Links: docs/il-guide.md#reference
#pragma once

#include "vm/VM.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/control_flow.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace il::vm::detail
{

/// @brief Collection of opcode handler entry points used by the VM dispatcher.
struct OpHandlers
{
    static VM::ExecResult handleLoadImpl(VM &vm,
                                         VM::ExecState *state,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleStoreImpl(VM &vm,
                                          VM::ExecState *state,
                                          Frame &fr,
                                          const il::core::Instr &in,
                                          const VM::BlockMap &blocks,
                                          const il::core::BasicBlock *&bb,
                                          size_t &ip);

    static VM::ExecResult handleAddImpl(VM &vm,
                                        VM::ExecState *state,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleSubImpl(VM &vm,
                                        VM::ExecState *state,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleMulImpl(VM &vm,
                                        VM::ExecState *state,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleBrImpl(VM &vm,
                                       VM::ExecState *state,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleCBrImpl(VM &vm,
                                        VM::ExecState *state,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleSwitchI32Impl(VM &vm,
                                              VM::ExecState *state,
                                              Frame &fr,
                                              const il::core::Instr &in,
                                              const VM::BlockMap &blocks,
                                              const il::core::BasicBlock *&bb,
                                              size_t &ip);

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

namespace inline_impl
{
inline Slot loadSlotFromPtr(il::core::Type::Kind kind, void *ptr)
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

inline void storeSlotToPtr(il::core::Type::Kind kind, void *ptr, const Slot &value)
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
            *reinterpret_cast<uint8_t *>(ptr) = static_cast<uint8_t>(value.i64 & 1);
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
            *reinterpret_cast<void **>(ptr) = value.ptr;
            break;
        case il::core::Type::Kind::Void:
            break;
    }
}

struct SwitchMeta
{
    const void *key = nullptr;
    std::vector<int32_t> values;
    std::vector<int32_t> succIdx;
    int32_t defaultIdx = -1;
};

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

inline int32_t lookupDense(const viper::vm::DenseJumpTable &table,
                           int32_t sel,
                           int32_t defIdx)
{
    const int64_t offset = static_cast<int64_t>(sel) - static_cast<int64_t>(table.base);
    if (offset < 0 || offset >= static_cast<int64_t>(table.targets.size()))
        return defIdx;
    const int32_t target = table.targets[static_cast<size_t>(offset)];
    return (target < 0) ? defIdx : target;
}

inline int32_t lookupSorted(const viper::vm::SortedCases &cases,
                            int32_t sel,
                            int32_t defIdx)
{
    auto it = std::lower_bound(cases.keys.begin(), cases.keys.end(), sel);
    if (it == cases.keys.end() || *it != sel)
        return defIdx;
    const size_t idx = static_cast<size_t>(it - cases.keys.begin());
    return cases.targetIdx[idx];
}

inline int32_t lookupHashed(const viper::vm::HashedCases &cases,
                            int32_t sel,
                            int32_t defIdx)
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
    const double density = static_cast<double>(meta.values.size()) /
                           static_cast<double>(range);

    if (range <= 4096 && density >= 0.60)
        return viper::vm::SwitchCacheEntry::Dense;
    if (meta.values.size() >= 64 && density < 0.15)
        return viper::vm::SwitchCacheEntry::Hashed;
    return viper::vm::SwitchCacheEntry::Sorted;
}

inline viper::vm::DenseJumpTable buildDense(const SwitchMeta &meta)
{
    const int32_t minv = *std::min_element(meta.values.begin(), meta.values.end());
    const int32_t maxv = *std::max_element(meta.values.begin(), meta.values.end());
    viper::vm::DenseJumpTable table;
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
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return meta.values[a] < meta.values[b];
    });
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

inline VM::ExecResult OpHandlers::handleLoadImpl(VM &vm,
                                                 VM::ExecState *state,
                                                 Frame &fr,
                                                 const il::core::Instr &in,
                                                 const VM::BlockMap &blocks,
                                                 const il::core::BasicBlock *&bb,
                                                 size_t &ip)
{
    (void)state;
    (void)blocks;
    (void)ip;

    void *ptr = vm.eval(fr, in.operands[0]).ptr;
    if (!ptr)
    {
        const std::string blockLabel = bb ? bb->label : std::string();
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            "null load",
                            in.loc,
                            fr.func ? fr.func->name : std::string(),
                            blockLabel);
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    ops::storeResult(fr, in, inline_impl::loadSlotFromPtr(in.type.kind, ptr));
    return {};
}

inline VM::ExecResult OpHandlers::handleStoreImpl(VM &vm,
                                                  VM::ExecState *state,
                                                  Frame &fr,
                                                  const il::core::Instr &in,
                                                  const VM::BlockMap &blocks,
                                                  const il::core::BasicBlock *&bb,
                                                  size_t &ip)
{
    (void)state;
    (void)blocks;

    const std::string blockLabel = bb ? bb->label : std::string();
    void *ptr = vm.eval(fr, in.operands[0]).ptr;
    if (!ptr)
    {
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            "null store",
                            in.loc,
                            fr.func ? fr.func->name : std::string(),
                            blockLabel);
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    Slot value = vm.eval(fr, in.operands[1]);

    inline_impl::storeSlotToPtr(in.type.kind, ptr, value);

    if (in.operands[0].kind == il::core::Value::Kind::Temp)
    {
        const unsigned id = in.operands[0].id;
        if (id < fr.func->valueNames.size())
        {
            const std::string &name = fr.func->valueNames[id];
            if (!name.empty())
            {
                const std::string_view fnView = fr.func ? std::string_view(fr.func->name) : std::string_view{};
                const std::string_view blockView = bb ? std::string_view(bb->label) : std::string_view{};
                vm.debug.onStore(name,
                                 in.type.kind,
                                 value.i64,
                                 value.f64,
                                 fnView,
                                 blockView,
                                 ip);
            }
        }
    }

    return {};
}

inline VM::ExecResult OpHandlers::handleAddImpl(VM &vm,
                                                VM::ExecState *state,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip)
{
    (void)state;
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [](Slot &out, const Slot &lhsVal, const Slot &rhsVal) {
                                out.i64 = lhsVal.i64 + rhsVal.i64;
                            });
}

inline VM::ExecResult OpHandlers::handleSubImpl(VM &vm,
                                                VM::ExecState *state,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip)
{
    (void)state;
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [](Slot &out, const Slot &lhsVal, const Slot &rhsVal) {
                                out.i64 = lhsVal.i64 - rhsVal.i64;
                            });
}

inline VM::ExecResult OpHandlers::handleMulImpl(VM &vm,
                                                VM::ExecState *state,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip)
{
    (void)state;
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [](Slot &out, const Slot &lhsVal, const Slot &rhsVal) {
                                out.i64 = lhsVal.i64 * rhsVal.i64;
                            });
}

inline VM::ExecResult OpHandlers::handleBrImpl(VM &vm,
                                               VM::ExecState *state,
                                               Frame &fr,
                                               const il::core::Instr &in,
                                               const VM::BlockMap &blocks,
                                               const il::core::BasicBlock *&bb,
                                               size_t &ip)
{
    (void)state;
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

inline VM::ExecResult OpHandlers::handleCBrImpl(VM &vm,
                                                VM::ExecState *state,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip)
{
    (void)state;
    Slot cond = vm.eval(fr, in.operands[0]);
    const size_t targetIdx = (cond.i64 != 0) ? 0 : 1;
    return branchToTarget(vm, fr, in, targetIdx, blocks, bb, ip);
}

inline VM::ExecResult OpHandlers::handleSwitchI32Impl(VM &vm,
                                                      VM::ExecState *state,
                                                      Frame &fr,
                                                      const il::core::Instr &in,
                                                      const VM::BlockMap &blocks,
                                                      const il::core::BasicBlock *&bb,
                                                      size_t &ip)
{
    (void)bb;
    (void)ip;

    const Slot scrutineeSlot = vm.eval(fr, switchScrutinee(in));
    const int32_t sel = static_cast<int32_t>(scrutineeSlot.i64);

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
            [&](auto &backend) {
                using BackendT = std::decay_t<decltype(backend)>;
                if constexpr (std::is_same_v<BackendT, viper::vm::DenseJumpTable>)
                    idx = inline_impl::lookupDense(backend, sel, entry.defaultIdx);
                else if constexpr (std::is_same_v<BackendT, viper::vm::SortedCases>)
                    idx = inline_impl::lookupSorted(backend, sel, entry.defaultIdx);
                else if constexpr (std::is_same_v<BackendT, viper::vm::HashedCases>)
                    idx = inline_impl::lookupHashed(backend, sel, entry.defaultIdx);
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
    return branchToTarget(vm, fr, in, targetIdx, blocks, bb, ip);
}

/// @brief Access the lazily initialised opcode handler table.
const VM::OpcodeHandlerTable &getOpcodeHandlers();

} // namespace il::vm::detail
