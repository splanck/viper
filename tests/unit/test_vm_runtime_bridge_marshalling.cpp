// File: tests/unit/test_vm_runtime_bridge_marshalling.cpp
// Purpose: Validate RuntimeBridge argument and result marshalling for supported types.
// Key invariants: Each IL type kind maps to the correct Slot storage and runtime buffer.
// Ownership: Uses runtime library helpers; callers release any allocated resources.
// Links: docs/class-catalog.md

#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "rt_internal.h"
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

int main()
{
    using il::support::SourceLoc;
    using il::vm::RuntimeBridge;
    using il::vm::RuntimeCallContext;
    using il::vm::Slot;

    RuntimeCallContext ctx{};
    const SourceLoc loc{};
    const std::string fn = "runtime.bridge";
    const std::string block = "entry";

    auto callBridge = [&](const std::string &name, std::vector<Slot> arguments) {
        return RuntimeBridge::call(ctx, name, arguments, loc, fn, block);
    };

    Slot intArg{};
    intArg.i64 = -42;
    Slot result = callBridge("rt_abs_i64", {intArg});
    assert(result.i64 == 42);

    Slot fArg{};
    fArg.f64 = -3.25;
    result = callBridge("rt_abs_f64", {fArg});
    assert(result.f64 == 3.25);

    const char *helloLiteral = "hello";
    Slot ptrArg{};
    ptrArg.ptr = const_cast<char *>(helloLiteral);
    result = callBridge("rt_const_cstr", {ptrArg});
    assert(result.str != nullptr);
    assert(result.str->data == helloLiteral);
    rt_string hello = result.str;

    Slot strArg{};
    strArg.str = hello;
    Slot lenResult = callBridge("rt_len", {strArg});
    assert(lenResult.i64 == 5);
    rt_string_unref(hello);

    Slot numberArg{};
    numberArg.i64 = 12345;
    Slot strNumberResult = callBridge("rt_int_to_str", {numberArg});
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
    Slot strResA = callBridge("rt_const_cstr", {strPtrArgA});
    Slot strResB = callBridge("rt_const_cstr", {strPtrArgB});
    Slot eqArgA{};
    eqArgA.str = strResA.str;
    Slot eqArgB{};
    eqArgB.str = strResB.str;
    Slot eqResult = callBridge("rt_str_eq", {eqArgA, eqArgB});
    assert(eqResult.i64 == 1);
    rt_string_unref(strResA.str);
    rt_string_unref(strResB.str);

    Slot allocArg{};
    allocArg.i64 = 16;
    Slot allocResult = callBridge("rt_alloc", {allocArg});
    assert(allocResult.ptr != nullptr);
    free(allocResult.ptr);

    Slot seedArg{};
    seedArg.i64 = 42;
    Slot voidResult = callBridge("rt_randomize_i64", {seedArg});
    assert(voidResult.i64 == 0);

    return 0;
}
