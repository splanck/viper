//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/EntryParamStringRetainTests.cpp
// Purpose: Verify entry block string parameters retain handles across caller release.
// Key invariants: VM retains incoming strings before transferring to registers.
// Ownership/Lifetime: Builds a synthetic module and inspects runtime heap headers.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include "../unit/VMTestHook.hpp"

#include "rt_internal.h"
#include "viper/runtime/rt.h"

#include <optional>
#include <vector>

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
    builder.addExtern("rt_str_release_maybe", Type(Type::Kind::Void), {Type(Type::Kind::Str)});

    auto &fn = builder.startFunction("entry_param_retains", Type(Type::Kind::I64), {});
    auto &entry = builder.createBlock(fn, "entry", {Param{"payload", Type(Type::Kind::Str), 0}});
    builder.setInsertPoint(entry);
    builder.emitCall("rt_str_release_maybe", {builder.blockParam(entry, 0)}, std::nullopt, kLoc(1));
    builder.emitRet(std::optional<Value>{Value::constInt(0)}, kLoc(2));

    il::vm::VM vm(module);

    // Use a string longer than RT_SSO_MAX_LEN (32) to ensure heap allocation
    static const char *long_str =
        "this_is_a_temp_string_for_testing_heap_refcount_behavior";
    rt_string incoming = rt_string_from_bytes(long_str, 57);
    if (!incoming)
        return 1;
    auto *impl = reinterpret_cast<rt_string_impl *>(incoming);
    if (!impl || !impl->heap || impl->heap == RT_SSO_SENTINEL)
        return 1;
    rt_heap_hdr_t *header = impl->heap;
    const size_t initialRef = header->refcnt;

    il::vm::Slot arg{};
    arg.str = incoming;
    std::vector<il::vm::Slot> args{arg};

    auto state = il::vm::VMTestHook::prepare(vm, fn, args);
    if (entry.params.empty())
        return 1;
    const unsigned paramId = entry.params[0].id;

    auto &pendingOpt = state.fr.params[paramId];
    if (!pendingOpt)
        return 1;
    rt_string staged = pendingOpt->str;
    if (!staged)
        return 1;

    if (header->refcnt != initialRef + 1)
        return 1;

    rt_str_release_maybe(incoming);
    if (header->refcnt != initialRef)
        return 1;

    rt_str_retain_maybe(staged);
    if (header->refcnt != initialRef + 1)
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

    if (header->refcnt != initialRef)
        return 1;

    rt_str_release_maybe(staged);

    return 0;
}
