//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/pi.cpp
// Purpose: Priority inheritance mutex support implementation.
//
//===----------------------------------------------------------------------===//

#include "pi.hpp"
#include "../console/serial.hpp"

namespace pi {

void init_mutex(PiMutex *m) {
    if (!m)
        return;

    // Spinlock is default-initialized via constructor
    m->owner = nullptr;
    m->owner_original_priority = task::PRIORITY_DEFAULT;
    m->boosted_priority = task::PRIORITY_DEFAULT;
    m->initialized = true;
}

bool try_lock(PiMutex *m) {
    if (!m || !m->initialized)
        return false;

    u64 saved_daif = m->lock.acquire();

    if (m->owner != nullptr) {
        // Already owned by someone
        m->lock.release(saved_daif);
        return false;
    }

    // Acquire the mutex
    task::Task *cur = task::current();
    if (!cur) {
        m->lock.release(saved_daif);
        return false;
    }

    m->owner = cur;
    m->owner_original_priority = cur->original_priority;
    m->boosted_priority = cur->priority;

    // Clear blocked_mutex since we now own this mutex
    cur->blocked_mutex = nullptr;

    m->lock.release(saved_daif);
    return true;
}

void contend(PiMutex *m, task::Task *waiter) {
    if (!m || !m->initialized || !waiter)
        return;

    u64 saved_daif = m->lock.acquire();

    task::Task *owner = m->owner;
    if (!owner) {
        // Mutex was released, nothing to do
        m->lock.release(saved_daif);
        return;
    }

    // Track which mutex we're blocked on for chain inheritance
    waiter->blocked_mutex = m;

    // If waiter has higher priority (lower number), boost the chain
    if (waiter->priority < owner->priority) {
        // Follow the chain of blocking and boost all tasks
        task::Task *current_owner = owner;
        u8 boost_priority = waiter->priority;
        constexpr int MAX_CHAIN_DEPTH = 8; // Prevent infinite loops
        int depth = 0;

        while (current_owner && depth < MAX_CHAIN_DEPTH) {
            if (boost_priority < current_owner->priority) {
                serial::puts("[pi] Boosting task '");
                serial::puts(current_owner->name);
                serial::puts("' priority from ");
                serial::put_dec(current_owner->priority);
                serial::puts(" to ");
                serial::put_dec(boost_priority);
                serial::puts(" (waiter: ");
                serial::puts(waiter->name);
                serial::puts(")\n");

                current_owner->priority = boost_priority;
            }

            // Check if this owner is also blocked on a mutex
            if (current_owner->blocked_mutex) {
                PiMutex *next_mutex = static_cast<PiMutex *>(current_owner->blocked_mutex);
                if (next_mutex->initialized) {
                    next_mutex->boosted_priority = boost_priority;
                    current_owner = next_mutex->owner;
                    depth++;
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        m->boosted_priority = boost_priority;
    }

    m->lock.release(saved_daif);
}

void unlock(PiMutex *m) {
    if (!m || !m->initialized)
        return;

    u64 saved_daif = m->lock.acquire();

    task::Task *cur = task::current();
    if (!cur || m->owner != cur) {
        // Not the owner, can't unlock
        m->lock.release(saved_daif);
        return;
    }

    // Restore original priority if it was boosted
    if (cur->priority != cur->original_priority) {
        serial::puts("[pi] Restoring task '");
        serial::puts(cur->name);
        serial::puts("' priority from ");
        serial::put_dec(cur->priority);
        serial::puts(" to ");
        serial::put_dec(cur->original_priority);
        serial::puts("\n");

        cur->priority = cur->original_priority;
    }

    // Clear blocked mutex tracking
    cur->blocked_mutex = nullptr;

    m->owner = nullptr;
    m->owner_original_priority = task::PRIORITY_DEFAULT;
    m->boosted_priority = task::PRIORITY_DEFAULT;

    m->lock.release(saved_daif);
}

bool is_locked(PiMutex *m) {
    if (!m || !m->initialized)
        return false;

    u64 saved_daif = m->lock.acquire();
    bool locked = (m->owner != nullptr);
    m->lock.release(saved_daif);

    return locked;
}

task::Task *get_owner(PiMutex *m) {
    if (!m || !m->initialized)
        return nullptr;

    u64 saved_daif = m->lock.acquire();
    task::Task *owner = m->owner;
    m->lock.release(saved_daif);

    return owner;
}

void boost_priority(task::Task *t, u8 new_priority) {
    if (!t)
        return;

    // Only boost if new priority is higher (lower number = higher priority)
    if (new_priority < t->priority) {
        t->priority = new_priority;
    }
}

void restore_priority(task::Task *t) {
    if (!t)
        return;

    // Restore to original priority (from before any PI boost)
    if (t->priority != t->original_priority) {
        serial::puts("[pi] Restoring task '");
        serial::puts(t->name);
        serial::puts("' priority from ");
        serial::put_dec(t->priority);
        serial::puts(" to original ");
        serial::put_dec(t->original_priority);
        serial::puts("\n");
        t->priority = t->original_priority;
    }

    // Clear blocked mutex pointer
    t->blocked_mutex = nullptr;
}

} // namespace pi
