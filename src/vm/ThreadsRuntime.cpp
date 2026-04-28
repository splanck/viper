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
#include "rt_parallel.h"
#include "rt_threadpool.h"
#include "rt_threads.h"
#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include "support/small_vector.hpp"

#include <cstdio>
#include <cstdint>
#include <exception>
#include <string>

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
    bool ownsArg = false;
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

/// @brief Release all resources owned by a VM thread-start payload.
static void releaseThreadStartPayload(VmThreadStartPayload *payload) {
    if (!payload)
        return;
    if (payload->ownsArg && payload->arg) {
        if (rt_obj_release_check0(payload->arg))
            rt_obj_free(payload->arg);
        payload->arg = nullptr;
        payload->ownsArg = false;
    }
    releaseExternRegistry(payload->externRegistry);
    payload->externRegistry = nullptr;
    delete payload;
}

static void vmThreadTrapPassthrough(const RuntimeTrapSignal &, void *) {}

static bool runVmThreadPayload(VmThreadStartPayload *payload, char *errorBuf, size_t errorBufSize) {
    if (errorBuf && errorBufSize > 0)
        errorBuf[0] = '\0';
    if (!payload || !payload->module || !payload->entry) {
        if (errorBuf && errorBufSize > 0)
            std::snprintf(errorBuf, errorBufSize, "%s", "Thread.StartSafe: invalid entry");
        releaseThreadStartPayload(payload);
        return false;
    }

    try {
        VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);
        ScopedRuntimeTrapInterceptor trapInterceptor(&vmThreadTrapPassthrough, nullptr);

        il::support::SmallVector<Slot, 2> args;
        if (payload->entry->params.size() == 1) {
            Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
        }

        detail::VMAccess::callFunction(vm, *payload->entry, args);
    } catch (const RuntimeTrapSignal &signal) {
        if (errorBuf && errorBufSize > 0) {
            std::snprintf(errorBuf,
                          errorBufSize,
                          "%s",
                          signal.message.empty() ? "Thread.StartSafe: trapped VM worker"
                                                 : signal.message.c_str());
        }
        releaseThreadStartPayload(payload);
        return false;
    } catch (...) {
        if (errorBuf && errorBufSize > 0)
            std::snprintf(errorBuf, errorBufSize, "%s", "Thread.StartSafe: unhandled exception");
        releaseThreadStartPayload(payload);
        return false;
    }

    releaseThreadStartPayload(payload);
    return true;
}

/// @brief Thread entry trampoline for VM-backed Thread.Start.
/// @details Validates the payload, creates a new VM bound to the same program
///          state, and invokes the entry function. Any unexpected exception
///          aborts the runtime to avoid silent thread failures.
/// @param raw Opaque pointer to a @ref VmThreadStartPayload.
extern "C" void vm_thread_entry_trampoline(void *raw) {
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        releaseThreadStartPayload(payload);
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
    } catch (const RuntimeTrapSignal &signal) {
        releaseThreadStartPayload(payload);
        const std::string message =
            signal.message.empty() ? "Thread.Start: trapped VM worker" : signal.message;
        rt_abort(message.c_str());
    } catch (const std::exception &ex) {
        releaseThreadStartPayload(payload);
        const std::string message = std::string("Thread.Start: unhandled exception: ") + ex.what();
        rt_abort(message.c_str());
    } catch (...) {
        releaseThreadStartPayload(payload);
        rt_abort("Thread.Start: unhandled non-standard exception");
    }

    releaseThreadStartPayload(payload);
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
        &module, std::move(program), parentVm->externRegistry(), entryFn, arg, false};
    retainExternRegistry(payload->externRegistry);
    void *thread = rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline), payload);
    if (!thread) {
        releaseThreadStartPayload(payload);
        rt_trap("Thread.Start: failed to create thread");
    }
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

