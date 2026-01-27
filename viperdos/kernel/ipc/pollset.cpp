//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "pollset.hpp"
#include "../cap/rights.hpp"
#include "../cap/table.hpp"
#include "../console/console.hpp"
#include "../console/serial.hpp"
#include "../input/input.hpp"
#include "../kobj/channel.hpp"
#include "../lib/spinlock.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../tty/tty.hpp"
#include "../viper/viper.hpp"
#include "channel.hpp"
#include "poll.hpp"

/**
 * @file pollset.cpp
 * @brief Implementation of poll set management and waiting.
 *
 * @details
 * Poll sets are stored in a global fixed-size table with spinlock protection.
 * Waiting uses event-driven notification when possible, falling back to
 * polling for pseudo-handles like console input.
 *
 * Features:
 * - Per-task ownership enforcement
 * - Edge-triggered mode for efficiency
 * - Oneshot mode for auto-removal
 * - Event-driven wakeup via poll::register_wait/notify_handle
 */
namespace pollset {

// Spinlock for poll set table access
static Spinlock pollset_lock;

// Global poll set table
static PollSet poll_sets[MAX_POLL_SETS];
static u32 next_poll_set_id = 1;

/** @copydoc pollset::init */
void init() {
    serial::puts("[pollset] Initializing pollset subsystem\n");

    for (u32 i = 0; i < MAX_POLL_SETS; i++) {
        poll_sets[i].id = 0;
        poll_sets[i].active = false;
        poll_sets[i].entry_count = 0;
        for (u32 j = 0; j < MAX_ENTRIES_PER_SET; j++) {
            poll_sets[i].entries[j].active = false;
        }
    }

    serial::puts("[pollset] Pollset subsystem initialized\n");
}

/** @copydoc pollset::get */
PollSet *get(u32 poll_id) {
    for (u32 i = 0; i < MAX_POLL_SETS; i++) {
        if (poll_sets[i].active && poll_sets[i].id == poll_id) {
            return &poll_sets[i];
        }
    }
    return nullptr;
}

/**
 * @brief Allocate an unused poll set slot.
 *
 * @return Pointer to an inactive poll set entry, or `nullptr` if table is full.
 */
static PollSet *alloc_poll_set() {
    for (u32 i = 0; i < MAX_POLL_SETS; i++) {
        if (!poll_sets[i].active) {
            return &poll_sets[i];
        }
    }
    return nullptr;
}

/** @copydoc pollset::create */
i64 create() {
    PollSet *ps = alloc_poll_set();
    if (!ps) {
        return error::VERR_OUT_OF_MEMORY;
    }

    ps->id = next_poll_set_id++;
    ps->active = true;
    ps->owner_task_id = task::current() ? task::current()->id : 0;
    ps->entry_count = 0;

    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++) {
        ps->entries[i].active = false;
    }

    return static_cast<i64>(ps->id);
}

/** @copydoc pollset::is_owner */
bool is_owner(u32 poll_id) {
    SpinlockGuard guard(pollset_lock);
    PollSet *ps = get(poll_id);
    if (!ps) {
        return false;
    }
    task::Task *current = task::current();
    return current && ps->owner_task_id == current->id;
}

/** @copydoc pollset::add */
i64 add(u32 poll_id, u32 handle, u32 mask, poll::PollFlags flags) {
    SpinlockGuard guard(pollset_lock);

    PollSet *ps = get(poll_id);
    if (!ps) {
        return error::VERR_NOT_FOUND;
    }

    // Enforce per-task isolation
    task::Task *current = task::current();
    if (current && ps->owner_task_id != current->id) {
        return error::VERR_PERMISSION;
    }

    // Check if handle already exists
    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++) {
        if (ps->entries[i].active && ps->entries[i].handle == handle) {
            // Update mask and flags for existing entry
            ps->entries[i].mask = static_cast<poll::EventType>(mask);
            ps->entries[i].flags = flags;
            return error::VOK;
        }
    }

    // Find free slot
    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++) {
        if (!ps->entries[i].active) {
            ps->entries[i].handle = handle;
            ps->entries[i].mask = static_cast<poll::EventType>(mask);
            ps->entries[i].flags = flags;
            ps->entries[i].last_state = poll::EventType::NONE;
            ps->entries[i].active = true;
            ps->entry_count++;
            return error::VOK;
        }
    }

    return error::VERR_OUT_OF_MEMORY; // No free slots
}

