// File: tests/vm/BranchParamStringRetainTests.cpp
// Purpose: Verify branch parameter transfers retain string handles.
// Key invariants: Branch staging mirrors entry-path retention semantics.
// Ownership/Lifetime: Builds a synthetic module and inspects runtime headers.
// Links: docs/testing.md

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include "../unit/VMTestHook.hpp"

#include "rt_internal.h"
#include "rt_string.h"

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

    auto &fn = builder.startFunction("branch_param_str_retain", Type(Type::Kind::I64), {});

    const size_t entryIndex = fn.blocks.size();
    builder.addBlock(fn, "entry");
    const size_t sinkIndex = fn.blocks.size();
    builder.createBlock(fn, "sink", {Param{"payload", Type(Type::Kind::Str), 0}});

    auto &entry = fn.blocks[entryIndex];
    auto &sink = fn.blocks[sinkIndex];

    builder.setInsertPoint(entry);
    const unsigned strId = builder.reserveTempId();
    builder.emitCall("rt_str_i32_alloc", {Value::constInt(7)}, Value::temp(strId), kLoc(1));

    Instr br;
    br.op = Opcode::Br;
    br.type = Type(Type::Kind::Void);
    br.labels.push_back(sink.label);
    br.brArgs.push_back({Value::temp(strId)});
    br.loc = kLoc(2);
    entry.instructions.push_back(br);
    entry.terminated = true;

    builder.setInsertPoint(sink);
    builder.emitCall("rt_str_release_maybe", {builder.blockParam(sink, 0)}, std::nullopt, kLoc(3));
    builder.emitRet(std::optional<Value>{Value::constInt(0)}, kLoc(4));

    il::vm::VM vm(module);
    auto state = il::vm::VMTestHook::prepare(vm, fn);

    if (il::vm::VMTestHook::step(vm, state))
        return 1;

    if (state.fr.regs.size() <= strId)
        return 1;
    rt_string produced = state.fr.regs[strId].str;
    if (!produced)
        return 1;
    auto *impl = reinterpret_cast<rt_string_impl *>(produced);
    if (!impl || !impl->heap)
        return 1;
    rt_heap_hdr_t *header = impl->heap;
    const size_t refAfterCall = header->refcnt;

    if (il::vm::VMTestHook::step(vm, state))
        return 1;

    if (sink.params.empty())
        return 1;
    const unsigned paramId = sink.params[0].id;
    auto &pendingOpt = state.fr.params[paramId];
    if (!pendingOpt)
        return 1;
    if (pendingOpt->str != produced)
        return 1;

    if (header->refcnt != refAfterCall + 1)
        return 1;

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

    if (header->refcnt != refAfterCall)
        return 1;

    return 0;
}
