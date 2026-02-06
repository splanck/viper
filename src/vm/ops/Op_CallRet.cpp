//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the VM opcode handlers that manage function calls and returns.
// The helpers evaluate operands, interact with the runtime bridge when the VM
// lacks a native implementation, and propagate return values back into the
// interpreter's frame.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Control.hpp"

#include "il/runtime/RuntimeSignatures.hpp"
#include "runtime/rt.hpp" // For fast-path runtime function calls
#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/ViperStringHandle.hpp"
#include "vm/tco.hpp"

/// @file
/// @brief Call and return opcode handlers for the VM interpreter.
/// @details These helpers provide the glue between IL call/return instructions
///          and the VM's execution environment.  Calls eagerly evaluate
///          arguments, dispatch to either VM-native functions or the runtime
///          bridge, synchronise mutated arguments back into registers/stack, and
///          write the result slot.  Returns capture the optional operand and
///          signal to the interpreter loop that unwinding should begin.

#include "support/small_vector.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <unordered_map>

// SmallArgBuffer: Uses SmallVector<Slot, 8> to avoid heap allocation for most function
// calls (those with <=8 arguments). The APIs now accept std::span<const Slot>.

namespace il::vm::detail::control
{
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
    il::support::SmallVector<Slot, 8> args;
    args.reserve(in.operands.size());
    for (const auto &op : in.operands)
        args.push_back(VMAccess::eval(vm, fr, op));

    Slot out{};
    // Guard to release any string result if not consumed (e.g., on exception or unused result)
    const bool isStringResult = (in.type.kind == il::core::Type::Kind::Str);
    ScopedSlotStringGuard outGuard(out.str, isStringResult);

