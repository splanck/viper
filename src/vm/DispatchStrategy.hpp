//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: vm/DispatchStrategy.hpp
// Purpose: Define interface for pluggable VM dispatch strategies
// Key invariants: Each strategy only handles opcode-to-handler mapping
// Ownership/Lifetime: Strategies are owned by the VM instance
// Links: docs/il-guide.md#reference
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/VM.hpp"
#include "il/core/Opcode.hpp"

namespace il::vm
{

/// @brief Abstract interface for opcode dispatch strategies.
/// @details Each concrete strategy only implements the mapping from opcode
///          to handler execution, while the shared loop handles all control flow,
///          trap handling, and debug hooks.
class DispatchStrategy
{
  public:
    /// @brief Virtual destructor for proper cleanup.
    virtual ~DispatchStrategy() = default;

    /// @brief Strategy identifier for diagnostics and configuration.
    enum class Kind
    {
        FnTable,  ///< Function table lookup
        Switch,   ///< Switch statement dispatch
        Threaded  ///< Computed goto threading
    };

    /// @brief Get the kind of this strategy.
    virtual Kind getKind() const = 0;

    /// @brief Execute a single instruction using this strategy.
    /// @param vm Virtual machine instance
    /// @param state Current execution state
    /// @param instr Instruction to execute
    /// @return Execution result from the handler
    virtual VM::ExecResult executeInstruction(VM &vm,
                                             VM::ExecState &state,
                                             const il::core::Instr &instr) = 0;

    /// @brief Check if this strategy requires special trap handling.
    /// @details The threaded strategy needs to catch TrapDispatchSignal
    ///          while others can let it propagate.
    virtual bool requiresTrapCatch() const { return false; }

    /// @brief Check if this strategy handles tracing and finalization internally.
    /// @details The switch strategy's inline handlers call handleInlineResult,
    ///          which traces and finalizes internally. Other strategies return
    ///          ExecResult and expect the main loop to handle finalization.
    virtual bool handlesFinalizationInternally() const { return false; }
};

/// @brief Shared dispatch loop implementation.
/// @details This function contains the common execution loop logic that all
///          strategies share: state management, instruction selection, debug hooks,
///          trap handling, and loop control.
/// @param vm Virtual machine instance
/// @param context VM context for trap and debug handling
/// @param state Execution state being driven
/// @param strategy Dispatch strategy to use for instruction execution
/// @return True when dispatch terminated normally, false when paused
bool runSharedDispatchLoop(VM &vm,
                          VMContext &context,
                          VM::ExecState &state,
                          DispatchStrategy &strategy);

} // namespace il::vm