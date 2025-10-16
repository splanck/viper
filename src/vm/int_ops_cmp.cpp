// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/int_ops_cmp.cpp
// Purpose: Implement integer comparison opcode handlers for the VM.
// Key invariants: Handlers produce canonical `i1` results obeying IL comparison
//                 semantics for signed and unsigned predicates.
// Links: docs/il-guide.md#reference §Comparisons

#include "vm/OpHandlers_Int.hpp"

namespace il::vm::detail::integer
{
VM::ExecResult handleICmpEq(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.i64 == rhsVal.i64; });
}

VM::ExecResult handleICmpNe(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.i64 != rhsVal.i64; });
}

VM::ExecResult handleSCmpGT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm, fr, in, [](const Slot &lhsVal, const Slot &rhsVal) { return lhsVal.i64 > rhsVal.i64; });
}

VM::ExecResult handleSCmpLT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm, fr, in, [](const Slot &lhsVal, const Slot &rhsVal) { return lhsVal.i64 < rhsVal.i64; });
}

VM::ExecResult handleSCmpLE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.i64 <= rhsVal.i64; });
}

VM::ExecResult handleSCmpGE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.i64 >= rhsVal.i64; });
}

VM::ExecResult handleUCmpLT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) < static_cast<uint64_t>(rhsVal.i64); });
}

VM::ExecResult handleUCmpLE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) <= static_cast<uint64_t>(rhsVal.i64); });
}

VM::ExecResult handleUCmpGT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) > static_cast<uint64_t>(rhsVal.i64); });
}

VM::ExecResult handleUCmpGE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) >= static_cast<uint64_t>(rhsVal.i64); });
}
} // namespace il::vm::detail::integer
