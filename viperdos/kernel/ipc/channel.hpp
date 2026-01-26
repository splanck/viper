//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/ipc/channel.hpp
// Purpose: In-kernel message-passing channels for IPC with handle transfer.
// Key invariants: Channels bidirectional; handles transferred atomically.
// Ownership/Lifetime: Fixed channel table; reference counted endpoints.
// Links: kernel/ipc/channel.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../cap/handle.hpp"
#include "../include/error.hpp"
#include "../include/types.hpp"
#include "../sched/task.hpp"
#include "../sched/wait.hpp"

/**
 * @file channel.hpp
 * @brief In-kernel message-passing channels for IPC with handle transfer.
 *
 * @details
 * The channel subsystem provides bidirectional message-passing between tasks.
 * Each channel has two endpoints:
 * - **Send endpoint**: Has CAP_WRITE right, used for sending messages
 * - **Recv endpoint**: Has CAP_READ right, used for receiving messages
 *
 * Messages can contain:
 * - Payload bytes (up to MAX_MSG_SIZE)
 * - Up to 4 capability handles that are transferred to the receiver
 *
 * When handles are transferred:
 * - They are removed from the sender's cap_table
 * - They are inserted into the receiver's cap_table with their original rights
 * - The receiver gets new handle values for the transferred capabilities
 */
namespace channel {

/** @brief Maximum bytes stored in a single channel message. */
/// Note: Increased to 8192 to support large protocol messages like SetMenuRequest
constexpr u32 MAX_MSG_SIZE = 8192;
/** @brief Maximum number of channels that can exist at once. */
constexpr u32 MAX_CHANNELS = 256;
/** @brief Default number of queued messages per channel. */
/// Reduced from 128 to save memory with larger MAX_MSG_SIZE
constexpr u32 DEFAULT_PENDING = 16;
/** @brief Maximum configurable pending message capacity. */
constexpr u32 MAX_PENDING = 32;
/** @brief Maximum number of handles that can be transferred in one message. */
constexpr u32 MAX_HANDLES_PER_MSG = 4;

/**
 * @brief A transferred handle in a message.
 *
 * @details
 * When a handle is transferred, we need to store enough information to
 * recreate it in the receiver's cap_table. This includes the object pointer,
 * kind, and rights from the sender's entry.
 */
struct TransferredHandle {
    void *object; // Kernel object pointer
    u16 kind;     // cap::Kind value
    u32 rights;   // Original rights
};

/**
 * @brief A single queued channel message.
 *
 * @details
 * Messages are stored inline in the channel buffer to avoid dynamic allocation.
 * Each message can optionally carry up to 4 handles for transfer.
 */
struct Message {
    u8 data[MAX_MSG_SIZE];
    u32 size;
    u32 sender_id;                                  // Task ID of sender
    u32 handle_count;                               // Number of handles (0-4)
    TransferredHandle handles[MAX_HANDLES_PER_MSG]; // Handles to transfer
};

/**
 * @brief Lifecycle state for a channel table entry.
 */
enum class ChannelState : u32 { FREE = 0, OPEN, CLOSED };

/**
 * @brief In-kernel channel object.
 *
 * @details
 * Channels are stored in a global fixed-size table. Each open channel maintains:
 * - A circular buffer of Message slots.
 * - Read/write indices and a count of queued messages.
 * - Optional pointers to blocked sender and receiver tasks.
 * - Reference counts for send and recv endpoints.
 * - Configurable capacity (1 to MAX_PENDING messages).
 */
struct Channel {
    u32 id;
    ChannelState state;

    // Circular buffer for messages (MAX_PENDING slots, capacity limits usage)
    Message buffer[MAX_PENDING];
    u32 read_idx;
    u32 write_idx;
    u32 count;
    u32 capacity; // Effective capacity (1 to MAX_PENDING)

    // Wait queues for blocked tasks (supports multiple waiters)
    sched::WaitQueue send_waiters; // Tasks blocked on send (buffer full)
    sched::WaitQueue recv_waiters; // Tasks blocked on recv (buffer empty)

    // Reference counts for endpoints
    u32 send_refs; // Number of send endpoint handles
    u32 recv_refs; // Number of recv endpoint handles

