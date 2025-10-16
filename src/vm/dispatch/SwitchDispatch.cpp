// File: src/vm/dispatch/SwitchDispatch.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements switch-based interpreter dispatch strategy.
// Key invariants: Maintains current instruction context and honours pause requests
//                 surfaced by VMContext helpers.
// Ownership/Lifetime: Stateless strategy; relies on VM-managed execution state.
// Links: docs/il-guide.md#reference

#include "vm/dispatch/DispatchStrategy.hpp"

namespace il::vm
{
namespace detail
{

class SwitchDispatchStrategy final : public DispatchStrategy
{
  public:
    bool run(VMContext &context, VM::ExecState &state) override
    {
        state.currentInstr = nullptr;

        while (true)
        {
            const il::core::Opcode opcode = context.fetchOpcode(state);
            if (state.exitRequested)
                return true;

            switch (opcode)
            {
#define TRACE_STEP(INSTR_PTR)                                                                                \
    do                                                                                                       \
    {                                                                                                        \
        const auto *_traceInstr = (INSTR_PTR);                                                               \
        if (_traceInstr)                                                                                     \
            context.traceStep(*_traceInstr, state.fr);                                                       \
    } while (false)

#define OP_SWITCH(NAME, ...)                                                                                  \
    case il::core::Opcode::NAME:                                                                              \
    {                                                                                                         \
        TRACE_STEP(state.currentInstr);                                                                       \
        context.vm().inline_handle_##NAME(state);                                                             \
        break;                                                                                                \
    }
#define IL_OPCODE(NAME, ...) OP_SWITCH(NAME, __VA_ARGS__)
#include "il/core/Opcode.def"
#undef IL_OPCODE
#undef OP_SWITCH
                default:
                    context.trapUnimplemented(opcode);
            }

#undef TRACE_STEP

            if (state.exitRequested)
                return true;
        }
    }
};

std::unique_ptr<DispatchStrategy> createSwitchDispatchStrategy()
{
    return std::make_unique<SwitchDispatchStrategy>();
}

} // namespace detail
} // namespace il::vm
