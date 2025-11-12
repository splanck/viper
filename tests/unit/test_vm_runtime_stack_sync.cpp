// File: tests/unit/test_vm_runtime_stack_sync.cpp
// Purpose: Verify runtime helpers writing through stack out-pointers propagate updates.
// Key invariants: Runtime call result length matches the source line written to disk.
// Ownership/Lifetime: Test manages temporary files and runtime handles locally.
// Links: docs/codemap.md

#include "il/build/IRBuilder.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include "VMTestHook.hpp"

#include "rt_internal.h"
#include "viper/runtime/rt.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

using namespace il::core;

namespace
{
constexpr il::support::SourceLoc kLoc(unsigned line)
{
    return {1, static_cast<uint32_t>(line), 0};
}

std::filesystem::path makeTempFile()
{
    namespace fs = std::filesystem;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path path = fs::temp_directory_path();
    path /= "viper_vm_stack_sync_" + std::to_string(unique) + ".txt";
    return path;
}
} // namespace

int main()
{
    namespace fs = std::filesystem;

    const int32_t channel = 47;
    const std::string payload = "stack-sync";

    const fs::path tempPath = makeTempFile();
    {
        std::ofstream out(tempPath, std::ios::binary);
        if (!out)
            return 1;
        out << payload << '\n';
    }

    const std::string pathStorage = tempPath.string();
    ViperString *pathHandle = rt_const_cstr(pathStorage.c_str());
    if (!pathHandle)
        return 1;

    const int32_t openStatus = rt_open_err_vstr(pathHandle, RT_F_INPUT, channel);
    if (openStatus != 0)
    {
        rt_string_unref(pathHandle);
        fs::remove(tempPath);
        return 1;
    }

    Module module;
    il::build::IRBuilder builder(module);
    builder.addExtern("rt_line_input_ch_err",
                      Type(Type::Kind::I32),
                      {Type(Type::Kind::I32), Type(Type::Kind::Ptr)});
    builder.addExtern("rt_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    builder.addExtern("rt_str_release_maybe", Type(Type::Kind::Void), {Type(Type::Kind::Str)});

    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    const unsigned slotId = builder.reserveTempId();
    Instr allocaInstr;
    allocaInstr.result = slotId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands.push_back(Value::constInt(static_cast<int64_t>(sizeof(rt_string))));
    allocaInstr.loc = kLoc(1);
    entry.instructions.push_back(allocaInstr);

    const unsigned statusId = builder.reserveTempId();
    builder.emitCall("rt_line_input_ch_err",
                     {Value::constInt(channel), Value::temp(slotId)},
                     Value::temp(statusId),
                     kLoc(2));

    const unsigned lineId = builder.reserveTempId();
    Instr loadLine;
    loadLine.result = lineId;
    loadLine.op = Opcode::Load;
    loadLine.type = Type(Type::Kind::Str);
    loadLine.operands.push_back(Value::temp(slotId));
    loadLine.loc = kLoc(3);
    entry.instructions.push_back(loadLine);

    const unsigned lengthId = builder.reserveTempId();
    builder.emitCall("rt_len", {Value::temp(lineId)}, Value::temp(lengthId), kLoc(4));

    builder.emitCall("rt_str_release_maybe", {Value::temp(lineId)}, std::nullopt, kLoc(5));
    builder.emitRet(std::optional<Value>{Value::temp(lengthId)}, kLoc(6));

    il::vm::VM vm(module);
    auto &mainFn = module.functions.front();
    auto state = il::vm::VMTestHook::prepare(vm, mainFn);

    auto step = [&]() -> std::optional<il::vm::Slot>
    { return il::vm::VMTestHook::step(vm, state); };

    if (step())
    {
        std::fprintf(stderr, "alloca returned unexpectedly\n");
        return 1;
    }

    auto *slotAddress = reinterpret_cast<rt_string *>(state.fr.regs[slotId].ptr);
    if (!slotAddress)
    {
        std::fprintf(stderr, "alloca slot pointer missing\n");
        return 1;
    }

    if (step())
    {
        std::fprintf(stderr, "call returned unexpectedly\n");
        return 1;
    }

    const int32_t callStatus = static_cast<int32_t>(state.fr.regs[statusId].i64);
    if (callStatus != 0)
    {
        std::fprintf(stderr, "runtime status %d\n", callStatus);
        return 1;
    }

    rt_string captured = *slotAddress;
    if (!captured)
    {
        std::fprintf(stderr, "stack slot unchanged\n");
        return 1;
    }

    const char *capturedData = rt_string_cstr(captured);
    if (!capturedData)
    {
        std::fprintf(stderr, "captured string view missing\n");
        return 1;
    }

    if (std::string_view(capturedData) != payload)
    {
        std::fprintf(stderr, "unexpected payload '%s'\n", capturedData);
        return 1;
    }

    auto *capturedImpl = reinterpret_cast<rt_string_impl *>(captured);
    if (!capturedImpl)
    {
        std::fprintf(stderr, "missing captured impl\n");
        return 1;
    }

    if (!capturedImpl->heap)
    {
        std::fprintf(stderr, "captured string is not heap backed\n");
        return 1;
    }

    const size_t preCallRefs = capturedImpl->heap->refcnt;
    if (preCallRefs != 1)
    {
        std::fprintf(stderr, "unexpected retained refs: %zu\n", preCallRefs);
        return 1;
    }

    if (step())
    {
        std::fprintf(stderr, "load returned unexpectedly\n");
        return 1;
    }

    if (step())
    {
        std::fprintf(stderr, "len call returned unexpectedly\n");
        return 1;
    }

    if (step())
    {
        std::fprintf(stderr, "release returned unexpectedly\n");
        return 1;
    }

    std::optional<il::vm::Slot> result = step();
    if (!result)
    {
        std::fprintf(stderr, "missing return value\n");
        return 1;
    }

    const int32_t closeStatus = rt_close_err(channel);
    rt_string_unref(pathHandle);
    fs::remove(tempPath);

    if (openStatus != 0)
    {
        std::fprintf(stderr, "open failed: %d\n", openStatus);
        return 1;
    }

    if (closeStatus != 0)
    {
        std::fprintf(stderr, "close failed: %d\n", closeStatus);
        return 1;
    }

    if (result->i64 != static_cast<int64_t>(payload.size()))
    {
        std::fprintf(stderr, "length mismatch: %lld\n", static_cast<long long>(result->i64));
        return 1;
    }

    return 0;
}
