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
namespace kobj {

// Channel wrapper - wraps the low-level channel for capability system
/**
 * @brief Reference-counted channel object.
 *
 * @details
 * Owns a low-level channel ID. The destructor closes the underlying channel.
 * Channel operations are forwarded to the low-level channel subsystem.
 */
class Channel : public Object {
  public:
    static constexpr cap::Kind KIND = cap::Kind::Channel;

    enum EndpointMask : u8 {
        ENDPOINT_SEND = 1u << 0,
        ENDPOINT_RECV = 1u << 1,
        ENDPOINT_BOTH = ENDPOINT_SEND | ENDPOINT_RECV,
    };

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
     * @brief Create a wrapper for an existing channel without modifying refcounts.
     *
     * @details
     * Intended for initial publication of a newly-created legacy channel where
     * endpoint reference counts are already initialized (send_refs=1, recv_refs=1).
     *
     * @param channel_id The low-level channel ID to wrap.
     * @param endpoints Endpoint mask describing what this handle owns.
     * @return New channel wrapper, or `nullptr` if the channel doesn't exist.
     */
    static Channel *adopt(u32 channel_id, u8 endpoints);

    /**
     * @brief Wrap an existing channel ID in a new kobj::Channel.
     *
     * @details
     * Creates a wrapper for an existing low-level channel. This is used when
     * sharing channel access across processes (e.g., via the assign system).
     * The underlying channel's reference count is incremented.
     *
     * @param channel_id The low-level channel ID to wrap.
     * @param is_send True if this wrapper is for the send side.
     * @return New channel wrapper, or `nullptr` if the channel doesn't exist.
     */
    static Channel *wrap(u32 channel_id, bool is_send = true);

    /**
     * @brief Destroy the channel object and close the underlying channel.
     *
     * @details
     * Called when the last reference is released.
     */
    ~Channel() override;

    /** @brief Get the underlying low-level channel ID. */
    u32 id() const {
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
    Channel(u32 channel_id, u8 endpoints)
        : Object(KIND), channel_id_(channel_id), endpoints_(endpoints) {}

    u32 channel_id_;
    u8 endpoints_;
};

} // namespace kobj
