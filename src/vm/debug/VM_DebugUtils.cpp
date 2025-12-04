//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/VM_DebugUtils.cpp
// Purpose: Provide VM-side helpers for opcode mnemonics and trap diagnostics.
// Key invariants: Diagnostic caches mirror the most recent execution context so
//                 debugger output remains coherent across pause/resume cycles.
// Ownership/Lifetime: Functions mutate VM-owned tracking structures in place
//                     without allocating persistent external state.
// Links: docs/runtime-vm.md#diagnostics
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief VM debugging utilities for opcode and trap reporting.
/// @details Provides convenience helpers for translating opcodes into readable
///          mnemonics, exposing trap messages, and synthesising frame summaries
///          when the VM encounters errors.  These functions are deliberately kept
///          out-of-line to keep the main VM implementation focused on execution
///          semantics.

#include "il/core/Function.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <string>

namespace il::vm
{
namespace
{
using il::core::getOpcodeInfo;
using il::core::kNumOpcodes;
} // namespace

/// @brief Translate an opcode to a printable mnemonic.
/// @details Consults the opcode metadata table and returns the canonical name
///          when available, falling back to a numeric placeholder when metadata
///          is missing.  Keeps debugger output stable even for unrecognised
///          opcodes.
/// @param op Opcode enumerator to translate.
/// @return String mnemonic or numeric placeholder.
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

/// @brief Retrieve the most recent trap message recorded by the VM.
/// @details Returns an optional containing the cached trap message when one is
///          available; otherwise @c std::nullopt so callers can distinguish
///          between "no trap" and "empty string" cases.
/// @return Optional string describing the last trap.
std::optional<std::string> VM::lastTrapMessage() const
{
    if (lastTrap.message.empty())
        return std::nullopt;
    return lastTrap.message;
}

/// @brief Clear stale trap state before a new execution.
/// @details Resets lastTrap, trapToken, and runtimeContext message so
///          subsequent executions start with a clean slate.
void VM::clearTrapState()
{
    lastTrap.error = {};
    lastTrap.frame = {};
    lastTrap.message.clear();
    trapToken.error = {};
    trapToken.message.clear();
    trapToken.valid = false;
    runtimeContext.message.clear();
}

/// @brief Construct a diagnostic frame snapshot for a VM error.
/// @details Aggregates function name, block label, instruction index, and source
///          location by consulting current execution context, runtime context,
///          and cached trap state.  The helper prefers freshly available data but
///          falls back to previously recorded information when necessary, ensuring
///          that debugger output always contains best-effort metadata.
/// @param error Error descriptor reported by the VM core.
/// @return Populated frame summary describing the failing execution point.
FrameInfo VM::buildFrameInfo(const VmError &error) const
{
    FrameInfo frame{};

    // Function name: prefer current context, then runtime context, then cached
    if (currentContext.function)
        frame.function = currentContext.function->name;
    else if (!runtimeContext.function.empty())
        frame.function = runtimeContext.function;
    else if (!lastTrap.frame.function.empty())
        frame.function = lastTrap.frame.function;

    // Block label: prefer current execution state, then runtime context, then cached
    if (!execStack.empty() && execStack.back() && execStack.back()->bb)
        frame.block = execStack.back()->bb->label;
    else if (!runtimeContext.block.empty())
        frame.block = runtimeContext.block;
    else if (!lastTrap.frame.block.empty())
        frame.block = lastTrap.frame.block;

    // Instruction pointer
    frame.ip = error.ip;
    if (frame.ip == 0 && currentContext.hasInstruction)
        frame.ip = static_cast<uint64_t>(currentContext.instructionIndex);
    else if (frame.ip == 0 && lastTrap.frame.ip != 0)
        frame.ip = lastTrap.frame.ip;

    // Source line
    frame.line = error.line;
    if (frame.line < 0 && currentContext.loc.hasLine())
        frame.line = static_cast<int32_t>(currentContext.loc.line);
    else if (frame.line < 0 && runtimeContext.loc.hasLine())
        frame.line = static_cast<int32_t>(runtimeContext.loc.line);
    else if (frame.line < 0 && lastTrap.frame.line >= 0)
        frame.line = lastTrap.frame.line;

    // Check if any handler is installed
    frame.handlerInstalled =
        std::any_of(execStack.begin(),
                    execStack.end(),
                    [](const ExecState *st) { return st && !st->fr.ehStack.empty(); });
    return frame;
}

/// @brief Cache details about the latest trap and return its message.
/// @details Stores the provided error and frame information, recomputes the user
///          facing message via @ref vm_format_error, and appends any pending
///          runtime-context message.  The combined message is cached for future
///          retrieval via @ref lastTrapMessage.
/// @param error Error descriptor raised by the VM.
/// @param frame Frame information produced by @ref buildFrameInfo.
/// @return The formatted trap message stored in the VM state.
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
