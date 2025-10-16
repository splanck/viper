// File: src/vm/dispatch/ThreadedDispatch.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements direct-threaded interpreter dispatch strategy using computed gotos.
// Key invariants: Maintains execution context across jumps and handles pauses via VMContext helpers.
// Ownership/Lifetime: Stateless strategy relying on VM-managed execution state.
// Links: docs/il-guide.md#reference

#include "vm/dispatch/DispatchStrategy.hpp"

namespace il::vm
{
namespace detail
{

#if VIPER_THREADING_SUPPORTED
class ThreadedDispatchStrategy final : public DispatchStrategy
{
  public:
    bool run(VMContext &context, VM::ExecState &state) override
    {
        state.pendingResult.reset();
        state.exitRequested = false;
        state.currentInstr = nullptr;

        const il::core::Instr *currentInstr = nullptr;
        il::core::Opcode opcode = il::core::Opcode::Trap;

        auto fetchNextOpcode = [&]() -> il::core::Opcode {
            const il::core::Opcode next = context.fetchOpcode(state);
            currentInstr = state.currentInstr;
            return next;
        };

#define OP_LABEL(name, ...) &&LBL_##name,
#define IL_OPCODE(name, ...) OP_LABEL(name, __VA_ARGS__)
        static void *kOpLabels[] = {
#include "il/core/Opcode.def"
            &&LBL_UNIMPL,
        };
#undef IL_OPCODE
#undef OP_LABEL

        static constexpr size_t kOpLabelCount = sizeof(kOpLabels) / sizeof(kOpLabels[0]);

#define DISPATCH_TO(OPCODE_VALUE)                                                                           \
    do                                                                                                      \
    {                                                                                                       \
        size_t dispatchIndex = static_cast<size_t>(OPCODE_VALUE);                                           \
        if (dispatchIndex >= kOpLabelCount - 1)                                                             \
            dispatchIndex = kOpLabelCount - 1;                                                              \
        goto *kOpLabels[dispatchIndex];                                                                     \
    } while (false)

#define TRACE_STEP(INSTR_PTR)                                                                                \
    do                                                                                                       \
    {                                                                                                        \
        const auto *_traceInstr = (INSTR_PTR);                                                               \
        if (_traceInstr)                                                                                     \
            context.traceStep(*_traceInstr, state.fr);                                                       \
    } while (false)

        for (;;)
        {
            context.clearCurrentContext();
            try
            {
                opcode = fetchNextOpcode();
                if (state.exitRequested)
                    return true;
                DISPATCH_TO(opcode);

#define OP_CASE(name, ...)                                                                                    \
    LBL_##name:                                                                                               \
    {                                                                                                         \
        TRACE_STEP(currentInstr);                                                                             \
        auto execResult = context.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);   \
        context.handleInlineResult(state, execResult);                                                        \
        if (state.exitRequested)                                                                              \
            return true;                                                                                      \
        opcode = fetchNextOpcode();                                                                           \
        if (state.exitRequested)                                                                              \
            return true;                                                                                      \
        DISPATCH_TO(opcode);                                                                                  \
    }

#define IL_OPCODE(name, ...) OP_CASE(name, __VA_ARGS__)
#include "il/core/Opcode.def"
#undef IL_OPCODE

#undef OP_CASE

                LBL_UNIMPL:
                {
                    context.trapUnimplemented(opcode);
                }
            }
            catch (const VM::TrapDispatchSignal &signal)
            {
                if (!context.handleTrapDispatch(signal, state))
                    throw;
            }
        }

#undef TRACE_STEP
#undef DISPATCH_TO

        return state.pendingResult.has_value();
    }
};

std::unique_ptr<DispatchStrategy> createThreadedDispatchStrategy()
{
    return std::make_unique<ThreadedDispatchStrategy>();
}
#endif // VIPER_THREADING_SUPPORTED

} // namespace detail
} // namespace il::vm
