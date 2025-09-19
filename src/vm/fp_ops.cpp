// File: src/vm/fp_ops.cpp
// Purpose: Implement VM handlers for floating-point math and conversions.
// Key invariants: Floating-point operations follow IEEE-754 semantics of host double type.
// Ownership/Lifetime: Handlers operate on frame-local slots without retaining references.
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
    VM::ExecResult applyFloatBinary(Frame &fr,
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
    VM::ExecResult applyFloatCompare(Frame &fr,
                                     const Instr &in,
                                     const Slot &lhs,
                                     const Slot &rhs,
                                     Compare compare)
    {
        Slot out{};
        out.i64 = compare(lhs.f64, rhs.f64) ? 1 : 0;
        ops::storeResult(fr, in, out);
        return {};
    }
} // namespace

VM::ExecResult OpHandlers::handleFAdd(VM &vm,
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
    return applyFloatBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                                { out.f64 = lhsVal.f64 + rhsVal.f64; });
}

VM::ExecResult OpHandlers::handleFSub(VM &vm,
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
    return applyFloatBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                                { out.f64 = lhsVal.f64 - rhsVal.f64; });
}

VM::ExecResult OpHandlers::handleFMul(VM &vm,
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
    return applyFloatBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                                { out.f64 = lhsVal.f64 * rhsVal.f64; });
}

VM::ExecResult OpHandlers::handleFDiv(VM &vm,
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
    return applyFloatBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                                { out.f64 = lhsVal.f64 / rhsVal.f64; });
}

VM::ExecResult OpHandlers::handleFCmpEQ(VM &vm,
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
    return applyFloatCompare(fr, in, lhs, rhs, [](double lhsVal, double rhsVal) { return lhsVal == rhsVal; });
}

VM::ExecResult OpHandlers::handleFCmpNE(VM &vm,
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
    return applyFloatCompare(fr, in, lhs, rhs, [](double lhsVal, double rhsVal) { return lhsVal != rhsVal; });
}

VM::ExecResult OpHandlers::handleFCmpGT(VM &vm,
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
    return applyFloatCompare(fr, in, lhs, rhs, [](double lhsVal, double rhsVal) { return lhsVal > rhsVal; });
}

VM::ExecResult OpHandlers::handleFCmpLT(VM &vm,
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
    return applyFloatCompare(fr, in, lhs, rhs, [](double lhsVal, double rhsVal) { return lhsVal < rhsVal; });
}

VM::ExecResult OpHandlers::handleFCmpLE(VM &vm,
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
    return applyFloatCompare(fr, in, lhs, rhs, [](double lhsVal, double rhsVal) { return lhsVal <= rhsVal; });
}

VM::ExecResult OpHandlers::handleFCmpGE(VM &vm,
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
    return applyFloatCompare(fr, in, lhs, rhs, [](double lhsVal, double rhsVal) { return lhsVal >= rhsVal; });
}

VM::ExecResult OpHandlers::handleSitofp(VM &vm,
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
    Slot out{};
    out.f64 = static_cast<double>(value.i64);
    ops::storeResult(fr, in, out);
    return {};
}

VM::ExecResult OpHandlers::handleFptosi(VM &vm,
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
    Slot out{};
    out.i64 = static_cast<int64_t>(value.f64);
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail

