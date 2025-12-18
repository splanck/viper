//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/ThreadsRuntime.cpp
// Purpose: VM-side implementations for Viper.Threads runtime helpers that need
//          VM-aware behavior (notably Thread.Start entry pointers).
//
//===----------------------------------------------------------------------===//

#include "vm/RuntimeBridge.hpp"

#include "il/core/Module.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt.hpp"
#include "rt_threads.h"
#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <cstdint>
#include <vector>

namespace il::vm
{
namespace
{

using il::runtime::signatures::make_signature;
using il::runtime::signatures::SigParam;

struct VmThreadStartPayload
{
    const il::core::Module *module = nullptr;
    std::shared_ptr<VM::ProgramState> program;
    const il::core::Function *entry = nullptr;
    void *arg = nullptr;
};

extern "C" void vm_thread_entry_trampoline(void *raw)
{
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry)
    {
        delete payload;
        rt_abort("Thread.Start: invalid entry");
    }

    try
    {
        VM vm(*payload->module, payload->program);

        std::vector<Slot> args;
        args.reserve(payload->entry->params.size());
        if (payload->entry->params.size() == 1)
        {
            Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
        }

        detail::VMAccess::callFunction(vm, *payload->entry, args);
    }
    catch (...)
    {
        rt_abort("Thread.Start: unhandled exception");
    }

    delete payload;
}

static const il::core::Function *resolveEntryFunction(const il::core::Module &module, void *entry)
{
    if (!entry)
        return nullptr;
    const auto *candidate = static_cast<const il::core::Function *>(entry);
    for (const auto &fn : module.functions)
    {
        if (&fn == candidate)
            return &fn;
    }
    return nullptr;
}

static void validateEntrySignature(const il::core::Function &fn)
{
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Void)
        rt_trap("Thread.Start: invalid entry signature");
    if (fn.params.empty())
        return;
    if (fn.params.size() == 1 && fn.params[0].type.kind == Kind::Ptr)
        return;
    rt_trap("Thread.Start: invalid entry signature");
}

static void threads_thread_start_handler(void **args, void *result)
{
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.Start: null entry");

    VM *parentVm = activeVMInstance();
    if (!parentVm)
    {
        void *thread = rt_thread_start(entry, arg);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    std::shared_ptr<VM::ProgramState> program = parentVm->programState();
    if (!program)
        rt_trap("Thread.Start: invalid runtime state");

    const il::core::Module &module = parentVm->module();
    const il::core::Function *entryFn = resolveEntryFunction(module, entry);
    if (!entryFn)
        rt_trap("Thread.Start: invalid entry");
    validateEntrySignature(*entryFn);

    auto *payload = new VmThreadStartPayload{&module, std::move(program), entryFn, arg};
    void *thread = rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline), payload);
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

} // namespace

void registerThreadsRuntimeExternals()
{
    ExternDesc ext;
    ext.name = "Viper.Threads.Thread.Start";
    ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
    ext.fn = reinterpret_cast<void *>(&threads_thread_start_handler);
    RuntimeBridge::registerExtern(ext);
}

} // namespace il::vm
