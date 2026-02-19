//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_channel.c
/// @brief Thread-safe channel implementation for inter-thread communication.
///
/// This file implements a bounded channel similar to Go channels, allowing
/// threads to communicate by sending and receiving values.
///
/// **Architecture:**
///
/// | Component     | Description                           |
/// |---------------|---------------------------------------|
/// | Ring Buffer   | Circular buffer for storing items     |
/// | Monitor       | Synchronization for buffer access     |
/// | Senders       | Blocked threads waiting to send       |
/// | Receivers     | Blocked threads waiting to receive    |
///
/// **Usage Example:**
/// ```
/// Dim ch = Channel.New(10)  ' Buffered channel with capacity 10
/// Thread.Start(Sub()
///     ch.Send("Hello")
/// End Sub)
/// Print ch.Recv()  ' "Hello"
/// ch.Close()
/// ```
///
/// **Thread Safety:** All operations are thread-safe.
///
//===----------------------------------------------------------------------===//

#include "rt_channel.h"

#include "rt_object.h"
#include "rt_threads.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structures
//=============================================================================

/// @brief Channel implementation.
typedef struct channel_impl
{
    void *monitor;             ///< Monitor for synchronization.
    void **buffer;             ///< Ring buffer for items.
    int64_t capacity;          ///< Buffer capacity.
    int64_t count;             ///< Number of items in buffer.
    int64_t head;              ///< Index of next read.
    int64_t tail;              ///< Index of next write.
    int64_t waiting_senders;   ///< Number of blocked senders.
    int64_t waiting_receivers; ///< Number of blocked receivers.
    int8_t closed;             ///< Closed flag.
} channel_impl;

//=============================================================================
// Forward Declarations
//=============================================================================

static void channel_finalizer(void *obj);

extern void rt_trap(const char *msg);

//=============================================================================
// Channel Management
//=============================================================================

static void channel_finalizer(void *obj)
{
    channel_impl *ch = (channel_impl *)obj;
    if (!ch)
        return;

    // Release all items in the buffer
    if (ch->buffer)
    {
        for (int64_t i = 0; i < ch->count; i++)
        {
            int64_t idx = (ch->head + i) % ch->capacity;
            void *item = ch->buffer[idx];
            if (item)
            {
                if (rt_obj_release_check0(item))
                    rt_obj_free(item);
            }
        }
        free(ch->buffer);
    }

    // Release monitor
    if (ch->monitor)
    {
        if (rt_obj_release_check0(ch->monitor))
            rt_obj_free(ch->monitor);
    }
}

void *rt_channel_new(int64_t capacity)
{
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
    if (!ch->monitor)
    {
        rt_obj_free(ch);
        return NULL;
    }

    ch->buffer = (void **)calloc((size_t)buffer_size, sizeof(void *));
    if (!ch->buffer)
    {
        if (rt_obj_release_check0(ch->monitor))
            rt_obj_free(ch->monitor);
        rt_obj_free(ch);
        return NULL;
    }

    ch->capacity = capacity;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->waiting_senders = 0;
    ch->waiting_receivers = 0;
    ch->closed = 0;

    return ch;
}

//=============================================================================
// Public API - Send Operations
//=============================================================================

