//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_watcher.c
// Purpose: Cross-platform filesystem watcher for the Zanna.IO.Watcher class.
//          Watches directories or files for changes (create, modify, delete,
//          rename) using native OS APIs: inotify on Linux, kqueue on macOS,
//          and ReadDirectoryChangesW on Windows.
//
// Key invariants:
//   - A watcher owns the descriptors/handles required by its platform backend.
//   - Events are queued internally and consumed through Poll/PollFor.
//   - Explicit Stop releases resources promptly; the finalizer also stops an
//     active watcher and clears queued event strings.
//   - A stub implementation is provided for unsupported platforms.
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
#include "rt_io_class_ids.h"
#include "rt_object.h"
#include "rt_path.h"
#include "rt_platform.h"
#include "rt_string.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// Platform-specific includes
#if RT_PLATFORM_LINUX
#include "rt_time.h" // rt_clock_ticks_us for the PollFor monotonic deadline (VDOC-191)

#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif RT_PLATFORM_MACOS
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef O_EVTONLY
#define O_EVTONLY 0x8000
#endif
#elif RT_PLATFORM_WINDOWS
#include "rt_win32_wait.h"
#else
// Stub platform
#endif

/// @brief Allocate a fresh rt_string from a C string, returning NULL for NULL input.
static inline rt_string str_from_cstr(const char *s) {
    return s ? rt_string_from_bytes(s, strlen(s)) : NULL;
}

/// @brief Clamp a millisecond timeout to the range accepted by POSIX poll(2) / kevent(2).
static int watcher_timeout_to_int(int64_t ms) {
    if (ms < 0)
        return -1;
    if (ms > INT_MAX)
        return INT_MAX;
    return (int)ms;
}

#define WATCHER_EVENT_QUEUE_SIZE 64

/// Sentinel `dropped_count` for a NATIVE (kernel) queue overflow, where the OS
/// dropped an unknown number of events before Zanna could read them. Distinct
/// from an internal ring overflow, which counts dropped events exactly
/// (VDOC-190). Exposed to callers via `Watcher.EventOverflowCount()`.
#define WATCHER_OVERFLOW_COUNT_UNKNOWN ((int64_t)-1)

/// @brief A single queued file system event.
typedef struct watcher_event {
    int64_t type;          ///< Event type (RT_WATCH_EVENT_*)
    void *path;            ///< Path of affected file (rt_string)
    int64_t dropped_count; ///< Number of file-system events represented by an overflow marker
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
    int64_t last_overflow_count;
    int8_t has_last_event;

#if RT_PLATFORM_LINUX
    int inotify_fd;       ///< inotify file descriptor
    int watch_descriptor; ///< Watch descriptor for the path
#elif RT_PLATFORM_MACOS
    int kqueue_fd;  ///< kqueue file descriptor
    int watched_fd; ///< File descriptor of watched path
#elif RT_PLATFORM_WINDOWS
    HANDLE dir_handle;     ///< Directory handle
    OVERLAPPED overlapped; ///< Overlapped I/O structure
    char buffer[4096];     ///< Buffer for change notifications
    BOOL pending_read;     ///< Whether a read is pending
#endif
} rt_watcher_impl;

#if RT_PLATFORM_WINDOWS
static void watcher_close_windows_handles(rt_watcher_impl *w);
#endif

static rt_watcher_impl *watcher_require(void *obj, const char *context) {
    if (!obj || rt_obj_class_id(obj) != RT_WATCHER_CLASS_ID) {
        rt_trap(context ? context : "Watcher: invalid watcher");
        return NULL;
    }
    return (rt_watcher_impl *)obj;
}

/// @brief Release all queued event strings and reset the ring buffer to empty.
/// @details Also clears the `last_event_path` so the watcher's finalizer can call
///          this safely without double-releasing any string references.
static void watcher_clear_events(rt_watcher_impl *w) {
    if (!w)
        return;
    for (int64_t i = 0; i < WATCHER_EVENT_QUEUE_SIZE; i++) {
        if (w->events[i].path) {
            rt_string_unref(w->events[i].path);
            w->events[i].path = NULL;
        }
        w->events[i].type = RT_WATCH_EVENT_NONE;
        w->events[i].dropped_count = 0;
    }
    w->event_head = 0;
    w->event_tail = 0;
    w->event_count = 0;
    if (w->last_event_path) {
        rt_string_unref(w->last_event_path);
        w->last_event_path = NULL;
    }
    w->last_event_type = RT_WATCH_EVENT_NONE;
    w->last_overflow_count = 0;
    w->has_last_event = 0;
}

