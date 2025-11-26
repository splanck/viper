//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/OpHandlers_Memory.hpp
// Purpose: Declare memory-related opcode handlers used by the VM dispatcher.
// Key invariants: Handlers honour IL semantics for loads, stores, allocations, and pointer ops.
// Ownership/Lifetime: Handlers mutate VM frames but never retain ownership of VM resources.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/OpHandlerAccess.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

namespace il::vm::detail::memory
{
using ExecState = VMAccess::ExecState;

namespace inline_impl
{
inline size_t minimumAlignmentFor(il::core::Type::Kind kind)
{
    switch (kind)
    {
        case il::core::Type::Kind::I1:
            return alignof(uint8_t);
        case il::core::Type::Kind::I16:
            return alignof(int16_t);
        case il::core::Type::Kind::I32:
            return alignof(int32_t);
        case il::core::Type::Kind::I64:
            return alignof(int64_t);
        case il::core::Type::Kind::F64:
            return alignof(double);
        case il::core::Type::Kind::Str:
            return alignof(rt_string);
        case il::core::Type::Kind::Ptr:
        case il::core::Type::Kind::Error:
        case il::core::Type::Kind::ResumeTok:
            return alignof(void *);
        case il::core::Type::Kind::Void:
            return 1U;
    }
    return 1U;
}

inline size_t sizeOfKind(il::core::Type::Kind kind)
{
    switch (kind)
    {
        case il::core::Type::Kind::I1:
            return 1;
        case il::core::Type::Kind::I16:
            return 2;
        case il::core::Type::Kind::I32:
            return 4;
        case il::core::Type::Kind::I64:
            return 8;
        case il::core::Type::Kind::F64:
            return 8;
        case il::core::Type::Kind::Str:
            return sizeof(rt_string);
        case il::core::Type::Kind::Ptr:
            return sizeof(void *);
        case il::core::Type::Kind::Error:
            return sizeof(void *);
        case il::core::Type::Kind::ResumeTok:
            return sizeof(void *);
        case il::core::Type::Kind::Void:
            return 0;
    }
    return 0;
}

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
            out.ptr = *reinterpret_cast<void **>(ptr);
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
        {
            auto *slot = reinterpret_cast<rt_string *>(ptr);
            rt_string current = *slot;
            rt_str_release_maybe(current);
            rt_string incoming = value.str;
            rt_str_retain_maybe(incoming);
            *slot = incoming;
            break;
        }
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
} // namespace inline_impl

inline VM::ExecResult handleLoadImpl(VM &vm,
                                     ExecState *state,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip)
{
    (void)state;
    (void)blocks;
    (void)ip;

    void *ptr = VMAccess::eval(vm, fr, in.operands[0]).ptr;
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

    const size_t alignment = inline_impl::minimumAlignmentFor(in.type.kind);
    const uintptr_t rawPtr = reinterpret_cast<uintptr_t>(ptr);
    // Fast-path: use bitmask for power-of-two alignments; fall back to modulo otherwise.
    if (alignment > 1U && (((alignment & (alignment - 1U)) == 0U)
                               ? ((rawPtr & (static_cast<uintptr_t>(alignment) - 1U)) != 0U)
                               : ((rawPtr % alignment) != 0U)))
    {
        const std::string blockLabel = bb ? bb->label : std::string();
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            "misaligned load",
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

inline VM::ExecResult handleStoreImpl(VM &vm,
                                      ExecState *state,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip)
{
    (void)state;
    (void)blocks;

    const std::string blockLabel = bb ? bb->label : std::string();
    void *ptr = VMAccess::eval(vm, fr, in.operands[0]).ptr;
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

    const size_t alignment = inline_impl::minimumAlignmentFor(in.type.kind);
    const uintptr_t rawPtr = reinterpret_cast<uintptr_t>(ptr);
    // Fast-path: use bitmask for power-of-two alignments; fall back to modulo otherwise.
    if (alignment > 1U && (((alignment & (alignment - 1U)) == 0U)
                               ? ((rawPtr & (static_cast<uintptr_t>(alignment) - 1U)) != 0U)
                               : ((rawPtr % alignment) != 0U)))
    {
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            "misaligned store",
                            in.loc,
                            fr.func ? fr.func->name : std::string(),
                            blockLabel);
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    Slot value = VMAccess::eval(vm, fr, in.operands[1]);

    // Memory watch hook: emit event on intersecting ranges with minimal overhead.
    auto &dbg = VMAccess::debug(vm);
    if (dbg.hasMemWatches())
    {
        const size_t writeSize = inline_impl::sizeOfKind(in.type.kind);
        if (writeSize)
            dbg.onMemWrite(ptr, writeSize);
    }

    inline_impl::storeSlotToPtr(in.type.kind, ptr, value);

    if (in.operands[0].kind == il::core::Value::Kind::Temp)
    {
        const unsigned id = in.operands[0].id;
        if (id < fr.func->valueNames.size())
        {
            const std::string &name = fr.func->valueNames[id];
            if (!name.empty())
            {
                const std::string_view fnView =
                    fr.func ? std::string_view(fr.func->name) : std::string_view{};
                const std::string_view blockView =
                    bb ? std::string_view(bb->label) : std::string_view{};
                VMAccess::debug(vm).onStore(
                    name, in.type.kind, value.i64, value.f64, fnView, blockView, ip);
            }
        }
    }

    return {};
}

VM::ExecResult handleAlloca(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleLoad(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

VM::ExecResult handleStore(VM &vm,
                           Frame &fr,
                           const il::core::Instr &in,
                           const VM::BlockMap &blocks,
                           const il::core::BasicBlock *&bb,
                           size_t &ip);

VM::ExecResult handleGEP(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip);

VM::ExecResult handleAddrOf(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleConstStr(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip);

VM::ExecResult handleGAddr(VM &vm,
                           Frame &fr,
                           const il::core::Instr &in,
                           const VM::BlockMap &blocks,
                           const il::core::BasicBlock *&bb,
                           size_t &ip);

VM::ExecResult handleConstNull(VM &vm,
                               Frame &fr,
                               const il::core::Instr &in,
                               const VM::BlockMap &blocks,
                               const il::core::BasicBlock *&bb,
                               size_t &ip);

} // namespace il::vm::detail::memory
