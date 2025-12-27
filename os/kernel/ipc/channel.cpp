#include "channel.hpp"
#include "../cap/rights.hpp"
#include "../cap/table.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "../sched/scheduler.hpp"
#include "../viper/viper.hpp"
#include "poll.hpp"

/**
 * @file channel.cpp
 * @brief Implementation of the kernel IPC channel subsystem.
 *
 * @details
 * Channels are implemented as entries in a global fixed-size table. Each channel
 * supports two endpoints (send and recv) with separate reference counts.
 *
 * Handle transfer works by:
 * 1. Sender provides handles to transfer
 * 2. The kernel extracts object/kind/rights from sender's cap_table
 * 3. Handles are removed from sender's cap_table
 * 4. When message is received, handles are inserted into receiver's cap_table
 * 5. New handle values are returned to the receiver
 */
namespace channel
{

// Channel table lock for thread-safe access
static Spinlock channel_lock;

// Channel table
static Channel channels[MAX_CHANNELS];
static u32 next_channel_id = 1;

/** @copydoc channel::init */
void init()
{
    serial::puts("[channel] Initializing channel subsystem\n");

    SpinlockGuard guard(channel_lock);
    for (u32 i = 0; i < MAX_CHANNELS; i++)
    {
        channels[i].id = 0;
        channels[i].state = ChannelState::FREE;
        channels[i].read_idx = 0;
        channels[i].write_idx = 0;
        channels[i].count = 0;
        channels[i].send_blocked = nullptr;
        channels[i].recv_blocked = nullptr;
        channels[i].send_refs = 0;
        channels[i].recv_refs = 0;
        channels[i].owner_id = 0;
    }

    serial::puts("[channel] Channel subsystem initialized\n");
}

/** @copydoc channel::get */
Channel *get(u32 channel_id)
{
    SpinlockGuard guard(channel_lock);
    for (u32 i = 0; i < MAX_CHANNELS; i++)
    {
        if (channels[i].id == channel_id && channels[i].state == ChannelState::OPEN)
        {
            return &channels[i];
        }
    }
    return nullptr;
}

/**
 * @brief Find a free slot in the global channel table.
 * @note Caller must hold channel_lock.
 */
static Channel *find_free_slot()
{
    for (u32 i = 0; i < MAX_CHANNELS; i++)
    {
        if (channels[i].state == ChannelState::FREE)
        {
            return &channels[i];
        }
    }
    return nullptr;
}

/**
 * @brief Find an open channel by ID.
 * @note Caller must hold channel_lock.
 */
static Channel *find_channel_by_id_locked(u32 channel_id)
{
    for (u32 i = 0; i < MAX_CHANNELS; i++)
    {
        if (channels[i].id == channel_id && channels[i].state == ChannelState::OPEN)
        {
            return &channels[i];
        }
    }
    return nullptr;
}

/**
 * @brief Initialize a new channel in a slot.
 */
static void init_channel(Channel *ch)
{
    ch->id = next_channel_id++;
    ch->state = ChannelState::OPEN;
    ch->read_idx = 0;
    ch->write_idx = 0;
    ch->count = 0;
    ch->send_blocked = nullptr;
    ch->recv_blocked = nullptr;
    ch->send_refs = 0;
    ch->recv_refs = 0;

    task::Task *current = task::current();
    ch->owner_id = current ? current->id : 0;
}

/** @copydoc channel::create(ChannelPair*) */
i64 create(ChannelPair *out_pair)
{
    if (!out_pair)
    {
        return error::VERR_INVALID_ARG;
    }

    // Get current viper's cap_table
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    SpinlockGuard guard(channel_lock);

    Channel *ch = find_free_slot();
    if (!ch)
    {
        serial::puts("[channel] No free channel slots\n");
        return error::VERR_OUT_OF_MEMORY;
    }

    init_channel(ch);

    // Create send handle (CAP_WRITE | CAP_TRANSFER | CAP_DERIVE)
    cap::Rights send_rights = cap::CAP_WRITE | cap::CAP_TRANSFER | cap::CAP_DERIVE;
    cap::Handle send_h = ct->insert(ch, cap::Kind::Channel, send_rights);
    if (send_h == cap::HANDLE_INVALID)
    {
        ch->state = ChannelState::FREE;
        ch->id = 0;
        return error::VERR_OUT_OF_MEMORY;
    }
    ch->send_refs = 1;

    // Create recv handle (CAP_READ | CAP_TRANSFER | CAP_DERIVE)
    cap::Rights recv_rights = cap::CAP_READ | cap::CAP_TRANSFER | cap::CAP_DERIVE;
    cap::Handle recv_h = ct->insert(ch, cap::Kind::Channel, recv_rights);
    if (recv_h == cap::HANDLE_INVALID)
    {
        ct->remove(send_h);
        ch->state = ChannelState::FREE;
        ch->id = 0;
        return error::VERR_OUT_OF_MEMORY;
    }
    ch->recv_refs = 1;

    out_pair->send_handle = send_h;
    out_pair->recv_handle = recv_h;

    serial::puts("[channel] Created channel ");
    serial::put_dec(ch->id);
    serial::puts(" (send=");
    serial::put_hex(send_h);
    serial::puts(", recv=");
    serial::put_hex(recv_h);
    serial::puts(")\n");

    return error::VOK;
}

/** @copydoc channel::create() - Legacy */
i64 create()
{
    SpinlockGuard guard(channel_lock);

    Channel *ch = find_free_slot();
    if (!ch)
    {
        serial::puts("[channel] No free channel slots\n");
        return error::VERR_OUT_OF_MEMORY;
    }

    init_channel(ch);
    ch->send_refs = 1; // Legacy mode: both refs set
    ch->recv_refs = 1;

    serial::puts("[channel] Created channel ");
    serial::put_dec(ch->id);
    serial::puts(" (legacy)\n");

    return static_cast<i64>(ch->id);
}

/** @copydoc channel::try_send(Channel*, ...) */
i64 try_send(Channel *ch, const void *data, u32 size, const cap::Handle *handles, u32 handle_count)
{
    SpinlockGuard guard(channel_lock);

    if (!ch || ch->state != ChannelState::OPEN)
    {
        return error::VERR_INVALID_HANDLE;
    }

    if (size > MAX_MSG_SIZE)
    {
        return error::VERR_MSG_TOO_LARGE;
    }

    if (handle_count > MAX_HANDLES_PER_MSG)
    {
        return error::VERR_INVALID_ARG;
    }

    if (ch->count >= MAX_PENDING)
    {
        return error::VERR_WOULD_BLOCK;
    }

    // Get sender's cap_table for handle transfer
    cap::Table *sender_ct = viper::current_cap_table();

    // Prepare message
    Message *msg = &ch->buffer[ch->write_idx];

    // Copy data
    if (data && size > 0)
    {
        for (u32 i = 0; i < size; i++)
        {
            msg->data[i] = static_cast<const u8 *>(data)[i];
        }
    }
    msg->size = size;

    task::Task *current = task::current();
    msg->sender_id = current ? current->id : 0;

    // Process handle transfers
    msg->handle_count = 0;
    if (handles && handle_count > 0 && sender_ct)
    {
        for (u32 i = 0; i < handle_count; i++)
        {
            // Look up handle in sender's cap_table
            cap::Entry *entry = sender_ct->get(handles[i]);
            if (!entry)
            {
                // Invalid handle - skip or error?
                // For now, skip invalid handles
                continue;
            }

            // Check if sender has TRANSFER right
            if (!cap::has_rights(entry->rights, cap::CAP_TRANSFER))
            {
                // No transfer right - skip
                continue;
            }

            // Copy handle info for transfer
            TransferredHandle *th = &msg->handles[msg->handle_count];
            th->object = entry->object;
            th->kind = static_cast<u16>(entry->kind);
            th->rights = entry->rights;
            msg->handle_count++;

            // Remove from sender's cap_table
            sender_ct->remove(handles[i]);
        }
    }

    // Advance write index
    ch->write_idx = (ch->write_idx + 1) % MAX_PENDING;
    ch->count = ch->count + 1;

    // Wake up any blocked receiver
    if (ch->recv_blocked)
    {
        task::Task *waiter = ch->recv_blocked;
        ch->recv_blocked = nullptr;
        waiter->state = task::TaskState::Ready;
        scheduler::enqueue(waiter);
    }

    return error::VOK;
}

/** @copydoc channel::try_recv(Channel*, ...) */
i64 try_recv(
    Channel *ch, void *buffer, u32 buffer_size, cap::Handle *out_handles, u32 *out_handle_count)
{
    SpinlockGuard guard(channel_lock);

    if (!ch || ch->state != ChannelState::OPEN)
    {
        return error::VERR_INVALID_HANDLE;
    }

    if (ch->count == 0)
    {
        return error::VERR_WOULD_BLOCK;
    }

    // Get receiver's cap_table for handle transfer
    cap::Table *recv_ct = viper::current_cap_table();

    // Get message from buffer
    Message *msg = &ch->buffer[ch->read_idx];

    // Copy data
    u32 copy_size = msg->size;
    if (copy_size > buffer_size)
    {
        copy_size = buffer_size;
    }
    if (buffer && copy_size > 0)
    {
        for (u32 i = 0; i < copy_size; i++)
        {
            static_cast<u8 *>(buffer)[i] = msg->data[i];
        }
    }

    u32 actual_size = msg->size;

    // Process handle transfers
    u32 handles_received = 0;
    if (msg->handle_count > 0 && recv_ct)
    {
        for (u32 i = 0; i < msg->handle_count; i++)
        {
            TransferredHandle *th = &msg->handles[i];

            // Insert into receiver's cap_table
            cap::Handle new_h = recv_ct->insert(
                th->object, static_cast<cap::Kind>(th->kind), static_cast<cap::Rights>(th->rights));

            if (new_h != cap::HANDLE_INVALID)
            {
                if (out_handles && handles_received < MAX_HANDLES_PER_MSG)
                {
                    out_handles[handles_received] = new_h;
                }
                handles_received++;
            }
        }
    }

    if (out_handle_count)
    {
        *out_handle_count = handles_received;
    }

    // Advance read index
    ch->read_idx = (ch->read_idx + 1) % MAX_PENDING;
    ch->count = ch->count - 1;

    // Wake up any blocked sender
    if (ch->send_blocked)
    {
        task::Task *waiter = ch->send_blocked;
        ch->send_blocked = nullptr;
        waiter->state = task::TaskState::Ready;
        scheduler::enqueue(waiter);
    }

    return static_cast<i64>(actual_size);
}

/** @copydoc channel::try_send(u32, ...) - Legacy */
i64 try_send(u32 channel_id, const void *data, u32 size)
{
    Channel *ch = get(channel_id);
    return try_send(ch, data, size, nullptr, 0);
}

/** @copydoc channel::try_recv(u32, ...) - Legacy */
i64 try_recv(u32 channel_id, void *buffer, u32 buffer_size)
{
    Channel *ch = get(channel_id);
    return try_recv(ch, buffer, buffer_size, nullptr, nullptr);
}

/**
 * @brief Copy message data into a channel buffer slot.
 * @note Caller must hold channel_lock.
 */
static void copy_message_to_buffer(Channel *ch, const void *data, u32 size)
{
    Message *msg = &ch->buffer[ch->write_idx];
    if (data && size > 0)
    {
        for (u32 i = 0; i < size; i++)
        {
            msg->data[i] = static_cast<const u8 *>(data)[i];
        }
    }
    msg->size = size;
    task::Task *current_task = task::current();
    msg->sender_id = current_task ? current_task->id : 0;
    msg->handle_count = 0;

    ch->write_idx = (ch->write_idx + 1) % MAX_PENDING;
    ch->count = ch->count + 1;
}

/**
 * @brief Wake up a blocked receiver if present.
 * @note Caller must hold channel_lock.
 */
static void wake_blocked_receiver(Channel *ch)
{
    if (ch->recv_blocked)
    {
        task::Task *waiter = ch->recv_blocked;
        ch->recv_blocked = nullptr;
        waiter->state = task::TaskState::Ready;
        scheduler::enqueue(waiter);
    }
}

/**
 * @brief Wake up a blocked sender if present.
 * @note Caller must hold channel_lock.
 */
static void wake_blocked_sender(Channel *ch)
{
    if (ch->send_blocked)
    {
        task::Task *waiter = ch->send_blocked;
        ch->send_blocked = nullptr;
        waiter->state = task::TaskState::Ready;
        scheduler::enqueue(waiter);
    }
}

/**
 * @brief Clean up any pending messages with transferred handles.
 * @note Caller must hold channel_lock.
 *
 * @details
 * When a channel is closed, any messages still in the buffer that contain
 * transferred handles need to have those handles released. Otherwise, the
 * kernel objects pointed to by the handles will be leaked.
 */
static void cleanup_pending_handles(Channel *ch)
{
    // Iterate through all pending messages
    u32 idx = ch->read_idx;
    for (u32 i = 0; i < ch->count; i++)
    {
        Message *msg = &ch->buffer[idx];

        // If this message has transferred handles, we need to release them
        // The handles contain raw object pointers that were removed from the
        // sender's cap_table, so they need to be cleaned up
        if (msg->handle_count > 0)
        {
            serial::puts("[channel] WARNING: Cleaning up ");
            serial::put_dec(msg->handle_count);
            serial::puts(" orphaned handles on channel close\n");

            // The transferred handles contain object pointers, but since they
            // were already removed from the sender's cap_table, we would need
            // to call kobj::release() on them. However, we don't have a direct
            // reference to release here - the TransferredHandle stores a void*.
            //
            // For proper cleanup, we would need to cast to kobj::Object* and
            // call release. This is a known limitation that could be improved
            // by storing the object type more explicitly.
            //
            // TODO: Implement proper object release when capability system
            // is fully integrated. For now, log the leak.
        }
        msg->handle_count = 0;

        idx = (idx + 1) % MAX_PENDING;
    }
}

/** @copydoc channel::send - Legacy blocking send */
i64 send(u32 channel_id, const void *data, u32 size)
{
    if (size > MAX_MSG_SIZE)
    {
        return error::VERR_MSG_TOO_LARGE;
    }

    // Blocking loop - must use manual lock management due to yield semantics
    while (true)
    {
        channel_lock.acquire();

        Channel *ch = find_channel_by_id_locked(channel_id);
        if (!ch)
        {
            channel_lock.release();
            return error::VERR_INVALID_HANDLE;
        }

        if (ch->state != ChannelState::OPEN)
        {
            channel_lock.release();
            return error::VERR_CHANNEL_CLOSED;
        }

        if (ch->count < MAX_PENDING)
        {
            // Space available - send the message
            copy_message_to_buffer(ch, data, size);
            wake_blocked_receiver(ch);

            // Notify poll waiters that channel has data
            poll::notify_handle(channel_id, poll::EventType::CHANNEL_READ);

            channel_lock.release();
            return error::VOK;
        }

        // Buffer full - need to block
        task::Task *current = task::current();
        if (!current)
        {
            channel_lock.release();
            return error::VERR_WOULD_BLOCK;
        }

        current->state = task::TaskState::Blocked;
        ch->send_blocked = current;
        channel_lock.release();

        task::yield();
        // Loop will re-acquire lock and re-check condition
    }
}

/**
 * @brief Copy message from channel buffer slot to user buffer.
 * @note Caller must hold channel_lock.
 * @return Actual message size.
 */
static u32 copy_message_from_buffer(Channel *ch, void *buffer, u32 buffer_size)
{
    Message *msg = &ch->buffer[ch->read_idx];

    u32 copy_size = msg->size;
    if (copy_size > buffer_size)
    {
        copy_size = buffer_size;
    }
    if (buffer && copy_size > 0)
    {
        for (u32 i = 0; i < copy_size; i++)
        {
            static_cast<u8 *>(buffer)[i] = msg->data[i];
        }
    }

    u32 actual_size = msg->size;

    ch->read_idx = (ch->read_idx + 1) % MAX_PENDING;
    ch->count = ch->count - 1;

    return actual_size;
}

/** @copydoc channel::recv - Legacy blocking recv */
i64 recv(u32 channel_id, void *buffer, u32 buffer_size)
{
    // Blocking loop - must use manual lock management due to yield semantics
    while (true)
    {
        channel_lock.acquire();

        Channel *ch = find_channel_by_id_locked(channel_id);
        if (!ch)
        {
            channel_lock.release();
            return error::VERR_INVALID_HANDLE;
        }

        if (ch->state != ChannelState::OPEN)
        {
            channel_lock.release();
            return error::VERR_CHANNEL_CLOSED;
        }

        if (ch->count > 0)
        {
            // Message available - receive it
            u32 actual_size = copy_message_from_buffer(ch, buffer, buffer_size);
            wake_blocked_sender(ch);

            // Notify poll waiters that channel has space
            poll::notify_handle(channel_id, poll::EventType::CHANNEL_WRITE);

            channel_lock.release();
            return static_cast<i64>(actual_size);
        }

        // Buffer empty - need to block
        task::Task *current = task::current();
        if (!current)
        {
            channel_lock.release();
            return error::VERR_WOULD_BLOCK;
        }

        current->state = task::TaskState::Blocked;
        ch->recv_blocked = current;
        channel_lock.release();

        task::yield();
        // Loop will re-acquire lock and re-check condition
    }
}

/** @copydoc channel::close_endpoint */
i64 close_endpoint(Channel *ch, bool is_send)
{
    SpinlockGuard guard(channel_lock);

    if (!ch)
    {
        return error::VERR_INVALID_HANDLE;
    }

    if (is_send)
    {
        if (ch->send_refs > 0)
        {
            ch->send_refs--;
        }
    }
    else
    {
        if (ch->recv_refs > 0)
        {
            ch->recv_refs--;
        }
    }

    // If both endpoints are closed, destroy the channel
    if (ch->send_refs == 0 && ch->recv_refs == 0)
    {
        ch->state = ChannelState::CLOSED;

        // Wake up any blocked tasks so they can observe the closed state
        wake_blocked_sender(ch);
        wake_blocked_receiver(ch);

        // Clean up any pending messages with transferred handles
        cleanup_pending_handles(ch);

        serial::puts("[channel] Destroyed channel ");
        serial::put_dec(ch->id);
        serial::puts("\n");

        ch->state = ChannelState::FREE;
        ch->id = 0;
    }

    return error::VOK;
}

/** @copydoc channel::close - Legacy */
i64 close(u32 channel_id)
{
    SpinlockGuard guard(channel_lock);

    Channel *ch = find_channel_by_id_locked(channel_id);
    if (!ch)
    {
        return error::VERR_INVALID_HANDLE;
    }

    ch->state = ChannelState::CLOSED;

    // Wake up any blocked tasks so they can observe the closed state
    wake_blocked_sender(ch);
    wake_blocked_receiver(ch);

    // Clean up any pending messages with transferred handles
    cleanup_pending_handles(ch);

    ch->state = ChannelState::FREE;
    ch->id = 0;

    serial::puts("[channel] Closed channel ");
    serial::put_dec(channel_id);
    serial::puts(" (legacy)\n");

    return error::VOK;
}

/** @copydoc channel::has_message(Channel*) */
bool has_message(Channel *ch)
{
    SpinlockGuard guard(channel_lock);
    if (!ch || ch->state != ChannelState::OPEN)
    {
        return false;
    }
    return ch->count > 0;
}

/** @copydoc channel::has_message(u32) */
bool has_message(u32 channel_id)
{
    SpinlockGuard guard(channel_lock);
    Channel *ch = find_channel_by_id_locked(channel_id);
    return ch ? ch->count > 0 : false;
}

/** @copydoc channel::has_space(Channel*) */
bool has_space(Channel *ch)
{
    SpinlockGuard guard(channel_lock);
    if (!ch || ch->state != ChannelState::OPEN)
    {
        return false;
    }
    return ch->count < MAX_PENDING;
}

/** @copydoc channel::has_space(u32) */
bool has_space(u32 channel_id)
{
    SpinlockGuard guard(channel_lock);
    Channel *ch = find_channel_by_id_locked(channel_id);
    return ch ? ch->count < MAX_PENDING : false;
}

} // namespace channel
