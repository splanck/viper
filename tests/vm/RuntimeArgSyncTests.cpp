// File: tests/vm/RuntimeArgSyncTests.cpp
// Purpose: Ensure runtime bridge propagates mutated call arguments back to VM state.
// Key invariants: Pointer-to-pointer arguments updated by the runtime appear in VM memory.
// Ownership/Lifetime: Builds a synthetic module that exercises the runtime bridge.
// Links: docs/il-guide.md#reference

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include "../unit/VMTestHook.hpp"

#include "rt_file.h"
#include "rt_string.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace il::core;

namespace
{
il::support::SourceLoc loc(unsigned line)
{
    return {1, static_cast<uint32_t>(line), 0};
}
} // namespace

int main()
{
    constexpr int32_t kChannel = 7;
    constexpr const char *kExpectedLine = "runtime line";

    char pathBuffer[128];
    const int written = std::snprintf(pathBuffer, sizeof(pathBuffer), "vm_runtime_arg_sync_%ld.txt", static_cast<long>(getpid()));
    if (written <= 0 || written >= static_cast<int>(sizeof(pathBuffer)))
        return 1;

    std::remove(pathBuffer);
    std::FILE *file = std::fopen(pathBuffer, "w");
    if (!file)
        return 1;
    if (std::fputs(kExpectedLine, file) < 0 || std::fputc('\n', file) == EOF)
    {
        std::fclose(file);
        return 1;
    }
    std::fclose(file);

    ViperString *path = rt_const_cstr(pathBuffer);
    if (!path)
        return 1;

    int32_t openRc = rt_open_err_vstr(path, RT_F_INPUT, kChannel);
    if (openRc != Err_None)
    {
        rt_string_unref(path);
        std::remove(pathBuffer);
        return 1;
    }

    Module module;
    il::build::IRBuilder builder(module);
    builder.addExtern("rt_line_input_ch_err", Type(Type::Kind::I32), {Type(Type::Kind::I32), Type(Type::Kind::Ptr)});
    builder.addExtern("rt_str_release_maybe", Type(Type::Kind::Void), {Type(Type::Kind::Str)});

    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    const unsigned ptrId = builder.reserveTempId();
    Instr alloca;
    alloca.result = ptrId;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(static_cast<long long>(sizeof(rt_string))));
    alloca.loc = loc(1);
    entry.instructions.push_back(alloca);

    const unsigned errId = builder.reserveTempId();
    Instr call;
    call.result = errId;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I32);
    call.callee = "rt_line_input_ch_err";
    call.operands.push_back(Value::constInt(kChannel));
    call.operands.push_back(Value::temp(ptrId));
    call.loc = loc(2);
    entry.instructions.push_back(call);

    const unsigned strId = builder.reserveTempId();
    Instr load;
    load.result = strId;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::Str);
    load.operands.push_back(Value::temp(ptrId));
    load.loc = loc(3);
    entry.instructions.push_back(load);

    builder.emitCall("rt_str_release_maybe", {Value::temp(strId)}, std::nullopt, loc(4));
    builder.emitRet(std::optional<Value>{Value::constInt(0)}, loc(5));

    il::vm::VM vm(module);
    auto &mainFn = module.functions.front();
    il::vm::VMTestHook::State state = il::vm::VMTestHook::prepare(vm, mainFn);

    auto stepExpectIp = [&](size_t expectedIp) -> bool {
        auto result = il::vm::VMTestHook::step(vm, state);
        if (result)
            return true;
        return state.ip != expectedIp;
    };

    int rc = 0;
    bool releaseInvoked = false;
    rt_string lineHandle = nullptr;
    rt_string *slotPtr = nullptr;
    const char *lineView = nullptr;

    if (stepExpectIp(1))
    {
        rc = 1;
        goto cleanup;
    }

    slotPtr = reinterpret_cast<rt_string *>(state.fr.regs[ptrId].ptr);
    if (!slotPtr || *slotPtr != nullptr)
    {
        rc = 1;
        goto cleanup;
    }

    if (stepExpectIp(2))
    {
        rc = 1;
        goto cleanup;
    }

    if (!slotPtr)
    {
        rc = 1;
        goto cleanup;
    }

    lineHandle = *slotPtr;
    if (!lineHandle)
    {
        rc = 1;
        goto cleanup;
    }

    if (state.fr.regs[errId].i64 != 0)
    {
        rc = 1;
        goto cleanup;
    }

    lineView = rt_string_cstr(lineHandle);
    if (!lineView || std::strcmp(lineView, kExpectedLine) != 0)
    {
        rc = 1;
        goto cleanup;
    }

    if (stepExpectIp(3))
    {
        rc = 1;
        goto cleanup;
    }

    if (state.fr.regs[strId].str != lineHandle)
    {
        rc = 1;
        goto cleanup;
    }

    if (stepExpectIp(4))
    {
        rc = 1;
        goto cleanup;
    }

    releaseInvoked = true;

    while (true)
    {
        auto result = il::vm::VMTestHook::step(vm, state);
        if (!result)
            continue;
        if (result->i64 != 0)
        {
            rc = 1;
            goto cleanup;
        }
        break;
    }

cleanup:
    if (lineHandle && !releaseInvoked)
        rt_str_release_maybe(lineHandle);
    rt_close_err(kChannel);
    std::remove(pathBuffer);
    rt_string_unref(path);
    return rc;
}
