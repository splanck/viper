//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_channel.h
// Purpose: Thread-safe channel for inter-thread communication providing FIFO bounded/unbounded send
// and receive with blocking semantics when empty or full.
//
// Key invariants:
//   - FIFO ordering is guaranteed across all operations.
//   - Bounded channels block senders when full until a receiver makes space.
//   - Unbounded channels (capacity 0) never block senders.
//   - rt_channel_recv blocks until a message is available or the channel is closed.
//
// Ownership/Lifetime:
//   - Channel objects are runtime-managed and reference-counted.
//   - Senders and receivers share the same reference-counted channel object.
//
// Links: src/runtime/threads/rt_channel.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Viper.Threads.Channel
    //=========================================================================

    /// @brief Create a new bounded channel with the specified capacity.
    /// @details A bounded channel blocks Send when at capacity.
    /// @param capacity Maximum number of items (0 for synchronous channel).
    /// @return Opaque Channel object pointer, or NULL on failure.
    void *rt_channel_new(int64_t capacity);

    /// @brief Send an item to the channel, blocking if full.
    /// @details Blocks until space is available or the channel is closed.
    ///          Traps if the channel is closed.
    /// @param channel Channel object pointer.
    /// @param item Item to send.
    void rt_channel_send(void *channel, void *item);

    /// @brief Try to send an item without blocking.
    /// @details Returns immediately with success if space is available.
    /// @param channel Channel object pointer.
    /// @param item Item to send.
    /// @return 1 if sent, 0 if channel is full or closed.
    int8_t rt_channel_try_send(void *channel, void *item);

    /// @brief Send with a timeout.
    /// @details Blocks up to @p ms milliseconds for space.
    /// @param channel Channel object pointer.
    /// @param item Item to send.
    /// @param ms Timeout in milliseconds.
    /// @return 1 if sent, 0 if timed out or closed.
    int8_t rt_channel_send_for(void *channel, void *item, int64_t ms);

    /// @brief Receive an item from the channel, blocking if empty.
    /// @details Blocks until an item is available or the channel is closed.
    ///          Returns NULL if the channel is closed and empty.
    /// @param channel Channel object pointer.
    /// @return Received item, or NULL if closed and empty.
    void *rt_channel_recv(void *channel);

    /// @brief Try to receive an item without blocking.
    /// @details Returns immediately with the item if available.
    /// @param channel Channel object pointer.
    /// @param out Pointer to store the received item (may be NULL to check only).
    /// @return 1 if received, 0 if channel is empty.
    int8_t rt_channel_try_recv(void *channel, void **out);

    /// @brief Receive with a timeout.
    /// @details Blocks up to @p ms milliseconds for an item.
    /// @param channel Channel object pointer.
    /// @param out Pointer to store the received item.
    /// @param ms Timeout in milliseconds.
    /// @return 1 if received, 0 if timed out or closed.
    int8_t rt_channel_recv_for(void *channel, void **out, int64_t ms);

    /// @brief Close the channel.
    /// @details Prevents further sends. Receivers can still drain remaining items.
    ///          Wakes all blocked senders and receivers.
    /// @param channel Channel object pointer.
    void rt_channel_close(void *channel);

    /// @brief Get the number of items currently in the channel.
    /// @param channel Channel object pointer.
    /// @return Number of items.
    int64_t rt_channel_get_len(void *channel);

    /// @brief Get the channel capacity.
    /// @param channel Channel object pointer.
    /// @return Capacity (0 for synchronous channels).
    int64_t rt_channel_get_cap(void *channel);

    /// @brief Check if the channel is closed.
    /// @param channel Channel object pointer.
    /// @return 1 if closed, 0 otherwise.
    int8_t rt_channel_get_is_closed(void *channel);

    /// @brief Check if the channel is empty.
    /// @param channel Channel object pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_channel_get_is_empty(void *channel);

    /// @brief Check if the channel is full.
    /// @param channel Channel object pointer.
    /// @return 1 if full, 0 otherwise.
    int8_t rt_channel_get_is_full(void *channel);

#ifdef __cplusplus
}
#endif