/** @copydoc pollset::remove */
i64 remove(u32 poll_id, u32 handle) {
    PollSet *ps = get(poll_id);
    if (!ps) {
        return error::VERR_NOT_FOUND;
    }

    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++) {
        if (ps->entries[i].active && ps->entries[i].handle == handle) {
            ps->entries[i].active = false;
            ps->entry_count--;
            return error::VOK;
        }
    }

    return error::VERR_NOT_FOUND;
}

// Check readiness for a single entry
/**
 * @brief Compute which events are currently ready for a given handle/mask.
 *
 * @details
 * Supports:
 * - The console input pseudo-handle (keyboard/serial readiness).
 * - Channel readiness (readable when messages queued, writable when space).
 * - Timer readiness (expired).
 *
 * For channel handles, the handle is looked up in the current viper's
 * cap_table to get the Channel pointer. Rights determine endpoint type:
 * - CAP_READ: recv endpoint, check for messages
 * - CAP_WRITE: send endpoint, check for space
 *
 * @param handle Handle to test (capability handle or pseudo-handle).
 * @param mask Requested event mask.
 * @return Mask of triggered events (may be NONE).
 */
static poll::EventType check_readiness(u32 handle, poll::EventType mask) {
    poll::EventType triggered = poll::EventType::NONE;

    // Check console input readiness (special pseudo-handle)
    if (handle == poll::HANDLE_CONSOLE_INPUT) {
        if (poll::has_event(mask, poll::EventType::CONSOLE_INPUT)) {
            // Poll input devices and check for characters
            input::poll();
            // Check all possible input sources:
            // - input::char_buffer (raw keyboard input not yet drained)
            // - serial input
            // - console::input_buffer (already drained from input subsystem)
            // - tty::input_buffer (in GUI mode, timer ISR drains input to TTY)
            if (input::has_char() || serial::has_char() || console::has_input() ||
                tty::has_input()) {
                triggered = triggered | poll::EventType::CONSOLE_INPUT;
            }
        }
        return triggered;
    }

    // Network RX pseudo-handle removed - use netd user-space server instead
    if (handle == poll::HANDLE_NETWORK_RX) {
        return triggered; // No kernel networking
    }

    // For channel events, look up handle in cap_table to get channel ID
    // Use ID-based has_message/has_space which are TOCTOU-safe
    u32 channel_id = 0;
    cap::Table *ct = viper::current_cap_table();

    if (poll::has_event(mask, poll::EventType::CHANNEL_READ) ||
        poll::has_event(mask, poll::EventType::CHANNEL_WRITE)) {
        if (ct) {
            // Userspace context: handle is a cap_table index, look up channel ID
            cap::Entry *entry = ct->get(handle);
            if (entry && entry->kind == cap::Kind::Channel) {
                kobj::Channel *kch = static_cast<kobj::Channel *>(entry->object);
                if (kch) {
                    channel_id = kch->id();
                }
            }
        } else {
            // Kernel test context: no viper, handle IS the channel ID directly
            channel_id = handle;
        }
    }

    // Check channel read readiness (recv endpoint) using TOCTOU-safe ID lookup
    if (poll::has_event(mask, poll::EventType::CHANNEL_READ)) {
        if (channel_id != 0 && channel::has_message(channel_id)) {
            triggered = triggered | poll::EventType::CHANNEL_READ;
        }
    }

    // Check channel write readiness (send endpoint) using TOCTOU-safe ID lookup
    if (poll::has_event(mask, poll::EventType::CHANNEL_WRITE)) {
        if (channel_id != 0 && channel::has_space(channel_id)) {
            triggered = triggered | poll::EventType::CHANNEL_WRITE;
        }
    }

    // Check timer expiry
    if (poll::has_event(mask, poll::EventType::TIMER)) {
        if (poll::timer_expired(handle)) {
            triggered = triggered | poll::EventType::TIMER;
        }
    }

    return triggered;
}

/**
 * @brief Check and return triggered events for a poll entry with edge/level support.
 *
 * @param entry Poll entry to check.
 * @return Triggered events (accounting for edge-triggered mode).
 */
