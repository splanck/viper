//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/DispatchStrategy.cpp
// Purpose: Implement shared dispatch loop and concrete strategies.
//
// This file contains:
//   1. runSharedDispatchLoop() - Common execution loop for all strategies
//   2. FnTableStrategy - Function table dispatch implementation
//   3. SwitchStrategy - Switch statement dispatch implementation
//   4. ThreadedStrategy - Computed goto dispatch (when supported)
//   5. createDispatchStrategy() - Factory for strategy instantiation
//
// How Strategies Map Opcodes to Handlers:
//
//   FnTableStrategy:
//     - Calls VM::executeOpcode() which indexes into getOpcodeHandlers()
//     - Handler table: vm/ops/generated/HandlerTable.hpp
//     - Mapping: table[static_cast<size_t>(opcode)] → handler function
//
//   SwitchStrategy:
//     - Calls VM::dispatchOpcodeSwitch() which uses a switch statement
//     - Switch impl: vm/ops/generated/SwitchDispatchImpl.inc
//     - Mapping: case Opcode::X → inline_handle_X(state)
//
//   ThreadedStrategy:
//     - Actual dispatch in ThreadedDispatchDriver (VM.cpp) due to goto scope
//     - Label table: vm/ops/generated/ThreadedLabels.inc
//     - Case labels: vm/ops/generated/ThreadedCases.inc
//     - Mapping: goto *kOpLabels[opcode] → LBL_X: { handle and dispatch next }
//
// All strategies rely on tables synchronized with il/core/Opcode.def.
// See docs/generated-files.md for details on regeneration.
//
// Key invariants: Loop handles all control flow, strategies only map opcodes
// Ownership/Lifetime: Strategies are owned by the VM instance
// Links: docs/generated-files.md, docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "vm/DispatchStrategy.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "vm/OpHandlers.hpp"
#include "vm/VMContext.hpp"

namespace il::vm
{

/// @brief Shared dispatch loop that all strategies use.
/// @details Contains the common execution logic: state setup, instruction
///          selection, debug hooks, trap handling, and exit conditions.
///          The strategy is only responsible for executing individual instructions.
bool runSharedDispatchLoop(VM &vm,
                           VMContext &context,
                           VM::ExecState &state,
                           DispatchStrategy &strategy)
{
    while (true)
    {
        // Step 1: Reset per-iteration state
        vm.beginDispatch(state);

        // Step 2: Select next instruction
        const il::core::Instr *instr = nullptr;
        if (!vm.selectInstruction(state, instr))
        {
            // Exit or pause requested
            return state.exitRequested;
        }

        // Step 3: Debug hook before execution
        VIPER_VM_DISPATCH_BEFORE(context, instr->op);

        // Step 4: Execute instruction via strategy (with optional trap handling)
        VM::ExecResult exec{};

        if (strategy.requiresTrapCatch())
        {
            // Threaded strategy needs special trap handling
            try
            {
                vm.traceInstruction(*instr, state.fr);
                exec = strategy.executeInstruction(vm, state, *instr);
            }
            catch (const VM::TrapDispatchSignal &signal)
            {
                if (!context.handleTrapDispatch(signal, state))
                    throw;
                // Trap handled, continue to next iteration
                continue;
            }
        }
        else if (strategy.handlesFinalizationInternally())
        {
            // Switch strategy: inline handlers trace and finalize internally
            exec = strategy.executeInstruction(vm, state, *instr);
        }
        else
        {
            // Function table strategy: trace here, finalize below
            vm.traceInstruction(*instr, state.fr);
            exec = strategy.executeInstruction(vm, state, *instr);
        }

        // Step 5: Finalize dispatch and check for exit (skip if already done internally)
        if (!strategy.handlesFinalizationInternally())
        {
            if (vm.finalizeDispatch(state, exec))
            {
                return true;
            }
        }
        else if (state.exitRequested)
        {
            // Strategy handled finalization, just check if exit was requested
            return true;
        }
    }
}

//===----------------------------------------------------------------------===//
// Concrete Strategy Implementations
//===----------------------------------------------------------------------===//

namespace detail
{

// =============================================================================
// FnTableStrategy: Function Table Dispatch
// =============================================================================
// Opcode Resolution:
//   1. VM::executeOpcode() is called with the instruction
//   2. executeOpcode() gets the handler table via getOpcodeHandlers()
//   3. Handler is looked up: table[static_cast<size_t>(instr.op)]
//   4. Handler is invoked: handler(vm, frame, instr, blocks, bb, ip)
//
// Generated Tables Used:
//   - vm/ops/generated/HandlerTable.hpp: opcodeHandlers() array
//   - Each entry is a function pointer to detail::handle<OpName>()
//
// Performance: Moderate (one indirect call per opcode)
// Portability: Universal (standard C++)
// =============================================================================

/// @brief Function table dispatch strategy.
class FnTableStrategy final : public DispatchStrategy
{
  public:
    Kind getKind() const override
    {
        return Kind::FnTable;
    }