/// @brief Runtime bridge handler for Viper.Threads.Thread.StartOwned.
/// @details VM variant retains the managed argument until the IL entry returns.
static void threads_thread_start_owned_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.StartOwned: null entry");

    VM *parentVm = activeVMInstance();
    if (!parentVm) {
        void *thread = rt_thread_start_owned(entry, arg);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    std::shared_ptr<VM::ProgramState> program = parentVm->programState();
    if (!program)
        rt_trap("Thread.StartOwned: invalid runtime state");

    const il::core::Module &module = parentVm->module();
    const il::core::Function *entryFn = resolveEntryFunction(module, entry);
    if (!entryFn)
        rt_trap("Thread.StartOwned: invalid entry");
    validateEntrySignature(*entryFn);

    auto *payload = new VmThreadStartPayload{
        &module, std::move(program), parentVm->externRegistry(), entryFn, arg, arg != nullptr};
    if (payload->ownsArg)
        rt_obj_retain_maybe(arg);
    retainExternRegistry(payload->externRegistry);
    void *thread = rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline), payload);
    if (!thread) {
        releaseThreadStartPayload(payload);
        rt_trap("Thread.StartOwned: failed to create thread");
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
    char error[512];
    if (!runVmThreadPayload(static_cast<VmThreadStartPayload *>(raw), error, sizeof(error)))
        rt_trap(error[0] ? error : "Thread.StartSafe: trapped VM worker");
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
        &module, std::move(program), parentVm->externRegistry(), entryFn, arg, false};
    retainExternRegistry(payload->externRegistry);
    void *thread =
        rt_thread_start_safe(reinterpret_cast<void *>(&vm_thread_safe_entry_trampoline), payload);
    if (!thread) {
        releaseThreadStartPayload(payload);
        rt_trap("Thread.StartSafe: failed to create thread");
    }
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

/// @brief Runtime bridge handler for Viper.Threads.Thread.StartSafeOwned.
/// @details VM variant combines safe trap capture with retaining the managed argument.
static void threads_thread_start_safe_owned_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.StartSafeOwned: null entry");

    VM *parentVm = activeVMInstance();
    if (!parentVm) {
        void *thread = rt_thread_start_safe_owned(entry, arg);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    std::shared_ptr<VM::ProgramState> program = parentVm->programState();
    if (!program)
        rt_trap("Thread.StartSafeOwned: invalid runtime state");

    const il::core::Module &module = parentVm->module();
    const il::core::Function *entryFn = resolveEntryFunction(module, entry);
    if (!entryFn)
        rt_trap("Thread.StartSafeOwned: invalid entry");
    validateEntrySignature(*entryFn);

    auto *payload = new VmThreadStartPayload{
        &module, std::move(program), parentVm->externRegistry(), entryFn, arg, arg != nullptr};
    if (payload->ownsArg)
        rt_obj_retain_maybe(arg);
    retainExternRegistry(payload->externRegistry);
    void *thread =
        rt_thread_start_safe(reinterpret_cast<void *>(&vm_thread_safe_entry_trampoline), payload);
    if (!thread) {
        releaseThreadStartPayload(payload);
        rt_trap("Thread.StartSafeOwned: failed to create thread");
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
        // Worker VMs unwind immediately after resolving; the Future must retain the result.
        rt_promise_set_owned(payload->promise, result.ptr);
    } catch (const RuntimeTrapSignal &signal) {
        const char *message =
            signal.message.empty() ? "Async.Run: trapped VM worker" : signal.message.c_str();
        rt_promise_set_error(payload->promise, rt_const_cstr(message));
    } catch (const std::exception &ex) {
        const std::string message = std::string("Async.Run: unhandled exception: ") + ex.what();
        rt_promise_set_error(payload->promise, rt_const_cstr(message.c_str()));
    } catch (...) {
        rt_promise_set_error(payload->promise,
                             rt_const_cstr("Async.Run: unhandled non-standard exception"));
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
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        if (result)
            *reinterpret_cast<void **>(result) = future;
        return;
    }

    if (rt_obj_release_check0(thread))
        rt_obj_free(thread);
    if (result)
        *reinterpret_cast<void **>(result) = future;
}

static void threads_pool_submit_handler(void **args, void *result) {
    void *pool = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *callback = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    void *arg = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;

    if (activeVMInstance())
        rt_trap("Pool.Submit: VM callback pointers are not supported");

    int8_t submitted = rt_threadpool_submit(pool, callback, arg);
    if (result)
        *reinterpret_cast<int8_t *>(result) = submitted;
}

static void threads_parallel_foreach_handler(void **args, void *result) {
    (void)result;
    void *seq = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *func = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.ForEach: VM callback pointers are not supported");
    rt_parallel_foreach(seq, func);
}

static void threads_parallel_foreach_pool_handler(void **args, void *result) {
    (void)result;
    void *seq = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *func = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    void *pool = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.ForEachPool: VM callback pointers are not supported");
    rt_parallel_foreach_pool(seq, func, pool);
}

