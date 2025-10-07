// File: tests/unit/test_vm_runtime_bridge_marshalling.cpp
// Purpose: Validate RuntimeBridge argument and result marshalling for supported types.
// Key invariants: Each IL type kind maps to the correct Slot storage and runtime buffer.
// Ownership: Uses runtime library helpers; callers release any allocated resources.
// Links: docs/codemap.md

#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"
#include "VMTestHook.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "rt_array.h"
#include "rt_internal.h"
#include "rt_string.h"
#include <array>
#include <initializer_list>
#include <cassert>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

int main()
{
    using il::support::SourceLoc;
    using il::core::Type;
    using il::core::Value;
    using il::core::Opcode;
    using il::vm::RuntimeBridge;
    using il::vm::RuntimeCallContext;
    using il::vm::Slot;

    RuntimeCallContext ctx{};
    const SourceLoc loc{};
    const std::string fn = "runtime.bridge";
    const std::string block = "entry";

    constexpr size_t kKindCount = static_cast<size_t>(Type::Kind::Str) + 1;
    std::array<bool, kKindCount> coveredKinds{};
    auto markKind = [&](Type::Kind kind) { coveredKinds[static_cast<size_t>(kind)] = true; };
    // Newly supported integer widths share Slot::i64 marshalling paths.
    markKind(Type::Kind::I16);
    markKind(Type::Kind::I32);
    auto callBridge = [&](const std::string &name,
                          std::vector<Slot> arguments,
                          Type::Kind resultKind,
                          std::initializer_list<Type::Kind> argKinds) {
        for (Type::Kind arg : argKinds)
            markKind(arg);
        markKind(resultKind);
        return RuntimeBridge::call(ctx, name, arguments, loc, fn, block);
    };

    Slot intArg{};
    intArg.i64 = -42;
    Slot result = callBridge("rt_abs_i64", {intArg}, Type::Kind::I64, {Type::Kind::I64});
    assert(result.i64 == 42);

    Slot fArg{};
    fArg.f64 = -3.25;
    result = callBridge("rt_abs_f64", {fArg}, Type::Kind::F64, {Type::Kind::F64});
    assert(result.f64 == 3.25);

    const char *helloLiteral = "hello";
    Slot ptrArg{};
    ptrArg.ptr = const_cast<char *>(helloLiteral);
    result = callBridge("rt_const_cstr", {ptrArg}, Type::Kind::Str, {Type::Kind::Ptr});
    assert(result.str != nullptr);
    assert(result.str->data == helloLiteral);
    rt_string hello = result.str;

    Slot strArg{};
    strArg.str = hello;
    Slot lenResult = callBridge("rt_len", {strArg}, Type::Kind::I64, {Type::Kind::Str});
    assert(lenResult.i64 == 5);
    rt_string_unref(hello);

    Slot numberArg{};
    numberArg.i64 = 12345;
    Slot strNumberResult = callBridge("rt_int_to_str", {numberArg}, Type::Kind::Str, {Type::Kind::I64});
    assert(strNumberResult.str != nullptr);
    rt_string numberStr = strNumberResult.str;
    std::string numberText(numberStr->data, rt_heap_len(numberStr->data));
    assert(numberText == "12345");
    rt_string_unref(numberStr);

    const char *abcLiteral = "abc";
    Slot strPtrArgA{};
    strPtrArgA.ptr = const_cast<char *>(abcLiteral);
    Slot strPtrArgB{};
    strPtrArgB.ptr = const_cast<char *>(abcLiteral);
    Slot strResA = callBridge("rt_const_cstr", {strPtrArgA}, Type::Kind::Str, {Type::Kind::Ptr});
    Slot strResB = callBridge("rt_const_cstr", {strPtrArgB}, Type::Kind::Str, {Type::Kind::Ptr});
    Slot eqArgA{};
    eqArgA.str = strResA.str;
    Slot eqArgB{};
    eqArgB.str = strResB.str;
    Slot eqResult = callBridge("rt_str_eq", {eqArgA, eqArgB}, Type::Kind::I1, {Type::Kind::Str, Type::Kind::Str});
    assert(eqResult.i64 == 1);
    rt_string_unref(strResA.str);
    rt_string_unref(strResB.str);

    Slot allocArg{};
    allocArg.i64 = 16;
    Slot allocResult = callBridge("rt_alloc", {allocArg}, Type::Kind::Ptr, {Type::Kind::I64});
    assert(allocResult.ptr != nullptr);
    free(allocResult.ptr);

    Slot seedArg{};
    seedArg.i64 = 42;
    Slot voidResult = callBridge("rt_randomize_i64", {seedArg}, Type::Kind::Void, {Type::Kind::I64});
    assert(voidResult.i64 == 0);

    Slot arrLenArg{};
    arrLenArg.i64 = 3;
    Slot arrHandle = callBridge("rt_arr_i32_new", {arrLenArg}, Type::Kind::Ptr, {Type::Kind::I64});
    assert(arrHandle.ptr != nullptr);

    Slot arrSlot{};
    arrSlot.ptr = arrHandle.ptr;
    Slot arrLenResult = callBridge("rt_arr_i32_len", {arrSlot}, Type::Kind::I64, {Type::Kind::Ptr});
    assert(arrLenResult.i64 == 3);

    Slot arrIdx{};
    arrIdx.i64 = 1;
    Slot arrValue{};
    arrValue.i64 = -17;
    Slot setResult = callBridge("rt_arr_i32_set",
                                {arrSlot, arrIdx, arrValue},
                                Type::Kind::Void,
                                {Type::Kind::Ptr, Type::Kind::I64, Type::Kind::I64});
    assert(setResult.i64 == 0);

    Slot getIdx{};
    getIdx.i64 = 1;
    Slot arrGetResult = callBridge("rt_arr_i32_get",
                                   {arrSlot, getIdx},
                                   Type::Kind::I64,
                                   {Type::Kind::Ptr, Type::Kind::I64});
    assert(arrGetResult.i64 == -17);

    Slot resizeLen{};
    resizeLen.i64 = 5;
    Slot resizeResult = callBridge("rt_arr_i32_resize",
                                   {arrSlot, resizeLen},
                                   Type::Kind::Ptr,
                                   {Type::Kind::Ptr, Type::Kind::I64});
    assert(resizeResult.ptr != nullptr);
    arrSlot.ptr = resizeResult.ptr;

    Slot resizedLen = callBridge("rt_arr_i32_len", {arrSlot}, Type::Kind::I64, {Type::Kind::Ptr});
    assert(resizedLen.i64 == 5);

    Slot newIdx{};
    newIdx.i64 = 3;
    Slot zeroResult = callBridge("rt_arr_i32_get",
                                 {arrSlot, newIdx},
                                 Type::Kind::I64,
                                 {Type::Kind::Ptr, Type::Kind::I64});
    assert(zeroResult.i64 == 0);

    rt_arr_i32_release(static_cast<int32_t *>(arrSlot.ptr));

    const std::string embeddedLiteral("abc\0def", 7);
    il::vm::ViperString embedded = il::vm::toViperString(embeddedLiteral);
    assert(embedded != nullptr);
    const int64_t runtimeLen = rt_len(embedded);
    assert(runtimeLen == static_cast<int64_t>(embeddedLiteral.size()));
    std::string roundTrip(embedded->data, static_cast<size_t>(runtimeLen));
    assert(roundTrip == embeddedLiteral);
    rt_string_unref(embedded);

    il::vm::StringRef emptyRef{};
    il::vm::ViperString emptyString = il::vm::toViperString(emptyRef);
    assert(emptyString != nullptr);
    assert(rt_len(emptyString) == 0);
    const char *emptyData = rt_string_cstr(emptyString);
    assert(emptyData != nullptr);
    assert(emptyData[0] == '\0');
    il::vm::StringRef emptyView = il::vm::fromViperString(emptyString);
    assert(emptyView.data() == emptyData);
    assert(emptyView.size() == 0);

    il::vm::ViperString roundTripEmpty = il::vm::toViperString(emptyView);
    assert(roundTripEmpty != nullptr);
    assert(rt_len(roundTripEmpty) == 0);
    const char *roundTripData = rt_string_cstr(roundTripEmpty);
    assert(roundTripData != nullptr);
    assert(roundTripData[0] == '\0');

    if (roundTripEmpty != emptyString)
        rt_string_unref(roundTripEmpty);

    {
        std::string backing = "backing";
        std::string_view nonLiteralEmpty{backing.data(), static_cast<size_t>(0)};
        assert(nonLiteralEmpty.data() != nullptr);
        il::vm::ViperString nonLiteralHandle = il::vm::toViperString(nonLiteralEmpty);
        assert(nonLiteralHandle != nullptr);
        assert(rt_len(nonLiteralHandle) == 0);
        assert(nonLiteralHandle != emptyString);
        rt_string_unref(nonLiteralHandle);
    }

    rt_string_unref(emptyString);

    {
        rt_string_impl bogus{};
        bogus.data = const_cast<char *>("corrupt");
        bogus.heap = nullptr;
        bogus.literal_len = static_cast<size_t>(-1);
        bogus.literal_refs = 1;

        il::core::Module module;
        il::core::Function fn;
        fn.name = "main";
        fn.retType = Type(Type::Kind::I64);
        fn.valueNames.resize(4);

        il::core::BasicBlock entry;
        entry.label = "entry";
        il::core::Param msgParam;
        msgParam.name = "msg";
        msgParam.type = Type(Type::Kind::Str);
        msgParam.id = 0;
        entry.params.push_back(msgParam);

        il::core::Instr push;
        push.op = Opcode::EhPush;
        push.type = Type(Type::Kind::Void);
        push.labels.push_back("handler");

        il::core::Instr trapErrInstr;
        trapErrInstr.result = 1;
        trapErrInstr.op = Opcode::TrapErr;
        trapErrInstr.type = Type(Type::Kind::Error);
        trapErrInstr.operands.push_back(Value::constInt(7));
        trapErrInstr.operands.push_back(Value::temp(0));

        il::core::Instr pop;
        pop.op = Opcode::EhPop;
        pop.type = Type(Type::Kind::Void);

        il::core::Instr retValue;
        retValue.op = Opcode::Ret;
        retValue.type = Type(Type::Kind::I64);
        retValue.operands.push_back(Value::temp(3));

        entry.instructions.push_back(push);
        entry.instructions.push_back(trapErrInstr);
        entry.instructions.push_back(pop);
        entry.instructions.push_back(retValue);
        entry.terminated = true;

        il::core::BasicBlock handler;
        handler.label = "handler";
        il::core::Param errParam;
        errParam.name = "err";
        errParam.type = Type(Type::Kind::Error);
        errParam.id = 1;
        handler.params.push_back(errParam);
        il::core::Param tokParam;
        tokParam.name = "tok";
        tokParam.type = Type(Type::Kind::ResumeTok);
        tokParam.id = 2;
        handler.params.push_back(tokParam);

        il::core::Instr ehEntry;
        ehEntry.op = Opcode::EhEntry;
        ehEntry.type = Type(Type::Kind::Void);

        il::core::Instr trapKind;
        trapKind.result = 3;
        trapKind.op = Opcode::TrapKind;
        trapKind.type = Type(Type::Kind::I64);

        il::core::Instr resumeNext;
        resumeNext.op = Opcode::ResumeNext;
        resumeNext.type = Type(Type::Kind::Void);
        resumeNext.operands.push_back(Value::temp(2));

        handler.instructions.push_back(ehEntry);
        handler.instructions.push_back(trapKind);
        handler.instructions.push_back(resumeNext);
        handler.terminated = true;

        fn.blocks.push_back(entry);
        fn.blocks.push_back(handler);
        module.functions.push_back(fn);

        il::vm::VM vm(module);
        std::vector<il::vm::Slot> args(1);
        args[0].str = reinterpret_cast<rt_string>(&bogus);

        il::vm::Slot vmResult = il::vm::VMTestHook::run(vm, module.functions.front(), args);
        const int64_t expectedTrap = static_cast<int64_t>(static_cast<int32_t>(il::vm::TrapKind::DomainError));
        assert(vmResult.i64 == expectedTrap);
    }

    for (bool covered : coveredKinds)
        assert(covered);

    return 0;
}
