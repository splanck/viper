#include "timerwheel.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../console/serial.hpp"

/**
 * @file timerwheel.cpp
 * @brief Hierarchical timer wheel implementation.
 *
 * @details
 * Implements a two-level hierarchical timer wheel for efficient timeout
 * management. The algorithm provides O(1) insert/delete and amortized O(1)
 * per-tick processing.
 */
namespace timerwheel
{

namespace
{
// Global timer wheel instance
TimerWheel g_wheel;
bool g_initialized = false;
} // namespace

void TimerWheel::init(u64 current_time_ms)
{
    // Initialize timer storage
    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        timers_[i].active = false;
        timers_[i].id = 0;
        timers_[i].next = nullptr;
        timers_[i].prev = nullptr;
    }

    // Initialize wheel slots
    for (u32 i = 0; i < WHEEL0_SIZE; i++)
    {
        wheel0_[i] = nullptr;
    }
    for (u32 i = 0; i < WHEEL1_SIZE; i++)
    {
        wheel1_[i] = nullptr;
    }
    overflow_ = nullptr;

    // Initialize state
    current_time_ = current_time_ms;
    wheel0_index_ = current_time_ms & WHEEL0_MASK;
    wheel1_index_ = (current_time_ms >> WHEEL0_BITS) & WHEEL1_MASK;
    next_id_ = 1;
    active_count_ = 0;
}

TimerEntry *TimerWheel::alloc_timer()
{
    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        if (!timers_[i].active)
        {
            return &timers_[i];
        }
    }
    return nullptr;
}

TimerEntry *TimerWheel::find_timer(u32 id)
{
    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        if (timers_[i].active && timers_[i].id == id)
        {
            return &timers_[i];
        }
    }
    return nullptr;
}

void TimerWheel::remove_from_slot(TimerEntry *entry)
{
    if (!entry)
        return;

    // Unlink from doubly-linked list
    if (entry->prev)
    {
        entry->prev->next = entry->next;
    }
    if (entry->next)
    {
        entry->next->prev = entry->prev;
    }

    // Update head pointers if this was the head
    // Check wheel0
    for (u32 i = 0; i < WHEEL0_SIZE; i++)
    {
        if (wheel0_[i] == entry)
        {
            wheel0_[i] = entry->next;
            break;
        }
    }
    // Check wheel1
    for (u32 i = 0; i < WHEEL1_SIZE; i++)
    {
        if (wheel1_[i] == entry)
        {
            wheel1_[i] = entry->next;
            break;
        }
    }
    // Check overflow
    if (overflow_ == entry)
    {
        overflow_ = entry->next;
    }

    entry->next = nullptr;
    entry->prev = nullptr;
}

void TimerWheel::add_to_wheel(TimerEntry *entry)
{
    if (!entry)
        return;

    u64 delta = entry->expire_time - current_time_;

    TimerEntry **slot = nullptr;

    if (delta < WHEEL0_SIZE)
    {
        // Level 0: expires within 256ms
        u32 idx = (wheel0_index_ + delta) & WHEEL0_MASK;
        slot = &wheel0_[idx];
    }
    else if (delta < MAX_TIMEOUT_MS)
    {
        // Level 1: expires within 16.4s
        u64 ticks_from_now = delta >> WHEEL0_BITS;
        u32 idx = (wheel1_index_ + ticks_from_now) & WHEEL1_MASK;
        slot = &wheel1_[idx];
    }
    else
    {
        // Overflow: expires beyond wheel range
        slot = &overflow_;
    }

    // Insert at head of slot's list
    entry->next = *slot;
    entry->prev = nullptr;
    if (*slot)
    {
        (*slot)->prev = entry;
    }
    *slot = entry;
}

