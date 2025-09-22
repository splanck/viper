// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/control_flow.cpp
// Purpose: Implement VM handlers for branching, calls, and traps.
// Key invariants: Control-flow handlers maintain block parameters and frame state.
// Ownership/Lifetime: Handlers mutate the active frame without persisting external state.
// Links: docs/il-spec.md

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Value.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include <algorithm>
#include <cassert>
#include <vector>

using namespace il::core;

namespace il::vm::detail
{
/// @brief Transfer control to a branch target and seed its parameter slots.
/// @param vm Active VM used to evaluate branch argument values.
/// @param fr Current frame receiving parameter updates for the successor block.
/// @param in Branch or terminator instruction describing successor labels and arguments.
/// @param idx Index of the branch label/argument tuple that should be taken.
/// @param blocks Mapping from block labels to IR blocks; lookup must succeed (verified).
/// @param bb Output reference updated to the resolved successor block pointer.
/// @param ip Output instruction index reset to start executing at the new block.
/// @return Execution result flagged as a jump without producing a value.
/// @note Invariant: the branch target must exist in @p blocks; malformed IL would have
///       been rejected earlier by verification. When present, branch arguments are
///       evaluated and copied into the target block parameters before control moves.
VM::ExecResult OpHandlers::branchToTarget(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          size_t idx,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    const auto &label = in.labels[idx];
    auto it = blocks.find(label);
    assert(it != blocks.end() && "invalid branch target");
    const BasicBlock *target = it->second;

    if (idx < in.brArgs.size())
    {
        const auto &args = in.brArgs[idx];
        const size_t limit = std::min(args.size(), target->params.size());
        for (size_t i = 0; i < limit; ++i)
        {
            const auto id = target->params[i].id;
            assert(id < fr.params.size());
            fr.params[id] = vm.eval(fr, args[i]);
        }
    }

    bb = target;
    ip = 0;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Handle an unconditional `br` terminator by taking the sole successor.
/// @param vm Active VM instance (forwarded to @ref branchToTarget for argument eval).
/// @param fr Current execution frame that will carry the successor's parameters.
/// @param in Instruction describing the successor label and optional argument tuple.
/// @param blocks Mapping of reachable blocks for the parent function.
/// @param bb Output reference receiving the successor block pointer.
/// @param ip Output instruction index reset to the start of the new block.
/// @return Execution result indicating a taken jump.
/// @note Invariant: the first label entry in @p in is the unconditional successor and
///       must resolve within @p blocks; argument propagation mirrors
///       @ref OpHandlers::branchToTarget.
VM::ExecResult OpHandlers::handleBr(VM &vm,
                                    Frame &fr,
                                    const Instr &in,
                                    const VM::BlockMap &blocks,
                                    const BasicBlock *&bb,
                                    size_t &ip)
{
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

/// @brief Handle a conditional `cbr` terminator by selecting between two successors.
/// @param vm Active VM used to evaluate the boolean branch condition.
/// @param fr Current frame whose parameters are rewritten for the chosen successor.
/// @param in Instruction providing the condition operand and two successor labels.
/// @param blocks Mapping of blocks for the current function; both labels must resolve.
/// @param bb Output reference receiving the chosen successor block pointer.
/// @param ip Output instruction index reset to the beginning of the successor block.
/// @return Execution result indicating a taken jump to the selected block.
/// @note Invariant: @p in must supply exactly two branch labels whose entries exist in
///       @p blocks. The truthiness of the evaluated `i1` operand selects between index
///       0 (true) and 1 (false) before delegating to @ref branchToTarget.
VM::ExecResult OpHandlers::handleCBr(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    Slot cond = vm.eval(fr, in.operands[0]);
    const size_t targetIdx = (cond.i64 != 0) ? 0 : 1;
    return branchToTarget(vm, fr, in, targetIdx, blocks, bb, ip);
}

/// @brief Handle a `ret` terminator by yielding control to the caller.
/// @param vm Active VM used to evaluate an optional return operand.
/// @param fr Current frame whose result is propagated through @ref VM::ExecResult.
/// @param in Instruction describing the return operand (if present).
/// @param blocks Unused lookup table required by the handler signature.
/// @param bb Unused basic block reference for this terminator.
/// @param ip Unused instruction pointer reference for this terminator.
/// @return Execution result flagged with @c returned and carrying an optional value.
/// @note Invariant: when a return operand exists it is evaluated exactly once and the
///       resulting slot is stored in the execution result; control never touches
///       subsequent instructions within the block.
VM::ExecResult OpHandlers::handleRet(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    VM::ExecResult result{};
    if (!in.operands.empty())
        result.value = vm.eval(fr, in.operands[0]);
    result.returned = true;
    return result;
}

/// @brief Handle direct and indirect function calls from within the VM.
/// @param vm Active VM responsible for resolving internal callee definitions.
/// @param fr Current frame providing argument values and storing call results.
/// @param in Instruction describing the callee symbol and operand list.
/// @param blocks Unused block map parameter required by the generic handler signature.
/// @param bb Current basic block, used only for diagnostic context if bridging.
/// @param ip Unused instruction pointer reference for this opcode.
/// @return Execution result representing normal fallthrough.
/// @note Invariant: all operand slots are evaluated prior to dispatch. If the callee
///       exists in @p vm.fnMap, the VM executes it natively; otherwise
///       @ref RuntimeBridge routes the call to the runtime. Any produced value is
///       stored via @ref ops::storeResult, ensuring the destination slot is updated.
VM::ExecResult OpHandlers::handleCall(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    std::vector<Slot> args;
    args.reserve(in.operands.size());
    for (const auto &op : in.operands)
        args.push_back(vm.eval(fr, op));

    Slot out{};
    auto it = vm.fnMap.find(in.callee);
    if (it != vm.fnMap.end())
        out = vm.execFunction(*it->second, args);
    else
        out = RuntimeBridge::call(vm.runtimeContext, in.callee, args, in.loc, fr.func->name, bb->label);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Handle the `trap` terminator by delegating to the runtime bridge.
/// @param vm Active VM (unused) included for signature uniformity.
/// @param fr Current frame providing function metadata for diagnostics.
/// @param in Instruction containing source location metadata for the trap.
/// @param blocks Unused block map reference required by the handler interface.
/// @param bb Current block pointer supplying the trap's label in diagnostics.
/// @param ip Unused instruction pointer reference for this terminator.
/// @return Execution result flagged as @c returned to signal termination of the frame.
/// @note Invariant: @ref RuntimeBridge::trap never returns normally; invoking it reports
///       the trap and hands control back to the embedding host. The handler still marks
///       the frame as returned so the VM unwinds without executing further IL.
VM::ExecResult OpHandlers::handleTrap(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)vm;
    (void)blocks;
    (void)ip;
    RuntimeBridge::trap("trap", in.loc, fr.func->name, bb->label);
    VM::ExecResult result{};
    result.returned = true;
    return result;
}

} // namespace il::vm::detail

