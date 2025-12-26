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

    Channel *ch = new Channel(static_cast<u32>(result));

    serial::puts("[kobj::channel] Created channel object for ID ");
    serial::put_dec(ch->channel_id_);
    serial::puts("\n");

    return ch;
}

/** @copydoc kobj::Channel::~Channel */
Channel::~Channel()
{
    if (channel_id_ != 0)
    {
        channel::close(channel_id_);
        serial::puts("[kobj::channel] Closed channel ");
        serial::put_dec(channel_id_);
        serial::puts("\n");
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
