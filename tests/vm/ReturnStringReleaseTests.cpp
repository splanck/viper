// File: tests/vm/ReturnStringReleaseTests.cpp
// Purpose: Ensure functions returning strings do not leak runtime handles.
// Key invariants: Repeated calls balance refcounts for returned strings.
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

    auto &fn = builder.startFunction("make_str", Type(Type::Kind::Str), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    const unsigned strId = builder.reserveTempId();
    builder.emitCall("rt_str_i32_alloc", {Value::constInt(42)}, Value::temp(strId), kLoc(1));
    builder.emitRet(std::optional<Value>{Value::temp(strId)}, kLoc(2));

    il::vm::VM vm(module);
    auto &makeFn = module.functions.front();

    for (int i = 0; i < 8; ++i)
    {
        il::vm::Slot result = il::vm::VMTestHook::run(vm, makeFn, {});
        if (!result.str)
            return 1;

        auto *impl = reinterpret_cast<rt_string_impl *>(result.str);
        if (!impl)
            return 1;
        rt_heap_hdr_t *header = impl->heap;
        if (!header)
            return 1;
        if (header->refcnt != 1)
            return 1;

        rt_str_release_maybe(result.str);
    }

    return 0;
}
