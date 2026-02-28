//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_context.c
// Purpose: Per-VM runtime context management. Each Viper VM instance owns an
//   RtContext that holds all per-VM state: RNG seed, open file handles,
//   command-line arguments, module-level variables, and the OOP type registry.
//   Multiple independent VMs can coexist in a single process because all
//   mutable state is confined to the context rather than process-global vars.
//
// Key invariants:
//   - A context is bound to a thread via a thread-local pointer
//     (_Thread_local RtContext *g_rt_context). At most one context is active
//     per thread at any time; VMs must bind before executing and unbind after.
//   - bind_count is an atomic reference count incremented on bind and
//     decremented on unbind. When it reaches 0, the context's open files and
//     registered types are migrated to the legacy context so native post-VM
//     code continues to work correctly.
//   - A process-wide legacy context is lazily initialized exactly once (atomic
//     compare-exchange) and used as a fallback when g_rt_context is NULL. It
//     is never explicitly destroyed.
//   - rt_context_init() zero-initializes all subsystems. Callers must call
//     rt_context_cleanup() to free heap resources when done.
//   - Contexts must not be shared across threads without external
//     synchronization; the thread-local binding pattern is the intended idiom.
//
// Ownership/Lifetime:
//   - The embedding application (VM or host) owns the RtContext struct storage.
//     The runtime does not allocate or free the struct itself.
//   - Internal heap allocations (file state, args, type registry) are freed
//     by rt_context_cleanup().
//
// Links: src/runtime/core/rt_context.h (public API),
//        src/runtime/core/rt_file.c (file handle state),
//        src/runtime/core/rt_args.c (command-line argument state),
//        src/runtime/core/rt_type_registry.c (OOP type registration),
//        src/runtime/core/rt_modvar.c (module variable storage)
//
//===----------------------------------------------------------------------===//

#include "rt_context.h"
#include "rt_internal.h"
#include "rt_platform.h"
#include "rt_string.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
// GetCurrentThreadId() is available via windows.h (included from rt_platform.h)
#elif !RT_PLATFORM_VIPERDOS
#include <pthread.h>
#endif

void rt_file_state_cleanup(RtContext *ctx);
void rt_type_registry_cleanup(RtContext *ctx);
void rt_args_state_cleanup(RtContext *ctx);

/// @brief Thread-local pointer to the active runtime context.
///
/// Each thread can have at most one active VM context bound at a time.
/// The VM sets this pointer before executing Viper code and clears it
/// afterward. When NULL, runtime functions fall back to the legacy context.
///
/// @note Thread-local storage ensures thread safety without locking.
RT_THREAD_LOCAL RtContext *g_rt_context = NULL;

/// @brief Global legacy context for backward compatibility.
///
/// Used when no VM context is bound to the current thread. This enables
/// native code and tests to use runtime functions without explicit context
/// management. Lazily initialized on first access.
static RtContext g_legacy_ctx;

/// @brief Initialization state for the legacy context.
///
/// State machine:
/// - 0: Uninitialized (initial state)
/// - 1: Initializing (one thread is running rt_context_init)
/// - 2: Initialized (ready for use)
///
/// Uses atomic operations to ensure exactly-once initialization even under
/// concurrent access from multiple threads.
static int g_legacy_state = 0;

/// @brief Spinlock protecting state handoff between VM and legacy contexts.
///
/// Serializes the transfer of file handles, arguments, and type registrations
/// during context bind/unbind to prevent data races.
static int g_legacy_handoff_lock = 0;

/// @brief Acquire a simple spinlock.
///
/// Uses atomic test-and-set with acquire semantics. Spins without yielding
/// since this lock protects very short critical sections during context
/// handoff only.
///
/// @param lock Pointer to the lock variable (0 = unlocked, 1 = locked).
///
/// @note Only used for init/handoff paths where contention is rare.
/// @warning Do not use for long-held locks or high-contention scenarios.
static void rt_spin_lock(int *lock)
{
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE))
    {
        // spin (init/handoff paths only)
    }
}

