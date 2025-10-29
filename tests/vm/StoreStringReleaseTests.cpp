// File: tests/vm/StoreStringReleaseTests.cpp
// Purpose: Ensure stores to pointers release previous string handles.
// Key invariants: Writing successive strings to the same address balances refcounts.
// Ownership/Lifetime: Constructs a synthetic module and inspects runtime headers.
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
    builder.addExtern(
        "rt_left", Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::I64)});

    builder.addGlobalStr("literal", "sample");

    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    const unsigned ptrId = builder.reserveTempId();
    Instr allocaInstr;
    allocaInstr.result = ptrId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands.push_back(Value::constInt(static_cast<int64_t>(sizeof(rt_string))));
    allocaInstr.loc = kLoc(1);
    entry.instructions.push_back(allocaInstr);

    const unsigned firstStrId = builder.reserveTempId();
    builder.emitCall("rt_str_i32_alloc", {Value::constInt(1)}, Value::temp(firstStrId), kLoc(2));

    Instr storeFirst;
    storeFirst.op = Opcode::Store;
    storeFirst.type = Type(Type::Kind::Str);
    storeFirst.operands.push_back(Value::temp(ptrId));
    storeFirst.operands.push_back(Value::temp(firstStrId));
    storeFirst.loc = kLoc(3);
    entry.instructions.push_back(storeFirst);

    const unsigned secondStrId = builder.reserveTempId();
    builder.emitCall("rt_str_i32_alloc", {Value::constInt(2)}, Value::temp(secondStrId), kLoc(4));

    Instr storeSecond;
    storeSecond.op = Opcode::Store;
    storeSecond.type = Type(Type::Kind::Str);
    storeSecond.operands.push_back(Value::temp(ptrId));
    storeSecond.operands.push_back(Value::temp(secondStrId));
    storeSecond.loc = kLoc(5);
    entry.instructions.push_back(storeSecond);

    const unsigned loadedId = builder.reserveTempId();
    Instr loadStored;
    loadStored.result = loadedId;
    loadStored.op = Opcode::Load;
    loadStored.type = Type(Type::Kind::Str);
    loadStored.operands.push_back(Value::temp(ptrId));
    loadStored.loc = kLoc(6);
    entry.instructions.push_back(loadStored);

    builder.emitCall("rt_str_release_maybe", {Value::temp(loadedId)}, std::nullopt, kLoc(7));

    const Value literalValue = builder.emitConstStr("literal", kLoc(8));
    const unsigned literalId = literalValue.id;
    builder.emitCall("rt_left", {literalValue, Value::constInt(64)}, std::nullopt, kLoc(9));
    builder.emitCall("rt_str_release_maybe", {literalValue}, std::nullopt, kLoc(10));

    builder.emitRet(std::optional<Value>{Value::constInt(0)}, kLoc(11));

    il::vm::VM vm(module);
    auto &mainFn = module.functions.front();
    il::vm::VMTestHook::State state = il::vm::VMTestHook::prepare(vm, mainFn);

    auto stepExpectIp = [&](size_t expectedIp) -> bool
    {
        auto result = il::vm::VMTestHook::step(vm, state);
        if (result)
            return true;
        return state.ip != expectedIp;
    };

    if (stepExpectIp(1))
        return 1; // alloca

    if (stepExpectIp(2))
        return 1; // first allocation

    if (stepExpectIp(3))
        return 1; // store first string

    auto *slotPtr = reinterpret_cast<rt_string *>(state.fr.regs[ptrId].ptr);
    if (!slotPtr)
        return 1;

    rt_string first = *slotPtr;
    if (!first)
        return 1;

    auto *firstImpl = reinterpret_cast<rt_string_impl *>(first);
    if (!firstImpl || !firstImpl->heap)
        return 1;

    rt_heap_hdr_t *firstHdr = firstImpl->heap;
    const size_t initialRef = firstHdr->refcnt;
    rt_str_retain_maybe(first);

    if (stepExpectIp(4))
        return 1; // second allocation

    if (stepExpectIp(5))
        return 1; // store second string

    if (firstHdr->refcnt != initialRef)
        return 1;

    rt_str_release_maybe(first);

    if (stepExpectIp(6))
        return 1; // load stored string

    if (stepExpectIp(7))
        return 1; // release loaded string

    if (stepExpectIp(8))
        return 1; // materialise literal string

    rt_string literalHandle = state.fr.regs[literalId].str;
    if (!literalHandle)
        return 1;

    auto *literalImpl = reinterpret_cast<rt_string_impl *>(literalHandle);
    if (!literalImpl)
        return 1;

    rt_heap_hdr_t *literalHdr = literalImpl->heap;
    const size_t initialLiteralRefs = literalHdr ? literalHdr->refcnt : literalImpl->literal_refs;

    if (stepExpectIp(9))
        return 1; // call rt_left without capturing result

    literalHandle = state.fr.regs[literalId].str;
    literalImpl = reinterpret_cast<rt_string_impl *>(literalHandle);
    if (!literalImpl)
        return 1;

    literalHdr = literalImpl->heap;
    const size_t postCallRefs = literalHdr ? literalHdr->refcnt : literalImpl->literal_refs;
    if (postCallRefs != initialLiteralRefs)
        return 1;

    if (stepExpectIp(10))
        return 1; // release literal string

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
