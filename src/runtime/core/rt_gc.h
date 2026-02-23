//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_gc.h
// Purpose: Cycle-detecting garbage collector supplementing atomic reference counting, using a trial-deletion mark-sweep algorithm to find and break unreachable reference cycles among tracked objects.
//
// Key invariants:
//   - Only objects with RT_MAGIC headers may be registered via rt_gc_track.
//   - The collector does not move objects; heap addresses remain stable.
//   - rt_gc_collect is synchronous and must not be called from within a finalizer.
//   - Weak references registered via rt_gc_weak_ref are zeroed when their target is freed.
//
// Ownership/Lifetime:
//   - Tracked objects are owned by their reference counts; the GC only breaks cycles.
//   - rt_gc_track retains a weak internal reference; it does not increment the object's refcount.
//   - Caller must call rt_gc_untrack before manually freeing a tracked object.
//
// Links: src/runtime/core/rt_gc.c (implementation), src/runtime/oop/rt_object.h
//
//===----------------------------------------------------------------------===//
#ifndef RT_GC_H
#define RT_GC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Callback that enumerates every strong reference held by @p obj.
    /// For each reference, the callback must call @p visitor(child, ctx).
    typedef void (*rt_gc_visitor_t)(void *child, void *ctx);
    typedef void (*rt_gc_traverse_fn)(void *obj, rt_gc_visitor_t visitor, void *ctx);

    //=============================================================================
    // GC Tracking
    //=============================================================================

    /// @brief Register an object as potentially cyclic for cycle collection.
    /// @param obj Heap-allocated object (must have RT_MAGIC header).
    /// @param traverse Function that enumerates @p obj's strong references
    ///                 by calling the visitor for each child.
    void rt_gc_track(void *obj, rt_gc_traverse_fn traverse);

    /// @brief Remove an object from cycle tracking.
    /// @param obj The object to untrack (e.g. before manual free).
    void rt_gc_untrack(void *obj);

    /// @brief Check whether an object is currently tracked by the cycle collector.
    /// @param obj The object to query.
    /// @return 1 if @p obj is tracked, 0 otherwise.
    int8_t rt_gc_is_tracked(void *obj);

    //=============================================================================
    // Cycle Collection
    //=============================================================================

    /// @brief Run one cycle-collection pass over all tracked objects.
    /// @return Number of objects freed (cycle members that were reclaimed).
    int64_t rt_gc_collect(void);

    /// @brief Get the total number of currently tracked objects.
    /// @return The count of objects registered with rt_gc_track() that have
    ///         not yet been untracked or collected.
    int64_t rt_gc_tracked_count(void);

    //=============================================================================
    // Zeroing Weak References
    //=============================================================================

    /// Opaque weak reference handle.
    typedef struct rt_weakref rt_weakref;

    /// @brief Create a zeroing weak reference to a target object.
    /// @details The target's refcount is NOT incremented. When the target is
    ///          freed, the weak reference automatically becomes NULL.
    /// @param target The object to weakly reference.
    /// @return A new weak reference handle (caller owns; free with
    ///         rt_weakref_free()).
    rt_weakref *rt_weakref_new(void *target);

    /// @brief Dereference a weak reference.
    /// @param ref The weak reference handle.
    /// @return The target object, or NULL if the target has been freed.
    void *rt_weakref_get(rt_weakref *ref);

    /// @brief Check if the weak reference's target is still alive.
    /// @param ref The weak reference handle.
    /// @return 1 if the target is alive, 0 if it has been freed.
    int8_t rt_weakref_alive(rt_weakref *ref);

    /// @brief Destroy a weak reference handle (does NOT affect the target).
    /// @param ref The weak reference handle to free.
    void rt_weakref_free(rt_weakref *ref);

    /// @brief Clear all weak references pointing to a target being freed.
    /// @details Called internally when an object is being freed. Integrated
    ///          into rt_obj_free().
    /// @param target The object being freed; all weak references to it are
    ///               zeroed.
    void rt_gc_clear_weak_refs(void *target);

    //=============================================================================
    // Statistics
    //=============================================================================

    /// @brief Get the total number of objects freed by the collector since startup.
    /// @return Cumulative count of objects reclaimed by cycle collection.
    int64_t rt_gc_total_collected(void);

    /// @brief Get the number of collection passes run since startup.
    /// @return Cumulative count of rt_gc_collect() invocations.
    int64_t rt_gc_pass_count(void);

#ifdef __cplusplus
}
#endif

#endif /* RT_GC_H */
