// File: src/vm/int_ops.cpp
// Purpose: Implement VM handlers for integer arithmetic and comparisons.
// Key invariants: Results use 64-bit two's complement semantics consistent with the IL spec.
// Ownership/Lifetime: Handlers mutate the current frame without persisting state.
// Links: docs/il-spec.md

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "vm/OpHandlerUtils.hpp"

using namespace il::core;

namespace il::vm::detail
{
namespace
{
    template <typename Compute>
    VM::ExecResult applyBinary(Frame &fr,
                               const Instr &in,
                               const Slot &lhs,
                               const Slot &rhs,
                               Compute compute)
    {
        Slot out{};
        compute(out, lhs, rhs);
        ops::storeResult(fr, in, out);
        return {};
    }

    template <typename Compare>
    VM::ExecResult applyCompare(Frame &fr,
                                const Instr &in,
                                const Slot &lhs,
                                const Slot &rhs,
                                Compare compare)
    {
        Slot out{};
        out.i64 = compare(lhs, rhs) ? 1 : 0;
        ops::storeResult(fr, in, out);
        return {};
    }
} // namespace

VM::ExecResult OpHandlers::handleAdd(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 + rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleSub(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 - rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleMul(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 * rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleXor(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 ^ rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleShl(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 << rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleICmpEq(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 == rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleICmpNe(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 != rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleSCmpGT(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 > rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleSCmpLT(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 < rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleSCmpLE(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 <= rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleSCmpGE(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 >= rhsVal.i64; });
}

VM::ExecResult OpHandlers::handleTruncOrZext1(VM &vm,
                                              Frame &fr,
                                              const Instr &in,
                                              const VM::BlockMap &blocks,
                                              const BasicBlock *&bb,
                                              size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot value = vm.eval(fr, in.operands[0]);
    value.i64 &= 1;
    ops::storeResult(fr, in, value);
    return {};
}

} // namespace il::vm::detail

