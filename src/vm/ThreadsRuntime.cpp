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

/// @file
/// @brief VM-aware runtime helpers for Viper.Threads.
/// @details Implements the Thread.Start bridge so Viper threads can invoke
///          IL entry functions directly when running inside the VM.

#include "vm/RuntimeBridge.hpp"

#include "il/core/Module.hpp"
#include "il/runtime/RuntimeNames.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt.hpp"
#include "rt_async.h"
#include "rt_future.h"
#include "rt_object.h"
#include "rt_threads.h"
#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include "support/small_vector.hpp"

#include <cstdint>

namespace il::vm {
namespace {

using il::runtime::signatures::make_signature;
using il::runtime::signatures::SigParam;

/// @brief Payload passed to the thread entry trampoline.
/// @details Captures the module, program state, entry function, and user arg
///          so a new VM can be created and invoked on the target function.
struct VmThreadStartPayload {
    const il::core::Module *module = nullptr;
    std::shared_ptr<VM::ProgramState> program;
    ExternRegistry *externRegistry = nullptr;
    const il::core::Function *entry = nullptr;
    void *arg = nullptr;
};

/// @brief Payload passed to VM-backed Async.Run worker threads.
/// @details Extends the basic thread payload with a promise reference owned by
///          the worker thread until it resolves the asynchronous result.
struct VmAsyncRunPayload {
    const il::core::Module *module = nullptr;
    std::shared_ptr<VM::ProgramState> program;
    ExternRegistry *externRegistry = nullptr;
    const il::core::Function *entry = nullptr;
    void *arg = nullptr;
    void *promise = nullptr;
};

/// @brief Thread entry trampoline for VM-backed Thread.Start.
/// @details Validates the payload, creates a new VM bound to the same program
///          state, and invokes the entry function. Any unexpected exception
///          aborts the runtime to avoid silent thread failures.
/// @param raw Opaque pointer to a @ref VmThreadStartPayload.
extern "C" void vm_thread_entry_trampoline(void *raw) {
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        if (payload)
            releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_abort("Thread.Start: invalid entry");
    }

    try {
        VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);

        il::support::SmallVector<Slot, 2> args;
        if (payload->entry->params.size() == 1) {
            Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
        }

        detail::VMAccess::callFunction(vm, *payload->entry, args);
    } catch (...) {
        releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        rt_abort("Thread.Start: unhandled exception");
    }

    releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// @brief Resolve a function pointer into a module function.
/// @details The runtime passes a raw function pointer; this helper verifies it
///          matches one of the module's function objects before use.
/// @param module Module containing candidate functions.
/// @param entry Raw pointer supplied by the runtime.
/// @return Pointer to the matching function, or nullptr if invalid.
static const il::core::Function *resolveEntryFunction(const il::core::Module &module, void *entry) {
    if (!entry)
        return nullptr;
    const auto *candidate = static_cast<const il::core::Function *>(entry);
    for (const auto &fn : module.functions) {
        if (&fn == candidate)
            return &fn;
    }
    return nullptr;
}

/// @brief Validate the signature of a thread entry function.
/// @details Thread entry functions must return void and accept either zero
///          parameters or a single pointer parameter. Violations trap with a
///          diagnostic message.
/// @param fn Function to validate.
static void validateEntrySignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Void)
        rt_trap("Thread.Start: invalid entry signature");
    if (fn.params.empty())
        return;
    if (fn.params.size() == 1 && fn.params[0].type.kind == Kind::Ptr)
        return;
    rt_trap("Thread.Start: invalid entry signature");
}

/// @brief Validate the signature of an Async.Run worker entry function.
/// @details Async worker trampolines lowered from Zia async functions must
///          return an object pointer and accept exactly one environment pointer.
static void validateAsyncEntrySignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Ptr)
        rt_trap("Async.Run: invalid entry signature");
    if (fn.params.size() == 1 && fn.params[0].type.kind == Kind::Ptr)
        return;
    rt_trap("Async.Run: invalid entry signature");
}

/// @brief Runtime bridge handler for Viper.Threads.Thread.Start.
/// @details When running inside a VM, this handler validates the entry function
///          pointer, constructs a thread payload, and spawns a native thread
///          that executes the IL entry via the trampoline. Outside the VM it
///          forwards directly to @ref rt_thread_start.
/// @param args Argument array provided by the runtime bridge.
/// @param result Optional out-parameter to receive the thread handle.
static void threads_thread_start_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.Start: null entry");

    VM *parentVm = activeVMInstance();
    if (!parentVm) {
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

    auto *payload = new VmThreadStartPayload{
        &module, std::move(program), parentVm->externRegistry(), entryFn, arg};
    retainExternRegistry(payload->externRegistry);
    void *thread = rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline), payload);
    if (!thread) {
        releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_trap("Thread.Start: failed to create thread");
    }
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