/// @brief Release a spinlock.
///
/// Uses atomic clear with release semantics to ensure all writes within
/// the critical section are visible to subsequent lock acquirers.
///
/// @param lock Pointer to the lock variable to release.
static void rt_spin_unlock(int *lock)
{
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

//===----------------------------------------------------------------------===//
// Main-Thread Tracking
//===----------------------------------------------------------------------===//

#if RT_PLATFORM_WINDOWS
static DWORD g_main_thread_id_;
#elif RT_PLATFORM_VIPERDOS
// ViperDOS is single-threaded; no tracking needed.
#else
static pthread_t g_main_thread_;
#endif
static int g_main_thread_set_ = 0;

void rt_set_main_thread(void)
{
#if RT_PLATFORM_WINDOWS
    g_main_thread_id_ = GetCurrentThreadId();
#elif !RT_PLATFORM_VIPERDOS
    g_main_thread_ = pthread_self();
#endif
    __atomic_store_n(&g_main_thread_set_, 1, __ATOMIC_RELEASE);
}

int rt_is_main_thread(void)
{
    if (!__atomic_load_n(&g_main_thread_set_, __ATOMIC_ACQUIRE))
        return 1; // Before init, assume main thread (avoids false positives)
#if RT_PLATFORM_WINDOWS
    return GetCurrentThreadId() == g_main_thread_id_;
#elif RT_PLATFORM_VIPERDOS
    return 1;
#else
    return pthread_equal(pthread_self(), g_main_thread_);
#endif
}

void rt_assert_main_thread_(const char *file, int line)
{
    if (!rt_is_main_thread())
    {
        fprintf(stderr, "%s:%d: GUI/input state accessed from non-main thread\n", file, line);
        abort();
    }
}

/// @brief Ensure the legacy context is initialized (thread-safe).
///
/// Implements a thread-safe double-checked locking pattern using atomics.
/// The first caller to observe state=0 transitions to state=1, initializes
/// the context, then transitions to state=2. Concurrent callers wait by
/// spinning until state reaches 2.
///
/// **Initialization sequence:**
/// ```
///          Thread A                Thread B
///             │                       │
///     load state=0                    │
///     CAS 0→1 succeeds                │
///             │               load state=1
///     rt_context_init()       spin waiting...
///             │                       │
///     store state=2                   │
///             │               load state=2
///     return                  return
/// ```
///
/// @note Called automatically by rt_legacy_context() and rt_set_current_context().
static void rt_legacy_ensure_init(void)
{
    int state = __atomic_load_n(&g_legacy_state, __ATOMIC_ACQUIRE);
    if (state == 2)
        return;

    int expected = 0;
    if (__atomic_compare_exchange_n(
            &g_legacy_state, &expected, 1, /*weak=*/0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        rt_context_init(&g_legacy_ctx);
        // The first thread to reach here is the main thread.
        if (!__atomic_load_n(&g_main_thread_set_, __ATOMIC_ACQUIRE))
            rt_set_main_thread();
        __atomic_store_n(&g_legacy_state, 2, __ATOMIC_RELEASE);
        return;
    }

    // Another thread is initializing; wait.
    while (__atomic_load_n(&g_legacy_state, __ATOMIC_ACQUIRE) != 2)
    {
        // spin
    }
}

/// @brief Initialize a runtime context with default values.
///
/// Sets up a fresh context with:
/// - Deterministic RNG seed for reproducible random number sequences
/// - Empty file handle table
/// - Empty argument list
/// - Empty module variable storage
/// - Empty type registry
/// - Zero bind count (no threads attached)
///
/// **Usage example:**
/// ```c
/// RtContext ctx;
/// rt_context_init(&ctx);
///
/// // Use the context
/// rt_set_current_context(&ctx);
/// // ... execute Viper code ...
/// rt_set_current_context(NULL);
///
/// // Cleanup when done
/// rt_context_cleanup(&ctx);
/// ```
///
/// @param ctx Pointer to the context to initialize. Must not be NULL.
///
/// @note Does not allocate memory; sets up empty arrays with zero capacity.
/// @note The deterministic seed ensures tests produce repeatable results.
///
/// @see rt_context_cleanup For releasing resources when done
void rt_context_init(RtContext *ctx)
{
    if (!ctx)
        return;

    // Initialize RNG with deterministic seed (same as old global default)
    ctx->rng_state = 0xDEADBEEFCAFEBABEULL;

    // Initialize empty modvar table
    ctx->modvar_entries = NULL;
    ctx->modvar_count = 0;
    ctx->modvar_capacity = 0;

    // Initialize file state
    ctx->file_state.entries = NULL;
    ctx->file_state.count = 0;
    ctx->file_state.capacity = 0;

    // Initialize argument store
    ctx->args_state.items = NULL;
    ctx->args_state.size = 0;
    ctx->args_state.cap = 0;

    // Initialize type registry state
    ctx->type_registry.classes = NULL;
    ctx->type_registry.classes_len = 0;
    ctx->type_registry.classes_cap = 0;
    ctx->type_registry.ifaces = NULL;
    ctx->type_registry.ifaces_len = 0;
    ctx->type_registry.ifaces_cap = 0;
    ctx->type_registry.bindings = NULL;
    ctx->type_registry.bindings_len = 0;
    ctx->type_registry.bindings_cap = 0;

    ctx->bind_count = 0;
}

/// @brief Cleanup a runtime context and free owned resources.
///
/// Releases all resources associated with a context:
/// - Closes open file handles
/// - Releases command-line argument strings
/// - Frees module variable storage and releases any string values
/// - Frees registered class/interface metadata
///
/// After cleanup, the context can be reused by calling rt_context_init() again.
///
/// **Cleanup order:**
/// 1. File state (closes any open files)
/// 2. Argument state (releases string references)
/// 3. Module variables (releases string slot values, frees names and storage)
/// 4. Type registry (frees owned class info structures)
///
/// @param ctx Pointer to the context to clean up. Ignored if NULL.
///
/// @note Safe to call on an already-cleaned or uninitialized context.
/// @note Should only be called when no threads are using the context.
///
/// @see rt_context_init For initializing a context
void rt_context_cleanup(RtContext *ctx)
{
    if (!ctx)
        return;

    rt_file_state_cleanup(ctx);
    rt_args_state_cleanup(ctx);

    for (size_t i = 0; i < ctx->modvar_count; ++i)
    {
        RtModvarEntry *e = &ctx->modvar_entries[i];
        if (e->kind == 4 && e->addr)
        {
            rt_string *slot = (rt_string *)e->addr;
            if (slot && *slot)
            {
                rt_string_unref(*slot);
                *slot = NULL;
            }
        }
        free(e->name);
        free(e->addr);
        e->name = NULL;
        e->addr = NULL;
        e->size = 0;
        e->kind = 0;
    }
    free(ctx->modvar_entries);
    ctx->modvar_entries = NULL;
    ctx->modvar_count = 0;
    ctx->modvar_capacity = 0;

    rt_type_registry_cleanup(ctx);
}

/// @brief Bind a runtime context to the current thread.
///
/// Associates a context with the calling thread, enabling all runtime
/// functions to use that context's state. This is the primary mechanism
/// by which VMs execute Viper code with isolated state.
///
/// **Binding lifecycle:**
/// ```
/// Thread                              Context
///   │                                    │
///   │── set_current_context(ctx) ───────▶│  bind_count++
///   │                                    │
///   │          [execute code]            │
///   │                                    │
///   │── set_current_context(NULL) ──────▶│  bind_count--
///   │                                    │
/// ```
///
/// **State transfer on first bind:**
/// When a context is bound for the first time (bind_count 0→1), any state
/// accumulated in the legacy context is transferred to the new context:
/// - File handles (if destination empty)
/// - Command-line arguments (if destination empty)
/// - Type registrations (if destination empty)
///
/// **State transfer on last unbind:**
/// When the last thread unbinds (bind_count 1→0 with NULL destination),
/// state is transferred back to the legacy context so code running after
/// VM exit continues to work.
///
/// @param ctx Context to bind, or NULL to unbind the current context.
///
/// @note Thread-safe: updates are serialized by atomic counters and spinlock.
/// @note Binding the same context that's already bound is a no-op.
/// @note Calling with NULL when no context is bound is a no-op.
///
/// @see rt_get_current_context For reading the current context
/// @see rt_legacy_context For the fallback when no context is bound
void rt_set_current_context(RtContext *ctx)
{
    RtContext *old = g_rt_context;
    if (old == ctx)
        return;
    g_rt_context = ctx;

    if (old)
    {
        size_t prev = __atomic_fetch_sub(&old->bind_count, 1, __ATOMIC_ACQ_REL);
        assert(prev > 0);
        if (prev == 1 && ctx == NULL)
        {
            // Last thread unbound: move state back to legacy so calls after VM exit keep working.
            rt_legacy_ensure_init();
            rt_spin_lock(&g_legacy_handoff_lock);

            if (g_legacy_ctx.file_state.entries == NULL && old->file_state.entries != NULL)
            {
                g_legacy_ctx.file_state = old->file_state;
                old->file_state.entries = NULL;
                old->file_state.count = 0;
                old->file_state.capacity = 0;
            }

            if (g_legacy_ctx.args_state.items == NULL && old->args_state.items != NULL)
            {
                g_legacy_ctx.args_state = old->args_state;
                old->args_state.items = NULL;
                old->args_state.size = 0;
                old->args_state.cap = 0;
            }

            if (g_legacy_ctx.type_registry.classes == NULL && old->type_registry.classes != NULL)
            {
                g_legacy_ctx.type_registry = old->type_registry;
                old->type_registry.classes = NULL;
                old->type_registry.classes_len = 0;
                old->type_registry.classes_cap = 0;
                old->type_registry.ifaces = NULL;
                old->type_registry.ifaces_len = 0;
                old->type_registry.ifaces_cap = 0;
                old->type_registry.bindings = NULL;
                old->type_registry.bindings_len = 0;
                old->type_registry.bindings_cap = 0;
            }

            rt_spin_unlock(&g_legacy_handoff_lock);
        }
    }

    if (ctx)
    {
        rt_legacy_ensure_init();

        size_t prev = __atomic_fetch_add(&ctx->bind_count, 1, __ATOMIC_ACQ_REL);
        if (prev == 0)
        {
            // First bind: adopt legacy state to preserve pre-context behaviour.
            rt_spin_lock(&g_legacy_handoff_lock);

            // Move file_state if destination is empty and legacy has entries.
            if (ctx->file_state.entries == NULL && g_legacy_ctx.file_state.entries != NULL)
            {
                ctx->file_state = g_legacy_ctx.file_state;
                g_legacy_ctx.file_state.entries = NULL;
                g_legacy_ctx.file_state.count = 0;
                g_legacy_ctx.file_state.capacity = 0;
            }

            // Move args_state if destination is empty and legacy has entries.
            if (ctx->args_state.items == NULL && g_legacy_ctx.args_state.items != NULL)
            {
                ctx->args_state = g_legacy_ctx.args_state;
                g_legacy_ctx.args_state.items = NULL;
                g_legacy_ctx.args_state.size = 0;
                g_legacy_ctx.args_state.cap = 0;
            }

            // Move type registry if destination is empty and legacy has data.
            if (ctx->type_registry.classes == NULL && g_legacy_ctx.type_registry.classes != NULL)
            {
                ctx->type_registry = g_legacy_ctx.type_registry;
                g_legacy_ctx.type_registry.classes = NULL;
                g_legacy_ctx.type_registry.classes_len = 0;
                g_legacy_ctx.type_registry.classes_cap = 0;
                g_legacy_ctx.type_registry.ifaces = NULL;
                g_legacy_ctx.type_registry.ifaces_len = 0;
                g_legacy_ctx.type_registry.ifaces_cap = 0;
                g_legacy_ctx.type_registry.bindings = NULL;
                g_legacy_ctx.type_registry.bindings_len = 0;
                g_legacy_ctx.type_registry.bindings_cap = 0;
            }

            rt_spin_unlock(&g_legacy_handoff_lock);
        }
    }
}

/// @brief Retrieve the current thread's runtime context.
///
/// Returns the context bound to the calling thread via rt_set_current_context(),
/// or NULL if no context is currently bound. Runtime functions typically call
/// this first, then fall back to rt_legacy_context() if the result is NULL.
///
/// **Common usage pattern:**
/// ```c
/// static RtContext *get_effective_context(void) {
///     RtContext *ctx = rt_get_current_context();
///     if (!ctx)
///         ctx = rt_legacy_context();
///     return ctx;
/// }
/// ```
///
/// @return The currently bound context, or NULL if none is bound.
///
/// @note Thread-safe: reads thread-local storage.
/// @note O(1) time complexity.
///
/// @see rt_set_current_context For binding a context
/// @see rt_legacy_context For the fallback context
RtContext *rt_get_current_context(void)
{
    return g_rt_context;
}

/// @brief Get the global legacy context for backward compatibility.
///
/// Returns the shared fallback context used when no VM context is bound.
/// The legacy context is lazily initialized on first access and persists
/// for the lifetime of the process.
///
/// **When to use:**
/// - Native code not running under a VM
/// - Runtime functions called after VM exit
/// - Test code that doesn't need context isolation
///
/// **Example:**
/// ```c
/// // In runtime function:
/// RtContext *ctx = rt_get_current_context();
/// if (!ctx)
///     ctx = rt_legacy_context();  // Use fallback
/// // Now ctx is guaranteed non-NULL
/// ```
///
/// @return Pointer to the global legacy context (never NULL after first call).
///
/// @note Thread-safe: uses atomic lazy initialization.
/// @note The returned context is shared across all threads not using a VM context.
///
/// @see rt_get_current_context For getting the thread's bound context
RtContext *rt_legacy_context(void)
{
    rt_legacy_ensure_init();
    return &g_legacy_ctx;
}

/// @brief Clean up the legacy runtime context at process shutdown.
/// @details Calls rt_context_cleanup on the static g_legacy_ctx to close
///          any open BASIC file channels, release argument storage, and
///          free the type registry.  No-op if the legacy context was never
///          initialized.
///
///          Called from the rt_global_shutdown atexit handler in rt_heap.c,
///          AFTER GC finalizers have run and BEFORE string intern teardown.
void rt_legacy_context_shutdown(void)
{
    int state = __atomic_load_n(&g_legacy_state, __ATOMIC_ACQUIRE);
    if (state != 2)
        return; /* never initialized — nothing to clean up */

    rt_context_cleanup(&g_legacy_ctx);
}