static poll::EventType check_entry_readiness(PollEntry &entry) {
    poll::EventType current_state = check_readiness(entry.handle, entry.mask);

    // For level-triggered (default), return current state
    if (!poll::has_flag(entry.flags, poll::PollFlags::EDGE_TRIGGERED)) {
        return current_state;
    }

    // For edge-triggered, only return events that are newly set
    poll::EventType triggered = poll::EventType::NONE;
    u32 current_bits = static_cast<u32>(current_state);
    u32 last_bits = static_cast<u32>(entry.last_state);

    // Find bits that went from 0 to 1
    u32 edge_bits = current_bits & ~last_bits;
    triggered = static_cast<poll::EventType>(edge_bits);

    // Update last state for next check
    entry.last_state = current_state;

    return triggered;
}

/** @copydoc pollset::wait */
i64 wait(u32 poll_id, poll::PollEvent *out_events, u32 max_events, i64 timeout_ms) {
    PollSet *ps = get(poll_id);
    if (!ps) {
        return error::VERR_NOT_FOUND;
    }

    if (!out_events || max_events == 0) {
        return error::VERR_INVALID_ARG;
    }

    // Enforce per-task isolation
    task::Task *current = task::current();
    if (current && ps->owner_task_id != current->id) {
        return error::VERR_PERMISSION;
    }

    u64 deadline = 0;
    if (timeout_ms > 0) {
        deadline = poll::time_now_ms() + static_cast<u64>(timeout_ms);
    }

    // Event-driven wait loop
    while (true) {
        u32 ready_count = 0;
        bool has_pseudo_handles = false;

        // Check each entry in the poll set
        for (u32 i = 0; i < MAX_ENTRIES_PER_SET && ready_count < max_events; i++) {
            if (!ps->entries[i].active)
                continue;

            // Track if we have pseudo-handles that need polling
            if (ps->entries[i].handle == poll::HANDLE_CONSOLE_INPUT ||
                ps->entries[i].handle == poll::HANDLE_NETWORK_RX) {
                has_pseudo_handles = true;
            }

            poll::EventType triggered = check_entry_readiness(ps->entries[i]);

            if (triggered != poll::EventType::NONE) {
                out_events[ready_count].handle = ps->entries[i].handle;
                out_events[ready_count].events = ps->entries[i].mask;
                out_events[ready_count].triggered = triggered;
                ready_count++;

                // Handle oneshot mode - deactivate entry after trigger
                if (poll::has_flag(ps->entries[i].flags, poll::PollFlags::ONESHOT)) {
                    ps->entries[i].active = false;
                    ps->entry_count--;
                }
            }
        }

        // Return if any events are ready
        if (ready_count > 0) {
            return static_cast<i64>(ready_count);
        }

        // Non-blocking mode: return immediately
        if (timeout_ms == 0) {
            return 0;
        }

        // Check timeout
        if (timeout_ms > 0 && poll::time_now_ms() >= deadline) {
            return 0;
        }

        // Register for event-driven wakeup on channel handles.
        // We always register channel handles so notify_handle() can wake us,
        // even when pseudo-handles are present.
        bool has_channel_handles = false;
        for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++) {
            if (!ps->entries[i].active)
                continue;

            u32 handle = ps->entries[i].handle;
            poll::EventType mask = ps->entries[i].mask;

            // Only register for real handles, not pseudo-handles
            if (handle != poll::HANDLE_CONSOLE_INPUT && handle != poll::HANDLE_NETWORK_RX) {
                poll::register_wait(handle, mask);
                has_channel_handles = true;
            }
        }

        // Choose blocking strategy based on handle types present.
        // The key requirement is that we can be woken by EITHER:
        // - notify_handle() when a channel has data (for channel handles)
        // - check_timers() when the poll interval expires (for timeout/pseudo-handles)
        u32 poll_timer_id = 0;

        if (has_pseudo_handles) {
            // When pseudo-handles are present, create a timer for periodic polling
            // while also being wakeable by channel events via notify_handle().
            // This enables dual-wake: both timer expiry and channel events wake us.
            constexpr u64 PSEUDO_POLL_INTERVAL_MS = 10;
            i64 timer_result = poll::timer_create(PSEUDO_POLL_INTERVAL_MS);

            if (timer_result >= 0) {
                poll_timer_id = static_cast<u32>(timer_result);
                // Register as timer waiter and set state to Blocked atomically.
                // This enables dual-wake: check_timers() wakes on timer expiry,
                // notify_handle() wakes on channel events.
                poll::register_timer_wait_and_block(poll_timer_id);
            } else if (current) {
                // Fallback if timer creation fails - just block
                current->state = task::TaskState::Blocked;
            }
        } else if (has_channel_handles) {
            // No pseudo-handles but have channel handles.
            // If a timeout is specified, create a timer to honor it.
            // Otherwise block until a channel event wakes us.
            if (timeout_ms > 0) {
                // Calculate remaining time until deadline
                u64 now = poll::time_now_ms();
                u64 remaining = (deadline > now) ? (deadline - now) : 1;
                i64 timer_result = poll::timer_create(remaining);

                if (timer_result >= 0) {
                    poll_timer_id = static_cast<u32>(timer_result);
                    poll::register_timer_wait_and_block(poll_timer_id);
                } else if (current) {
                    current->state = task::TaskState::Blocked;
                }
            } else if (current) {
                // No timeout (wait indefinitely) - block until channel event
                current->state = task::TaskState::Blocked;
            }
        }
        // else: No handles at all (shouldn't happen) - just yield without blocking

        task::yield();

        // Clean up: cancel timer and unregister waits
        if (poll_timer_id != 0) {
            poll::timer_cancel(poll_timer_id);
        }
        poll::unregister_wait();
    }
}