/// @brief Safe thread entry trampoline that captures trap errors.
/// @details Like vm_thread_entry_trampoline but wraps execution in a
///          setjmp/longjmp recovery point so traps are captured instead of
///          terminating the process.
/// @param raw Opaque pointer to a @ref VmThreadStartPayload.
extern "C" void vm_thread_safe_entry_trampoline(void *raw) {
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        if (payload)
            releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_abort("Thread.StartSafe: invalid entry");
    }

    try {
        VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);

        il::support::SmallVector<Slot, 2> args;
        if (payload->entry->params.size() == 1) {
            Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
        }

        detail::VMAccess::callFunction(vm, *payload->entry, args);
    } catch (...) {
        // Trap was intercepted by setjmp in the caller (safe_thread_entry).
        // If not, this is an unexpected exception.  Cannot re-throw from
        // extern "C" linkage (UB / MSVC C4297), so abort.
        releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_abort("Thread.StartSafe: unhandled exception in thread entry");
        return;
    }

    releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// @brief Runtime bridge handler for Viper.Threads.Thread.StartSafe.
/// @details Like threads_thread_start_handler but uses the safe entry trampoline
///          that wraps execution in trap recovery via setjmp/longjmp.
static void threads_thread_start_safe_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.StartSafe: null entry");

    VM *parentVm = activeVMInstance();
    if (!parentVm) {
        void *thread = rt_thread_start_safe(entry, arg);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    std::shared_ptr<VM::ProgramState> program = parentVm->programState();
    if (!program)
        rt_trap("Thread.StartSafe: invalid runtime state");

    const il::core::Module &module = parentVm->module();
    const il::core::Function *entryFn = resolveEntryFunction(module, entry);
    if (!entryFn)
        rt_trap("Thread.StartSafe: invalid entry");
    validateEntrySignature(*entryFn);

    auto *payload = new VmThreadStartPayload{
        &module, std::move(program), parentVm->externRegistry(), entryFn, arg};
    retainExternRegistry(payload->externRegistry);
    void *thread =
        rt_thread_start_safe(reinterpret_cast<void *>(&vm_thread_safe_entry_trampoline), payload);
    if (!thread) {
        releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_trap("Thread.StartSafe: failed to create thread");
    }
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

/// @brief Async worker trampoline for VM-backed Async.Run.
/// @details Executes the IL worker function in a child VM, then resolves the
///          promise with the returned object pointer.
extern "C" void vm_async_run_entry_trampoline(void *raw) {
    VmAsyncRunPayload *payload = static_cast<VmAsyncRunPayload *>(raw);
    if (!payload || !payload->module || !payload->entry || !payload->promise) {
        if (payload)
            releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_abort("Async.Run: invalid entry");
    }

    try {
        VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);

        il::support::SmallVector<Slot, 2> args;
        Slot argSlot{};
        argSlot.ptr = payload->arg;
        args.push_back(argSlot);

        Slot result = detail::VMAccess::callFunction(vm, *payload->entry, args);
        rt_promise_set(payload->promise, result.ptr);
    } catch (...) {
        rt_promise_set_error(payload->promise, rt_const_cstr("Async.Run: unhandled exception"));
    }

    if (rt_obj_release_check0(payload->promise))
        rt_obj_free(payload->promise);
    releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// @brief Runtime bridge handler for Viper.Threads.Async.Run.
/// @details Uses the native runtime helper outside the VM, but when an IL
///          function pointer is supplied from VM execution it spawns a child VM
///          and resolves a Future with that worker's returned object.
static void threads_async_run_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Async.Run: null entry");

    VM *parentVm = activeVMInstance();
    if (!parentVm) {
        void *future = rt_async_run(entry, arg);
        if (result)
            *reinterpret_cast<void **>(result) = future;
        return;
    }

    std::shared_ptr<VM::ProgramState> program = parentVm->programState();
    if (!program)
        rt_trap("Async.Run: invalid runtime state");

    const il::core::Module &module = parentVm->module();
    const il::core::Function *entryFn = resolveEntryFunction(module, entry);
    if (!entryFn)
        rt_trap("Async.Run: invalid entry");
    validateAsyncEntrySignature(*entryFn);

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    auto *payload = new VmAsyncRunPayload{
        &module, std::move(program), parentVm->externRegistry(), entryFn, arg, promise};
    retainExternRegistry(payload->externRegistry);
    void *thread =
        rt_thread_start(reinterpret_cast<void *>(&vm_async_run_entry_trampoline), payload);
    if (!thread) {
        releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_promise_set_error(promise, rt_const_cstr("Async.Run: failed to create thread"));
        if (result)
            *reinterpret_cast<void **>(result) = future;
        return;
    }

    if (rt_obj_release_check0(thread))
        rt_obj_free(thread);
    if (result)
        *reinterpret_cast<void **>(result) = future;
}

} // namespace

/// @brief Register VM-aware thread externals with the runtime bridge.
/// @details Installs the `Viper.Threads.Thread.Start`, `Thread.StartSafe`, and
///          `Async.Run`
///          handlers so they use the VM trampoline when invoked from managed code.
///          When the BytecodeVM is linked, its unified handlers overwrite these
///          registrations via a static initializer.
void registerThreadsRuntimeExternals() {
    {
        ExternDesc ext;
        ext.name = il::runtime::names::kThreadsThreadStart;
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_thread_start_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = il::runtime::names::kThreadsThreadStartSafe;
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_thread_start_safe_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = il::runtime::names::kThreadsAsyncRun;
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_async_run_handler);
        RuntimeBridge::registerExtern(ext);
    }
}

} // namespace il::vm
