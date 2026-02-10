//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_gc.h
/// @brief Cycle-detecting garbage collector for reference-counted objects.
///
/// Supplements the existing atomic reference counting with a cycle collector
/// that detects and breaks unreachable reference cycles.  Uses a trial
/// deletion algorithm (synchronous mark-sweep on tracked objects).
///
/// Objects that may participate in cycles (e.g. containers with back-pointers)
/// register with rt_gc_track().  Periodically calling rt_gc_collect() finds
/// and frees cycles that simple refcounting cannot reclaim.
///
//===----------------------------------------------------------------------===//

#ifndef RT_GC_H
#define RT_GC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Callback that enumerates every strong reference held by @p obj.
/// For each reference, the callback must call @p visitor(child, ctx).
typedef void (*rt_gc_visitor_t)(void *child, void *ctx);
typedef void (*rt_gc_traverse_fn)(void *obj, rt_gc_visitor_t visitor, void *ctx);

//=============================================================================
// GC Tracking
//=============================================================================

/// Register @p obj as a potentially cyclic object.
/// @param obj        Heap-allocated object (must have RT_MAGIC header).
/// @param traverse   Function that enumerates obj's strong references.
void rt_gc_track(void *obj, rt_gc_traverse_fn traverse);

/// Remove @p obj from cycle tracking (e.g. before manual free).
void rt_gc_untrack(void *obj);

/// Check whether @p obj is currently tracked by the cycle collector.
int8_t rt_gc_is_tracked(void *obj);

//=============================================================================
// Cycle Collection
//=============================================================================

/// Run one cycle-collection pass over all tracked objects.
/// @return Number of objects freed (cycle members).
int64_t rt_gc_collect(void);

/// Return the total number of currently tracked objects.
int64_t rt_gc_tracked_count(void);

//=============================================================================
// Zeroing Weak References
//=============================================================================

/// Opaque weak reference handle.
typedef struct rt_weakref rt_weakref;

/// Create a zeroing weak reference to @p target.
/// The target's refcount is NOT incremented.
/// When the target is freed, the weak ref automatically becomes NULL.
/// @return New weak reference (caller owns).
rt_weakref *rt_weakref_new(void *target);

/// Dereference a weak reference.
/// @return The target object, or NULL if the target has been freed.
void *rt_weakref_get(rt_weakref *ref);

/// Check if the target is still alive.
int8_t rt_weakref_alive(rt_weakref *ref);

/// Destroy a weak reference (does NOT affect the target).
void rt_weakref_free(rt_weakref *ref);

/// Called internally when an object is being freed — clears all weak
/// references pointing to @p target.  Integrated into rt_obj_free().
void rt_gc_clear_weak_refs(void *target);

//=============================================================================
// Statistics
//=============================================================================

/// Return the total number of objects freed by the collector since startup.
int64_t rt_gc_total_collected(void);

/// Return the number of collection passes run since startup.
int64_t rt_gc_pass_count(void);

#ifdef __cplusplus
}
#endif

#endif /* RT_GC_H */
