//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_context.h
// Purpose: Per-VM runtime context that isolates all global mutable state across concurrent VM instances, including module variables, file channels, argument stores, and type registry.
//
// Key invariants:
//   - Thread-local binding ensures each thread accesses its active VM context.
//   - A thread must call rt_context_set before invoking any stateful runtime function.
//   - All runtime subsystems that use global state read from the active context pointer.
//   - RtContext owns all sub-state structures; releasing the context releases all children.
//
// Ownership/Lifetime:
//   - The VM owns the RtContext object and is responsible for its lifetime.
//   - The thread-local pointer is a borrowed reference; the VM must not free the context while other threads reference it.
//
// Links: src/runtime/core/rt_context.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Module-level variable entry for per-VM storage.
    typedef struct RtModvarEntry
    {
        char *name;  ///< Owned copy of variable name.
        int kind;    ///< Storage kind (I64, F64, I1, PTR, STR).
        void *addr;  ///< Allocated storage block.
        size_t size; ///< Size in bytes.
    } RtModvarEntry;

    // Forward declarations for opaque per-module state stored in context
    struct RtFileChannelEntry;

    typedef struct RtFileState
    {
        struct RtFileChannelEntry *entries;
        size_t count;
        size_t capacity;
    } RtFileState;

    typedef struct RtArgsState
    {
        rt_string *items;
        size_t size;
        size_t cap;
    } RtArgsState;

    typedef struct RtTypeRegistryState
    {
        void *classes;
        size_t classes_len, classes_cap;
        void *ifaces;
        size_t ifaces_len, ifaces_cap;
        void *bindings;
        size_t bindings_len, bindings_cap;
    } RtTypeRegistryState;

    /// @brief Per-VM runtime context isolating global state.
    /// @details Moves runtime global variables into per-VM storage so multiple
    ///          VM instances can coexist without interfering. Each VM owns one
    ///          RtContext and binds it to the current thread before execution.
    typedef struct RtContext
    {
        // Random number generator state (from rt_random.c)
        uint64_t rng_state;

        // Module-level variable table (from rt_modvar.c)
        RtModvarEntry *modvar_entries;
        size_t modvar_count;
        size_t modvar_capacity;

        // File channel table (rt_file.c)
        RtFileState file_state;

        // Command-line argument store (rt_args.c)
        RtArgsState args_state;

        // Type registry (rt_type_registry.c)
        RtTypeRegistryState type_registry;

        // Number of threads currently bound to this context via rt_set_current_context.
        // Used to make legacy state handoff safe under concurrent VM threads.
        size_t bind_count;

        // Future expansions:
        // - VM-only state
    } RtContext;

    /// @brief Initialize a runtime context with default values.
    /// @details Sets RNG to deterministic seed, initializes empty modvar table.
    /// @param ctx Context to initialize.
    void rt_context_init(RtContext *ctx);

    /// @brief Cleanup a runtime context and free owned resources.
    /// @details Frees all modvar entries and their storage.
    /// @param ctx Context to cleanup.
    void rt_context_cleanup(RtContext *ctx);

    /// @brief Bind a runtime context to the current thread.
    /// @details Sets the thread-local context pointer so subsequent runtime calls
    ///          access this VM's state.
    /// @param ctx Context to bind (may be NULL to unbind).
    void rt_set_current_context(RtContext *ctx);

    /// @brief Retrieve the current thread's runtime context.
    /// @details Returns the context bound via rt_set_current_context.
    /// @return Active context, or NULL if none bound.
    RtContext *rt_get_current_context(void);

    /// @brief Access the process-wide legacy runtime context.
    /// @details Initializes on first use. Used to preserve single-VM behaviour when
    ///          no VM-bound context is active.
    RtContext *rt_legacy_context(void);

#ifdef __cplusplus
}
#endif