/// @brief Finalizer callback for Watcher.
static void rt_watcher_finalize(void *obj) {
    if (!obj)
        return;
    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");

    // Stop watching if active
    if (w->is_watching) {
#if RT_PLATFORM_LINUX
        if (w->watch_descriptor >= 0)
            inotify_rm_watch(w->inotify_fd, w->watch_descriptor);
        if (w->inotify_fd >= 0)
            close(w->inotify_fd);
#elif RT_PLATFORM_MACOS
        if (w->watched_fd >= 0)
            close(w->watched_fd);
        if (w->kqueue_fd >= 0)
            close(w->kqueue_fd);
#elif RT_PLATFORM_WINDOWS
        watcher_close_windows_handles(w);
#endif
    }

    watcher_clear_events(w);
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

#if RT_PLATFORM_LINUX || RT_PLATFORM_WINDOWS
/// @brief Convert a relative name from an OS event into a full path string.
///
/// inotify and ReadDirectoryChangesW report names relative to the
/// watched directory. When watching a single file (not a directory),
/// they still fire on siblings in the parent dir — we filter those
/// out by checking the event's leaf name against `watch_leaf_name`
/// and returning NULL for mismatches. For directory watches the name
/// is joined onto `watch_dir_path`; the result is absolute only when
/// the caller supplied an absolute watch path.
/// empty names (e.g., the self-event) become the watched path itself.
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

/// @brief Push an event into the ring buffer, replacing its newest slot with an overflow marker
/// when the queue is already full.
///
/// The queue is a fixed-size ring of 64 events. Once full, the newest
/// queued event and the incoming event are represented by an overflow
/// marker in that newest slot; older queued events remain available.
/// Takes ownership of the passed-in `path` string.
static void watcher_queue_event_owned(rt_watcher_impl *w, int64_t type, rt_string path) {
    if (w->event_count >= WATCHER_EVENT_QUEUE_SIZE) {
        int64_t overflow_slot =
            (w->event_tail + WATCHER_EVENT_QUEUE_SIZE - 1) % WATCHER_EVENT_QUEUE_SIZE;
        int64_t dropped;
        if (w->events[overflow_slot].type == RT_WATCH_EVENT_OVERFLOW) {
            // Coalesce with the existing marker; once the count is UNKNOWN (a
            // native overflow folded in), it stays UNKNOWN (VDOC-190).
            dropped = w->events[overflow_slot].dropped_count == WATCHER_OVERFLOW_COUNT_UNKNOWN
                          ? WATCHER_OVERFLOW_COUNT_UNKNOWN
                          : w->events[overflow_slot].dropped_count + 1;
        } else {
            dropped = 2;
        }
        rt_string overflow_path = rt_string_ref((rt_string)w->watch_path);
        if (w->events[overflow_slot].path)
            rt_string_unref(w->events[overflow_slot].path);
        w->events[overflow_slot].type = RT_WATCH_EVENT_OVERFLOW;
        w->events[overflow_slot].path = overflow_path;
        w->events[overflow_slot].dropped_count = dropped;
        if (path)
            rt_string_unref(path);
        return;
    }

    w->events[w->event_tail].type = type;
    w->events[w->event_tail].path = path;
    w->events[w->event_tail].dropped_count = type == RT_WATCH_EVENT_OVERFLOW ? 1 : 0;
    w->event_tail = (w->event_tail + 1) % WATCHER_EVENT_QUEUE_SIZE;
    w->event_count++;
}

/// @brief Queue an overflow marker for a NATIVE (kernel) queue overflow.
/// @details The OS dropped an unknown number of events before Zanna could read
///          them, so the marker carries the UNKNOWN count sentinel rather than
///          a fabricated zero (VDOC-190). If the ring is full it coalesces into
///          the newest slot, keeping the count UNKNOWN. Distinct from the
///          internal ring overflow, which counts precisely. Only Linux inotify
///          (IN_Q_OVERFLOW) and Windows (failed/zero-byte overlapped read)
///          report native overflow; macOS kqueue is level-triggered per-fd and
///          has no queue-overflow notification, so this helper is unused there.
#if RT_PLATFORM_LINUX || RT_PLATFORM_WINDOWS
static void watcher_queue_native_overflow(rt_watcher_impl *w) {
    if (w->event_count >= WATCHER_EVENT_QUEUE_SIZE) {
        int64_t slot = (w->event_tail + WATCHER_EVENT_QUEUE_SIZE - 1) % WATCHER_EVENT_QUEUE_SIZE;
        rt_string overflow_path = rt_string_ref((rt_string)w->watch_path);
        if (w->events[slot].path)
            rt_string_unref(w->events[slot].path);
        w->events[slot].type = RT_WATCH_EVENT_OVERFLOW;
        w->events[slot].path = overflow_path;
        w->events[slot].dropped_count = WATCHER_OVERFLOW_COUNT_UNKNOWN;
        return;
    }
    w->events[w->event_tail].type = RT_WATCH_EVENT_OVERFLOW;
    w->events[w->event_tail].path = rt_string_ref((rt_string)w->watch_path);
    w->events[w->event_tail].dropped_count = WATCHER_OVERFLOW_COUNT_UNKNOWN;
    w->event_tail = (w->event_tail + 1) % WATCHER_EVENT_QUEUE_SIZE;
    w->event_count++;
}
#endif // RT_PLATFORM_LINUX || RT_PLATFORM_WINDOWS

/// @brief Pop the oldest queued event into `*out`, transferring string ownership.
///
/// Zeroes the slot's path pointer so the ring-buffer's own reference
/// count isn't decremented when the slot is later overwritten or the
/// watcher is finalized. The caller becomes responsible for
/// releasing `out->path`.
static int watcher_dequeue_event(rt_watcher_impl *w, watcher_event *out) {
    if (w->event_count == 0)
        return 0;

    *out = w->events[w->event_head];
    w->events[w->event_head].path = NULL; // Ownership transferred
    w->event_head = (w->event_head + 1) % WATCHER_EVENT_QUEUE_SIZE;
    w->event_count--;
    return 1;
}

#if RT_PLATFORM_LINUX
static void watcher_close_inotify(rt_watcher_impl *w) {
    if (!w)
        return;
    if (w->watch_descriptor >= 0 && w->inotify_fd >= 0)
        (void)inotify_rm_watch(w->inotify_fd, w->watch_descriptor);
    w->watch_descriptor = -1;
    if (w->inotify_fd >= 0)
        (void)close(w->inotify_fd);
    w->inotify_fd = -1;
    w->is_watching = 0;
}

/// @brief Drain pending inotify events from the kernel and translate to RT_WATCH_EVENT_*.
///
/// A single `read` can return multiple packed `struct inotify_event`
/// records — the loop walks them using each event's `len` field for
/// stride. Maps inotify's event flags to the Zanna event taxonomy:
/// MOVED_FROM/TO/MOVE_SELF collapse to RENAMED, DELETE/DELETE_SELF to
/// DELETED. Each event's `name` (relative to the watched dir) is
/// converted to a full path via `watcher_event_path_from_relative`,
/// which also discards sibling-file events when the watcher is
/// configured for a specific file rather than a directory.
static void watcher_read_inotify_events(rt_watcher_impl *w) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    int terminal = 0;
    for (;;) {
        ssize_t len = read(w->inotify_fd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                watcher_queue_native_overflow(w);
                terminal = 1;
            }
            break;
        }
        if (len == 0) {
            watcher_queue_native_overflow(w);
            terminal = 1;
            break;
        }

        char *ptr = buf;
        char *end = buf + len;
        while ((size_t)(end - ptr) >= sizeof(struct inotify_event)) {
            struct inotify_event *event = (struct inotify_event *)ptr;
            size_t stride = sizeof(struct inotify_event) + (size_t)event->len;
            if (stride > (size_t)(end - ptr)) {
                watcher_queue_native_overflow(w);
                terminal = 1;
                break;
            }
            int64_t type = RT_WATCH_EVENT_NONE;

        // The kernel sets IN_Q_OVERFLOW (wd == -1, no name) when its own event
        // queue overflowed and events were lost. Translate it into an overflow
        // marker with an UNKNOWN loss count so clients get the documented
        // rescan signal (VDOC-190).
            if (event->mask & IN_Q_OVERFLOW) {
                watcher_queue_native_overflow(w);
                ptr += stride;
                continue;
            }
            if (event->mask & (IN_IGNORED | IN_UNMOUNT)) {
                watcher_queue_native_overflow(w);
                terminal = 1;
                ptr += stride;
                continue;
            }

            if (event->mask & IN_CREATE)
                type = RT_WATCH_EVENT_CREATED;
            else if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB))
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

            ptr += stride;
        }
        if (ptr != end && !terminal) {
            watcher_queue_native_overflow(w);
            terminal = 1;
        }
        if (terminal)
            break;
    }
    if (terminal)
        watcher_close_inotify(w);
}
#endif

