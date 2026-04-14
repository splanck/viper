//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_watcher.c
// Purpose: Cross-platform filesystem watcher for the Viper.IO.Watcher class.
//          Watches directories or files for changes (create, modify, delete,
//          rename) using native OS APIs: inotify on Linux, kqueue on macOS,
//          and ReadDirectoryChangesW on Windows.
//
// Key invariants:
//   - Each watcher instance holds exactly one OS watch handle/descriptor.
//   - Events are delivered via a callback registered at construction time.
//   - Watcher objects must be stopped before being freed to avoid use-after-free.
//   - A stub implementation is provided for unsupported platforms (ViperDOS).
//   - All public functions guard against NULL watcher handles.
//
// Ownership/Lifetime:
//   - Watcher objects are heap-allocated and managed by the runtime GC.
//   - The watcher holds a retained reference to the watched path string.
//   - OS resources (inotify fd, kqueue fd, Win32 handle) are released on stop.
//
// Links: src/runtime/io/rt_watcher.h (public API),
//        src/runtime/rt_platform.h (platform detection macros)
//
//===----------------------------------------------------------------------===//

#include "rt_watcher.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_path.h"
#include "rt_platform.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// Platform-specific includes
#if defined(__linux__)
#include <limits.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef O_EVTONLY
#define O_EVTONLY 0x8000
#endif
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__viperdos__)
// ViperDOS: file watching deferred until kernel inotify-like support exists.
#else
// Stub platform
#endif

// Helper to create rt_string from C string
static inline rt_string str_from_cstr(const char *s) {
    return s ? rt_string_from_bytes(s, strlen(s)) : NULL;
}

#define WATCHER_EVENT_QUEUE_SIZE 64

/// @brief A single queued file system event.
typedef struct watcher_event {
    int64_t type; ///< Event type (RT_WATCH_EVENT_*)
    void *path;   ///< Path of affected file (rt_string)
} watcher_event;

/// @brief Internal watcher implementation structure.
typedef struct rt_watcher_impl {
    void *watch_path;      ///< The path being watched (rt_string)
    void *watch_dir_path;  ///< Directory path used for full event path reconstruction (rt_string)
    void *watch_leaf_name; ///< Final path component when watching a single file (rt_string)
    int8_t is_watching;    ///< 1 if actively watching
    int8_t is_directory;   ///< 1 if watching a directory

    // Event queue
    watcher_event events[WATCHER_EVENT_QUEUE_SIZE];
    int64_t event_head;  ///< Next event to read
    int64_t event_tail;  ///< Next slot to write
    int64_t event_count; ///< Number of queued events

    // Last polled event
    int64_t last_event_type;
    void *last_event_path;
    int8_t has_last_event;

#if defined(__linux__)
    int inotify_fd;       ///< inotify file descriptor
    int watch_descriptor; ///< Watch descriptor for the path
#elif defined(__APPLE__)
    int kqueue_fd;  ///< kqueue file descriptor
    int watched_fd; ///< File descriptor of watched path
#elif defined(_WIN32)
    HANDLE dir_handle;     ///< Directory handle
    OVERLAPPED overlapped; ///< Overlapped I/O structure
    char buffer[4096];     ///< Buffer for change notifications
    BOOL pending_read;     ///< Whether a read is pending
#endif
} rt_watcher_impl;

