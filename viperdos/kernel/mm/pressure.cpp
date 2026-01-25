/**
 * @file pressure.cpp
 * @brief Memory pressure monitoring and reclaim implementation.
 */

#include "pressure.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "../lib/str.hpp"
#include "buddy.hpp"
#include "pmm.hpp"
#include "slab.hpp"

namespace mm::pressure {

namespace {

// Pressure thresholds (percentage of total memory)
constexpr u32 THRESHOLD_LOW = 50;      // Below 50% free -> LOW
constexpr u32 THRESHOLD_MEDIUM = 25;   // Below 25% free -> MEDIUM
constexpr u32 THRESHOLD_HIGH = 10;     // Below 10% free -> HIGH
constexpr u32 THRESHOLD_CRITICAL = 5;  // Below 5% free -> CRITICAL

// Callback registration
struct CallbackEntry {
    const char *name;
    PressureCallback callback;
    bool active;
};

CallbackEntry callbacks[MAX_CALLBACKS];
usize callback_count = 0;
Spinlock callback_lock;

// Statistics
u64 total_reclaim_calls = 0;
u64 total_pages_reclaimed = 0;
bool initialized = false;

} // namespace

void init() {
    serial::puts("[pressure] Initializing memory pressure monitor\n");

    for (usize i = 0; i < MAX_CALLBACKS; i++) {
        callbacks[i].active = false;
    }
    callback_count = 0;
    total_reclaim_calls = 0;
    total_pages_reclaimed = 0;
    initialized = true;

    // Register slab reaper as first callback
    register_callback("slab", [](Level) -> u64 { return slab::reap(); });

    serial::puts("[pressure] Memory pressure monitor initialized\n");
}

bool register_callback(const char *name, PressureCallback callback) {
    SpinlockGuard guard(callback_lock);

    if (callback_count >= MAX_CALLBACKS) {
        serial::puts("[pressure] ERROR: Callback table full\n");
        return false;
    }

    for (usize i = 0; i < MAX_CALLBACKS; i++) {
        if (!callbacks[i].active) {
            callbacks[i].name = name;
            callbacks[i].callback = callback;
            callbacks[i].active = true;
            callback_count++;

            serial::puts("[pressure] Registered callback: ");
            serial::puts(name);
            serial::puts("\n");
            return true;
        }
    }

    return false;
}

u32 get_free_percent() {
    u64 free_pages = pmm::get_free_pages();
    u64 total_pages = pmm::get_total_pages();

    if (total_pages == 0)
        return 100;

    return static_cast<u32>((free_pages * 100) / total_pages);
}

Level check_level() {
    u32 free_pct = get_free_percent();

    if (free_pct < THRESHOLD_CRITICAL) {
        return Level::CRITICAL;
    } else if (free_pct < THRESHOLD_HIGH) {
        return Level::HIGH;
    } else if (free_pct < THRESHOLD_MEDIUM) {
        return Level::MEDIUM;
    } else if (free_pct < THRESHOLD_LOW) {
        return Level::LOW;
    }
    return Level::NONE;
}

const char *level_name(Level level) {
    switch (level) {
        case Level::NONE:
            return "NONE";
        case Level::LOW:
            return "LOW";
        case Level::MEDIUM:
            return "MEDIUM";
        case Level::HIGH:
            return "HIGH";
        case Level::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

u64 force_reclaim() {
    Level level = check_level();

    serial::puts("[pressure] Forcing reclaim at level ");
    serial::puts(level_name(level));
    serial::puts("\n");

    u64 reclaimed = 0;

    SpinlockGuard guard(callback_lock);

    for (usize i = 0; i < MAX_CALLBACKS; i++) {
        if (callbacks[i].active && callbacks[i].callback) {
            u64 pages = callbacks[i].callback(level);
            reclaimed += pages;
        }
    }

    total_reclaim_calls++;
    total_pages_reclaimed += reclaimed;

    return reclaimed;
}

u64 reclaim_if_needed() {
    Level level = check_level();

    if (level == Level::NONE) {
        return 0;
    }

    serial::puts("[pressure] Memory pressure detected: ");
    serial::puts(level_name(level));
    serial::puts(" (");
    serial::put_dec(get_free_percent());
    serial::puts("% free)\n");

    return force_reclaim();
}

void get_stats(Level *out_level, u64 *out_free_pages, u64 *out_total_pages,
               u64 *out_reclaim_calls, u64 *out_pages_reclaimed) {
    if (out_level)
        *out_level = check_level();
    if (out_free_pages)
        *out_free_pages = pmm::get_free_pages();
    if (out_total_pages)
        *out_total_pages = pmm::get_total_pages();
    if (out_reclaim_calls)
        *out_reclaim_calls = total_reclaim_calls;
    if (out_pages_reclaimed)
        *out_pages_reclaimed = total_pages_reclaimed;
}

} // namespace mm::pressure
