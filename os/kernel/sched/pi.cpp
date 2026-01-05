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

namespace pi
{

void init_mutex(PiMutex *m)
{
    if (!m)
        return;

    // Spinlock is default-initialized via constructor
    m->owner = nullptr;
    m->owner_original_priority = 128; // Default priority
    m->boosted_priority = 128;
    m->initialized = true;
}

bool try_lock(PiMutex *m)
{
    if (!m || !m->initialized)
        return false;

    m->lock.acquire();

    if (m->owner != nullptr)
    {
        // Already owned by someone
        m->lock.release();
        return false;
    }

    // Acquire the mutex
    task::Task *cur = task::current();
    if (!cur)
    {
        m->lock.release();
        return false;
    }

    m->owner = cur;
    m->owner_original_priority = cur->priority;
    m->boosted_priority = cur->priority;

    m->lock.release();
    return true;
}

void contend(PiMutex *m, task::Task *waiter)
{
    if (!m || !m->initialized || !waiter)
        return;

    m->lock.acquire();

    task::Task *owner = m->owner;
    if (!owner)
    {
        // Mutex was released, nothing to do
        m->lock.release();
        return;
    }

    // If waiter has higher priority (lower number), boost owner
    if (waiter->priority < owner->priority)
    {
        // Boost owner to waiter's priority
        owner->priority = waiter->priority;
        m->boosted_priority = waiter->priority;

        serial::puts("[pi] Boosting task '");
        serial::puts(owner->name);
        serial::puts("' priority to ");
        serial::put_dec(waiter->priority);
        serial::puts(" (waiter: ");
        serial::puts(waiter->name);
        serial::puts(")\n");
    }

    m->lock.release();
}

void unlock(PiMutex *m)
{
    if (!m || !m->initialized)
        return;

    m->lock.acquire();

    task::Task *cur = task::current();
    if (!cur || m->owner != cur)
    {
        // Not the owner, can't unlock
        m->lock.release();
        return;
    }

    // Restore original priority if it was boosted
    if (cur->priority != m->owner_original_priority)
    {
        serial::puts("[pi] Restoring task '");
        serial::puts(cur->name);
        serial::puts("' priority from ");
        serial::put_dec(cur->priority);
        serial::puts(" to ");
        serial::put_dec(m->owner_original_priority);
        serial::puts("\n");

        cur->priority = m->owner_original_priority;
    }

    m->owner = nullptr;
    m->owner_original_priority = 128;
    m->boosted_priority = 128;

    m->lock.release();
}

bool is_locked(PiMutex *m)
{
    if (!m || !m->initialized)
        return false;

    m->lock.acquire();
    bool locked = (m->owner != nullptr);
    m->lock.release();

    return locked;
}

task::Task *get_owner(PiMutex *m)
{
    if (!m || !m->initialized)
        return nullptr;

    m->lock.acquire();
    task::Task *owner = m->owner;
    m->lock.release();

    return owner;
}

void boost_priority(task::Task *t, u8 new_priority)
{
    if (!t)
        return;

    // Only boost if new priority is higher (lower number = higher priority)
    if (new_priority < t->priority)
    {
        t->priority = new_priority;
    }
}

void restore_priority(task::Task *t)
{
    if (!t)
        return;

    // Restore to default priority (128)
    // In a full implementation, we'd track the original priority per-mutex
    // For now, we use a simple restore to default
    t->priority = 128;
}

} // namespace pi
