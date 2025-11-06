//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/OpHelpers.cpp
// Purpose: Implement the shared trap helpers declared in OpHelpers.hpp.
// Key invariants: Diagnostics always provide function and block context when
//                 available, route through the runtime bridge, and never throw
//                 exceptions across the VM boundary.
// Ownership/Lifetime: Operates entirely on VM-managed state without persisting
//                     data beyond the duration of the helper call.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements error-reporting helpers shared across VM opcode handlers.
/// @details Centralises the plumbing that bridges interpreter failures to the
///          runtime trap interface so that individual opcode handlers remain
///          focused on semantics while still delivering consistent diagnostics.

#include "viper/vm/internal/OpHelpers.hpp"

#include "il/core/Function.hpp"

#include <string>

namespace il::vm::internal::detail
{
/// @brief Raise a runtime trap with contextual information about the faulting instruction.
/// @details Collects the current function and block labels (when available) and
///          forwards them to @ref RuntimeBridge::trap alongside the source
///          location stored on @p instr.  Keeping this logic in one helper keeps
///          opcode handlers terse and ensures all traps carry consistent
///          metadata.
/// @param kind Trap category describing the failure.
/// @param message Human-readable explanation of the trap.
/// @param instr Instruction that triggered the trap; its source location is forwarded.
/// @param frame Execution frame owning the current function pointer.
/// @param block Optional basic block providing additional context.
void trapWithMessage(TrapKind kind,
                     const char *message,
                     const il::core::Instr &instr,
                     Frame &frame,
                     const il::core::BasicBlock *block)
{
    const std::string functionName = frame.func ? frame.func->name : std::string();
    const std::string blockLabel = block ? block->label : std::string();
    RuntimeBridge::trap(kind, message, instr.loc, functionName, blockLabel);
}
} // namespace il::vm::internal::detail