    const auto &fnMap = VMAccess::functionMap(vm);
    auto it = fnMap.find(in.callee);
    if (it != fnMap.end())
    {
        // Tail-call optimisation: call immediately followed by ret
#if defined(VIPER_VM_TAILCALL) && VIPER_VM_TAILCALL
        if (bb && (ip + 1) < bb->instructions.size())
        {
            const auto &nextInstr = bb->instructions[ip + 1];
            if (nextInstr.op == il::core::Opcode::Ret)
            {
                // Only apply TCO if return types are compatible:
                // - If ret has no operand, callee must return void
                // - If ret has operand, callee must return non-void (value will be propagated)
                const bool retHasOperand = !nextInstr.operands.empty();
                const bool calleeReturnsVoid =
                    (it->second->retType.kind == il::core::Type::Kind::Void);
                const bool compatibleReturn = (retHasOperand == !calleeReturnsVoid);

                if (compatibleReturn &&
                    il::vm::tryTailCall(vm, it->second, std::span<const Slot>{args}))
                {
                    VM::ExecResult r{};
                    r.jumped = true; // prevent ip++ so entry ip=0 is executed
                    return r;
                }
            }
        }
#endif
        out = VMAccess::callFunction(vm, *it->second, args);
    }
    else
    {
        // =========================================================================
        // FAST PATH: Direct calls for hot runtime functions
        // =========================================================================
        // These functions are called frequently in game loops. Bypassing the
        // RuntimeBridge eliminates descriptor lookup, argument marshalling, and
        // context guard overhead - typically ~10-20x faster.
        // Uses a static hash map for O(1) lookup instead of sequential string
        // comparisons.

        enum class FastPathId : uint8_t
        {
            InkeyStr,
            TermLocate,
            TermColor,
            TermCls,
            TimerMs,
            SleepMs,
            Keypressed,
            TermAltScreen,
            TermCursorVisible
        };

        struct SvHash
        {
            using is_transparent = void;

            size_t operator()(std::string_view sv) const
            {
                return std::hash<std::string_view>{}(sv);
            }
        };

        static const std::unordered_map<std::string_view, FastPathId> kFastPathMap = {
            {"rt_inkey_str", FastPathId::InkeyStr},
            {"rt_term_locate_i32", FastPathId::TermLocate},
            {"rt_term_color_i32", FastPathId::TermColor},
            {"rt_term_cls", FastPathId::TermCls},
            {"rt_timer_ms", FastPathId::TimerMs},
            {"rt_sleep_ms", FastPathId::SleepMs},
            {"rt_keypressed", FastPathId::Keypressed},
            {"rt_term_alt_screen_i32", FastPathId::TermAltScreen},
            {"rt_term_cursor_visible_i32", FastPathId::TermCursorVisible},
        };

        auto fpIt = kFastPathMap.find(std::string_view(in.callee));
        if (fpIt != kFastPathMap.end())
        {
            bool handled = true;
            switch (fpIt->second)
            {
                case FastPathId::InkeyStr:
                    out.str = rt_inkey_str();
                    break;
                case FastPathId::TermLocate:
                    if (args.size() >= 2)
                        rt_term_locate_i32(static_cast<int32_t>(args[0].i64),
                                           static_cast<int32_t>(args[1].i64));
                    else
                        handled = false;
                    break;
                case FastPathId::TermColor:
                    if (args.size() >= 2)
                        rt_term_color_i32(static_cast<int32_t>(args[0].i64),
                                          static_cast<int32_t>(args[1].i64));
                    else
                        handled = false;
                    break;
                case FastPathId::TermCls:
                    rt_term_cls();
                    break;
                case FastPathId::TimerMs:
                    out.i64 = rt_timer_ms();
                    break;
                case FastPathId::SleepMs:
                    if (args.size() >= 1)
                        rt_sleep_ms(static_cast<int32_t>(args[0].i64));
                    else
                        handled = false;
                    break;
                case FastPathId::Keypressed:
                    out.i64 = rt_keypressed();
                    break;
                case FastPathId::TermAltScreen:
                    if (args.size() >= 1)
                        rt_term_alt_screen_i32(static_cast<int32_t>(args[0].i64));
                    else
                        handled = false;
                    break;
                case FastPathId::TermCursorVisible:
                    if (args.size() >= 1)
                        rt_term_cursor_visible_i32(static_cast<int32_t>(args[0].i64));
                    else
                        handled = false;
                    break;
            }
            if (handled)
            {
                ops::storeResult(fr, in, out);
                return {};
            }
        }

        // End of fast path - fall through to generic RuntimeBridge
        // =========================================================================

        // Function and block names for error reporting
        const std::string functionName = fr.func ? fr.func->name : std::string{};
        const std::string blockLabel = bb ? bb->label : std::string{};

        // Build bindings and original values lazily only for runtime calls
        // Use SmallVector to avoid heap allocation for typical argument counts
        struct ArgBinding
        {
            Slot *reg = nullptr;
            uint8_t *stackPtr = nullptr;
        };

        il::support::SmallVector<ArgBinding, 8> bindings;
        bindings.reserve(in.operands.size());
        il::support::SmallVector<Slot, 8> originalArgs;
        originalArgs.reserve(in.operands.size());

        uint8_t *const stackBegin = fr.stack.data();
        uint8_t *const stackEnd = stackBegin + fr.stack.size();

        for (size_t i = 0; i < in.operands.size(); ++i)
        {
            const auto &op = in.operands[i];
            ArgBinding binding{};
            if (op.kind == il::core::Value::Kind::Temp && op.id < fr.regs.size())
                binding.reg = &fr.regs[op.id];
            if (args[i].ptr)
            {
                auto *ptr = static_cast<uint8_t *>(args[i].ptr);
                if (ptr >= stackBegin && ptr < stackEnd)
                    binding.stackPtr = ptr;
            }
            bindings.push_back(binding);
            originalArgs.push_back(args[i]);
        }

        out = RuntimeBridge::call(VMAccess::runtimeContext(vm),
                                  std::string_view(in.callee),
                                  std::span<const Slot>{args},
                                  in.loc,
                                  functionName,
                                  blockLabel);

        const auto *signature = il::runtime::findRuntimeSignature(in.callee);
        if (signature)
        {
            const size_t paramCount = std::min(args.size(), signature->paramTypes.size());
            for (size_t index = 0; index < paramCount; ++index)
            {
                // Compare slots using bitwise equality (safe for all types)
                if (args[index].bitwiseEquals(originalArgs[index]))
                    continue;

                const auto kind = signature->paramTypes[index].kind;
                const ArgBinding &binding = bindings[index];
                const bool releaseTempStr = (kind == il::core::Type::Kind::Str);

                auto assignRegister = [&](Slot *destination)
                {
                    if (!destination)
                        return;
                    if (kind == il::core::Type::Kind::Str)
                    {
                        rt_str_release_maybe(destination->str);
                        Slot stored = args[index];
                        rt_str_retain_maybe(stored.str);
                        *destination = stored;
                    }
                    else
                    {
                        *destination = args[index];
                    }
                };

                assignRegister(binding.reg);

                if (binding.stackPtr)
                {
                    auto copyWidthForKind = [](il::core::Type::Kind k) -> size_t
                    {
                        switch (k)
                        {
                            case il::core::Type::Kind::I1:
                                return sizeof(uint8_t);
                            case il::core::Type::Kind::I16:
                                return sizeof(int16_t);
                            case il::core::Type::Kind::I32:
                                return sizeof(int32_t);
                            case il::core::Type::Kind::I64:
                                return sizeof(int64_t);
                            case il::core::Type::Kind::F64:
                                return sizeof(double);
                            case il::core::Type::Kind::Ptr:
                            case il::core::Type::Kind::Error:
                            case il::core::Type::Kind::ResumeTok:
                                return sizeof(void *);
                            case il::core::Type::Kind::Str:
                                return sizeof(rt_string);
                            case il::core::Type::Kind::Void:
                                return 0;
                        }
                        return 0;
                    };

                    const size_t width = copyWidthForKind(kind);
                    if (width != 0 && binding.stackPtr >= stackBegin && binding.stackPtr < stackEnd)
                    {
                        const auto stackPtrAddr =
                            reinterpret_cast<std::uintptr_t>(binding.stackPtr);
                        const auto stackEndAddr = reinterpret_cast<std::uintptr_t>(stackEnd);
                        if (stackEndAddr - stackPtrAddr >= width)
                        {
                            Slot &mutated = args[index];
                            if (kind == il::core::Type::Kind::Str)
                            {
                                // Avoid strict-aliasing UB: copy via memcpy
                                rt_string current{};
                                std::memcpy(&current, binding.stackPtr, sizeof(current));
                                rt_str_release_maybe(current);
                                rt_string incoming = mutated.str;
                                rt_str_retain_maybe(incoming);
                                std::memcpy(binding.stackPtr, &incoming, sizeof(incoming));
                            }
                            else if (void *src = il::vm::slotToArgPointer(mutated, kind))
                            {
                                std::memcpy(binding.stackPtr, src, width);
                            }
                        }
                    }
                }

                if (releaseTempStr)
                {
                    rt_str_release_maybe(args[index].str);
                    args[index].str = nullptr;
                }
            }
        }
    }
    // If there's a result destination, storeResult will take ownership; dismiss the guard.
    // If no result destination, the guard will release the string on scope exit.
    if (in.result)
        outGuard.dismiss();
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Indirect call via a callee operand (global name or function pointer).
///
/// @details The first operand supplies either a GlobalAddr (callee identifier)
///          or a pointer value produced by loads (e.g., from an itable). When a
///          name is provided, behaviour mirrors direct calls. When a function
///          pointer is provided, the pointer must reference an IL function
///          instance and the VM invokes it directly. Remaining operands (if
///          any) are treated as arguments.
VM::ExecResult handleCallIndirect(VM &vm,
                                  Frame &fr,
                                  const il::core::Instr &in,
                                  const VM::BlockMap &blocks,
                                  const il::core::BasicBlock *&bb,
                                  size_t &ip)
{
    (void)blocks;
    (void)ip;

    VM::ExecResult result{};
    if (in.operands.empty())
        return result;

    // Operand 0 may be a GlobalAddr or a function pointer value.
    const il::core::Value &calleeVal = in.operands[0];
    Slot out{};
    // Guard to release any string result if not consumed (e.g., on exception or unused result)
    const bool isStringResult = (in.type.kind == il::core::Type::Kind::Str);
    ScopedSlotStringGuard outGuard(out.str, isStringResult);

    if (calleeVal.kind == il::core::Value::Kind::GlobalAddr)
    {
        std::string calleeName = calleeVal.str;
        il::support::SmallVector<Slot, 8> args;
        if (in.operands.size() > 1)
        {
            args.reserve(in.operands.size() - 1);
            for (size_t i = 1; i < in.operands.size(); ++i)
                args.push_back(VMAccess::eval(vm, fr, in.operands[i]));
        }

        const auto &fnMap = VMAccess::functionMap(vm);
        auto it = fnMap.find(calleeName);
        if (it != fnMap.end())
        {
            out = VMAccess::callFunction(vm, *it->second, args);
        }
        else
        {
            const std::string functionName = fr.func ? fr.func->name : std::string{};
            const std::string blockLabel = bb ? bb->label : std::string{};
            out = RuntimeBridge::call(VMAccess::runtimeContext(vm),
                                      std::string_view(calleeName),
                                      std::span<const Slot>{args},
                                      in.loc,
                                      functionName,
                                      blockLabel);
        }
    }
    else
    {
        // Pointer-based indirect call
        Slot callee = VMAccess::eval(vm, fr, calleeVal);
        if (!callee.ptr)
        {
            const std::string blockLabel = bb ? bb->label : std::string{};
            RuntimeBridge::trap(TrapKind::InvalidOperation,
                                "null indirect callee",
                                in.loc,
                                fr.func ? fr.func->name : std::string(),
                                blockLabel);
            VM::ExecResult res{};
            res.returned = true;
            return res;
        }
        const auto *fn = reinterpret_cast<const il::core::Function *>(callee.ptr);
        il::support::SmallVector<Slot, 8> args;
        if (in.operands.size() > 1)
        {
            args.reserve(in.operands.size() - 1);
            for (size_t i = 1; i < in.operands.size(); ++i)
                args.push_back(VMAccess::eval(vm, fr, in.operands[i]));
        }
        out = VMAccess::callFunction(vm, *fn, args);
    }

    // If there's a result destination, storeResult will take ownership; dismiss the guard.
    // If no result destination, the guard will release the string on scope exit.
    if (in.result)
        outGuard.dismiss();
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail::control
