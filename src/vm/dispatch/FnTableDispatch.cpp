// File: src/vm/dispatch/FnTableDispatch.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements function-table based interpreter dispatch strategy.
// Key invariants: Resets execution result state before entering the loop and
//                 terminates once a step reports a pending result.
// Ownership/Lifetime: Does not own VM resources; operates on state passed in run().
// Links: docs/il-guide.md#reference

#include "vm/dispatch/DispatchStrategy.hpp"

namespace il::vm
{
namespace detail
{

class FnTableDispatchStrategy final : public DispatchStrategy
{
  public:
    bool run(VMContext &context, VM::ExecState &state) override
    {
        state.pendingResult.reset();
        state.exitRequested = false;
        state.currentInstr = nullptr;

        while (true)
        {
            auto result = context.stepOnce(state);
            if (result.has_value())
            {
                state.pendingResult = *result;
                state.exitRequested = true;
                return true;
            }
        }
    }
};

std::unique_ptr<DispatchStrategy> createFnTableDispatchStrategy()
{
    return std::make_unique<FnTableDispatchStrategy>();
}

} // namespace detail
} // namespace il::vm
