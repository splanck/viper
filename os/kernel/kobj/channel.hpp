#pragma once

#include "../ipc/channel.hpp"
#include "object.hpp"

/**
 * @file channel.hpp
 * @brief Kernel object wrapper for IPC channels.
 *
 * @details
 * The low-level channel subsystem (`kernel/ipc/channel.*`) implements the
 * message queue and blocking behavior. This wrapper turns a channel ID into a
 * reference-counted `kobj::Object` so it can be stored in capability tables and
 * shared across domains using handles.
 */
namespace kobj
{

// Channel wrapper - wraps the low-level channel for capability system
/**
 * @brief Reference-counted channel object.
 *
 * @details
 * Owns a low-level channel ID. The destructor closes the underlying channel.
 * Channel operations are forwarded to the low-level channel subsystem.
 */
class Channel : public Object
{
  public:
    static constexpr cap::Kind KIND = cap::Kind::Channel;

    /**
     * @brief Create a new channel object.
     *
     * @details
     * Allocates a low-level channel ID and wraps it in a heap-allocated
     * `kobj::Channel` object.
     *
     * @return New channel object, or `nullptr` on failure.
     */
    static Channel *create();

    /**
     * @brief Destroy the channel object and close the underlying channel.
     *
     * @details
     * Called when the last reference is released.
     */
    ~Channel() override;

    /** @brief Get the underlying low-level channel ID. */
    u32 id() const
    {
        return channel_id_;
    }

    // Channel operations (delegate to low-level channel)
    /** @brief Blocking send (see @ref channel::send). */
    i64 send(const void *data, u32 size);
    /** @brief Blocking receive (see @ref channel::recv). */
    i64 recv(void *buffer, u32 buffer_size);
    /** @brief Non-blocking send (see @ref channel::try_send). */
    i64 try_send(const void *data, u32 size);
    /** @brief Non-blocking receive (see @ref channel::try_recv). */
    i64 try_recv(void *buffer, u32 buffer_size);
    /** @brief Check whether the channel has pending messages. */
    bool has_message() const;

  private:
    Channel(u32 channel_id) : Object(KIND), channel_id_(channel_id) {}

    u32 channel_id_;
};

} // namespace kobj
