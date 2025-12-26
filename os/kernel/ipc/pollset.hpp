#pragma once

#include "../include/error.hpp"
#include "../include/types.hpp"
#include "poll.hpp"

/**
 * @file pollset.hpp
 * @brief Poll set abstraction built on top of the poll subsystem.
 *
 * @details
 * A poll set is a kernel-managed collection of handles and event masks that can
 * be waited on as a group. User-space can create a poll set, add/remove handles,
 * and then wait for any of them to become ready.
 *
 * This is an early implementation that:
 * - Stores poll sets in a global fixed-size table.
 * - Stores entries in a fixed-size array per poll set.
 * - Implements waiting by repeatedly checking readiness and yielding.
 */
namespace pollset
{

/** @brief Maximum number of poll sets that can exist system-wide. */
constexpr u32 MAX_POLL_SETS = 16;

/** @brief Maximum number of entries that can be registered in a single poll set. */
constexpr u32 MAX_ENTRIES_PER_SET = 16;

/**
 * @brief Internal entry describing one watched handle and its event mask.
 */
struct PollEntry
{
    u32 handle;           // Channel ID or timer handle
    poll::EventType mask; // Events to watch for
    bool active;          // Entry is in use
};

/**
 * @brief Kernel poll set object.
 *
 * @details
 * Each poll set has an ID, an owning task ID, and a fixed-size array of entries.
 * Ownership is currently informational; policy enforcement can be added later.
 */
struct PollSet
{
    u32 id;
    bool active;
    u32 owner_task_id; // Task that created this poll set
    PollEntry entries[MAX_ENTRIES_PER_SET];
    u32 entry_count;
};

/**
 * @brief Initialize the pollset subsystem.
 *
 * @details
 * Resets the global poll set table.
 */
void init();

/**
 * @brief Create a new poll set.
 *
 * @details
 * Allocates a free poll set slot, assigns a new ID, and records the current
 * task as the owner.
 *
 * @return Poll set ID on success, or negative error code.
 */
i64 create();

/**
 * @brief Add (or update) a watched handle in a poll set.
 *
 * @details
 * If the handle already exists in the set, its mask is updated. Otherwise a new
 * entry is allocated. The mask is interpreted as a bitmask of
 * @ref poll::EventType values.
 *
 * @param poll_id Poll set ID.
 * @param handle Handle to watch (channel ID, timer ID, or special pseudo-handle).
 * @param mask Event mask bits to watch for.
 * @return Result code.
 */
i64 add(u32 poll_id, u32 handle, u32 mask);

/**
 * @brief Remove a watched handle from a poll set.
 *
 * @param poll_id Poll set ID.
 * @param handle Handle to remove.
 * @return Result code.
 */
i64 remove(u32 poll_id, u32 handle);

/**
 * @brief Wait for readiness events across the handles in a poll set.
 *
 * @details
 * Checks each active entry for readiness and fills `out_events` with triggered
 * entries (up to `max_events`). Waiting behavior matches @ref poll::poll:
 * non-blocking, timed, or infinite.
 *
 * @param poll_id Poll set ID.
 * @param out_events Output array to receive triggered events.
 * @param max_events Maximum number of output entries to write.
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite).
 * @return Number of ready events, 0 on timeout, or negative error code.
 */
i64 wait(u32 poll_id, poll::PollEvent *out_events, u32 max_events, i64 timeout_ms);

/**
 * @brief Destroy a poll set and release its slot.
 *
 * @param poll_id Poll set ID.
 * @return Result code.
 */
i64 destroy(u32 poll_id);

/**
 * @brief Look up an active poll set by ID.
 *
 * @param poll_id Poll set ID.
 * @return Pointer to poll set, or `nullptr` if not found.
 */
PollSet *get(u32 poll_id);

/**
 * @brief Run a simple self-test of the pollset subsystem.
 *
 * @details
 * Creates a poll set and a channel, registers the channel for read/write, and
 * verifies basic readiness behavior.
 */
void test_pollset();

} // namespace pollset
