//===----------------------------------------------------------------------===//
//
// This file is part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/vm/ops/Op_CallRet.cpp
// Purpose: Implement VM opcode handlers for function calls and returns.
// Key invariants: Result slots are updated exactly once per handler invocation
//                 and bridge lookups fall back to the runtime dispatcher when a
//                 direct VM function implementation is unavailable.
// Ownership/Lifetime: Handlers borrow the active frame and never take ownership
//                     of operands or result slots.
// Links: docs/runtime-vm.md#dispatch
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Control.hpp"

#include "vm/RuntimeBridge.hpp"

#include "il/core/Function.hpp"
#include "il/core/Type.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace il::vm::detail::control
{
namespace
{
std::optional<il::core::Type> findValueType(const il::core::Function &fn, unsigned id)
{
    for (const auto &param : fn.params)
    {
        if (param.id == id)
            return param.type;
    }

    for (const auto &block : fn.blocks)
    {
        for (const auto &param : block.params)
        {
            if (param.id == id)
                return param.type;
        }

        for (const auto &instr : block.instructions)
        {
            if (instr.result && *instr.result == id)
                return instr.type;
        }
    }

    return std::nullopt;
}

bool slotsEqual(const Slot &lhs, const Slot &rhs, il::core::Type::Kind kind)
{
    using il::core::Type;
    switch (kind)
    {
        case Type::Kind::I1:
        case Type::Kind::I16:
        case Type::Kind::I32:
        case Type::Kind::I64:
            return lhs.i64 == rhs.i64;
        case Type::Kind::F64:
            return lhs.f64 == rhs.f64;
        case Type::Kind::Ptr:
        case Type::Kind::Error:
        case Type::Kind::ResumeTok:
            return lhs.ptr == rhs.ptr;
        case Type::Kind::Str:
            return lhs.str == rhs.str;
        case Type::Kind::Void:
            return true;
    }
    return true;
}

bool pointerIntoStack(const Frame &fr, const Slot &slot)
{
    const auto *begin = fr.stack.data();
    const auto *end = begin + fr.stack.size();
    const auto *ptr = static_cast<const uint8_t *>(slot.ptr);
    return ptr && ptr >= begin && ptr < end;
}

void syncMutatedRuntimeArgs(Frame &fr,
                            const il::core::Instr &in,
                            const std::vector<Slot> &before,
                            const std::vector<Slot> &after)
{
    if (!fr.func)
        return;

    const size_t operandCount = std::min({in.operands.size(), before.size(), after.size()});
    for (size_t index = 0; index < operandCount; ++index)
    {
        const auto &operand = in.operands[index];
        const Slot &original = before[index];
        const Slot &current = after[index];

        if (operand.kind != il::core::Value::Kind::Temp)
            continue;

        auto valueType = findValueType(*fr.func, operand.id);
        if (!valueType)
            continue;

        if (valueType->kind == il::core::Type::Kind::Ptr && original.ptr != current.ptr &&
            pointerIntoStack(fr, original))
        {
            *reinterpret_cast<void **>(original.ptr) = current.ptr;
            continue;
        }

        if (!slotsEqual(original, current, valueType->kind))
        {
            il::core::Instr pseudo{};
            pseudo.result = operand.id;
            pseudo.type = *valueType;
            ops::storeResult(fr, pseudo, current);
        }
    }
}
} // namespace

/// @brief Finalise a function by propagating the return value and signalling exit.
///
/// @details Return instructions optionally carry a single operand that is
///          evaluated before the frame unwinds.  The helper extracts that
///          operand, captures the resulting slot on the @ref VM::ExecResult, and
///          flips the @ref VM::ExecResult::returned flag so the dispatch loop can
///          unwind to the caller.  Branch metadata parameters are ignored for
///          this opcode; they are present to satisfy the handler signature.
///
/// @param vm Active virtual machine instance (unused).
/// @param fr Frame owning the registers and temporary storage for the call.
/// @param in IL instruction describing the return operation.
/// @param blocks Map of block labels to basic block pointers (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer within @p bb (unused).
/// @return Execution result populated with the returned slot when present.
VM::ExecResult handleRet(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip)
{
    (void)vm;
    (void)blocks;
    (void)bb;
    (void)ip;
    VM::ExecResult result{};
    if (!in.operands.empty())
        result.value = VMAccess::eval(vm, fr, in.operands[0]);
    result.returned = true;
    return result;
}

/// @brief Invoke a callee and write the result back into the destination register.
///
/// @details The handler performs the following sequence:
///          1. Evaluate all operand expressions eagerly so argument side effects
///             occur before dispatch.  This mirrors the IL semantics and keeps
///             runtime bridges deterministic.
///          2. Look up the callee within the VM's direct function map.  When a
///             match is found the VM-specific implementation executes via
///             @ref VMAccess::callFunction.
///          3. Fall back to @ref RuntimeBridge::call when the VM lacks a native
///             implementation, thereby delegating to the runtime library.
///          4. Persist the returned slot using @ref ops::storeResult so that
///             register lifetime management is centralised.
///          The handler never manipulates control-flow metadata directly; the
///          interpreter loop continues execution in the current block after the
///          call completes.
///
/// @param vm Virtual machine coordinating the call.
/// @param fr Active frame whose registers supply arguments and receive results.
/// @param in Instruction describing the call site and callee symbol.
/// @param blocks Map of block labels to block pointers (unused).
/// @param bb Pointer to the current basic block (unused for calls).
/// @param ip Instruction pointer within the block (unused for calls).
/// @return Result structure with @ref VM::ExecResult::returned left false.
VM::ExecResult handleCall(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)ip;

    // Evaluate operands up front so argument propagation is explicit and
    // deterministic before dispatch.  This mirrors the IL semantics and avoids
    // leaking partially evaluated slots if a bridge call traps.
    std::vector<Slot> args;
    args.reserve(in.operands.size());
    for (const auto &op : in.operands)
        args.push_back(VMAccess::eval(vm, fr, op));

    Slot out{};
    const auto &fnMap = VMAccess::functionMap(vm);
    auto it = fnMap.find(in.callee);
    if (it != fnMap.end())
    {
        out = VMAccess::callFunction(vm, *it->second, args);
    }
    else
    {
        const std::vector<Slot> preservedArgs = args;
        const std::string functionName = fr.func ? fr.func->name : std::string{};
        const std::string blockLabel = bb ? bb->label : std::string{};
        out = RuntimeBridge::call(
            VMAccess::runtimeContext(vm), in.callee, args, in.loc, functionName, blockLabel);
        syncMutatedRuntimeArgs(fr, in, preservedArgs, args);
    }
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail::control