#if RT_PLATFORM_MACOS
/// @brief Wait up to `timeout_ms` for a kqueue EVFILT_VNODE event and queue it.
///
/// Unlike inotify/ReadDirectoryChangesW, kqueue reports vnode changes
/// against the *watched inode* rather than by filename — so every
/// event is associated with the watched path itself, and the queued
/// event's path is just `w->watch_path`. The filter bits are mapped
/// to Zanna event types (DELETE→DELETED, WRITE/EXTEND/ATTRIB→
/// MODIFIED, RENAME→RENAMED). `timeout_ms < 0` means wait forever;
/// 0 is a non-blocking poll.
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

#if RT_PLATFORM_WINDOWS
typedef BOOL(WINAPI *watcher_cancel_io_ex_fn)(HANDLE, LPOVERLAPPED);

/// @brief Request cross-thread overlapped cancellation without a static import.
/// @details CancelIoEx is present on every supported Windows release, but the
///          native Zanna linker intentionally keeps a fixed import surface.
///          Resolve it from kernel32 and retain CancelIo as a legacy fallback.
static int watcher_cancel_pending_windows_io(rt_watcher_impl *w) {
    HMODULE kernel32;
    watcher_cancel_io_ex_fn cancel_io_ex;
    if (!w || w->dir_handle == INVALID_HANDLE_VALUE || !w->pending_read)
        return 0;
    kernel32 = GetModuleHandleW(L"kernel32.dll");
    cancel_io_ex =
        kernel32 ? (watcher_cancel_io_ex_fn)GetProcAddress(kernel32, "CancelIoEx") : NULL;
    if (cancel_io_ex) {
        (void)cancel_io_ex(w->dir_handle, &w->overlapped);
        return 1;
    }
    return CancelIo(w->dir_handle) != FALSE;
}

