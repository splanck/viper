//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/VM_DebugUtils.cpp
// Purpose: Implements VM helper utilities for opcode naming and trap diagnostics.
// Key invariants: Trap metadata updates mirror execution context to preserve
//                 accurate pause/resume behaviour.
// Ownership/Lifetime: Utilities operate on VM state owned elsewhere.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

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

/// @brief Produce a human-readable mnemonic for an opcode.
/// @details Queries the opcode metadata table and falls back to a synthetic
///          `opcode#NN` string when no mnemonic is available.  Debug tools use
///          the helper to avoid duplicating lookup code.
/// @param op Opcode to describe.
/// @return Mnemonic string suitable for diagnostics and tracing.
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

/// @brief Retrieve the cached message from the most recent trap.
/// @details Returns a disengaged optional when no trap has been recorded.
///          Callers can surface the message to users without mutating the VM
///          state.
/// @return Optional containing the last trap message when present.
std::optional<std::string> VM::lastTrapMessage() const
{
    if (lastTrap.message.empty())
        return std::nullopt;
    return lastTrap.message;
}

/// @brief Assemble stack frame information associated with an error.
/// @details Chooses the most relevant function, instruction pointer, and line
///          metadata from the current execution context, runtime state, and
///          cached trap record.  The resulting structure feeds diagnostic
///          printers and debugger views.
/// @param error Error describing the failing state.
/// @return Frame snapshot capturing function name, IP, line, and EH metadata.
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

/// @brief Update the VM's trap cache and render the diagnostic string.
/// @details Copies @p error and @p frame into the cached trap state, invokes
///          the formatting helper, and appends any queued runtime-context
///          message.  The fully composed string is returned for logging.
/// @param error Error raised by the VM execution engine.
/// @param frame Frame snapshot describing where the trap occurred.
/// @return Human-readable trap message stored in the cache.
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
