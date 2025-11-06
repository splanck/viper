//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/OpHelpers.cpp
// Purpose: Provide the shared trap helpers used by VM opcode implementations.
// Design notes:
//   * Trap helpers must never throw exceptions; they report structured
//     diagnostics through RuntimeBridge and allow interpretation to unwind.
//   * Messages always include optional function/block context so user-facing
//     diagnostics remain actionable when surfaced by the CLI or debugger.
//   * The helpers rely exclusively on VM-managed state and therefore keep the
//     interpreter deterministic across embedders and tests.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "viper/vm/internal/OpHelpers.hpp"

#include "il/core/Function.hpp"

#include <string>

/// @file
/// @brief Shared trap helpers consumed by opcode handler implementations.
/// @details The helpers coordinate with @ref RuntimeBridge to record
///          interpreter diagnostics while enriching them with contextual
///          information about the currently executing function and basic block.

namespace il::vm::internal::detail
{
/// @brief Emit a VM trap augmented with human-readable execution context.
/// @details The helper extracts the active function and basic block identifiers
///          (when available) from @p frame and @p block, then forwards the
///          diagnostic through @ref RuntimeBridge::trap.  Centralising this logic
///          ensures every opcode that delegates to the helper produces consistent
///          error strings, including source locations captured on @p instr.
/// @param kind Trap classification describing the semantic failure.
/// @param message Human-readable explanation of the failure.
/// @param instr Instruction responsible for the trap; contributes location
///        metadata.
/// @param frame Execution frame providing the current function pointer.
/// @param block Optional current basic block providing label context.
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