/// @brief Cancel pending directory I/O and release all Win32 watcher handles.
static void watcher_close_windows_handles(rt_watcher_impl *w) {
    DWORD ignored = 0;
    if (!w)
        return;
    if (w->dir_handle != INVALID_HANDLE_VALUE) {
        if (watcher_cancel_pending_windows_io(w))
            (void)GetOverlappedResult(w->dir_handle, &w->overlapped, &ignored, TRUE);
        CloseHandle(w->dir_handle);
        w->dir_handle = INVALID_HANDLE_VALUE;
    }
    if (w->overlapped.hEvent) {
        CloseHandle(w->overlapped.hEvent);
        w->overlapped.hEvent = NULL;
    }
    w->pending_read = FALSE;
}

static BOOL watcher_start_windows_read(rt_watcher_impl *w) {
    if (!w || w->dir_handle == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!w->overlapped.hEvent || !ResetEvent(w->overlapped.hEvent))
        return FALSE;
    memset(w->buffer, 0, sizeof(w->buffer));
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
    w->pending_read = ok ? TRUE : FALSE;
    return ok;
}

/// @brief Rearm overlapped monitoring or report and retire a broken watcher.
static void watcher_rearm_windows_or_stop(rt_watcher_impl *w, int failure_already_reported) {
    if (!w || !w->is_watching)
        return;
    if (watcher_start_windows_read(w))
        return;
    if (!failure_already_reported)
        watcher_queue_event_owned(
            w, RT_WATCH_EVENT_OVERFLOW, rt_string_ref((rt_string)w->watch_path));
    watcher_close_windows_handles(w);
    w->is_watching = 0;
}

