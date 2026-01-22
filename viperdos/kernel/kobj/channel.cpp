#include "channel.hpp"
#include "../console/serial.hpp"

/**
 * @file channel.cpp
 * @brief Channel kernel object wrapper implementation.
 *
 * @details
 * The wrapper owns a low-level channel ID allocated from the channel subsystem.
 * It forwards operations to the underlying channel and ensures the channel is
 * closed when the object is destroyed.
 */
namespace kobj
{

/** @copydoc kobj::Channel::create */
Channel *Channel::create()
{
    i64 result = channel::create();
    if (result < 0)
    {
        return nullptr;
    }

    // Legacy channel creation starts with both endpoints owned by the creator.
    Channel *ch = new Channel(static_cast<u32>(result), ENDPOINT_BOTH);

    return ch;
}

/** @copydoc kobj::Channel::adopt */
Channel *Channel::adopt(u32 channel_id, u8 endpoints)
{
    // Atomically verify the channel exists and add references for the endpoints.
    // This avoids TOCTOU race where channel could be closed between get() and use.
    if (endpoints & ENDPOINT_SEND)
    {
        i64 result = channel::add_endpoint_ref(channel_id, true);
        if (result != error::VOK)
        {
            return nullptr;
        }
    }
    if (endpoints & ENDPOINT_RECV)
    {
        i64 result = channel::add_endpoint_ref(channel_id, false);
        if (result != error::VOK)
        {
            // Rollback the send ref if we added it
            if (endpoints & ENDPOINT_SEND)
            {
                channel::close_endpoint_by_id(channel_id, true);
            }
            return nullptr;
        }
    }

    return new Channel(channel_id, endpoints);
}

/** @copydoc kobj::Channel::wrap */
Channel *Channel::wrap(u32 channel_id, bool is_send)
{
    // Atomically verify the channel exists and increment reference count.
    // This avoids TOCTOU race where channel could be closed between get() and ref++.
    i64 result = channel::add_endpoint_ref(channel_id, is_send);
    if (result != error::VOK)
    {
        return nullptr;
    }

    // Create wrapper - reference count already incremented
    Channel *ch = new Channel(channel_id, is_send ? ENDPOINT_SEND : ENDPOINT_RECV);

    serial::puts("[kobj::channel] Wrapped channel ID ");
    serial::put_dec(channel_id);
    serial::puts(" as ");
    serial::puts(is_send ? "send" : "recv");
    serial::puts(" endpoint\n");

    return ch;
}

/** @copydoc kobj::Channel::~Channel */
Channel::~Channel()
{
    if (channel_id_ != 0)
    {
        // Use atomic close_endpoint_by_id to avoid TOCTOU race with get()
        if (endpoints_ & ENDPOINT_SEND)
        {
            channel::close_endpoint_by_id(channel_id_, true);
        }
        if (endpoints_ & ENDPOINT_RECV)
        {
            channel::close_endpoint_by_id(channel_id_, false);
        }
    }
}

/** @copydoc kobj::Channel::send */
i64 Channel::send(const void *data, u32 size)
{
    return channel::send(channel_id_, data, size);
}

/** @copydoc kobj::Channel::recv */
i64 Channel::recv(void *buffer, u32 buffer_size)
{
    return channel::recv(channel_id_, buffer, buffer_size);
}

/** @copydoc kobj::Channel::try_send */
i64 Channel::try_send(const void *data, u32 size)
{
    return channel::try_send(channel_id_, data, size);
}

/** @copydoc kobj::Channel::try_recv */
i64 Channel::try_recv(void *buffer, u32 buffer_size)
{
    return channel::try_recv(channel_id_, buffer, buffer_size);
}

/** @copydoc kobj::Channel::has_message */
bool Channel::has_message() const
{
    return channel::has_message(channel_id_);
}

} // namespace kobj
