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
            fr.params[target->params[i].id] = vm.eval(fr, args[i]);
    }

    bb = target;
    ip = 0;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

VM::ExecResult OpHandlers::handleBr(VM &vm,
                                    Frame &fr,
                                    const Instr &in,
                                    const VM::BlockMap &blocks,
                                    const BasicBlock *&bb,
                                    size_t &ip)
{
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

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
        out = RuntimeBridge::call(in.callee, args, in.loc, fr.func->name, bb->label);
    ops::storeResult(fr, in, out);
    return {};
}

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