u32 TimerWheel::schedule(u64 expire_time_ms, TimerCallback callback, void *context)
{
    if (expire_time_ms <= current_time_)
    {
        // Already expired - fire immediately
        if (callback)
        {
            callback(context);
        }
        return 0;
    }

    TimerEntry *entry = alloc_timer();
    if (!entry)
    {
        serial::puts("[timerwheel] No free timer slots\n");
        return 0;
    }

    entry->expire_time = expire_time_ms;
    entry->callback = callback;
    entry->context = context;
    entry->id = next_id_++;
    if (next_id_ == 0)
        next_id_ = 1; // Skip 0
    entry->active = true;

    add_to_wheel(entry);
    active_count_++;

    return entry->id;
}

bool TimerWheel::cancel(u32 timer_id)
{
    if (timer_id == 0)
        return false;

    TimerEntry *entry = find_timer(timer_id);
    if (!entry)
        return false;

    remove_from_slot(entry);
    entry->active = false;
    entry->id = 0;
    active_count_--;

    return true;
}

void TimerWheel::cascade(u32 level)
{
    if (level == 1)
    {
        // Cascade from level 1 to level 0
        TimerEntry *head = wheel1_[wheel1_index_];
        wheel1_[wheel1_index_] = nullptr;

        while (head)
        {
            TimerEntry *next = head->next;
            head->next = nullptr;
            head->prev = nullptr;
            add_to_wheel(head); // Re-add to correct slot (should go to level 0)
            head = next;
        }
    }
    else if (level == 2)
    {
        // Cascade from overflow to lower levels
        TimerEntry *head = overflow_;
        overflow_ = nullptr;

        while (head)
        {
            TimerEntry *next = head->next;
            head->next = nullptr;
            head->prev = nullptr;
            add_to_wheel(head); // Re-add to correct slot
            head = next;
        }
    }
}

void TimerWheel::tick(u64 current_time_ms)
{
    // Process all ticks between last time and current time
    while (current_time_ < current_time_ms)
    {
        current_time_++;
        wheel0_index_ = (wheel0_index_ + 1) & WHEEL0_MASK;

        // Check if we need to cascade from level 1
        if (wheel0_index_ == 0)
        {
            wheel1_index_ = (wheel1_index_ + 1) & WHEEL1_MASK;
            cascade(1);

            // Check if we need to cascade from overflow
            if (wheel1_index_ == 0)
            {
                cascade(2);
            }
        }

        // Fire all expired timers in current slot
        TimerEntry *head = wheel0_[wheel0_index_];
        wheel0_[wheel0_index_] = nullptr;

        while (head)
        {
            TimerEntry *next = head->next;

            // Double-check expiration (in case of re-scheduling bugs)
            if (head->expire_time <= current_time_ && head->active)
            {
                TimerCallback cb = head->callback;
                void *ctx = head->context;

                // Mark as inactive before calling
                head->active = false;
                head->id = 0;
                head->next = nullptr;
                head->prev = nullptr;
                active_count_--;

                // Call the callback
                if (cb)
                {
                    cb(ctx);
                }
            }
            else if (head->active)
            {
                // Timer hasn't expired yet - re-add to wheel
                head->next = nullptr;
                head->prev = nullptr;
                add_to_wheel(head);
            }

            head = next;
        }
    }
}

// Global interface functions

TimerWheel &get_wheel()
{
    return g_wheel;
}

void init(u64 current_time_ms)
{
    g_wheel.init(current_time_ms);
    g_initialized = true;
    serial::puts("[timerwheel] Timer wheel initialized\n");
}

u32 schedule(u64 timeout_ms, TimerCallback callback, void *context)
{
    if (!g_initialized)
        return 0;

    // Get current time from timer module
    u64 now = timer::get_ticks();
    return g_wheel.schedule(now + timeout_ms, callback, context);
}

bool cancel(u32 timer_id)
{
    if (!g_initialized)
        return false;
    return g_wheel.cancel(timer_id);
}

void tick(u64 current_time_ms)
{
    if (!g_initialized)
        return;
    g_wheel.tick(current_time_ms);
}

} // namespace timerwheel
