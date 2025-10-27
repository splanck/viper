//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the VM opcode handlers responsible for memory and pointer
// operations.  These routines manage the interpreter's stack storage, compute
// pointer arithmetic, and bridge to runtime-managed string constants while
// ensuring traps fire when callers violate safety invariants.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Memory.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

using namespace il::core;

/// @file
/// @brief Memory and pointer opcode handlers for the Viper virtual machine.
/// @details The handlers in this file evaluate IL instructions that manipulate
///          memory addresses or interact with runtime-managed buffers.  They
///          ensure stack allocations stay in-bounds, pointer arithmetic obeys
///          provenance rules, and constant materialisation honours type-specific
///          storage conventions.

namespace il::vm::detail::memory
{
/// @brief Handle the @c alloca opcode by reserving stack storage.
/// @details Validates the requested size, aligns the stack pointer to
///          @c std::max_align_t, zeros the allocated memory, and advances the
///          frame's stack pointer.  On overflow or invalid operands the handler
///          emits a trap and returns an execution result that signals unwinding.
/// @param vm Virtual machine instance used for operand evaluation and traps.
/// @param fr Active frame whose stack storage is extended.
/// @param in IL instruction supplying the allocation size and destination slot.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
VM::ExecResult handleAlloca(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    if (in.operands.empty())
    {
        RuntimeBridge::trap(
            TrapKind::DomainError, "missing allocation size", in.loc, fr.func->name, "");
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    int64_t bytes = VMAccess::eval(vm, fr, in.operands[0]).i64;
    if (bytes < 0)
    {
        RuntimeBridge::trap(
            TrapKind::DomainError, "negative allocation", in.loc, fr.func->name, "");
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    const size_t size = static_cast<size_t>(bytes);
    const size_t addr = fr.sp;
    const size_t stackSize = fr.stack.size();
    constexpr size_t alignment = alignof(std::max_align_t);

    auto trapOverflow = [&]() -> VM::ExecResult
    {
        RuntimeBridge::trap(
            TrapKind::Overflow, "stack overflow in alloca", in.loc, fr.func->name, "");
        VM::ExecResult result{};
        result.returned = true;
        return result;
    };

    if (addr > stackSize)
        return trapOverflow();

    size_t alignedAddr = addr;
    if (alignment > 1)
    {
        const size_t remainder = alignedAddr % alignment;
        if (remainder != 0U)
        {
            const size_t padding = alignment - remainder;
            if (alignedAddr > std::numeric_limits<size_t>::max() - padding)
                return trapOverflow();
            alignedAddr += padding;
        }
    }

    if (alignedAddr > stackSize)
        return trapOverflow();

    if (size > std::numeric_limits<size_t>::max() - alignedAddr)
        return trapOverflow();

    const size_t remaining = stackSize - alignedAddr;
    if (size > remaining)
        return trapOverflow();

    std::memset(fr.stack.data() + alignedAddr, 0, size);

    Slot out{};
    out.ptr = fr.stack.data() + alignedAddr;
    fr.sp = alignedAddr + size;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Handle the @c gep opcode by computing pointer arithmetic.
/// @details Evaluates the base pointer and byte offset operands, performs the
///          offset calculation, and stores the resulting pointer.  The handler
///          assumes callers respect allocation bounds; later loads or stores will
///          trap if the pointer escapes its provenance.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame storing operands and results.
/// @param in Instruction describing the base pointer, offset, and destination.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
VM::ExecResult handleGEP(VM &vm,
                         Frame &fr,
                         const Instr &in,
                         const VM::BlockMap &blocks,
                         const BasicBlock *&bb,
                         size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot base = VMAccess::eval(vm, fr, in.operands[0]);
    Slot offset = VMAccess::eval(vm, fr, in.operands[1]);

    Slot out{};
    if (base.ptr == nullptr)
    {
        if (offset.i64 == 0)
        {
            out.ptr = nullptr;
            ops::storeResult(fr, in, out);
            return {};
        }
    }

    const std::uintptr_t baseAddr = reinterpret_cast<std::uintptr_t>(base.ptr);
    const int64_t delta = offset.i64;
    std::uintptr_t resultAddr = baseAddr;
    if (delta >= 0)
    {
        resultAddr += static_cast<std::uintptr_t>(delta);
    }
    else
    {
        resultAddr -= static_cast<std::uintptr_t>(-delta);
    }

    out.ptr = reinterpret_cast<void *>(resultAddr);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Handle the @c addr.of opcode by reifying runtime string pointers.
/// @details Forwards the pointer held in the operand slot to the destination
///          without modification, allowing IL to reference immutable runtime
///          strings.  The handler relies on the runtime to guarantee the operand
///          points to a valid string payload.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame storing operands and results.
/// @param in Instruction referencing the string operand and destination slot.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
VM::ExecResult handleAddrOf(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot tmp = VMAccess::eval(vm, fr, in.operands[0]);
    Slot out{};
    out.ptr = tmp.str;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Handle the @c const.str opcode by materialising constant string slots.
/// @details Copies the evaluated operand directly into the destination without
///          further transformation, providing a convenient way to expose runtime
///          string handles to subsequent instructions.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame storing operands and results.
/// @param in Instruction describing the constant string operand.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
VM::ExecResult handleConstStr(VM &vm,
                              Frame &fr,
                              const Instr &in,
                              const VM::BlockMap &blocks,
                              const BasicBlock *&bb,
                              size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot out = VMAccess::eval(vm, fr, in.operands[0]);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Handle the @c const.null opcode by writing a type-appropriate null.
/// @details Inspects the destination type to determine which slot member should
///          receive the null value.  Pointer-like types clear the generic pointer
///          field, while string handles set the runtime string pointer to @c nullptr.
/// @param vm Virtual machine instance executing the instruction (unused).
/// @param fr Active frame storing the destination slot.
/// @param in Instruction describing the destination type.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
VM::ExecResult handleConstNull(VM &vm,
                               Frame &fr,
                               const Instr &in,
                               const VM::BlockMap &blocks,
                               const BasicBlock *&bb,
                               size_t &ip)
{
    (void)vm;
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot out{};
    switch (in.type.kind)
    {
        case Type::Kind::I1:
        case Type::Kind::I16:
        case Type::Kind::I32:
        case Type::Kind::I64:
        case Type::Kind::Error:
        case Type::Kind::ResumeTok:
            out.i64 = 0;
            break;
        case Type::Kind::F64:
            out.f64 = 0.0;
            break;
        case Type::Kind::Ptr:
            out.ptr = nullptr;
            break;
        case Type::Kind::Str:
            out.str = nullptr;
            break;
        default:
            out.ptr = nullptr;
            break;
    }

    ops::storeResult(fr, in, out);
    return {};
}
} // namespace il::vm::detail::memory
