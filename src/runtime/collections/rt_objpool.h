//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_objpool.h
/// @brief Object pool for efficient object reuse.
///
/// Provides a fixed-size pool of integer slots that can be acquired and
/// released efficiently, avoiding allocation churn for frequently
/// created/destroyed game objects like bullets, enemies, and particles.
///
/// Key invariants: Pool capacity is fixed at creation and cannot exceed
///     RT_OBJPOOL_MAX. Slot indices are stable across acquire/release cycles.
/// Ownership/Lifetime: Caller owns the pool handle; destroy with
///     rt_objpool_destroy(). Slots are logically owned by the caller while
///     acquired.
/// Links: Viper.ObjectPool standard library module.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_OBJPOOL_H
#define VIPER_RT_OBJPOOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Maximum pool size.
#define RT_OBJPOOL_MAX 4096

    /// Opaque handle to an ObjectPool instance.
    typedef struct rt_objpool_impl *rt_objpool;

    /// @brief Create a new ObjectPool.
    /// @param capacity Maximum number of objects (up to RT_OBJPOOL_MAX).
    /// @return A new ObjectPool instance.
    rt_objpool rt_objpool_new(int64_t capacity);

    /// @brief Destroy an ObjectPool and free its memory.
    /// @param pool The pool to destroy.
    void rt_objpool_destroy(rt_objpool pool);

    /// @brief Acquire a slot from the pool.
    /// @param pool The pool.
    /// @return Slot index (0 to capacity-1), or -1 if pool is full.
    int64_t rt_objpool_acquire(rt_objpool pool);

    /// @brief Release a slot back to the pool.
    /// @param pool The pool.
    /// @param slot Slot index to release.
    /// @return 1 on success, 0 if invalid slot.
    int8_t rt_objpool_release(rt_objpool pool, int64_t slot);

    /// @brief Check if a slot is currently active (acquired).
    /// @param pool The pool.
    /// @param slot Slot index.
    /// @return 1 if active, 0 if free or invalid.
    int8_t rt_objpool_is_active(rt_objpool pool, int64_t slot);

    /// @brief Get the number of active (acquired) slots.
    /// @param pool The pool.
    /// @return Number of active slots.
    int64_t rt_objpool_active_count(rt_objpool pool);

    /// @brief Get the number of free (available) slots.
    /// @param pool The pool.
    /// @return Number of free slots.
    int64_t rt_objpool_free_count(rt_objpool pool);

    /// @brief Get the total capacity.
    /// @param pool The pool.
    /// @return Total capacity as specified at creation.
    int64_t rt_objpool_capacity(rt_objpool pool);

    /// @brief Check if the pool is full (no free slots).
    /// @param pool The pool.
    /// @return 1 if full, 0 otherwise.
    int8_t rt_objpool_is_full(rt_objpool pool);

    /// @brief Check if the pool is empty (all slots free).
    /// @param pool The pool.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_objpool_is_empty(rt_objpool pool);

    /// @brief Release all slots back to the pool.
    /// @param pool The pool.
    void rt_objpool_clear(rt_objpool pool);

    /// @brief Get the first active slot index (for iteration).
    /// @param pool The pool.
    /// @return First active slot, or -1 if none.
    int64_t rt_objpool_first_active(rt_objpool pool);

    /// @brief Get the next active slot after the given index.
    /// @param pool The pool.
    /// @param after Slot index to search after.
    /// @return Next active slot, or -1 if none.
    int64_t rt_objpool_next_active(rt_objpool pool, int64_t after);

    /// @brief Associate user data with a slot.
    /// @param pool The pool.
    /// @param slot Slot index.
    /// @param data User data (64-bit value).
    /// @return 1 on success, 0 if invalid slot.
    int8_t rt_objpool_set_data(rt_objpool pool, int64_t slot, int64_t data);

    /// @brief Get user data associated with a slot.
    /// @param pool The pool.
    /// @param slot Slot index.
    /// @return User data, or 0 if invalid slot.
    int64_t rt_objpool_get_data(rt_objpool pool, int64_t slot);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_OBJPOOL_H