    // Owner task (creator)
    u32 owner_id;
};

/**
 * @brief Result of channel creation containing both endpoint handles.
 */
struct ChannelPair {
    cap::Handle send_handle; // Handle with CAP_WRITE for sending
    cap::Handle recv_handle; // Handle with CAP_READ for receiving
};

/**
 * @brief Initialize the channel subsystem.
 */
void init();

/**
 * @brief Create a new channel and return both endpoint handles.
 *
 * @details
 * Creates a new channel and inserts two handles into the current viper's
 * cap_table:
 * - A send handle with CAP_WRITE | CAP_TRANSFER | CAP_DERIVE rights
 * - A recv handle with CAP_READ | CAP_TRANSFER | CAP_DERIVE rights
 *
 * @param out_pair Output structure to receive both handles.
 * @param capacity Number of pending messages (1 to MAX_PENDING, default DEFAULT_PENDING).
 * @return @ref error::VOK on success, or negative error code.
 */
i64 create(ChannelPair *out_pair, u32 capacity = DEFAULT_PENDING);

/**
 * @brief Legacy create for backward compatibility - returns channel ID.
 *
 * @deprecated Use create(ChannelPair*) for proper endpoint separation.
 * @param capacity Number of pending messages (1 to MAX_PENDING, default DEFAULT_PENDING).
 * @return Non-negative channel ID on success, or negative error code.
 */
i64 create(u32 capacity = DEFAULT_PENDING);

/**
 * @brief Get the capacity of a channel.
 *
 * @param ch Pointer to channel object.
 * @return Channel capacity, or 0 if invalid.
 */
u32 get_capacity(Channel *ch);

/**
 * @brief Set the capacity of a channel.
 *
 * @details
 * Capacity can only be increased, not decreased below the current message count.
 *
 * @param ch Pointer to channel object.
 * @param new_capacity New capacity (1 to MAX_PENDING).
 * @return @ref error::VOK on success, or negative error code.
 */
i64 set_capacity(Channel *ch, u32 new_capacity);

/**
 * @brief Send a message with optional handle transfer (non-blocking).
 *
 * @details
 * Enqueues a message onto the channel. If handles are provided, they are
 * removed from the sender's cap_table and will be inserted into the
 * receiver's cap_table when the message is received.
 *
 * @param ch Pointer to channel object.
 * @param data Pointer to message bytes to send.
 * @param size Number of bytes to send (must be <= MAX_MSG_SIZE).
 * @param handles Array of handles to transfer (may be nullptr).
 * @param handle_count Number of handles to transfer (0-4).
 * @return @ref error::VOK on success, or negative error code.
 */
i64 try_send(Channel *ch, const void *data, u32 size, const cap::Handle *handles, u32 handle_count);

/**
 * @brief Receive a message with handle transfer (non-blocking).
 *
 * @details
 * Dequeues the next message from the channel. If the message contained
 * transferred handles, they are inserted into the receiver's cap_table
 * and the new handle values are written to out_handles.
 *
 * @param ch Pointer to channel object.
 * @param buffer Destination buffer for message data.
 * @param buffer_size Size of destination buffer.
 * @param out_handles Array to receive transferred handles (may be nullptr).
 * @param out_handle_count Receives the number of handles transferred.
 * @return Message size on success, or negative error code.
 */
i64 try_recv(
    Channel *ch, void *buffer, u32 buffer_size, cap::Handle *out_handles, u32 *out_handle_count);

/**
 * @brief Try send using channel ID (TOCTOU-safe).
 *
 * @details
 * Looks up the channel by ID under the lock and performs the send atomically,
 * avoiding TOCTOU races with channel closure.
 */
i64 try_send(u32 channel_id, const void *data, u32 size);

/**
 * @brief Try send with handle transfer using channel ID (TOCTOU-safe).
 *
 * @details
 * Looks up the channel by ID under the lock and performs the send atomically,
 * including handle transfer, avoiding TOCTOU races with channel closure.
 */
i64 try_send(
    u32 channel_id, const void *data, u32 size, const cap::Handle *handles, u32 handle_count);

/**
 * @brief Try recv using channel ID (TOCTOU-safe).
 */
i64 try_recv(u32 channel_id, void *buffer, u32 buffer_size);

/**
 * @brief Try recv with handle transfer using channel ID (TOCTOU-safe).
 */
i64 try_recv(
    u32 channel_id, void *buffer, u32 buffer_size, cap::Handle *out_handles, u32 *out_handle_count);

/**
 * @brief Blocking send (legacy interface).
 */
i64 send(u32 channel_id, const void *data, u32 size);

/**
 * @brief Blocking recv (legacy interface).
 */
i64 recv(u32 channel_id, void *buffer, u32 buffer_size);

/**
 * @brief Close a channel endpoint.
 *
 * @details
 * Decrements the reference count for the endpoint type. When both send
 * and recv references reach zero, the channel is destroyed.
 *
 * @param ch Pointer to channel object.
 * @param is_send True if closing send endpoint, false for recv.
 * @return Result code.
 */
i64 close_endpoint(Channel *ch, bool is_send);

/**
 * @brief Legacy close using channel ID.
 */
i64 close(u32 channel_id);

/**
 * @brief Check if a channel has at least one queued message.
 */
bool has_message(Channel *ch);
bool has_message(u32 channel_id);

/**
 * @brief Check if a channel has space for another message.
 */
bool has_space(Channel *ch);
bool has_space(u32 channel_id);

/**
 * @brief Look up an open channel by ID.
 *
 * @warning The returned pointer is only valid while the caller holds the
 * channel lock. If you need to use the channel outside a locked section,
 * use add_endpoint_ref() / close_endpoint_by_id() for reference counting.
 */
Channel *get(u32 channel_id);

/**
 * @brief Atomically increment endpoint reference count.
 *
 * @details
 * Looks up the channel by ID and increments the appropriate reference count
 * under the channel lock, avoiding TOCTOU races.
 *
 * @param channel_id Channel ID to look up.
 * @param is_send True to increment send_refs, false for recv_refs.
 * @return @ref error::VOK on success, or negative error code.
 */
i64 add_endpoint_ref(u32 channel_id, bool is_send);

/**
 * @brief Atomically close an endpoint by channel ID.
 *
 * @details
 * Looks up the channel by ID and closes the endpoint under the lock,
 * avoiding TOCTOU races.
 *
 * @param channel_id Channel ID to look up.
 * @param is_send True to close send endpoint, false for recv.
 * @return @ref error::VOK on success, or negative error code.
 */
i64 close_endpoint_by_id(u32 channel_id, bool is_send);

} // namespace channel