/// @brief Finalizer callback for Watcher.
static void rt_watcher_finalize(void *obj) {
    if (!obj)
        return;
    rt_watcher_impl *w = (rt_watcher_impl *)obj;

    // Stop watching if active
    if (w->is_watching) {
#if defined(__linux__)
        if (w->watch_descriptor >= 0)
            inotify_rm_watch(w->inotify_fd, w->watch_descriptor);
        if (w->inotify_fd >= 0)
            close(w->inotify_fd);
#elif defined(__APPLE__)
        if (w->watched_fd >= 0)
            close(w->watched_fd);
        if (w->kqueue_fd >= 0)
            close(w->kqueue_fd);
#elif defined(_WIN32)
        if (w->dir_handle != INVALID_HANDLE_VALUE) {
            CancelIo(w->dir_handle);
            if (w->overlapped.hEvent)
                CloseHandle(w->overlapped.hEvent);
            CloseHandle(w->dir_handle);
        }
#endif
    }

    // Clear event queue paths
    for (int64_t i = 0; i < WATCHER_EVENT_QUEUE_SIZE; i++) {
        if (w->events[i].path) {
            rt_string_unref(w->events[i].path);
            w->events[i].path = NULL;
        }
    }
    if (w->last_event_path) {
        rt_string_unref(w->last_event_path);
        w->last_event_path = NULL;
    }
    if (w->watch_path) {
        rt_string_unref(w->watch_path);
        w->watch_path = NULL;
    }
    if (w->watch_dir_path) {
        rt_string_unref(w->watch_dir_path);
        w->watch_dir_path = NULL;
    }
    if (w->watch_leaf_name) {
        rt_string_unref(w->watch_leaf_name);
        w->watch_leaf_name = NULL;
    }
}

#if defined(__linux__) || defined(_WIN32)
static rt_string watcher_event_path_from_relative(rt_watcher_impl *w, const char *path) {
    if (!w)
        return NULL;

    if (!path || path[0] == '\0')
        return rt_string_ref((rt_string)w->watch_path);

    rt_string rel = str_from_cstr(path);
    if (!rel)
        return rt_string_ref((rt_string)w->watch_path);

    if (!w->is_directory && w->watch_leaf_name) {
        int matches = rt_str_eq((rt_string)w->watch_leaf_name, rel);
        rt_string_unref(rel);
        return matches ? rt_string_ref((rt_string)w->watch_path) : NULL;
    }

    rt_string full = rt_path_join((rt_string)w->watch_dir_path, rel);
    rt_string_unref(rel);
    return full;
}
#endif

/// @brief Queue an event internally.
static void watcher_queue_event_owned(rt_watcher_impl *w, int64_t type, rt_string path) {
    if (w->event_count >= WATCHER_EVENT_QUEUE_SIZE) {
        // Queue full, drop oldest
        if (w->events[w->event_head].path)
            rt_string_unref(w->events[w->event_head].path);
        w->event_head = (w->event_head + 1) % WATCHER_EVENT_QUEUE_SIZE;
        w->event_count--;
    }

    w->events[w->event_tail].type = type;
    w->events[w->event_tail].path = path;
    w->event_tail = (w->event_tail + 1) % WATCHER_EVENT_QUEUE_SIZE;
    w->event_count++;
}

/// @brief Dequeue an event.
static int watcher_dequeue_event(rt_watcher_impl *w, watcher_event *out) {
    if (w->event_count == 0)
        return 0;

    *out = w->events[w->event_head];
    w->events[w->event_head].path = NULL; // Ownership transferred
    w->event_head = (w->event_head + 1) % WATCHER_EVENT_QUEUE_SIZE;
    w->event_count--;
    return 1;
}

#if defined(__linux__)
/// @brief Read and process inotify events (Linux).
static void watcher_read_inotify_events(rt_watcher_impl *w) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(w->inotify_fd, buf, sizeof(buf));
    if (len <= 0)
        return;

    char *ptr = buf;
    while (ptr < buf + len) {
        struct inotify_event *event = (struct inotify_event *)ptr;
        int64_t type = RT_WATCH_EVENT_NONE;

        if (event->mask & IN_CREATE)
            type = RT_WATCH_EVENT_CREATED;
        else if (event->mask & IN_MODIFY)
            type = RT_WATCH_EVENT_MODIFIED;
        else if (event->mask & (IN_DELETE | IN_DELETE_SELF))
            type = RT_WATCH_EVENT_DELETED;
        else if (event->mask & (IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF))
            type = RT_WATCH_EVENT_RENAMED;

        if (type != RT_WATCH_EVENT_NONE) {
            const char *name = event->len > 0 ? event->name : NULL;
            rt_string path = watcher_event_path_from_relative(w, name);
            if (path)
                watcher_queue_event_owned(w, type, path);
        }

        ptr += sizeof(struct inotify_event) + event->len;
    }
}
#endif