/** @copydoc pollset::destroy */
i64 destroy(u32 poll_id) {
    PollSet *ps = get(poll_id);
    if (!ps) {
        return error::VERR_NOT_FOUND;
    }

    ps->active = false;
    ps->id = 0;
    ps->entry_count = 0;

    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++) {
        ps->entries[i].active = false;
    }

    return error::VOK;
}

/** @copydoc pollset::test_pollset */
void test_pollset() {
    serial::puts("[pollset] Testing pollset functionality...\n");

    // Create a poll set
    i64 ps_result = create();
    if (ps_result < 0) {
        serial::puts("[pollset] Failed to create poll set\n");
        return;
    }
    u32 ps_id = static_cast<u32>(ps_result);
    serial::puts("[pollset] Created poll set ");
    serial::put_dec(ps_id);
    serial::puts("\n");

    // Create a test channel
    i64 ch_result = channel::create();
    if (ch_result < 0) {
        serial::puts("[pollset] Failed to create channel\n");
        destroy(ps_id);
        return;
    }
    u32 ch_id = static_cast<u32>(ch_result);

    // Add channel to poll set
    i64 add_result = add(ps_id,
                         ch_id,
                         static_cast<u32>(poll::EventType::CHANNEL_READ) |
                             static_cast<u32>(poll::EventType::CHANNEL_WRITE));
    if (add_result < 0) {
        serial::puts("[pollset] Failed to add channel to poll set\n");
        channel::close(ch_id);
        destroy(ps_id);
        return;
    }

    // Test 1: Empty channel should be writable
    poll::PollEvent events[1];
    i64 ready = wait(ps_id, events, 1, 0); // Non-blocking

    serial::puts("[pollset] Test 1 (empty channel): wait returned ");
    serial::put_dec(ready);
    if (ready > 0) {
        serial::puts(", triggered=");
        serial::put_hex(static_cast<u32>(events[0].triggered));
    }
    serial::puts("\n");

    if (ready == 1 && poll::has_event(events[0].triggered, poll::EventType::CHANNEL_WRITE)) {
        serial::puts("[pollset] Test 1 PASSED: channel writable\n");
    } else {
        serial::puts("[pollset] Test 1 FAILED\n");
    }

    // Send a message to channel
    const char *msg = "test";
    channel::send(ch_id, msg, 5);

    // Test 2: Channel with message should be readable
    ready = wait(ps_id, events, 1, 0);
    serial::puts("[pollset] Test 2 (message queued): wait returned ");
    serial::put_dec(ready);
    if (ready > 0) {
        serial::puts(", triggered=");
        serial::put_hex(static_cast<u32>(events[0].triggered));
    }
    serial::puts("\n");

    if (ready >= 1 && poll::has_event(events[0].triggered, poll::EventType::CHANNEL_READ)) {
        serial::puts("[pollset] Test 2 PASSED: channel readable\n");
    } else {
        serial::puts("[pollset] Test 2 FAILED\n");
    }

    // Clean up
    channel::close(ch_id);
    destroy(ps_id);
    serial::puts("[pollset] Pollset tests complete\n");
}

} // namespace pollset
