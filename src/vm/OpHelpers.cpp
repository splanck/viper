//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/OpHelpers.cpp
// Purpose: Implement the shared trap helpers declared in OpHelpers.hpp that
//          bridge interpreter failures to the runtime diagnostics system.
// Key invariants: Diagnostics always provide function and block context when
//                 available and never throw exceptions across the VM boundary.
// Ownership/Lifetime: Operates entirely on VM-managed state without persisting
//                     data, so helpers stay side-effect free aside from the
//                     emitted trap.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "viper/vm/internal/OpHelpers.hpp"

#include "il/core/Function.hpp"

#include <string>

/// @file
/// @brief Trap helpers that bridge VM failures to the runtime diagnostics system.
/// @details Centralises the logic for forwarding trap metadata—including source
///          locations, function names, and block labels—to @ref RuntimeBridge so
///          every VM failure reports consistent context.

namespace il::vm::internal::detail
{
/// @brief Forward a VM trap to the runtime diagnostics bridge with context.
/// @details Collects the enclosing function name, active block label, and source
///          location information before invoking @ref RuntimeBridge::trap.  By
///          funnelling every failure through this helper the VM keeps trap
///          reporting deterministic and decoupled from the runtime's diagnostic
///          formatting.
/// @param kind High-level trap classification associated with the failure.
/// @param message Human-readable description explaining what went wrong.
/// @param instr Instruction that triggered the trap; used for source locations.
/// @param frame Current VM frame containing function metadata and registers.
/// @param block Optional pointer to the active basic block, or null when
///              unavailable.
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
