// File: tests/vm/StringReturnReleaseTests.cpp
// Purpose: Verify that executing functions returning strings does not leak references.
// Key invariants: Repeated executions maintain constant runtime refcounts.
// Ownership/Lifetime: Builds IL module on the fly; releases returned strings explicitly.
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

    auto &fn = builder.startFunction("make_string", Type(Type::Kind::Str), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    const unsigned tmpId = builder.reserveTempId();
    builder.emitCall("rt_str_i32_alloc", {Value::constInt(7)}, Value::temp(tmpId), kLoc(1));
    builder.emitRet(std::optional<Value>{Value::temp(tmpId)}, kLoc(2));

    il::vm::VM vm(module);
    auto &mainFn = module.functions.front();

    constexpr int kIterations = 5;
    size_t expectedRefcount = 0;

    for (int iter = 0; iter < kIterations; ++iter)
    {
        il::vm::Slot result = il::vm::VMTestHook::run(vm, mainFn, {});
        rt_string handle = result.str;
        if (!handle)
            return 1;

        auto *impl = reinterpret_cast<rt_string_impl *>(handle);
        if (!impl || !impl->heap)
            return 1;
        rt_heap_hdr_t *hdr = impl->heap;

        rt_str_retain_maybe(handle);
        const size_t refcount = hdr->refcnt;
        if (iter == 0)
            expectedRefcount = refcount;
        else if (refcount != expectedRefcount)
            return 1;

        rt_str_release_maybe(handle);
        rt_str_release_maybe(handle);
    }

    return 0;
}
