//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/BlockParamStringReleaseTests.cpp
// Purpose: Ensure block parameter transfers release previous string registers. 
// Key invariants: Re-entering a block with a new string decrements the old refcount.
// Ownership/Lifetime: Builds a synthetic module and inspects VM frame state.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include "../unit/VMTestHook.hpp"

#include "rt_internal.h"
#include "viper/runtime/rt.h"

#include <optional>

using namespace il::core;

namespace
{
constexpr il::support::SourceLoc kLoc(unsigned line)
{
    return {1, static_cast<uint32_t>(line), 0};
}
} // namespace

int main()
{
    Module module;
    il::build::IRBuilder builder(module);
    builder.addExtern("rt_str_i32_alloc", Type(Type::Kind::Str), {Type(Type::Kind::I32)});
    builder.addExtern("rt_str_release_maybe", Type(Type::Kind::Void), {Type(Type::Kind::Str)});

    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});

    const size_t entryIndex = fn.blocks.size();
    builder.addBlock(fn, "entry");
    const size_t loopIndex = fn.blocks.size();
    builder.createBlock(
        fn,
        "loop",
        {Param{"iter", Type(Type::Kind::I64), 0}, Param{"carry", Type(Type::Kind::Str), 0}});
    const size_t updateIndex = fn.blocks.size();
    builder.createBlock(fn, "update", {Param{"iter", Type(Type::Kind::I64), 0}});
    const size_t finishIndex = fn.blocks.size();
    builder.createBlock(fn, "finish", {Param{"final", Type(Type::Kind::Str), 0}});

    auto &entry = fn.blocks[entryIndex];
    auto &loop = fn.blocks[loopIndex];
    auto &update = fn.blocks[updateIndex];
    auto &finish = fn.blocks[finishIndex];

    builder.setInsertPoint(entry);
    const unsigned firstStrId = builder.reserveTempId();
    builder.emitCall("rt_str_i32_alloc", {Value::constInt(1)}, Value::temp(firstStrId), kLoc(1));
    builder.br(loop, {Value::constInt(0), Value::temp(firstStrId)});

    builder.setInsertPoint(loop);
    const unsigned cmpId = builder.reserveTempId();
    Instr cmp;
    cmp.result = cmpId;
    cmp.op = Opcode::ICmpEq;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(builder.blockParam(loop, 0));
    cmp.operands.push_back(Value::constInt(0));
    cmp.loc = kLoc(2);
    loop.instructions.push_back(cmp);

    Instr branch;
    branch.op = Opcode::CBr;
    branch.type = Type(Type::Kind::Void);
    branch.operands.push_back(Value::temp(cmpId));
    branch.labels.push_back(update.label);
    branch.labels.push_back(finish.label);
    branch.brArgs.push_back({builder.blockParam(loop, 0)});
    branch.brArgs.push_back({builder.blockParam(loop, 1)});
    branch.loc = kLoc(3);
    loop.instructions.push_back(branch);
    loop.terminated = true;

    builder.setInsertPoint(update);
    const unsigned nextIterId = builder.reserveTempId();
    Instr nextIter;
    nextIter.result = nextIterId;
    nextIter.op = Opcode::Add;
    nextIter.type = Type(Type::Kind::I64);
    nextIter.operands.push_back(builder.blockParam(update, 0));
    nextIter.operands.push_back(Value::constInt(1));
    nextIter.loc = kLoc(4);
    update.instructions.push_back(nextIter);

    const unsigned newStrId = builder.reserveTempId();
    builder.emitCall("rt_str_i32_alloc", {Value::constInt(2)}, Value::temp(newStrId), kLoc(5));

    Instr back;
    back.op = Opcode::Br;
    back.type = Type(Type::Kind::Void);
    back.labels.push_back(loop.label);
    back.brArgs.push_back({Value::temp(nextIterId), Value::temp(newStrId)});
    back.loc = kLoc(6);
    update.instructions.push_back(back);
    update.terminated = true;

    builder.setInsertPoint(finish);
    builder.emitCall(
        "rt_str_release_maybe", {builder.blockParam(finish, 0)}, std::nullopt, kLoc(7));
    builder.emitRet(std::optional<Value>{Value::constInt(0)}, kLoc(8));

    il::vm::VM vm(module);
    auto &mainFn = module.functions.front();
    il::vm::VMTestHook::State state = il::vm::VMTestHook::prepare(vm, mainFn);

    if (il::vm::VMTestHook::step(vm, state))
        return 1;
    if (il::vm::VMTestHook::step(vm, state))
        return 1;
    if (il::vm::VMTestHook::step(vm, state))
        return 1;

    const unsigned carryId = loop.params[1].id;
    rt_string first = state.fr.regs[carryId].str;
    if (!first)
        return 1;
    auto *firstImpl = reinterpret_cast<rt_string_impl *>(first);
    if (!firstImpl || !firstImpl->heap)
        return 1;
    rt_heap_hdr_t *firstHdr = firstImpl->heap;
    const size_t initialRef = firstHdr->refcnt;
    rt_str_retain_maybe(first);

    if (il::vm::VMTestHook::step(vm, state))
        return 1;
    if (il::vm::VMTestHook::step(vm, state))
        return 1;
    if (il::vm::VMTestHook::step(vm, state))
        return 1;

    if (il::vm::VMTestHook::step(vm, state))
        return 1;

    if (firstHdr->refcnt != initialRef + 1)
        return 1;
    rt_str_release_maybe(first);

    while (true)
    {
        auto result = il::vm::VMTestHook::step(vm, state);
        if (result)
        {
            if (result->i64 != 0)
                return 1;
            break;
        }
    }

    return 0;
}