    VM::ExecResult executeInstruction(VM &vm,
                                      VM::ExecState &state,
                                      const il::core::Instr &instr) override
    {
        // Delegates to VM::executeOpcode() which indexes into the handler table
        // (vm/ops/generated/HandlerTable.hpp) and calls the corresponding handler.
        return vm.executeOpcode(state.fr, instr, state.blocks, state.bb, state.ip);
    }
};

// =============================================================================
// SwitchStrategy: Switch Statement Dispatch
// =============================================================================
// Opcode Resolution:
//   1. VM::dispatchOpcodeSwitch() is called with the instruction
//   2. A switch statement dispatches on instr.op
//   3. Each case calls the corresponding inline_handle_<OpName>() method
//   4. inline_handle_* traces, executes, and finalizes internally
//
// Generated Tables Used:
//   - vm/ops/generated/SwitchDispatchImpl.inc: switch body with all cases
//   - vm/ops/generated/InlineHandlersImpl.inc: inline_handle_* implementations
//
// Performance: Good (compiler can optimize switch table)
// Portability: Universal (standard C++)
// =============================================================================

/// @brief Switch-based dispatch strategy.
class SwitchStrategy final : public DispatchStrategy
{
  public:
    Kind getKind() const override
    {
        return Kind::Switch;
    }

    bool handlesFinalizationInternally() const override
    {
        return true;
    }

    VM::ExecResult executeInstruction(VM &vm,
                                      VM::ExecState &state,
                                      const il::core::Instr &instr) override
    {
        // Delegates to VM::dispatchOpcodeSwitch() which uses a switch statement
        // generated from vm/ops/generated/SwitchDispatchImpl.inc. Each case
        // invokes inline_handle_<OpName>() which traces, executes, and finalizes.
        vm.dispatchOpcodeSwitch(state, instr);

        // Check if an exit was requested during switch execution
        if (state.exitRequested)
        {
            VM::ExecResult result{};
            result.returned = true;
            if (state.hasPendingResult)
                result.value = state.pendingResult;
            return result;
        }

        // Normal execution continues
        return VM::ExecResult{};
    }
};

#if VIPER_THREADING_SUPPORTED
// =============================================================================
// ThreadedStrategy: Computed Goto Dispatch
// =============================================================================
// Opcode Resolution:
//   1. ThreadedDispatchDriver::run() (in VM.cpp) handles the actual dispatch
//   2. Label addresses are stored in kOpLabels[] from ThreadedLabels.inc
//   3. goto *kOpLabels[opcode] jumps directly to LBL_<OpName>:
//   4. Each label block handles the opcode, fetches next, and dispatches
//
// Generated Tables Used:
//   - vm/ops/generated/ThreadedLabels.inc: &&LBL_* label address array
//   - vm/ops/generated/ThreadedCases.inc: LBL_*: case label definitions
//   - vm/ops/generated/InlineHandlersImpl.inc: inline_handle_* implementations
//
// Why in VM.cpp:
//   Computed gotos require labels and goto* in the same function scope.
//   ThreadedDispatchDriver::run() contains the labels and loop, so this
//   strategy class is a placeholder that falls back to function table.
//
// Performance: Fastest (no indirect call, direct jump)
// Portability: GCC/Clang only (VIPER_THREADING_SUPPORTED)
// =============================================================================

/// @brief Threaded (computed goto) dispatch strategy.
/// @note The actual threaded dispatch implementation is in ThreadedDispatchDriver (VM.cpp)
///       because computed gotos require the labels and goto*'s in the same function.
///       This class exists for the strategy interface but isn't used directly.
class ThreadedStrategy final : public DispatchStrategy
{
  public:
    Kind getKind() const override
    {
        return Kind::Threaded;
    }

    bool requiresTrapCatch() const override
    {
        return true;
    }

    VM::ExecResult executeInstruction(VM &vm,
                                      VM::ExecState &state,
                                      const il::core::Instr &instr) override
    {
        // Threaded dispatch is handled by ThreadedDispatchDriver in VM.cpp
        // which contains the actual computed goto loop with labels from
        // vm/ops/generated/ThreadedLabels.inc and ThreadedCases.inc.
        // This fallback uses function table dispatch for compatibility.
        return vm.executeOpcode(state.fr, instr, state.blocks, state.bb, state.ip);
    }
};
#endif // VIPER_THREADING_SUPPORTED

} // namespace detail

//===----------------------------------------------------------------------===//
// Factory Functions
//===----------------------------------------------------------------------===//

/// @brief Create a dispatch strategy for the given kind.
std::unique_ptr<DispatchStrategy> createDispatchStrategy(VM::DispatchKind kind)
{
    switch (kind)
    {
        case VM::DispatchKind::FnTable:
            return std::make_unique<detail::FnTableStrategy>();
        case VM::DispatchKind::Switch:
            return std::make_unique<detail::SwitchStrategy>();
        case VM::DispatchKind::Threaded:
#if VIPER_THREADING_SUPPORTED
            return std::make_unique<detail::ThreadedStrategy>();
#else
            return std::make_unique<detail::SwitchStrategy>();
#endif
    }
    return std::make_unique<detail::SwitchStrategy>();
}

} // namespace il::vm
