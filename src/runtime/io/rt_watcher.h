//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_watcher.h
// Purpose: File system watcher for Viper.IO.Watcher, monitoring files and directories for changes
// (created, modified, deleted) and delivering events via callbacks.
//
// Key invariants:
//   - Events are delivered asynchronously via a callback registered at creation.
//   - Watching a directory is recursive by default on all platforms.
//   - The watcher must be started with rt_watcher_start before events fire.
//   - Callback may be invoked from a background thread; caller must synchronize.
//
// Ownership/Lifetime:
//   - Watcher objects are heap-allocated; caller must stop and free when done.
//   - Callback function pointer must remain valid while the watcher is running.
//
// Links: src/runtime/io/rt_watcher.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Event type constants
#define RT_WATCH_EVENT_NONE 0
#define RT_WATCH_EVENT_CREATED 1
#define RT_WATCH_EVENT_MODIFIED 2
#define RT_WATCH_EVENT_DELETED 3
#define RT_WATCH_EVENT_RENAMED 4

    /// @brief Create a new watcher for the given path.
    /// @param path The file or directory path to watch.
    /// @return Opaque pointer to the new Watcher object.
    void *rt_watcher_new(rt_string path);

    /// @brief Get the watched path.
    /// @param obj Opaque Watcher object pointer.
    /// @return The path being watched.
    rt_string rt_watcher_get_path(void *obj);

    /// @brief Check if the watcher is actively watching.
    /// @param obj Opaque Watcher object pointer.
    /// @return 1 if watching, 0 otherwise.
    int8_t rt_watcher_get_is_watching(void *obj);

    /// @brief Start watching for file system changes.
    /// @param obj Opaque Watcher object pointer.
    void rt_watcher_start(void *obj);

    /// @brief Stop watching for file system changes.
    /// @param obj Opaque Watcher object pointer.
    void rt_watcher_stop(void *obj);

    /// @brief Poll for a file system event (non-blocking).
    /// @param obj Opaque Watcher object pointer.
    /// @return Event type (RT_WATCH_EVENT_*), or 0 if no event.
    int64_t rt_watcher_poll(void *obj);

    /// @brief Poll for a file system event with timeout.
    /// @param obj Opaque Watcher object pointer.
    /// @param ms Maximum milliseconds to wait.
    /// @return Event type (RT_WATCH_EVENT_*), or 0 if timeout.
    int64_t rt_watcher_poll_for(void *obj, int64_t ms);

    /// @brief Get the path of the file that triggered the last event.
    /// @param obj Opaque Watcher object pointer.
    /// @return Path of the file, or traps if no event polled yet.
    rt_string rt_watcher_event_path(void *obj);

    /// @brief Get the type of the last polled event.
    /// @param obj Opaque Watcher object pointer.
    /// @return Event type (RT_WATCH_EVENT_*).
    int64_t rt_watcher_event_type(void *obj);

    // Event type accessors (accept receiver for property dispatch compatibility)
    int64_t rt_watcher_event_none(void *self);
    int64_t rt_watcher_event_created(void *self);
    int64_t rt_watcher_event_modified(void *self);
    int64_t rt_watcher_event_deleted(void *self);
    int64_t rt_watcher_event_renamed(void *self);

#ifdef __cplusplus
}
#endif
