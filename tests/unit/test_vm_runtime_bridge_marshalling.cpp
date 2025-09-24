// File: tests/unit/test_vm_runtime_bridge_marshalling.cpp
// Purpose: Validate RuntimeBridge argument and result marshalling for supported types.
// Key invariants: Each IL type kind maps to the correct Slot storage and runtime buffer.
// Ownership: Uses runtime library helpers; callers release any allocated resources.
// Links: docs/codemap.md

#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "il/core/Type.hpp"
#include "rt_internal.h"
#include <array>
#include <initializer_list>
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

int main()
{
    using il::support::SourceLoc;
    using il::core::Type;
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
    std::string numberText(numberStr->data, static_cast<size_t>(numberStr->size));
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

    free(arrSlot.ptr);

    for (bool covered : coveredKinds)
        assert(covered);

    return 0;
}