void rt_channel_send(void *channel, void *item)
{
    if (!channel)
    {
        rt_trap("Channel.Send: nil channel");
        return;
    }

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // Check if closed
    if (ch->closed)
    {
        rt_monitor_exit(ch->monitor);
        rt_trap("Channel.Send: send on closed channel");
        return;
    }

    // Handle synchronous channel (capacity 0)
    if (ch->capacity == 0)
    {
        // Wait for a receiver
        ch->waiting_senders++;
        while (ch->waiting_receivers == 0 && !ch->closed)
        {
            rt_monitor_wait(ch->monitor);
        }
        ch->waiting_senders--;

        if (ch->closed)
        {
            rt_monitor_exit(ch->monitor);
            rt_trap("Channel.Send: send on closed channel");
            return;
        }

        // Direct handoff to receiver
        // Store item in buffer temporarily
        if (item)
            rt_obj_retain_maybe(item);
        ch->buffer[0] = item;
        ch->count = 1;

        // Signal receiver
        rt_monitor_pause(ch->monitor);
        rt_monitor_exit(ch->monitor);
        return;
    }

    // Wait for space in buffered channel
    while (ch->count >= ch->capacity && !ch->closed)
    {
        ch->waiting_senders++;
        rt_monitor_wait(ch->monitor);
        ch->waiting_senders--;
    }

    if (ch->closed)
    {
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
    if (ch->waiting_receivers > 0)
    {
        rt_monitor_pause(ch->monitor);
    }

    rt_monitor_exit(ch->monitor);
}

int8_t rt_channel_try_send(void *channel, void *item)
{
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // Check if closed or full
    if (ch->closed || (ch->capacity > 0 && ch->count >= ch->capacity))
    {
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    // For synchronous channel, need a waiting receiver
    if (ch->capacity == 0 && ch->waiting_receivers == 0)
    {
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    // Same logic as blocking send
    if (ch->capacity == 0)
    {
        if (item)
            rt_obj_retain_maybe(item);
        ch->buffer[0] = item;
        ch->count = 1;
        rt_monitor_pause(ch->monitor);
    }
    else
    {
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

int8_t rt_channel_send_for(void *channel, void *item, int64_t ms)
{
    if (!channel)
        return 0;

    if (ms <= 0)
        return rt_channel_try_send(channel, item);

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    if (ch->closed)
    {
        rt_monitor_exit(ch->monitor);
        return 0;
    }

    // Handle synchronous channel
    if (ch->capacity == 0)
    {
        ch->waiting_senders++;
        while (ch->waiting_receivers == 0 && !ch->closed)
        {
            if (!rt_monitor_wait_for(ch->monitor, ms))
            {
                ch->waiting_senders--;
                rt_monitor_exit(ch->monitor);
                return 0;
            }
        }
        ch->waiting_senders--;

        if (ch->closed)
        {
            rt_monitor_exit(ch->monitor);
            return 0;
        }

        if (item)
            rt_obj_retain_maybe(item);
        ch->buffer[0] = item;
        ch->count = 1;
        rt_monitor_pause(ch->monitor);
        rt_monitor_exit(ch->monitor);
        return 1;
    }

    // Buffered channel
    while (ch->count >= ch->capacity && !ch->closed)
    {
        ch->waiting_senders++;
        int8_t signaled = rt_monitor_wait_for(ch->monitor, ms);
        ch->waiting_senders--;
        if (!signaled)
        {
            rt_monitor_exit(ch->monitor);
            return 0;
        }
    }

    if (ch->closed)
    {
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

    rt_monitor_exit(ch->monitor);
    return 1;
}

//=============================================================================
// Public API - Receive Operations
//=============================================================================

void *rt_channel_recv(void *channel)
{
    if (!channel)
    {
        rt_trap("Channel.Recv: nil channel");
        return NULL;
    }

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // Handle synchronous channel
    if (ch->capacity == 0)
    {
        ch->waiting_receivers++;

        // Signal any waiting senders
        if (ch->waiting_senders > 0)
            rt_monitor_pause(ch->monitor);

        // Wait for item
        while (ch->count == 0 && !ch->closed)
        {
            rt_monitor_wait(ch->monitor);
        }
        ch->waiting_receivers--;

        if (ch->count == 0)
        {
            // Closed and empty
            rt_monitor_exit(ch->monitor);
            return NULL;
        }

        void *item = ch->buffer[0];
        ch->buffer[0] = NULL;
        ch->count = 0;

        // Signal any waiting senders
        if (ch->waiting_senders > 0)
            rt_monitor_pause(ch->monitor);

        rt_monitor_exit(ch->monitor);
        return item; // Already retained by sender
    }

    // Buffered channel - wait for item
    while (ch->count == 0 && !ch->closed)
    {
        ch->waiting_receivers++;
        rt_monitor_wait(ch->monitor);
        ch->waiting_receivers--;
    }

    if (ch->count == 0)
    {
        // Closed and empty
        rt_monitor_exit(ch->monitor);
        return NULL;
    }

    // Dequeue item
    void *item = ch->buffer[ch->head];
    ch->buffer[ch->head] = NULL;
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    // Signal waiting senders
    if (ch->waiting_senders > 0)
    {
        rt_monitor_pause(ch->monitor);
    }

    rt_monitor_exit(ch->monitor);
    return item; // Already retained by sender
}

int8_t rt_channel_try_recv(void *channel, void **out)
{
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // For synchronous channel, need a waiting sender
    if (ch->capacity == 0)
    {
        if (ch->count == 0)
        {
            rt_monitor_exit(ch->monitor);
            return 0;
        }

        void *item = ch->buffer[0];
        ch->buffer[0] = NULL;
        ch->count = 0;
        if (ch->waiting_senders > 0)
            rt_monitor_pause(ch->monitor);

        if (out)
            *out = item;
        else if (item && rt_obj_release_check0(item))
            rt_obj_free(item);

        rt_monitor_exit(ch->monitor);
        return 1;
    }

    if (ch->count == 0)
    {
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

int8_t rt_channel_recv_for(void *channel, void **out, int64_t ms)
{
    if (!channel)
        return 0;

    if (ms <= 0)
        return rt_channel_try_recv(channel, out);

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    // Handle synchronous channel
    if (ch->capacity == 0)
    {
        ch->waiting_receivers++;
        if (ch->waiting_senders > 0)
            rt_monitor_pause(ch->monitor);

        while (ch->count == 0 && !ch->closed)
        {
            if (!rt_monitor_wait_for(ch->monitor, ms))
            {
                ch->waiting_receivers--;
                rt_monitor_exit(ch->monitor);
                return 0;
            }
        }
        ch->waiting_receivers--;

        if (ch->count == 0)
        {
            rt_monitor_exit(ch->monitor);
            return 0;
        }

        void *item = ch->buffer[0];
        ch->buffer[0] = NULL;
        ch->count = 0;
        if (ch->waiting_senders > 0)
            rt_monitor_pause(ch->monitor);

        if (out)
            *out = item;
        else if (item && rt_obj_release_check0(item))
            rt_obj_free(item);

        rt_monitor_exit(ch->monitor);
        return 1;
    }

    // Buffered channel
    while (ch->count == 0 && !ch->closed)
    {
        ch->waiting_receivers++;
        int8_t signaled = rt_monitor_wait_for(ch->monitor, ms);
        ch->waiting_receivers--;
        if (!signaled)
        {
            rt_monitor_exit(ch->monitor);
            return 0;
        }
    }

    if (ch->count == 0)
    {
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

//=============================================================================
// Public API - Close
//=============================================================================

void rt_channel_close(void *channel)
{
    if (!channel)
        return;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);

    if (ch->closed)
    {
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

int64_t rt_channel_get_len(void *channel)
{
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);
    int64_t len = ch->count;
    rt_monitor_exit(ch->monitor);

    return len;
}

int64_t rt_channel_get_cap(void *channel)
{
    if (!channel)
        return 0;

    channel_impl *ch = (channel_impl *)channel;
    return ch->capacity;
}

int8_t rt_channel_get_is_closed(void *channel)
{
    if (!channel)
        return 1;

    channel_impl *ch = (channel_impl *)channel;
    return ch->closed;
}

int8_t rt_channel_get_is_empty(void *channel)
{
    if (!channel)
        return 1;

    channel_impl *ch = (channel_impl *)channel;

    rt_monitor_enter(ch->monitor);
    int8_t empty = (ch->count == 0) ? 1 : 0;
    rt_monitor_exit(ch->monitor);

    return empty;
}

int8_t rt_channel_get_is_full(void *channel)
{
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