#if defined(__APPLE__)
/// @brief Read and process kqueue events (macOS).
static void watcher_read_kqueue_events(rt_watcher_impl *w, int timeout_ms) {
    struct kevent event;
    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;

    int n = kevent(w->kqueue_fd, NULL, 0, &event, 1, timeout_ms >= 0 ? &ts : NULL);
    if (n <= 0)
        return;

    if (event.fflags & NOTE_DELETE)
        watcher_queue_event_owned(
            w, RT_WATCH_EVENT_DELETED, rt_string_ref((rt_string)w->watch_path));
    else if (event.fflags & NOTE_WRITE)
        watcher_queue_event_owned(
            w, RT_WATCH_EVENT_MODIFIED, rt_string_ref((rt_string)w->watch_path));
    else if (event.fflags & NOTE_RENAME)
        watcher_queue_event_owned(
            w, RT_WATCH_EVENT_RENAMED, rt_string_ref((rt_string)w->watch_path));
    else if (event.fflags & NOTE_EXTEND)
        watcher_queue_event_owned(
            w, RT_WATCH_EVENT_MODIFIED, rt_string_ref((rt_string)w->watch_path));
    else if (event.fflags & NOTE_ATTRIB)
        watcher_queue_event_owned(
            w, RT_WATCH_EVENT_MODIFIED, rt_string_ref((rt_string)w->watch_path));
}
#endif

#if defined(_WIN32)
/// @brief Read and process Windows directory changes.
static void watcher_read_windows_events(rt_watcher_impl *w) {
    if (!w->pending_read)
        return;

    DWORD bytes_returned = 0;
    if (!GetOverlappedResult(w->dir_handle, &w->overlapped, &bytes_returned, FALSE)) {
        if (GetLastError() == ERROR_IO_INCOMPLETE)
            return; // Still pending
        return;
    }

    w->pending_read = FALSE;

    if (bytes_returned == 0)
        return;

    FILE_NOTIFY_INFORMATION *info = (FILE_NOTIFY_INFORMATION *)w->buffer;
    while (1) {
        int64_t type = RT_WATCH_EVENT_NONE;
        switch (info->Action) {
            case FILE_ACTION_ADDED:
                type = RT_WATCH_EVENT_CREATED;
                break;
            case FILE_ACTION_REMOVED:
                type = RT_WATCH_EVENT_DELETED;
                break;
            case FILE_ACTION_MODIFIED:
                type = RT_WATCH_EVENT_MODIFIED;
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
            case FILE_ACTION_RENAMED_NEW_NAME:
                type = RT_WATCH_EVENT_RENAMED;
                break;
        }

        if (type != RT_WATCH_EVENT_NONE) {
            // Convert wide string to UTF-8
            int name_len = info->FileNameLength / sizeof(WCHAR);
            int utf8_len =
                WideCharToMultiByte(CP_UTF8, 0, info->FileName, name_len, NULL, 0, NULL, NULL);
            char *name = malloc(utf8_len + 1);
            if (name) {
                WideCharToMultiByte(
                    CP_UTF8, 0, info->FileName, name_len, name, utf8_len, NULL, NULL);
                name[utf8_len] = '\0';
                rt_string path = watcher_event_path_from_relative(w, name);
                if (path)
                    watcher_queue_event_owned(w, type, path);
                free(name);
            }
        }

        if (info->NextEntryOffset == 0)
            break;
        info = (FILE_NOTIFY_INFORMATION *)((char *)info + info->NextEntryOffset);
    }

    // Start another read
    if (w->overlapped.hEvent)
        ResetEvent(w->overlapped.hEvent);
    ReadDirectoryChangesW(w->dir_handle,
                          w->buffer,
                          sizeof(w->buffer),
                          FALSE,
                          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                              FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                          NULL,
                          &w->overlapped,
                          NULL);
    w->pending_read = TRUE;
}
#endif

