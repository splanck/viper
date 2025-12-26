#include "pollset.hpp"
#include "../cap/rights.hpp"
#include "../cap/table.hpp"
#include "../console/serial.hpp"
#include "../input/input.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../viper/viper.hpp"
#include "channel.hpp"
#include "poll.hpp"

/**
 * @file pollset.cpp
 * @brief Implementation of poll set management and waiting.
 *
 * @details
 * Poll sets are stored in a global fixed-size table. Waiting is implemented by
 * repeatedly checking the readiness of each entry and yielding between checks.
 *
 * This is intentionally simple for bring-up and can later evolve to a more
 * efficient mechanism using wakeups/notifications from channels and timers.
 */
namespace pollset
{

// Global poll set table
static PollSet poll_sets[MAX_POLL_SETS];
static u32 next_poll_set_id = 1;

/** @copydoc pollset::init */
void init()
{
    serial::puts("[pollset] Initializing pollset subsystem\n");

    for (u32 i = 0; i < MAX_POLL_SETS; i++)
    {
        poll_sets[i].id = 0;
        poll_sets[i].active = false;
        poll_sets[i].entry_count = 0;
        for (u32 j = 0; j < MAX_ENTRIES_PER_SET; j++)
        {
            poll_sets[i].entries[j].active = false;
        }
    }

    serial::puts("[pollset] Pollset subsystem initialized\n");
}

/** @copydoc pollset::get */
PollSet *get(u32 poll_id)
{
    for (u32 i = 0; i < MAX_POLL_SETS; i++)
    {
        if (poll_sets[i].active && poll_sets[i].id == poll_id)
        {
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
static PollSet *alloc_poll_set()
{
    for (u32 i = 0; i < MAX_POLL_SETS; i++)
    {
        if (!poll_sets[i].active)
        {
            return &poll_sets[i];
        }
    }
    return nullptr;
}

/** @copydoc pollset::create */
i64 create()
{
    PollSet *ps = alloc_poll_set();
    if (!ps)
    {
        return error::VERR_OUT_OF_MEMORY;
    }

    ps->id = next_poll_set_id++;
    ps->active = true;
    ps->owner_task_id = task::current() ? task::current()->id : 0;
    ps->entry_count = 0;

    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++)
    {
        ps->entries[i].active = false;
    }

    return static_cast<i64>(ps->id);
}

/** @copydoc pollset::add */
i64 add(u32 poll_id, u32 handle, u32 mask)
{
    PollSet *ps = get(poll_id);
    if (!ps)
    {
        return error::VERR_NOT_FOUND;
    }

    // Check if handle already exists
    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++)
    {
        if (ps->entries[i].active && ps->entries[i].handle == handle)
        {
            // Update mask for existing entry
            ps->entries[i].mask = static_cast<poll::EventType>(mask);
            return error::VOK;
        }
    }

    // Find free slot
    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++)
    {
        if (!ps->entries[i].active)
        {
            ps->entries[i].handle = handle;
            ps->entries[i].mask = static_cast<poll::EventType>(mask);
            ps->entries[i].active = true;
            ps->entry_count++;
            return error::VOK;
        }
    }

    return error::VERR_OUT_OF_MEMORY; // No free slots
}

/** @copydoc pollset::remove */
i64 remove(u32 poll_id, u32 handle)
{
    PollSet *ps = get(poll_id);
    if (!ps)
    {
        return error::VERR_NOT_FOUND;
    }

    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++)
    {
        if (ps->entries[i].active && ps->entries[i].handle == handle)
        {
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
static poll::EventType check_readiness(u32 handle, poll::EventType mask)
{
    poll::EventType triggered = poll::EventType::NONE;

    // Check console input readiness (special pseudo-handle)
    if (handle == poll::HANDLE_CONSOLE_INPUT)
    {
        if (poll::has_event(mask, poll::EventType::CONSOLE_INPUT))
        {
            // Poll input devices and check for characters
            input::poll();
            if (input::has_char() || serial::has_char())
            {
                triggered = triggered | poll::EventType::CONSOLE_INPUT;
            }
        }
        return triggered;
    }

    // For channel events, look up handle in cap_table
    cap::Table *ct = viper::current_cap_table();
    channel::Channel *ch = nullptr;

    if (ct && (poll::has_event(mask, poll::EventType::CHANNEL_READ) ||
               poll::has_event(mask, poll::EventType::CHANNEL_WRITE)))
    {
        cap::Entry *entry = ct->get(handle);
        if (entry && entry->kind == cap::Kind::Channel)
        {
            ch = static_cast<channel::Channel *>(entry->object);
        }
    }

    // Check channel read readiness (recv endpoint)
    if (poll::has_event(mask, poll::EventType::CHANNEL_READ))
    {
        if (ch)
        {
            if (channel::has_message(ch))
            {
                triggered = triggered | poll::EventType::CHANNEL_READ;
            }
        }
        else
        {
            // Fallback to legacy channel ID lookup
            if (channel::has_message(handle))
            {
                triggered = triggered | poll::EventType::CHANNEL_READ;
            }
        }
    }

    // Check channel write readiness (send endpoint)
    if (poll::has_event(mask, poll::EventType::CHANNEL_WRITE))
    {
        if (ch)
        {
            if (channel::has_space(ch))
            {
                triggered = triggered | poll::EventType::CHANNEL_WRITE;
            }
        }
        else
        {
            // Fallback to legacy channel ID lookup
            if (channel::has_space(handle))
            {
                triggered = triggered | poll::EventType::CHANNEL_WRITE;
            }
        }
    }

    // Check timer expiry
    if (poll::has_event(mask, poll::EventType::TIMER))
    {
        if (poll::timer_expired(handle))
        {
            triggered = triggered | poll::EventType::TIMER;
        }
    }

    return triggered;
}

/** @copydoc pollset::wait */
i64 wait(u32 poll_id, poll::PollEvent *out_events, u32 max_events, i64 timeout_ms)
{
    PollSet *ps = get(poll_id);
    if (!ps)
    {
        return error::VERR_NOT_FOUND;
    }

    if (!out_events || max_events == 0)
    {
        return error::VERR_INVALID_ARG;
    }

    u64 deadline = 0;
    if (timeout_ms > 0)
    {
        deadline = poll::time_now_ms() + static_cast<u64>(timeout_ms);
    }

    // Poll loop
    while (true)
    {
        u32 ready_count = 0;

        // Check each entry in the poll set
        for (u32 i = 0; i < MAX_ENTRIES_PER_SET && ready_count < max_events; i++)
        {
            if (!ps->entries[i].active)
                continue;

            poll::EventType triggered = check_readiness(ps->entries[i].handle, ps->entries[i].mask);

            if (triggered != poll::EventType::NONE)
            {
                out_events[ready_count].handle = ps->entries[i].handle;
                out_events[ready_count].events = ps->entries[i].mask;
                out_events[ready_count].triggered = triggered;
                ready_count++;
            }
        }

        // Return if any events are ready
        if (ready_count > 0)
        {
            return static_cast<i64>(ready_count);
        }

        // Non-blocking mode: return immediately
        if (timeout_ms == 0)
        {
            return 0;
        }

        // Check timeout
        if (timeout_ms > 0 && poll::time_now_ms() >= deadline)
        {
            return 0;
        }

        // Yield and try again
        task::yield();
    }
}

/** @copydoc pollset::destroy */
i64 destroy(u32 poll_id)
{
    PollSet *ps = get(poll_id);
    if (!ps)
    {
        return error::VERR_NOT_FOUND;
    }

    ps->active = false;
    ps->id = 0;
    ps->entry_count = 0;

    for (u32 i = 0; i < MAX_ENTRIES_PER_SET; i++)
    {
        ps->entries[i].active = false;
    }

    return error::VOK;
}

/** @copydoc pollset::test_pollset */
void test_pollset()
{
    serial::puts("[pollset] Testing pollset functionality...\n");

    // Create a poll set
    i64 ps_result = create();
    if (ps_result < 0)
    {
        serial::puts("[pollset] Failed to create poll set\n");
        return;
    }
    u32 ps_id = static_cast<u32>(ps_result);
    serial::puts("[pollset] Created poll set ");
    serial::put_dec(ps_id);
    serial::puts("\n");

    // Create a test channel
    i64 ch_result = channel::create();
    if (ch_result < 0)
    {
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
    if (add_result < 0)
    {
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
    if (ready > 0)
    {
        serial::puts(", triggered=");
        serial::put_hex(static_cast<u32>(events[0].triggered));
    }
    serial::puts("\n");

    if (ready == 1 && poll::has_event(events[0].triggered, poll::EventType::CHANNEL_WRITE))
    {
        serial::puts("[pollset] Test 1 PASSED: channel writable\n");
    }
    else
    {
        serial::puts("[pollset] Test 1 FAILED\n");
    }

    // Send a message to channel
    const char *msg = "test";
    channel::send(ch_id, msg, 5);

    // Test 2: Channel with message should be readable
    ready = wait(ps_id, events, 1, 0);
    serial::puts("[pollset] Test 2 (message queued): wait returned ");
    serial::put_dec(ready);
    if (ready > 0)
    {
        serial::puts(", triggered=");
        serial::put_hex(static_cast<u32>(events[0].triggered));
    }
    serial::puts("\n");

    if (ready >= 1 && poll::has_event(events[0].triggered, poll::EventType::CHANNEL_READ))
    {
        serial::puts("[pollset] Test 2 PASSED: channel readable\n");
    }
    else
    {
        serial::puts("[pollset] Test 2 FAILED\n");
    }

    // Clean up
    channel::close(ch_id);
    destroy(ps_id);
    serial::puts("[pollset] Pollset tests complete\n");
}

} // namespace pollset
