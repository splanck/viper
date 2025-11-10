//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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
#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/tco.hpp"

/// @file
/// @brief Call and return opcode handlers for the VM interpreter.
/// @details These helpers provide the glue between IL call/return instructions
///          and the VM's execution environment.  Calls eagerly evaluate
///          arguments, dispatch to either VM-native functions or the runtime
///          bridge, synchronise mutated arguments back into registers/stack, and
///          write the result slot.  Returns capture the optional operand and
///          signal to the interpreter loop that unwinding should begin.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

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

    struct ArgBinding
    {
        Slot *reg = nullptr;
        uint8_t *stackPtr = nullptr;
    };

    // Evaluate operands up front so argument propagation is explicit and
    // deterministic before dispatch.  This mirrors the IL semantics and avoids
    // leaking partially evaluated slots if a bridge call traps.  The original
    // values are preserved so post-call synchronisation can detect mutations.
    std::vector<Slot> args;
    args.reserve(in.operands.size());
    std::vector<Slot> originalArgs;
    originalArgs.reserve(in.operands.size());
    std::vector<ArgBinding> bindings;
    bindings.reserve(in.operands.size());

    uint8_t *const stackBegin = fr.stack.data();
    uint8_t *const stackEnd = stackBegin + fr.stack.size();

    for (const auto &op : in.operands)
    {
        Slot value = VMAccess::eval(vm, fr, op);

        ArgBinding binding{};
        if (op.kind == il::core::Value::Kind::Temp && op.id < fr.regs.size())
            binding.reg = &fr.regs[op.id];

        if (value.ptr)
        {
            auto *ptr = static_cast<uint8_t *>(value.ptr);
            if (ptr >= stackBegin && ptr < stackEnd)
                binding.stackPtr = ptr;
        }

        bindings.push_back(binding);
        originalArgs.push_back(value);
        args.push_back(value);
    }

    Slot out{};
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
                if (il::vm::tryTailCall(vm, it->second, std::span<const Slot>{args}))
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
        const std::string functionName = fr.func ? fr.func->name : std::string{};
        const std::string blockLabel = bb ? bb->label : std::string{};
        out = RuntimeBridge::call(
            VMAccess::runtimeContext(vm), in.callee, args, in.loc, functionName, blockLabel);

        const auto *signature = il::runtime::findRuntimeSignature(in.callee);
        if (signature)
        {
            const size_t paramCount = std::min(args.size(), signature->paramTypes.size());
            for (size_t index = 0; index < paramCount; ++index)
            {
                if (std::memcmp(&args[index], &originalArgs[index], sizeof(Slot)) == 0)
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
                    if (width != 0 && binding.stackPtr >= stackBegin &&
                        binding.stackPtr + width <= stackEnd)
                    {
                        Slot &mutated = args[index];
                        if (kind == il::core::Type::Kind::Str)
                        {
                            auto *slot = reinterpret_cast<rt_string *>(binding.stackPtr);
                            rt_str_release_maybe(*slot);
                            rt_string incoming = mutated.str;
                            rt_str_retain_maybe(incoming);
                            *slot = incoming;
                        }
                        else
                        {
                            void *src = il::vm::slotToArgPointer(mutated, kind);
                            if (src)
                                std::memcpy(binding.stackPtr, src, width);
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
    if (!in.result && in.type.kind == il::core::Type::Kind::Str && out.str)
        rt_str_release_maybe(out.str);
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail::control