static void threads_parallel_map_handler(void **args, void *result) {
    void *seq = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *func = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.Map: VM callback pointers are not supported");
    void *mapped = rt_parallel_map(seq, func);
    if (result)
        *reinterpret_cast<void **>(result) = mapped;
}

static void threads_parallel_map_pool_handler(void **args, void *result) {
    void *seq = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *func = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    void *pool = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.MapPool: VM callback pointers are not supported");
    void *mapped = rt_parallel_map_pool(seq, func, pool);
    if (result)
        *reinterpret_cast<void **>(result) = mapped;
}

static void threads_parallel_invoke_handler(void **args, void *result) {
    (void)result;
    void *funcs = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.Invoke: VM callback pointers are not supported");
    rt_parallel_invoke(funcs);
}

static void threads_parallel_invoke_pool_handler(void **args, void *result) {
    (void)result;
    void *funcs = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *pool = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.InvokePool: VM callback pointers are not supported");
    rt_parallel_invoke_pool(funcs, pool);
}

static void threads_parallel_for_handler(void **args, void *result) {
    (void)result;
    int64_t start = args && args[0] ? *reinterpret_cast<int64_t *>(args[0]) : 0;
    int64_t end = args && args[1] ? *reinterpret_cast<int64_t *>(args[1]) : 0;
    void *func = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.For: VM callback pointers are not supported");
    rt_parallel_for(start, end, func);
}

static void threads_parallel_for_pool_handler(void **args, void *result) {
    (void)result;
    int64_t start = args && args[0] ? *reinterpret_cast<int64_t *>(args[0]) : 0;
    int64_t end = args && args[1] ? *reinterpret_cast<int64_t *>(args[1]) : 0;
    void *func = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;
    void *pool = args && args[3] ? *reinterpret_cast<void **>(args[3]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.ForPool: VM callback pointers are not supported");
    rt_parallel_for_pool(start, end, func, pool);
}

static void threads_parallel_reduce_handler(void **args, void *result) {
    void *seq = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *func = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    void *identity = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.Reduce: VM callback pointers are not supported");
    void *reduced = rt_parallel_reduce(seq, func, identity);
    if (result)
        *reinterpret_cast<void **>(result) = reduced;
}

static void threads_parallel_reduce_pool_handler(void **args, void *result) {
    void *seq = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *func = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    void *identity = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;
    void *pool = args && args[3] ? *reinterpret_cast<void **>(args[3]) : nullptr;
    if (activeVMInstance())
        rt_trap("Parallel.ReducePool: VM callback pointers are not supported");
    void *reduced = rt_parallel_reduce_pool(seq, func, identity, pool);
    if (result)
        *reinterpret_cast<void **>(result) = reduced;
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
        ext.name = il::runtime::names::kThreadsThreadStartOwned;
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_thread_start_owned_handler);
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
        ext.name = il::runtime::names::kThreadsThreadStartSafeOwned;
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_thread_start_safe_owned_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = il::runtime::names::kThreadsAsyncRun;
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_async_run_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Pool.Submit";
        ext.signature =
            make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr, SigParam::Ptr}, {SigParam::I1});
        ext.fn = reinterpret_cast<void *>(&threads_pool_submit_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.ForEach";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_foreach_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.ForEachPool";
        ext.signature =
            make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr, SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_foreach_pool_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.Map";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_map_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.MapPool";
        ext.signature =
            make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_map_pool_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.Invoke";
        ext.signature = make_signature(ext.name, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_invoke_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.InvokePool";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_invoke_pool_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.For";
        ext.signature = make_signature(ext.name, {SigParam::I64, SigParam::I64, SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_for_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.ForPool";
        ext.signature =
            make_signature(ext.name, {SigParam::I64, SigParam::I64, SigParam::Ptr, SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_for_pool_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.Reduce";
        ext.signature =
            make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_reduce_handler);
        RuntimeBridge::registerExtern(ext);
    }
    {
        ExternDesc ext;
        ext.name = "Viper.Threads.Parallel.ReducePool";
        ext.signature = make_signature(
            ext.name, {SigParam::Ptr, SigParam::Ptr, SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&threads_parallel_reduce_pool_handler);
        RuntimeBridge::registerExtern(ext);
    }
}

} // namespace il::vm