/// @brief Process a completed ReadDirectoryChangesW batch and rearm for the next.
///
/// ReadDirectoryChangesW is overlapped/async — `GetOverlappedResult`
/// checks whether the pending request has completed. If so, the
/// buffer holds a packed chain of `FILE_NOTIFY_INFORMATION` records
/// (one per changed file, with a linked-list offset between them).
/// Each record's FileName is UTF-16; decoded to UTF-8 via
/// `WideCharToMultiByte`, then turned into a full path and queued.
/// After decoding, immediately re-issues the overlapped read so we
/// never miss a window of events while the queue is being consumed.
static void watcher_read_windows_events(rt_watcher_impl *w) {
    if (!w->pending_read)
        return;

    DWORD bytes_returned = 0;
    if (!GetOverlappedResult(w->dir_handle, &w->overlapped, &bytes_returned, FALSE)) {
        if (GetLastError() == ERROR_IO_INCOMPLETE)
            return; // Still pending
        w->pending_read = FALSE;
        // A failed overlapped read means the kernel dropped an unknown number
        // of change records — a native overflow, not a zero-loss marker
        // (VDOC-190).
        watcher_queue_native_overflow(w);
        watcher_rearm_windows_or_stop(w, 1);
        return;
    }

    w->pending_read = FALSE;

    if (bytes_returned == 0) {
        // Zero bytes with a completed read means the change buffer overflowed
        // and the kernel could not report which files changed (native overflow,
        // unknown loss count) (VDOC-190).
        watcher_queue_native_overflow(w);
        watcher_rearm_windows_or_stop(w, 1);
        return;
    }

    size_t offset = 0;
    int malformed_batch = 0;
    while (offset < (size_t)bytes_returned) {
        const size_t header_bytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        const size_t remaining = (size_t)bytes_returned - offset;
        size_t record_bytes;
        FILE_NOTIFY_INFORMATION *info;
        if (remaining < header_bytes) {
            malformed_batch = 1;
            break;
        }
        info = (FILE_NOTIFY_INFORMATION *)(w->buffer + offset);
        if ((info->FileNameLength % sizeof(WCHAR)) != 0 ||
            (size_t)info->FileNameLength > remaining - header_bytes) {
            malformed_batch = 1;
            break;
        }
        record_bytes = header_bytes + (size_t)info->FileNameLength;
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

        if (type != RT_WATCH_EVENT_NONE && info->FileNameLength != 0) {
            // Convert wide string to UTF-8
            int name_len = info->FileNameLength / sizeof(WCHAR);
            int utf8_len = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, info->FileName, name_len, NULL, 0, NULL, NULL);
            char *name = utf8_len > 0 ? (char *)malloc((size_t)utf8_len + 1u) : NULL;
            if (name && WideCharToMultiByte(CP_UTF8,
                                            WC_ERR_INVALID_CHARS,
                                            info->FileName,
                                            name_len,
                                            name,
                                            utf8_len,
                                            NULL,
                                            NULL) == utf8_len) {
                name[utf8_len] = '\0';
                rt_string path = watcher_event_path_from_relative(w, name);
                if (path)
                    watcher_queue_event_owned(w, type, path);
            } else {
                malformed_batch = 1;
            }
            free(name);
        }

        if (info->NextEntryOffset == 0)
            break;
        if ((size_t)info->NextEntryOffset < record_bytes ||
            (size_t)info->NextEntryOffset >= remaining ||
            (info->NextEntryOffset % sizeof(DWORD)) != 0) {
            malformed_batch = 1;
            break;
        }
        offset += info->NextEntryOffset;
    }

    if (malformed_batch)
        watcher_queue_event_owned(
            w, RT_WATCH_EVENT_OVERFLOW, rt_string_ref((rt_string)w->watch_path));
    watcher_rearm_windows_or_stop(w, malformed_batch);
}
#endif

/// @brief Construct a filesystem watcher for `path` (file or directory). `stat`'s the path up
/// front and traps if it doesn't exist. Distinguishes file vs directory mode (different OS
/// primitives needed). Returns a GC-managed handle; user must call `_start` to begin watching.
void *rt_watcher_new(rt_string path) {
    if (!path) {
        rt_trap("Watcher.New: null path");
        return NULL;
    }

    const char *cpath = NULL;
    if (!rt_file_path_from_vstr((const ZannaString *)path, &cpath) || !cpath || cpath[0] == '\0') {
        rt_trap("Watcher.New: empty path");
        return NULL;
    }

    // Check if path exists
#if RT_PLATFORM_WINDOWS
    wchar_t *wide_path = rt_file_path_utf8_to_wide(cpath);
    if (!wide_path) {
        rt_trap("Watcher.New: invalid path");
        return NULL;
    }
    DWORD attrs = GetFileAttributesW(wide_path);
    free(wide_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        rt_trap("Watcher.New: path does not exist");
        return NULL;
    }
    int8_t is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 ? 1 : 0;
#else
    struct stat st;
    if (stat(cpath, &st) != 0) {
        rt_trap("Watcher.New: path does not exist");
        return NULL;
    }
    int8_t is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
#endif

    rt_watcher_impl *w =
        (rt_watcher_impl *)rt_obj_new_i64(RT_WATCHER_CLASS_ID, (int64_t)sizeof(rt_watcher_impl));
    if (!w) {
        rt_trap("Watcher.New: alloc failed");
        return NULL;
    }

    memset(w, 0, sizeof(rt_watcher_impl));
    w->watch_path = str_from_cstr(cpath);
    w->is_directory = is_directory;
    if (w->is_directory) {
        w->watch_dir_path = rt_string_ref((rt_string)w->watch_path);
        w->watch_leaf_name = NULL;
    } else {
        w->watch_dir_path = rt_path_dir((rt_string)w->watch_path);
        w->watch_leaf_name = rt_path_name((rt_string)w->watch_path);
    }
    w->is_watching = 0;
    watcher_clear_events(w);

#if RT_PLATFORM_LINUX
    w->inotify_fd = -1;
    w->watch_descriptor = -1;
#elif RT_PLATFORM_MACOS
    w->kqueue_fd = -1;
    w->watched_fd = -1;
#elif RT_PLATFORM_WINDOWS
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
    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");
    if (w->watch_path)
        rt_string_ref(w->watch_path);
    return w->watch_path ? w->watch_path : str_from_cstr("");
}

