//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/pi.hpp
// Purpose: Priority inheritance mutex support.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"
#include "../lib/spinlock.hpp"
#include "task.hpp"

namespace pi
{

/**
 * @brief Priority inheritance mutex.
 *
 * @details
 * A mutex that implements priority inheritance to prevent priority inversion.
 * When a high-priority task blocks on a mutex held by a low-priority task,
 * the low-priority task temporarily inherits the high priority.
 */
struct PiMutex
{
    Spinlock lock;              ///< Protects mutex state
    task::Task *owner;          ///< Current owner (nullptr if unlocked)
    u8 owner_original_priority; ///< Owner's priority before any boost
    u8 boosted_priority;        ///< Current boosted priority (if any)
    bool initialized;           ///< True if mutex is valid
};

/**
 * @brief Initialize a PI mutex.
 *
 * @param m Mutex to initialize.
 */
void init_mutex(PiMutex *m);

/**
 * @brief Try to acquire a PI mutex without blocking.
 *
 * @param m Mutex to acquire.
 * @return true if acquired, false if already held.
 */
bool try_lock(PiMutex *m);

/**
 * @brief Handle contention on a PI mutex.
 *
 * @details
 * Called when a task fails to acquire a mutex because it's held by another task.
 * If the waiting task has higher priority than the owner, the owner's priority
 * is boosted to prevent priority inversion.
 *
 * @param m Mutex being contended.
 * @param waiter Task that is waiting for the mutex.
 */
void contend(PiMutex *m, task::Task *waiter);

/**
 * @brief Release a PI mutex.
 *
 * @details
 * Restores the owner's original priority if it was boosted.
 *
 * @param m Mutex to release.
 */
void unlock(PiMutex *m);

/**
 * @brief Check if mutex is locked.
 *
 * @param m Mutex to check.
 * @return true if locked.
 */
bool is_locked(PiMutex *m);

/**
 * @brief Get the current owner of a mutex.
 *
 * @param m Mutex to query.
 * @return Owner task, or nullptr if unlocked.
 */
task::Task *get_owner(PiMutex *m);

/**
 * @brief Boost a task's priority for priority inheritance.
 *
 * @details
 * Called when a higher-priority task is waiting on a mutex held by this task.
 *
 * @param t Task to boost.
 * @param new_priority Priority to boost to.
 */
void boost_priority(task::Task *t, u8 new_priority);

/**
 * @brief Restore a task's original priority after PI boost.
 *
 * @param t Task to restore.
 */
void restore_priority(task::Task *t);

} // namespace pi
