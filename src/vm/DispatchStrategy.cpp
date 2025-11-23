//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: vm/DispatchStrategy.cpp
// Purpose: Implement shared dispatch loop and concrete strategies
// Key invariants: Loop handles all control flow, strategies only map opcodes
// Ownership/Lifetime: Strategies are owned by the VM instance
// Links: docs/il-guide.md#reference
//===----------------------------------------------------------------------===//

#include "vm/DispatchStrategy.hpp"
#include "vm/VMContext.hpp"
#include "vm/OpHandlers.hpp"
#include "il/core/Instr.hpp"
#include "il/core/BasicBlock.hpp"

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

/// @brief Function table dispatch strategy.
class FnTableStrategy final : public DispatchStrategy
{
  public:
    Kind getKind() const override { return Kind::FnTable; }

    VM::ExecResult executeInstruction(VM &vm,
                                     VM::ExecState &state,
                                     const il::core::Instr &instr) override
    {
        return vm.executeOpcode(state.fr, instr, state.blocks, state.bb, state.ip);
    }
};

/// @brief Switch-based dispatch strategy.
class SwitchStrategy final : public DispatchStrategy
{
  public:
    Kind getKind() const override { return Kind::Switch; }

    bool handlesFinalizationInternally() const override { return true; }

    VM::ExecResult executeInstruction(VM &vm,
                                     VM::ExecState &state,
                                     const il::core::Instr &instr) override
    {
        // The switch dispatch is handled inline
        vm.dispatchOpcodeSwitch(state, instr);

        // Check if an exit was requested during switch execution
        if (state.exitRequested)
        {
            VM::ExecResult result{};
            result.returned = true;
            if (state.pendingResult)
                result.value = *state.pendingResult;
            return result;
        }

        // Normal execution continues
        return VM::ExecResult{};
    }
};

#if VIPER_THREADING_SUPPORTED
/// @brief Threaded (computed goto) dispatch strategy.
class ThreadedStrategy final : public DispatchStrategy
{
  private:
    /// @brief Label table for computed goto dispatch
    static void *getOpcodeLabels()
    {
        static void *labels[] = {
#include "vm/ops/generated/ThreadedLabels.inc"
        };
        return labels;
    }

  public:
    Kind getKind() const override { return Kind::Threaded; }

    bool requiresTrapCatch() const override { return true; }

    VM::ExecResult executeInstruction(VM &vm,
                                     VM::ExecState &state,
                                     const il::core::Instr &instr) override
    {
        void **labels = static_cast<void**>(getOpcodeLabels());
        const size_t kOpLabelCount = il::core::kNumOpcodes;

        // Dispatch to the appropriate label
        size_t index = static_cast<size_t>(instr.op);
        if (index >= kOpLabelCount - 1)
            index = kOpLabelCount - 1;

        // For threaded dispatch, we need to integrate the generated code
        // This is complex because the labels are embedded in the original implementation
        // For now, fall back to function table execution
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