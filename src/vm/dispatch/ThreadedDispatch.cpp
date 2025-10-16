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
namespace
{
struct ThreadedLoopState
{
    VMContext &context;
    VM::ExecState &state;
    const il::core::Instr *currentInstr = nullptr;
    il::core::Opcode opcode = il::core::Opcode::Trap;
};

il::core::Opcode fetchThreadedOpcode(ThreadedLoopState &loopState)
{
    const il::core::Opcode next = loopState.context.fetchOpcode(loopState.state);
    loopState.currentInstr = loopState.state.currentInstr;
    loopState.opcode = next;
    return next;
}

VM::ExecResult dispatchThreadedOp(ThreadedLoopState &loopState)
{
    return loopState.context.executeOpcode(loopState.state.fr,
                                           *loopState.currentInstr,
                                           loopState.state.blocks,
                                           loopState.state.bb,
                                           loopState.state.ip);
}

bool handleThreadedResult(ThreadedLoopState &loopState, const VM::ExecResult &result)
{
    loopState.context.handleInlineResult(loopState.state, result);
    loopState.opcode = il::core::Opcode::Trap;
    return loopState.state.exitRequested;
}

void *selectThreadedLabel(void *const *labels, size_t labelCount, il::core::Opcode opcode)
{
    size_t dispatchIndex = static_cast<size_t>(opcode);
    if (dispatchIndex >= labelCount - 1)
        dispatchIndex = labelCount - 1;
    return labels[dispatchIndex];
}
} // namespace

class ThreadedDispatchStrategy final : public DispatchStrategy
{
  public:
    bool run(VMContext &context, VM::ExecState &state) override
    {
        state.pendingResult.reset();
        state.exitRequested = false;
        state.currentInstr = nullptr;

        ThreadedLoopState loopState{context, state};

        struct DispatchTable
        {
            void **labels;
            size_t count;
        };

        const DispatchTable dispatch = []() -> DispatchTable {
#define OP_LABEL(name, ...) &&LBL_##name,
#define IL_OPCODE(name, ...) OP_LABEL(name, __VA_ARGS__)
            static void *labels[] = {
#include "il/core/Opcode.def"
                &&LBL_UNIMPL,
            };
#undef IL_OPCODE
#undef OP_LABEL
            static constexpr size_t labelCount = sizeof(labels) / sizeof(labels[0]);
            return DispatchTable{labels, labelCount};
        }();

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
                fetchThreadedOpcode(loopState);
                if (state.exitRequested)
                    return true;
                goto *selectThreadedLabel(dispatch.labels, dispatch.count, loopState.opcode);

#define OP_CASE(name, ...)                                                                                    \
    LBL_##name:                                                                                               \
    {                                                                                                         \
        TRACE_STEP(loopState.currentInstr);                                                                   \
        auto execResult = dispatchThreadedOp(loopState);                                                      \
        if (handleThreadedResult(loopState, execResult))                                                      \
            return true;                                                                                      \
        fetchThreadedOpcode(loopState);                                                                       \
        if (state.exitRequested)                                                                              \
            return true;                                                                                      \
        goto *selectThreadedLabel(dispatch.labels, dispatch.count, loopState.opcode);                        \
    }

#define IL_OPCODE(name, ...) OP_CASE(name, __VA_ARGS__)
#include "il/core/Opcode.def"
#undef IL_OPCODE

#undef OP_CASE

                LBL_UNIMPL:
                {
                    context.trapUnimplemented(loopState.opcode);
                }
            }
            catch (const VM::TrapDispatchSignal &signal)
            {
                if (!context.handleTrapDispatch(signal, state))
                    throw;
            }
        }

#undef TRACE_STEP

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