/// @brief Returns 1 between successful `_start` and `_stop`; 0 otherwise.
int8_t rt_watcher_get_is_watching(void *obj) {
    if (!obj)
        return 0;
    return watcher_require(obj, "Watcher: invalid watcher")->is_watching;
}

/// @brief Begin watching. Per platform:
///   - **Linux:** `inotify_init1(IN_NONBLOCK)` + `inotify_add_watch` with mask covering create/
///     delete/modify/move events.
///   - **macOS:** `kqueue` + open(O_EVTONLY) + EVFILT_VNODE for delete/write/extend/attrib/rename.
///   - **Win32:** `CreateFileW(FILE_LIST_DIRECTORY, FILE_FLAG_OVERLAPPED)` + initial
///     `ReadDirectoryChangesW`. Always watches the parent directory (file-mode filtering happens
///     at event-decode time via `watch_leaf_name`).
/// @details Programmer errors (null/already-watching/invalid path) still trap. A
///          transient OS resource failure (out of descriptors, path vanished)
///          instead leaves the watcher inactive (`IsWatching` stays false) and
///          returns, so callers can degrade to periodic rescans rather than crash.
void rt_watcher_start(void *obj) {
    if (!obj) {
        rt_trap("Watcher.Start: null watcher");
        return;
    }

    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");
    if (!w)
        return;
    if (w->is_watching) {
        rt_trap("Watcher.Start: already watching");
        return;
    }

#if RT_PLATFORM_LINUX
    w->inotify_fd = inotify_init1(IN_NONBLOCK
#ifdef IN_CLOEXEC
                                  | IN_CLOEXEC
#endif
    );
    if (w->inotify_fd < 0) {
        w->inotify_fd = -1;
        return; // out of descriptors: stay inactive, caller degrades to rescans
    }
#if defined(FD_CLOEXEC) && !defined(IN_CLOEXEC)
    int inotify_flags = fcntl(w->inotify_fd, F_GETFD);
    if (inotify_flags >= 0)
        (void)fcntl(w->inotify_fd, F_SETFD, inotify_flags | FD_CLOEXEC);
#endif

    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
                    IN_MOVED_FROM | IN_MOVED_TO;
    const char *watch_target =
        rt_string_cstr(w->is_directory ? (rt_string)w->watch_path : (rt_string)w->watch_dir_path);
    if (w->is_directory)
        mask |= IN_DELETE_SELF | IN_MOVE_SELF;

    w->watch_descriptor = inotify_add_watch(w->inotify_fd, watch_target, mask);
    if (w->watch_descriptor < 0) {
        close(w->inotify_fd);
        w->inotify_fd = -1;
        return; // watch limit reached / path gone: stay inactive
    }

#elif RT_PLATFORM_MACOS
    const char *cpath = rt_string_cstr(w->watch_path);
    w->kqueue_fd = kqueue();
    if (w->kqueue_fd < 0) {
        w->kqueue_fd = -1;
        return; // out of descriptors: stay inactive, caller degrades to rescans
    }
#if defined(FD_CLOEXEC)
    int kq_flags = fcntl(w->kqueue_fd, F_GETFD);
    if (kq_flags >= 0)
        (void)fcntl(w->kqueue_fd, F_SETFD, kq_flags | FD_CLOEXEC);
#endif

    int watch_open_flags = O_EVTONLY;
#ifdef O_CLOEXEC
    watch_open_flags |= O_CLOEXEC;
#endif
    w->watched_fd = open(cpath, watch_open_flags);
    if (w->watched_fd < 0) {
        close(w->kqueue_fd);
        w->kqueue_fd = -1;
        w->watched_fd = -1;
        return; // out of descriptors / path gone: stay inactive
    }
#if defined(FD_CLOEXEC) && !defined(O_CLOEXEC)
    int watched_flags = fcntl(w->watched_fd, F_GETFD);
    if (watched_flags >= 0)
        (void)fcntl(w->watched_fd, F_SETFD, watched_flags | FD_CLOEXEC);
#endif

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
        return; // kevent registration failed: stay inactive
    }

#elif RT_PLATFORM_WINDOWS
    // For Windows, we need to watch the directory (or parent directory for files)
    const char *watch_dir =
        rt_string_cstr(w->is_directory ? (rt_string)w->watch_path : (rt_string)w->watch_dir_path);
    wchar_t *wide_watch_dir = rt_file_path_utf8_to_wide(watch_dir);
    if (!wide_watch_dir) {
        rt_trap("Watcher.Start: invalid watch path");
        return;
    }

    w->dir_handle = CreateFileW(wide_watch_dir,
                                FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                NULL);
    free(wide_watch_dir);
    if (w->dir_handle == INVALID_HANDLE_VALUE) {
        rt_trap("Watcher.Start: failed to open directory for watching");
        return;
    }

    memset(&w->overlapped, 0, sizeof(w->overlapped));
    w->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!w->overlapped.hEvent) {
        CloseHandle(w->dir_handle);
        w->dir_handle = INVALID_HANDLE_VALUE;
        rt_trap("Watcher.Start: failed to create event");
        return;
    }

    if (!watcher_start_windows_read(w)) {
        watcher_close_windows_handles(w);
        rt_trap("Watcher.Start: failed to start watching");
        return;
    }

#else
    rt_trap("Watcher.Start: unsupported platform");
#endif

    w->is_watching = 1;
}

/// @brief Stop watching: tear down the platform-specific descriptor (inotify_rm_watch + close /
/// close(kqueue) / CancelIo + CloseHandle). Idempotent — no-op on already-stopped watchers.
/// Pending and last-event state are cleared before returning.
void rt_watcher_stop(void *obj) {
    if (!obj)
        return;

    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");
    if (!w->is_watching)
        return;

#if RT_PLATFORM_LINUX
    if (w->watch_descriptor >= 0) {
        inotify_rm_watch(w->inotify_fd, w->watch_descriptor);
        w->watch_descriptor = -1;
    }
    if (w->inotify_fd >= 0) {
        close(w->inotify_fd);
        w->inotify_fd = -1;
    }
#elif RT_PLATFORM_MACOS
    if (w->watched_fd >= 0) {
        close(w->watched_fd);
        w->watched_fd = -1;
    }
    if (w->kqueue_fd >= 0) {
        close(w->kqueue_fd);
        w->kqueue_fd = -1;
    }
#elif RT_PLATFORM_WINDOWS
    watcher_close_windows_handles(w);
#endif

    watcher_clear_events(w);
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

    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");
    // First check if we have queued events
    watcher_event ev;
    if (watcher_dequeue_event(w, &ev)) {
        // Store as last event
        if (w->last_event_path)
            rt_string_unref(w->last_event_path);
        w->last_event_type = ev.type;
        w->last_event_path = ev.path;
        w->last_overflow_count = ev.dropped_count;
        w->has_last_event = 1;
        return ev.type;
    }
    if (!w->is_watching)
        return RT_WATCH_EVENT_NONE;

    // Read new events from OS
#if RT_PLATFORM_LINUX
    struct pollfd pfd;
    pfd.fd = w->inotify_fd;
    pfd.events = POLLIN;
    int timeout = watcher_timeout_to_int(ms);
    int poll_rc = 0;
    // For a positive timeout, anchor a single monotonic deadline so an EINTR
    // retry resumes with the REMAINING budget rather than restarting the full
    // wait each time — otherwise repeated signals could block far longer than
    // the documented "up to `ms`" (VDOC-191). A negative timeout (wait forever)
    // and a zero timeout (poll once) keep their original semantics.
    int64_t deadline_us = timeout > 0 ? rt_clock_ticks_us() + (int64_t)timeout * 1000 : 0;
    for (;;) {
        poll_rc = poll(&pfd, 1, timeout);
        if (poll_rc >= 0 || errno != EINTR)
            break;
        if (timeout > 0) {
            int64_t remaining_us = deadline_us - rt_clock_ticks_us();
            if (remaining_us <= 0) {
                poll_rc = 0; // deadline reached during interruption: time out
                break;
            }
            timeout = (int)((remaining_us + 999) / 1000); // ceil to ms
        }
        // timeout <= 0 (wait-forever or poll-once) retries with the same value.
    }
    if (poll_rc > 0 && (pfd.revents & POLLIN)) {
        watcher_read_inotify_events(w);
    }
    if (poll_rc > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        watcher_queue_native_overflow(w);
        watcher_close_inotify(w);
    }
#elif RT_PLATFORM_MACOS
    watcher_read_kqueue_events(w, watcher_timeout_to_int(ms));
#elif RT_PLATFORM_WINDOWS
    if (w->pending_read) {
        DWORD wait_result;
        if (ms < 0) {
            wait_result = WaitForSingleObject(w->overlapped.hEvent, INFINITE);
        } else {
            ULONGLONG deadline = rt_win32_deadline_from_now_ms(ms);
            do {
                DWORD timeout = rt_win32_wait_slice_until(deadline);
                wait_result = WaitForSingleObject(w->overlapped.hEvent, timeout);
                if (wait_result != WAIT_TIMEOUT || timeout == 0)
                    break;
            } while (rt_win32_wait_slice_until(deadline) != 0);
        }
        if (wait_result == WAIT_OBJECT_0) {
            watcher_read_windows_events(w);
        } else if (wait_result == WAIT_FAILED) {
            watcher_queue_event_owned(
                w, RT_WATCH_EVENT_OVERFLOW, rt_string_ref((rt_string)w->watch_path));
            watcher_close_windows_handles(w);
            w->is_watching = 0;
        }
    }
#endif

    // Try to dequeue again after reading
    if (watcher_dequeue_event(w, &ev)) {
        if (w->last_event_path)
            rt_string_unref(w->last_event_path);
        w->last_event_type = ev.type;
        w->last_event_path = ev.path;
        w->last_overflow_count = ev.dropped_count;
        w->has_last_event = 1;
        return ev.type;
    }

    return RT_WATCH_EVENT_NONE;
}

/// @brief Read the path of the most recently polled event. **Traps** if no `_poll` call
/// has succeeded yet — the contract is "poll then ask"; not safe to call out of order.
rt_string rt_watcher_event_path(void *obj) {
    if (!obj) {
        rt_trap("Watcher.EventPath: null watcher");
        return str_from_cstr("");
    }

    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");
    if (!w)
        return str_from_cstr("");
    if (!w->has_last_event) {
        rt_trap("Watcher.EventPath: no event polled yet");
        return str_from_cstr("");
    }

    if (w->last_event_path)
        rt_string_ref(w->last_event_path);
    return w->last_event_path ? w->last_event_path : str_from_cstr("");
}

/// @brief Read the type code of the last polled event. Returns NONE if no event has been polled.
int64_t rt_watcher_event_type(void *obj) {
    if (!obj)
        return RT_WATCH_EVENT_NONE;

    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");
    return w->has_last_event ? w->last_event_type : RT_WATCH_EVENT_NONE;
}

/// @brief Read how many file-system events were represented by the last overflow marker.
/// @details Returns zero when no event has been polled or when the most recent
///          event was not `RT_WATCH_EVENT_OVERFLOW`. For an INTERNAL ring
///          overflow (Zanna's own 64-entry queue filled), the value is the exact
///          number of dropped events and is coalesced upward while the queue
///          stays full, so it can be greater than one. For a NATIVE (kernel)
///          overflow — Linux `IN_Q_OVERFLOW` or a Windows change-buffer overflow
///          — the OS does not report how many events were lost, so the value is
///          `-1` (unknown) rather than a fabricated count (VDOC-190).
int64_t rt_watcher_event_overflow_count(void *obj) {
    if (!obj)
        return 0;
    rt_watcher_impl *w = watcher_require(obj, "Watcher: invalid watcher");
    if (!w->has_last_event || w->last_event_type != RT_WATCH_EVENT_OVERFLOW)
        return 0;
    return w->last_overflow_count;
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

/// @brief Constant: `RT_WATCH_EVENT_OVERFLOW` (some file-system events were dropped).
int64_t rt_watcher_event_overflow(void *self) {
    (void)self;
    return RT_WATCH_EVENT_OVERFLOW;
}