/// @brief Construct a filesystem watcher for `path` (file or directory). `stat`'s the path up
/// front and traps if it doesn't exist. Distinguishes file vs directory mode (different OS
/// primitives needed). Returns a GC-managed handle; user must call `_start` to begin watching.
void *rt_watcher_new(rt_string path) {
    if (!path)
        rt_trap("Watcher.New: null path");

    const char *cpath = rt_string_cstr(path);
    if (!cpath || cpath[0] == '\0')
        rt_trap("Watcher.New: empty path");

    // Check if path exists
    struct stat st;
    if (stat(cpath, &st) != 0)
        rt_trap("Watcher.New: path does not exist");

    rt_watcher_impl *w = (rt_watcher_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_watcher_impl));
    if (!w)
        rt_trap("Watcher.New: alloc failed");

    memset(w, 0, sizeof(rt_watcher_impl));
    w->watch_path = str_from_cstr(cpath);
    w->is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
    if (w->is_directory) {
        w->watch_dir_path = rt_string_ref((rt_string)w->watch_path);
        w->watch_leaf_name = NULL;
    } else {
        w->watch_dir_path = rt_path_dir((rt_string)w->watch_path);
        w->watch_leaf_name = rt_path_name((rt_string)w->watch_path);
    }
    w->is_watching = 0;
    w->event_head = 0;
    w->event_tail = 0;
    w->event_count = 0;
    w->has_last_event = 0;

#if defined(__linux__)
    w->inotify_fd = -1;
    w->watch_descriptor = -1;
#elif defined(__APPLE__)
    w->kqueue_fd = -1;
    w->watched_fd = -1;
#elif defined(_WIN32)
    w->dir_handle = INVALID_HANDLE_VALUE;
    w->pending_read = FALSE;
#endif

    rt_obj_set_finalizer(w, rt_watcher_finalize);
    return w;
}

/// @brief Read the path the watcher is configured to monitor (returned freshly retained).
rt_string rt_watcher_get_path(void *obj) {
    if (!obj)
        return str_from_cstr("");
    rt_watcher_impl *w = (rt_watcher_impl *)obj;
    if (w->watch_path)
        rt_string_ref(w->watch_path);
    return w->watch_path ? w->watch_path : str_from_cstr("");
}

/// @brief Returns 1 between successful `_start` and `_stop`; 0 otherwise.
int8_t rt_watcher_get_is_watching(void *obj) {
    if (!obj)
        return 0;
    return ((rt_watcher_impl *)obj)->is_watching;
}

/// @brief Begin watching. Per platform:
///   - **Linux:** `inotify_init1(IN_NONBLOCK)` + `inotify_add_watch` with mask covering create/
///     delete/modify/move events.
///   - **macOS:** `kqueue` + open(O_EVTONLY) + EVFILT_VNODE for delete/write/extend/attrib/rename.
///   - **Win32:** `CreateFileW(FILE_LIST_DIRECTORY, FILE_FLAG_OVERLAPPED)` + initial
///     `ReadDirectoryChangesW`. Always watches the parent directory (file-mode filtering happens
///     at event-decode time via `watch_leaf_name`).
/// Traps if already watching, on syscall failure, or on unsupported platform.
void rt_watcher_start(void *obj) {
    if (!obj)
        rt_trap("Watcher.Start: null watcher");

    rt_watcher_impl *w = (rt_watcher_impl *)obj;
    if (w->is_watching)
        rt_trap("Watcher.Start: already watching");

    const char *cpath = rt_string_cstr(w->watch_path);

#if defined(__linux__)
    w->inotify_fd = inotify_init1(IN_NONBLOCK);
    if (w->inotify_fd < 0)
        rt_trap("Watcher.Start: failed to initialize inotify");

    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO;
    if (!w->is_directory)
        mask |= IN_DELETE_SELF | IN_MOVE_SELF;

    w->watch_descriptor = inotify_add_watch(w->inotify_fd, cpath, mask);
    if (w->watch_descriptor < 0) {
        close(w->inotify_fd);
        w->inotify_fd = -1;
        rt_trap("Watcher.Start: failed to add watch");
    }

#elif defined(__APPLE__)
    w->kqueue_fd = kqueue();
    if (w->kqueue_fd < 0)
        rt_trap("Watcher.Start: failed to create kqueue");

    w->watched_fd = open(cpath, O_EVTONLY);
    if (w->watched_fd < 0) {
        close(w->kqueue_fd);
        w->kqueue_fd = -1;
        rt_trap("Watcher.Start: failed to open path for watching");
    }

    struct kevent change;
    EV_SET(&change,
           w->watched_fd,
           EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME,
           0,
           NULL);

    if (kevent(w->kqueue_fd, &change, 1, NULL, 0, NULL) < 0) {
        close(w->watched_fd);
        close(w->kqueue_fd);
        w->watched_fd = -1;
        w->kqueue_fd = -1;
        rt_trap("Watcher.Start: failed to register kevent");
    }

#elif defined(_WIN32)
    // For Windows, we need to watch the directory (or parent directory for files)
    const char *watch_dir =
        rt_string_cstr(w->is_directory ? (rt_string)w->watch_path : (rt_string)w->watch_dir_path);
    wchar_t *wide_watch_dir = rt_file_path_utf8_to_wide(watch_dir);
    if (!wide_watch_dir)
        rt_trap("Watcher.Start: invalid watch path");

    w->dir_handle = CreateFileW(wide_watch_dir,
                                FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                NULL);
    free(wide_watch_dir);
    if (w->dir_handle == INVALID_HANDLE_VALUE)
        rt_trap("Watcher.Start: failed to open directory for watching");

    memset(&w->overlapped, 0, sizeof(w->overlapped));
    w->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    BOOL ok = ReadDirectoryChangesW(w->dir_handle,
                                    w->buffer,
                                    sizeof(w->buffer),
                                    FALSE,
                                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                                    NULL,
                                    &w->overlapped,
                                    NULL);
    if (!ok) {
        CloseHandle(w->overlapped.hEvent);
        CloseHandle(w->dir_handle);
        w->dir_handle = INVALID_HANDLE_VALUE;
        rt_trap("Watcher.Start: failed to start watching");
    }
    w->pending_read = TRUE;

#else
    rt_trap("Watcher.Start: unsupported platform");
#endif

    w->is_watching = 1;
}

/// @brief Stop watching: tear down the platform-specific descriptor (inotify_rm_watch + close /
/// close(kqueue) / CancelIo + CloseHandle). Idempotent — no-op on already-stopped watchers.
/// Pending events in the queue are NOT drained; they're released via the finalizer.
void rt_watcher_stop(void *obj) {
    if (!obj)
        return;

    rt_watcher_impl *w = (rt_watcher_impl *)obj;
    if (!w->is_watching)
        return;

#if defined(__linux__)
    if (w->watch_descriptor >= 0) {
        inotify_rm_watch(w->inotify_fd, w->watch_descriptor);
        w->watch_descriptor = -1;
    }
    if (w->inotify_fd >= 0) {
        close(w->inotify_fd);
        w->inotify_fd = -1;
    }
#elif defined(__APPLE__)
    if (w->watched_fd >= 0) {
        close(w->watched_fd);
        w->watched_fd = -1;
    }
    if (w->kqueue_fd >= 0) {
        close(w->kqueue_fd);
        w->kqueue_fd = -1;
    }
#elif defined(_WIN32)
    if (w->dir_handle != INVALID_HANDLE_VALUE) {
        CancelIo(w->dir_handle);
        if (w->overlapped.hEvent) {
            CloseHandle(w->overlapped.hEvent);
            w->overlapped.hEvent = NULL;
        }
        CloseHandle(w->dir_handle);
        w->dir_handle = INVALID_HANDLE_VALUE;
    }
    w->pending_read = FALSE;
#endif

    w->is_watching = 0;
}

/// @brief Non-blocking poll for the next event. Returns the event-type code (CREATED / MODIFIED /
/// DELETED / RENAMED) or NONE if no events queued AND the OS reports no new events. The event's
/// path becomes accessible via `_event_path` immediately after.
int64_t rt_watcher_poll(void *obj) {
    return rt_watcher_poll_for(obj, 0);
}

