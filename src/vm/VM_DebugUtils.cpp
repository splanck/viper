// File: src/vm/VM_DebugUtils.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements VM helper utilities for opcode naming and trap diagnostics.
// Key invariants: Trap metadata updates mirror execution context to preserve
//                 accurate pause/resume behaviour.
// Ownership/Lifetime: Utilities operate on VM state owned elsewhere.
// Links: docs/il-guide.md#reference

#include "vm/VM.hpp"
#include "il/core/Function.hpp"
#include "il/core/OpcodeInfo.hpp"
#include <algorithm>
#include <string>

namespace il::vm
{
namespace
{
using il::core::kNumOpcodes;
using il::core::getOpcodeInfo;
}

std::string opcodeMnemonic(il::core::Opcode op)
{
    const size_t index = static_cast<size_t>(op);
    if (index < kNumOpcodes)
    {
        const auto &info = getOpcodeInfo(op);
        if (info.name && info.name[0] != '\0')
            return info.name;
    }
    return std::string("opcode#") + std::to_string(static_cast<int>(op));
}

std::optional<std::string> VM::lastTrapMessage() const
{
    if (lastTrap.message.empty())
        return std::nullopt;
    return lastTrap.message;
}

FrameInfo VM::buildFrameInfo(const VmError &error) const
{
    FrameInfo frame{};
    if (currentContext.function)
        frame.function = currentContext.function->name;
    else if (!runtimeContext.function.empty())
        frame.function = runtimeContext.function;
    else if (!lastTrap.frame.function.empty())
        frame.function = lastTrap.frame.function;

    frame.ip = error.ip;
    if (frame.ip == 0 && currentContext.hasInstruction)
        frame.ip = static_cast<uint64_t>(currentContext.instructionIndex);
    else if (frame.ip == 0 && lastTrap.frame.ip != 0)
        frame.ip = lastTrap.frame.ip;

    frame.line = error.line;
    if (frame.line < 0 && currentContext.loc.isValid())
        frame.line = static_cast<int32_t>(currentContext.loc.line);
    else if (frame.line < 0 && runtimeContext.loc.isValid())
        frame.line = static_cast<int32_t>(runtimeContext.loc.line);
    else if (frame.line < 0 && lastTrap.frame.line >= 0)
        frame.line = lastTrap.frame.line;

    frame.handlerInstalled = std::any_of(execStack.begin(), execStack.end(), [](const ExecState *st) {
        return st && !st->fr.ehStack.empty();
    });
    return frame;
}

std::string VM::recordTrap(const VmError &error, const FrameInfo &frame)
{
    lastTrap.error = error;
    lastTrap.frame = frame;
    lastTrap.message = vm_format_error(error, frame);
    if (!runtimeContext.message.empty())
    {
        lastTrap.message += ": ";
        lastTrap.message += runtimeContext.message;
        runtimeContext.message.clear();
    }
    return lastTrap.message;
}

} // namespace il::vm
