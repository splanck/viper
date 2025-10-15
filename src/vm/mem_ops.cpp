// File: src/vm/mem_ops.cpp
// Purpose: Implement VM handlers for memory and pointer operations (MIT License,
// see LICENSE).
// Key invariants: Operations respect frame stack bounds, pointer provenance,
// and type semantics.
// Ownership/Lifetime: Handlers mutate the active frame without retaining state.
// Links: docs/il-guide.md#reference

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

using namespace il::core;

namespace il::vm::detail
{

/// Handles `alloca` instructions by reserving zero-initialized stack space for
/// the current frame.
///
/// The handler expects an operand describing the allocation size in bytes and
/// raises a runtime trap when it is missing or negative. The frame stack is
/// treated as a bump allocator; callers rely on the invariant that `fr.sp +
/// size <= fr.stack.size()` (enforced in debug builds) so allocations never
/// escape the stack bounds.
///
/// @param vm   Virtual machine instance used for operand evaluation and traps.
/// @param fr   Active frame whose stack pointer is advanced.
/// @param in   IL instruction supplying the allocation size and destination.
/// @return ExecResult indicating whether execution should continue or unwind.
VM::ExecResult OpHandlers::handleAlloca(VM &vm,
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
        RuntimeBridge::trap(TrapKind::DomainError, "missing allocation size", in.loc, fr.func->name, "");
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    int64_t bytes = vm.eval(fr, in.operands[0]).i64;
    if (bytes < 0)
    {
        RuntimeBridge::trap(TrapKind::DomainError, "negative allocation", in.loc, fr.func->name, "");
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    const size_t size = static_cast<size_t>(bytes);
    const size_t addr = fr.sp;
    const size_t stackSize = fr.stack.size();
    constexpr size_t alignment = alignof(std::max_align_t);

    auto trapOverflow = [&]() -> VM::ExecResult {
        RuntimeBridge::trap(TrapKind::Overflow, "stack overflow in alloca", in.loc, fr.func->name, "");
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

/// Loads a value from memory into a temporary according to the instruction's
/// type annotation.
///
/// The handler evaluates the source pointer operand, trapping when it is
/// null, and then performs a type-directed `switch` to reinterpret the
/// pointed-to bytes. Callers must ensure the pointer references a live object
/// of the declared type; violating this breaks pointer provenance invariants
/// and is undefined from the VM's perspective.
///
/// @param vm Virtual machine providing operand evaluation.
/// @param fr Current frame receiving the loaded slot.
/// @param in Instruction describing the pointer operand and result type.
VM::ExecResult OpHandlers::handleLoad(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    auto state = makeLegacyState(vm, fr, blocks, bb, ip);
    return handleLoadInline(vm, state, in);
}

/// Stores a value to memory using type-directed semantics and triggers debug
/// notifications for observable writes.
///
/// The handler evaluates the destination pointer and value operands, trapping
/// when the pointer is null before dispatching on the instruction type. The
/// type switch mirrors `handleLoad` to preserve load/store symmetry. After the
/// write, any named temporaries route through `vm.debug.onStore`, allowing
/// tracing hooks to observe the operation.
///
/// @param vm Virtual machine used to evaluate operands and access debug hooks.
/// @param fr Active frame supplying locals and metadata.
/// @param in Instruction describing the destination pointer, value, and type.
VM::ExecResult OpHandlers::handleStore(VM &vm,
                                       Frame &fr,
                                       const Instr &in,
                                       const VM::BlockMap &blocks,
                                       const BasicBlock *&bb,
                                       size_t &ip)
{
    auto state = makeLegacyState(vm, fr, blocks, bb, ip);
    return handleStoreInline(vm, state, in);
}

/// Computes a pointer by applying a byte offset to a base address, emulating
/// the IL `gep` semantics.
///
/// The base pointer and offset operands are evaluated and combined with
/// byte-level pointer arithmetic. Callers are responsible for ensuring the
/// resulting pointer remains within the same allocation; violating that
/// invariant undermines stack discipline and may be trapped by later loads.
///
/// @param vm Virtual machine used for operand evaluation.
/// @param fr Active frame that stores the computed pointer.
/// @param in Instruction providing the base pointer and offset.
VM::ExecResult OpHandlers::handleGEP(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot base = vm.eval(fr, in.operands[0]);
    Slot offset = vm.eval(fr, in.operands[1]);
    Slot out{};
    out.ptr = static_cast<char *>(base.ptr) + offset.i64;
    ops::storeResult(fr, in, out);
    return {};
}

/// Reifies the address of a runtime string constant for use in pointer-typed
/// temporaries.
///
/// The handler forwards the runtime string pointer stored in the operand slot.
/// The operand must already represent an immutable string managed by the
/// runtime; consumers must not mutate through the resulting pointer.
///
/// @param vm Virtual machine context.
/// @param fr Frame receiving the pointer value.
/// @param in Instruction referencing the source string slot.
VM::ExecResult OpHandlers::handleAddrOf(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot tmp = vm.eval(fr, in.operands[0]);
    Slot out{};
    out.ptr = tmp.str;
    ops::storeResult(fr, in, out);
    return {};
}

/// Materializes a constant string slot by forwarding the evaluated operand.
///
/// The handler assumes the operand already yields a pointer or tagged runtime
/// string object and simply stores it in the destination without further
/// transformation.
///
/// @param vm Virtual machine context.
/// @param fr Frame storing the resulting slot.
/// @param in Instruction whose first operand encodes the constant string.
VM::ExecResult OpHandlers::handleConstStr(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot out = vm.eval(fr, in.operands[0]);
    ops::storeResult(fr, in, out);
    return {};
}

/// Materializes a null constant for pointer-like handle types.
///
/// The handler writes a zero value into the slot corresponding to the
/// instruction's result type. Pointer-oriented kinds (Ptr, Error, ResumeTok)
/// use the generic pointer field, while string handles clear the runtime
/// string slot. Other kinds fall back to the pointer field to preserve the
/// historical behaviour for mis-specified instructions.
VM::ExecResult OpHandlers::handleConstNull(VM &vm,
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
        case Type::Kind::Str:
            out.str = nullptr;
            break;
        case Type::Kind::Error:
        case Type::Kind::ResumeTok:
        case Type::Kind::Ptr:
            out.ptr = nullptr;
            break;
        default:
            out.ptr = nullptr;
            break;
    }

    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail

