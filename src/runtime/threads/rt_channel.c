//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_channel.c
// Purpose: Implements a bounded, thread-safe channel for the Viper.Threads.Channel
//          class, modelled after Go channels. Uses a ring buffer protected by a
//          monitor; senders block when full, receivers block when empty.
//
// Key invariants:
//   - Capacity is fixed at construction; Send blocks when count == capacity.
//   - Recv blocks when count == 0; it returns NULL immediately if closed+empty.
//   - Closing a channel unblocks all waiting senders and receivers.
//   - Sending to a closed channel traps immediately.
//   - The ring buffer is indexed by head/tail modulo capacity.
//   - All public operations acquire the monitor before touching shared state.
//
// Ownership/Lifetime:
//   - The channel retains references to all objects stored in the ring buffer.
//   - The finalizer releases retained items and frees the buffer on GC collection.
//   - Callers transfer ownership of sent objects to the channel; receivers own
//     the returned objects.
//
// Links: src/runtime/threads/rt_channel.h (public API),
//        src/runtime/threads/rt_monitor.h (monitor used for synchronization),
//        src/runtime/threads/rt_threads.h (thread primitives)
//
//===----------------------------------------------------------------------===//

#include "rt_channel.h"

#include "rt_object.h"
#include "rt_threads.h"

#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

//=============================================================================
// Internal Structures
//=============================================================================

/// @brief Channel implementation.
typedef struct channel_impl {
    void *monitor;             ///< Monitor for synchronization.
    void **buffer;             ///< Ring buffer for items.
    int64_t capacity;          ///< Buffer capacity.
    int64_t count;             ///< Number of items in buffer.
    int64_t head;              ///< Index of next read.
    int64_t tail;              ///< Index of next write.
    int64_t waiting_senders;   ///< Number of blocked senders.
    int64_t waiting_receivers; ///< Number of blocked receivers.
    int64_t sync_epoch;        ///< Monotonic synchronous handoff identifier.
    int64_t sync_acked_epoch;  ///< Last synchronous handoff consumed by a receiver.
    int8_t closed;             ///< Closed flag.
} channel_impl;

//=============================================================================
// Forward Declarations
//=============================================================================

static void channel_finalizer(void *obj);

extern void rt_trap(const char *msg);

#if defined(_WIN32)
typedef struct {
    ULONGLONG deadline;
} channel_deadline;

static channel_deadline channel_deadline_from_now(int64_t ms) {
    channel_deadline d;
    d.deadline = GetTickCount64() + (ms > 0 ? (ULONGLONG)ms : 0);
    return d;
}

static int64_t channel_remaining_ms(channel_deadline d) {
    ULONGLONG now = GetTickCount64();
    return now >= d.deadline ? 0 : (int64_t)(d.deadline - now);
}
#else
typedef struct {
    struct timespec deadline;
} channel_deadline;

static channel_deadline channel_deadline_from_now(int64_t ms) {
    channel_deadline d;
    clock_gettime(CLOCK_MONOTONIC, &d.deadline);
    if (ms > 0) {
        d.deadline.tv_sec += ms / 1000;
        d.deadline.tv_nsec += (ms % 1000) * 1000000L;
        if (d.deadline.tv_nsec >= 1000000000L) {
            d.deadline.tv_sec++;
            d.deadline.tv_nsec -= 1000000000L;
        }
    }
    return d;
}

static int64_t channel_remaining_ms(channel_deadline d) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t sec = (int64_t)d.deadline.tv_sec - (int64_t)now.tv_sec;
    int64_t ns = (int64_t)d.deadline.tv_nsec - (int64_t)now.tv_nsec;
    if (ns < 0) {
        sec--;
        ns += 1000000000L;
    }
    if (sec < 0)
        return 0;
    return sec * 1000 + ns / 1000000L;
}
#endif

//=============================================================================
// Channel Management
//=============================================================================

static void channel_finalizer(void *obj) {
    channel_impl *ch = (channel_impl *)obj;
    if (!ch)
        return;

    // Release all items in the buffer
    if (ch->buffer) {
        int64_t slots = (ch->capacity == 0) ? 1 : ch->capacity;
        for (int64_t i = 0; i < ch->count; i++) {
            int64_t idx = (ch->head + i) % slots;
            void *item = ch->buffer[idx];
            if (item) {
                if (rt_obj_release_check0(item))
                    rt_obj_free(item);
            }
        }
        free(ch->buffer);
    }

    // Release monitor
    if (ch->monitor) {
        if (rt_obj_release_check0(ch->monitor))
            rt_obj_free(ch->monitor);
    }
}

/// @brief Create a new channel for thread-safe message passing between threads.
/// @details Capacity 0 creates a synchronous (unbuffered) channel where send blocks
///          until a receiver is ready. Capacity > 0 creates a buffered channel that
///          holds up to that many items before blocking senders.
void *rt_channel_new(int64_t capacity) {
    // Minimum capacity of 1 for buffered channels
    // Capacity of 0 means synchronous (unbuffered) channel
    if (capacity < 0)
        capacity = 0;
    if (capacity > 1000000) // Reasonable limit
        capacity = 1000000;

    // For synchronous channels, use capacity of 1 internally
    int64_t buffer_size = (capacity == 0) ? 1 : capacity;

    channel_impl *ch = (channel_impl *)rt_obj_new_i64(0, sizeof(channel_impl));
    if (!ch)
        return NULL;

    rt_obj_set_finalizer(ch, channel_finalizer);

    ch->monitor = rt_obj_new_i64(0, 1); // Create a monitor object
    if (!ch->monitor) {
        rt_obj_free(ch);
        return NULL;
    }

    ch->buffer = (void **)calloc((size_t)buffer_size, sizeof(void *));
    if (!ch->buffer) {
        if (rt_obj_release_check0(ch->monitor))
            rt_obj_free(ch->monitor);
        ch->monitor = NULL;
        rt_obj_free(ch);
        return NULL;
    }

    ch->capacity = capacity;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->waiting_senders = 0;
    ch->waiting_receivers = 0;
    ch->sync_epoch = 0;
    ch->sync_acked_epoch = 0;
    ch->closed = 0;

    return ch;
}

//=============================================================================
// Public API - Send Operations
//=============================================================================

/// @brief Send an item into the channel, blocking if the buffer is full (or until a receiver is ready for sync channels).
void rt_channel_send(void *channel, void *item) {
    if (!channel) {
        rt_trap("Channel.Send: nil channel");
        return;
    }

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // Check if closed
    if (ch->closed) {
        rt_monitor_exit(ch->monitor);
        rt_trap("Channel.Send: send on closed channel");
        return;
    }

    // Handle synchronous channel (capacity 0)
    if (ch->capacity == 0) {
        ch->waiting_senders++;
        while ((ch->count != 0 || ch->waiting_receivers == 0) && !ch->closed) {
            rt_monitor_wait(ch->monitor);
        }

        if (ch->closed) {
            ch->waiting_senders--;
            rt_monitor_exit(ch->monitor);
            rt_trap("Channel.Send: send on closed channel");
            return;
        }

        if (item)
            rt_obj_retain_maybe(item);
        int64_t my_epoch = ++ch->sync_epoch;
        ch->buffer[0] = item;
        ch->count = 1;

        rt_monitor_pause_all(ch->monitor);

        while (ch->sync_acked_epoch < my_epoch) {
            rt_monitor_wait(ch->monitor);
        }
        ch->waiting_senders--;
        rt_monitor_exit(ch->monitor);
        return;
    }

    // Wait for space in buffered channel
    ch->waiting_senders++;
    while (ch->count >= ch->capacity && !ch->closed) {
        rt_monitor_wait(ch->monitor);
    }

    if (ch->closed) {
        ch->waiting_senders--;
        rt_monitor_exit(ch->monitor);
        rt_trap("Channel.Send: send on closed channel");
        return;
    }

    // Retain the item and enqueue
    if (item)
        rt_obj_retain_maybe(item);
    ch->buffer[ch->tail] = item;
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    // Signal waiting receivers
    if (ch->waiting_receivers > 0) {
        rt_monitor_pause(ch->monitor);
    }

    ch->waiting_senders--;
    rt_monitor_exit(ch->monitor);
}

/// @brief Try to send an item without blocking. Returns 1 if sent, 0 if full or closed.
int8_t rt_channel_try_send(void *channel, void *item) {
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // Check if closed or full
    if (ch->closed || (ch->capacity > 0 && ch->count >= ch->capacity)) {
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    // For synchronous channel, need a waiting receiver and an empty handoff slot
    if (ch->capacity == 0 && (ch->waiting_receivers == 0 || ch->count != 0)) {
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    // Same logic as blocking send
    if (ch->capacity == 0) {
        if (item)
            rt_obj_retain_maybe(item);
        ch->sync_epoch++;
        ch->buffer[0] = item;
        ch->count = 1;
        rt_monitor_pause_all(ch->monitor);
    } else {
        if (item)
            rt_obj_retain_maybe(item);
        ch->buffer[ch->tail] = item;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        if (ch->waiting_receivers > 0)
            rt_monitor_pause(ch->monitor);
    }

    rt_monitor_exit(ch->monitor);
    return 1;
}

/// @brief Send with a timeout. Returns 1 if sent, 0 if the timeout elapsed or channel closed.
int8_t rt_channel_send_for(void *channel, void *item, int64_t ms) {
    if (!channel)
        return 0;

    if (ms <= 0)
        return rt_channel_try_send(channel, item);

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    if (ch->closed) {
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    channel_deadline deadline = channel_deadline_from_now(ms);

    // Handle synchronous channel
    if (ch->capacity == 0) {
        ch->waiting_senders++;
        while ((ch->count != 0 || ch->waiting_receivers == 0) && !ch->closed) {
            int64_t remaining = channel_remaining_ms(deadline);
            if (remaining <= 0 || !rt_monitor_wait_for(ch->monitor, remaining)) {
                if (ch->count == 0 && ch->waiting_receivers == 0 && !ch->closed) {
                    ch->waiting_senders--;
                    rt_monitor_exit(ch->monitor);
                    return 0;
                }
                if (ch->count != 0 || ch->waiting_receivers == 0) {
                    ch->waiting_senders--;
                    rt_monitor_exit(ch->monitor);
                    return 0;
                }
            }
        }

        if (ch->closed) {
            ch->waiting_senders--;
            rt_monitor_exit(ch->monitor);
            return 0;
        }

        if (item)
            rt_obj_retain_maybe(item);
        int64_t my_epoch = ++ch->sync_epoch;
        ch->buffer[0] = item;
        ch->count = 1;
        rt_monitor_pause_all(ch->monitor);

        while (ch->sync_acked_epoch < my_epoch) {
            int64_t remaining = channel_remaining_ms(deadline);
            if (remaining <= 0 || !rt_monitor_wait_for(ch->monitor, remaining)) {
                if (ch->sync_acked_epoch >= my_epoch)
                    break;
                if (ch->count != 0) {
                    void *pending = ch->buffer[0];
                    ch->buffer[0] = NULL;
                    ch->count = 0;
                    if (pending && rt_obj_release_check0(pending))
                        rt_obj_free(pending);
                    rt_monitor_pause_all(ch->monitor);
                }
                ch->waiting_senders--;
                rt_monitor_exit(ch->monitor);
                return 0;
            }
        }
        ch->waiting_senders--;
        rt_monitor_exit(ch->monitor);
        return 1;
    }

    // Buffered channel
    ch->waiting_senders++;
    while (ch->count >= ch->capacity && !ch->closed) {
        int64_t remaining = channel_remaining_ms(deadline);
        int8_t signaled = remaining > 0 ? rt_monitor_wait_for(ch->monitor, remaining) : 0;
        if (!signaled && ch->count >= ch->capacity && !ch->closed) {
            ch->waiting_senders--;
            rt_monitor_exit(ch->monitor);
            return 0;
        }
    }

    if (ch->closed) {
        ch->waiting_senders--;
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    if (item)
        rt_obj_retain_maybe(item);
    ch->buffer[ch->tail] = item;
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;
    if (ch->waiting_receivers > 0)
        rt_monitor_pause(ch->monitor);

    ch->waiting_senders--;
    rt_monitor_exit(ch->monitor);
    return 1;
}

//=============================================================================
// Public API - Receive Operations
//=============================================================================

/// @brief Receive an item from the channel, blocking until one is available or the channel closes.
void *rt_channel_recv(void *channel) {
    if (!channel) {
        rt_trap("Channel.Recv: nil channel");
        return NULL;
    }

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // Handle synchronous channel
    if (ch->capacity == 0) {
        ch->waiting_receivers++;

        // Signal any waiting senders
        if (ch->waiting_senders > 0)
            rt_monitor_pause(ch->monitor);

        // Wait for item
        while (ch->count == 0 && !ch->closed) {
            rt_monitor_wait(ch->monitor);
        }

        if (ch->count == 0) {
            // Closed and empty
            ch->waiting_receivers--;
            rt_monitor_exit(ch->monitor);
            return NULL;
        }

        void *item = ch->buffer[0];
        ch->buffer[0] = NULL;
        ch->count = 0;
        ch->sync_acked_epoch = ch->sync_epoch;

        // Signal any waiting senders
        if (ch->waiting_senders > 0)
            rt_monitor_pause_all(ch->monitor);

        ch->waiting_receivers--;
        rt_monitor_exit(ch->monitor);
        return item; // Already retained by sender
    }

    // Buffered channel - wait for item
    ch->waiting_receivers++;
    while (ch->count == 0 && !ch->closed) {
        rt_monitor_wait(ch->monitor);
    }

    if (ch->count == 0) {
        // Closed and empty
        ch->waiting_receivers--;
        rt_monitor_exit(ch->monitor);
        return NULL;
    }

    // Dequeue item
    void *item = ch->buffer[ch->head];
    ch->buffer[ch->head] = NULL;
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    // Signal waiting senders
    if (ch->waiting_senders > 0) {
        rt_monitor_pause(ch->monitor);
    }

    ch->waiting_receivers--;
    rt_monitor_exit(ch->monitor);
    return item; // Already retained by sender
}

/// @brief Try to receive an item without blocking. Returns 1 and sets *out if available.
int8_t rt_channel_try_recv(void *channel, void **out) {
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // For synchronous channel, need a waiting sender
    if (ch->capacity == 0) {
        if (ch->count == 0) {
            rt_monitor_exit(ch->monitor);
            return 0;
        }

        void *item = ch->buffer[0];
        ch->buffer[0] = NULL;
        ch->count = 0;
        ch->sync_acked_epoch = ch->sync_epoch;
        if (ch->waiting_senders > 0)
            rt_monitor_pause_all(ch->monitor);

        if (out)
            *out = item;
        else if (item && rt_obj_release_check0(item))
            rt_obj_free(item);

        rt_monitor_exit(ch->monitor);
        return 1;
    }

    if (ch->count == 0) {
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    void *item = ch->buffer[ch->head];
    ch->buffer[ch->head] = NULL;
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;
    if (ch->waiting_senders > 0)
        rt_monitor_pause(ch->monitor);

    if (out)
        *out = item;
    else if (item && rt_obj_release_check0(item))
        rt_obj_free(item);

    rt_monitor_exit(ch->monitor);
    return 1;
}

/// @brief Receive with a timeout. Returns 1 and sets *out if received, 0 on timeout/close.
int8_t rt_channel_recv_for(void *channel, void **out, int64_t ms) {
    if (!channel)
        return 0;

    if (ms <= 0)
        return rt_channel_try_recv(channel, out);

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);
    channel_deadline deadline = channel_deadline_from_now(ms);

    // Handle synchronous channel
    if (ch->capacity == 0) {
        ch->waiting_receivers++;
        if (ch->waiting_senders > 0)
            rt_monitor_pause(ch->monitor);

        while (ch->count == 0 && !ch->closed) {
            int64_t remaining = channel_remaining_ms(deadline);
            if (remaining <= 0 || !rt_monitor_wait_for(ch->monitor, remaining)) {
                ch->waiting_receivers--;
                rt_monitor_exit(ch->monitor);
                return 0;
            }
        }

        if (ch->count == 0) {
            ch->waiting_receivers--;
            rt_monitor_exit(ch->monitor);
            return 0;
        }

        void *item = ch->buffer[0];
        ch->buffer[0] = NULL;
        ch->count = 0;
        ch->sync_acked_epoch = ch->sync_epoch;
        if (ch->waiting_senders > 0)
            rt_monitor_pause_all(ch->monitor);

        if (out)
            *out = item;
        else if (item && rt_obj_release_check0(item))
            rt_obj_free(item);

        ch->waiting_receivers--;
        rt_monitor_exit(ch->monitor);
        return 1;
    }

    // Buffered channel
    ch->waiting_receivers++;
    while (ch->count == 0 && !ch->closed) {
        int64_t remaining = channel_remaining_ms(deadline);
        int8_t signaled = remaining > 0 ? rt_monitor_wait_for(ch->monitor, remaining) : 0;
        if (!signaled && ch->count == 0 && !ch->closed) {
            ch->waiting_receivers--;
            rt_monitor_exit(ch->monitor);
            return 0;
        }
    }

    if (ch->count == 0) {
        ch->waiting_receivers--;
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    void *item = ch->buffer[ch->head];
    ch->buffer[ch->head] = NULL;
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;
    if (ch->waiting_senders > 0)
        rt_monitor_pause(ch->monitor);

    if (out)
        *out = item;
    else if (item && rt_obj_release_check0(item))
        rt_obj_free(item);

    ch->waiting_receivers--;
    rt_monitor_exit(ch->monitor);
    return 1;
}

//=============================================================================
// Public API - Close
//=============================================================================

/// @brief Close the channel. Blocked senders/receivers are woken and return failure.
void rt_channel_close(void *channel) {
    if (!channel)
        return;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    if (ch->closed) {
        rt_monitor_exit(ch->monitor);
        return; // Already closed
    }

    ch->closed = 1;

    // Wake all waiters
    rt_monitor_pause_all(ch->monitor);

    rt_monitor_exit(ch->monitor);
}

//=============================================================================
// Public API - Properties
//=============================================================================

/// @brief Get the number of items currently buffered in the channel.
int64_t rt_channel_get_len(void *channel) {
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);
    int64_t len = ch->count;
    rt_monitor_exit(ch->monitor);

    return len;
}

/// @brief Get the channel's buffer capacity (0 = synchronous/unbuffered).
int64_t rt_channel_get_cap(void *channel) {
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;
    return ch->capacity;
}

/// @brief Check whether the channel has been closed.
int8_t rt_channel_get_is_closed(void *channel) {
    if (!channel)
        return 1;

    channel_impl *ch = (channel_impl *)channel;
    return ch->closed;
}

/// @brief Check whether the channel buffer is empty (no items waiting to be received).
int8_t rt_channel_get_is_empty(void *channel) {
    if (!channel)
        return 1;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);
    int8_t empty = (ch->count == 0) ? 1 : 0;
    rt_monitor_exit(ch->monitor);

    return empty;
}

/// @brief Check whether the channel buffer is full (send would block).
int8_t rt_channel_get_is_full(void *channel) {
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;

    if (ch->capacity == 0)
        return 1; // Synchronous channels are always "full"

    rt_monitor_enter(ch->monitor);
    int8_t full = (ch->count >= ch->capacity) ? 1 : 0;
    rt_monitor_exit(ch->monitor);

    return full;
}
