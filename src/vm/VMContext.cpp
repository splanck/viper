// File: src/vm/VMContext.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements helper utilities for interacting with VM execution state and
//          trap context shared across dispatch strategies.
// Key invariants: Maintains accurate execution context for trap reporting and
//                 ensures evaluation helpers respect frame register bounds.
// Ownership/Lifetime: Operates on VM instances owned externally; no ownership of
//                     runtime resources.
// Links: docs/il-guide.md#reference

#include "vm/VMContext.hpp"

#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "il/core/Function.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"

#include <exception>
#include <sstream>
#include <string>

namespace il::vm
{
namespace
{
thread_local VM *tlsActiveVM = nullptr; ///< Active VM for trap reporting.

std::string opcodeMnemonic(il::core::Opcode op)
{
    const size_t index = static_cast<size_t>(op);
    if (index < il::core::kNumOpcodes)
    {
        const auto &info = getOpcodeInfo(op);
        if (info.name && info.name[0] != '\0')
            return info.name;
    }
    return std::string("opcode#") + std::to_string(static_cast<int>(op));
}

} // namespace

ActiveVMGuard::ActiveVMGuard(VM *vm) : previous(tlsActiveVM)
{
    tlsActiveVM = vm;
}

ActiveVMGuard::~ActiveVMGuard()
{
    tlsActiveVM = previous;
}

VMContext::VMContext(VM &vm) noexcept : vmInstance(&vm) {}

Slot VMContext::eval(Frame &fr, const il::core::Value &value) const
{
    Slot slot{};
    switch (value.kind)
    {
        case il::core::Value::Kind::Temp:
        {
            if (value.id < fr.regs.size())
                return fr.regs[value.id];

            const std::string fnName = fr.func ? fr.func->name : std::string("<unknown>");
            const il::core::BasicBlock *block = vmInstance->currentContext.block;
            const std::string blockLabel = block ? block->label : std::string();
            const auto loc = vmInstance->currentContext.loc;

            std::ostringstream os;
            os << "temp %" << value.id << " out of range (regs=" << fr.regs.size() << ") in function " << fnName;
            if (!blockLabel.empty())
                os << ", block " << blockLabel;
            if (loc.isValid())
            {
                os << ", at line " << loc.line;
                if (loc.column > 0)
                    os << ':' << loc.column;
            }
            else
            {
                os << ", at unknown location";
            }

            RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), loc, fnName, blockLabel);
            return slot;
        }
        case il::core::Value::Kind::ConstInt:
            slot.i64 = toI64(value);
            return slot;
        case il::core::Value::Kind::ConstFloat:
            slot.f64 = toF64(value);
            return slot;
        case il::core::Value::Kind::ConstStr:
        {
            if (value.str.find('\0') == std::string::npos)
            {
                slot.str = rt_const_cstr(value.str.c_str());
                return slot;
            }

            auto [it, inserted] = vmInstance->inlineLiteralCache.try_emplace(value.str);
            if (inserted)
                it->second = rt_string_from_bytes(value.str.data(), value.str.size());
            slot.str = it->second;
            return slot;
        }
        case il::core::Value::Kind::GlobalAddr:
        {
            auto it = vmInstance->strMap.find(value.str);
            if (it == vmInstance->strMap.end())
                RuntimeBridge::trap(TrapKind::DomainError, "unknown global", {}, fr.func->name, "");
            else
                slot.str = it->second;
            return slot;
        }
        case il::core::Value::Kind::NullPtr:
            slot.ptr = nullptr;
            return slot;
    }
    return slot;
}

std::optional<Slot> VMContext::stepOnce(VM::ExecState &state) const
{
    vmInstance->beginDispatch(state);

    const il::core::Instr *instr = nullptr;
    if (!vmInstance->selectInstruction(state, instr))
        return state.pendingResult;

    vmInstance->traceInstruction(*instr, state.fr);
    auto result = vmInstance->executeOpcode(state.fr, *instr, state.blocks, state.bb, state.ip);
    if (vmInstance->finalizeDispatch(state, result))
        return state.pendingResult;

    return std::nullopt;
}

bool VMContext::handleTrapDispatch(const VM::TrapDispatchSignal &signal, VM::ExecState &state) const
{
    if (signal.target != &state)
        return false;
    vmInstance->clearCurrentContext();
    return true;
}

il::core::Opcode VMContext::fetchOpcode(VM::ExecState &state) const
{
    vmInstance->beginDispatch(state);

    const il::core::Instr *instr = nullptr;
    if (!vmInstance->selectInstruction(state, instr))
        return instr ? instr->op : il::core::Opcode::Trap;

    return instr->op;
}

void VMContext::handleInlineResult(VM::ExecState &state, const VM::ExecResult &exec) const
{
    vmInstance->finalizeDispatch(state, exec);
}

[[noreturn]] void VMContext::trapUnimplemented(il::core::Opcode opcode) const
{
    const std::string funcName = vmInstance->currentContext.function ? vmInstance->currentContext.function->name : std::string("<unknown>");
    const std::string blockLabel = vmInstance->currentContext.block ? vmInstance->currentContext.block->label : std::string();
    RuntimeBridge::trap(TrapKind::InvalidOperation,
                        "unimplemented opcode: " + opcodeMnemonic(opcode),
                        vmInstance->currentContext.loc,
                        funcName,
                        blockLabel);
    std::terminate();
}

void VMContext::traceStep(const il::core::Instr &instr, Frame &frame) const
{
    vmInstance->traceInstruction(instr, frame);
}

VM::ExecResult VMContext::executeOpcode(Frame &frame,
                                        const il::core::Instr &instr,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip) const
{
    return vmInstance->executeOpcode(frame, instr, blocks, bb, ip);
}

void VMContext::clearCurrentContext() const
{
    vmInstance->clearCurrentContext();
}

TraceSink &VMContext::traceSink() const noexcept
{
    return vmInstance->tracer;
}

DebugCtrl &VMContext::debugController() const noexcept
{
    return vmInstance->debug;
}

VM &VMContext::vm() const noexcept
{
    return *vmInstance;
}

VM *activeVMInstance()
{
    return tlsActiveVM;
}

Slot VM::eval(Frame &fr, const il::core::Value &value)
{
    VMContext ctx(*this);
    return ctx.eval(fr, value);
}

std::optional<Slot> VM::stepOnce(ExecState &state)
{
    VMContext ctx(*this);
    return ctx.stepOnce(state);
}

bool VM::handleTrapDispatch(const TrapDispatchSignal &signal, ExecState &state)
{
    VMContext ctx(*this);
    return ctx.handleTrapDispatch(signal, state);
}

il::core::Opcode VM::fetchOpcode(ExecState &state)
{
    VMContext ctx(*this);
    return ctx.fetchOpcode(state);
}

void VM::handleInlineResult(ExecState &state, const ExecResult &exec)
{
    VMContext ctx(*this);
    ctx.handleInlineResult(state, exec);
}

[[noreturn]] void VM::trapUnimplemented(il::core::Opcode opcode)
{
    VMContext ctx(*this);
    ctx.trapUnimplemented(opcode);
}

VM *VM::activeInstance()
{
    return activeVMInstance();
}

} // namespace il::vm