/// @brief Bounded-wait poll: same as `_poll` but waits up to `ms` milliseconds for an event.
/// `ms < 0` means wait forever; `ms == 0` is non-blocking. First drains the internal queue, then
/// asks the OS via `poll`/`kqueue`/`WaitForSingleObject`, then drains the queue again.
int64_t rt_watcher_poll_for(void *obj, int64_t ms) {
    if (!obj)
        return RT_WATCH_EVENT_NONE;

    rt_watcher_impl *w = (rt_watcher_impl *)obj;
    if (!w->is_watching)
        return RT_WATCH_EVENT_NONE;

    // First check if we have queued events
    watcher_event ev;
    if (watcher_dequeue_event(w, &ev)) {
        // Store as last event
        if (w->last_event_path)
            rt_string_unref(w->last_event_path);
        w->last_event_type = ev.type;
        w->last_event_path = ev.path;
        w->has_last_event = 1;
        return ev.type;
    }

    // Read new events from OS
#if defined(__linux__)
    struct pollfd pfd;
    pfd.fd = w->inotify_fd;
    pfd.events = POLLIN;
    int timeout = ms < 0 ? -1 : (int)ms;
    if (poll(&pfd, 1, timeout) > 0 && (pfd.revents & POLLIN)) {
        watcher_read_inotify_events(w);
    }
#elif defined(__APPLE__)
    watcher_read_kqueue_events(w, (int)ms);
#elif defined(_WIN32)
    if (w->pending_read) {
        DWORD wait_result =
            WaitForSingleObject(w->overlapped.hEvent, ms < 0 ? INFINITE : (DWORD)ms);
        if (wait_result == WAIT_OBJECT_0) {
            watcher_read_windows_events(w);
        }
    }
#endif

    // Try to dequeue again after reading
    if (watcher_dequeue_event(w, &ev)) {
        if (w->last_event_path)
            rt_string_unref(w->last_event_path);
        w->last_event_type = ev.type;
        w->last_event_path = ev.path;
        w->has_last_event = 1;
        return ev.type;
    }

    return RT_WATCH_EVENT_NONE;
}

/// @brief Read the absolute path of the most recently polled event. **Traps** if no `_poll` call
/// has succeeded yet — the contract is "poll then ask"; not safe to call out of order.
rt_string rt_watcher_event_path(void *obj) {
    if (!obj)
        rt_trap("Watcher.EventPath: null watcher");

    rt_watcher_impl *w = (rt_watcher_impl *)obj;
    if (!w->has_last_event)
        rt_trap("Watcher.EventPath: no event polled yet");

    if (w->last_event_path)
        rt_string_ref(w->last_event_path);
    return w->last_event_path ? w->last_event_path : str_from_cstr("");
}

/// @brief Read the type code of the last polled event. Returns NONE if no event has been polled.
int64_t rt_watcher_event_type(void *obj) {
    if (!obj)
        return RT_WATCH_EVENT_NONE;

    rt_watcher_impl *w = (rt_watcher_impl *)obj;
    return w->has_last_event ? w->last_event_type : RT_WATCH_EVENT_NONE;
}

// =============================================================================
// Event-type constant accessors
// Static methods that return the int64 enum value for each event kind. The
// `void *self` parameter is unused — these exist so Zia code can write
// `Watcher.EventCreated()` instead of magic-numbering the constants. Compiles
// to a single `mov rax, <const>; ret`.
// =============================================================================

/// @brief Constant: `RT_WATCH_EVENT_NONE` (no event polled).
int64_t rt_watcher_event_none(void *self) {
    (void)self;
    return RT_WATCH_EVENT_NONE;
}

/// @brief Constant: `RT_WATCH_EVENT_CREATED` (file/dir was created).
int64_t rt_watcher_event_created(void *self) {
    (void)self;
    return RT_WATCH_EVENT_CREATED;
}

/// @brief Constant: `RT_WATCH_EVENT_MODIFIED` (content or attributes changed).
int64_t rt_watcher_event_modified(void *self) {
    (void)self;
    return RT_WATCH_EVENT_MODIFIED;
}

/// @brief Constant: `RT_WATCH_EVENT_DELETED` (file/dir removed).
int64_t rt_watcher_event_deleted(void *self) {
    (void)self;
    return RT_WATCH_EVENT_DELETED;
}

/// @brief Constant: `RT_WATCH_EVENT_RENAMED` (file/dir was renamed/moved).
int64_t rt_watcher_event_renamed(void *self) {
    (void)self;
    return RT_WATCH_EVENT_RENAMED;
}
