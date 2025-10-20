//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements call/return opcode handlers.  The routines focus on argument
// propagation and consolidating the bridging between VM-defined functions and
// the runtime surface so that dispatch remains easy to reason about.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Control.hpp"

#include "vm/RuntimeBridge.hpp"

#include <vector>

namespace il::vm::detail::control
{

VM::ExecResult handleRet(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
                         size_t &ip)
{
    (void)vm;
    (void)blocks;
    (void)bb;
    (void)ip;
    VM::ExecResult result{};
    if (!in.operands.empty())
        result.value = VMAccess::eval(vm, fr, in.operands[0]);
    result.returned = true;
    return result;
}

VM::ExecResult handleCall(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)ip;

    // Evaluate operands up front so argument propagation is explicit and
    // deterministic before dispatch.  This mirrors the IL semantics and avoids
    // leaking partially evaluated slots if a bridge call traps.
    std::vector<Slot> args;
    args.reserve(in.operands.size());
    for (const auto &op : in.operands)
        args.push_back(VMAccess::eval(vm, fr, op));

    Slot out{};
    const auto &fnMap = VMAccess::functionMap(vm);
    auto it = fnMap.find(in.callee);
    if (it != fnMap.end())
    {
        out = VMAccess::callFunction(vm, *it->second, args);
    }
    else
    {
        const std::string functionName = fr.func ? fr.func->name : std::string{};
        const std::string blockLabel = bb ? bb->label : std::string{};
        out = RuntimeBridge::call(VMAccess::runtimeContext(vm), in.callee, args, in.loc, functionName, blockLabel);
    }
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail::control

