#pragma once

#include "../include/types.hpp"

/**
 * @file spinlock.hpp
 * @brief Spinlock primitives for kernel synchronization (AArch64).
 *
 * @details
 * This header provides basic spinlock functionality for protecting shared
 * kernel data structures from concurrent access. The implementation uses
 * AArch64 load-exclusive/store-exclusive instructions (LDAXR/STXR) to ensure
 * atomicity.
 *
 * ## Usage
 *
 * ```cpp
 * Spinlock lock;
 *
 * void critical_section() {
 *     SpinlockGuard guard(lock);
 *     // ... protected code ...
 * }  // lock automatically released
 *
 * // Or manually:
 * lock.acquire();
 * // ... protected code ...
 * lock.release();
 * ```
 *
 * ## Interrupt Safety
 *
 * These spinlocks save and restore the interrupt state (DAIF register) to
 * prevent deadlock when an interrupt handler tries to acquire a lock held
 * by the interrupted code.
 *
 * ## Limitations
 *
 * - Non-recursive: acquiring a held lock will deadlock
 * - Not suitable for long critical sections
 * - No priority inheritance or fairness guarantees
 */

namespace kernel
{

/**
 * @brief Simple ticket spinlock for mutual exclusion.
 *
 * @details
 * Ticket locks provide fairness: threads acquire the lock in FIFO order.
 * This prevents starvation under contention.
 */
class Spinlock
{
  public:
    Spinlock() : next_ticket_(0), now_serving_(0), saved_daif_(0) {}

    // Non-copyable, non-movable
    Spinlock(const Spinlock &) = delete;
    Spinlock &operator=(const Spinlock &) = delete;

    /**
     * @brief Acquire the spinlock, spinning until available.
     *
     * @details
     * Disables interrupts before acquiring to prevent deadlock.
     * The interrupt state is saved and restored on release.
     */
    void acquire()
    {
        // Save and disable interrupts (prevent interrupt handler deadlock)
        u64 daif;
        asm volatile("mrs %0, daif" : "=r"(daif));
        asm volatile("msr daifset, #0xf" ::: "memory");
        saved_daif_ = daif;

        // Get our ticket number atomically
        u32 my_ticket;
        u32 new_ticket;
        u32 status;
        asm volatile("1: ldaxr   %w0, [%3]       \n" // Load-acquire exclusive
                     "   add     %w1, %w0, #1    \n" // Increment
                     "   stxr    %w2, %w1, [%3]  \n" // Store-exclusive (status in w2)
                     "   cbnz    %w2, 1b         \n" // Retry if failed
                     : "=&r"(my_ticket), "=&r"(new_ticket), "=&r"(status)
                     : "r"(&next_ticket_)
                     : "memory");

        // Spin until it's our turn
        while (true)
        {
            u32 serving;
            asm volatile("ldar %w0, [%1]" : "=r"(serving) : "r"(&now_serving_));
            if (serving == my_ticket)
            {
                break;
            }
            // Yield hint to save power while spinning
            asm volatile("yield");
        }
    }

    /**
     * @brief Release the spinlock.
     *
     * @details
     * Increments the "now serving" counter to allow the next waiter to proceed,
     * then restores the saved interrupt state.
     */
    void release()
    {
        // Increment now_serving atomically with release semantics
        u32 val;
        asm volatile("ldr    %w0, [%1]        \n"
                     "add    %w0, %w0, #1     \n"
                     "stlr   %w0, [%1]        \n"
                     : "=&r"(val)
                     : "r"(&now_serving_)
                     : "memory");

        // Restore interrupt state
        asm volatile("msr daif, %0" ::"r"(saved_daif_) : "memory");
    }

    /**
     * @brief Try to acquire the lock without spinning.
     *
     * @return true if lock was acquired, false if already held.
     */
    bool try_acquire()
    {
        // Save and disable interrupts
        u64 daif;
        asm volatile("mrs %0, daif" : "=r"(daif));
        asm volatile("msr daifset, #0xf" ::: "memory");

        // Check if lock is available (next == serving)
        u32 next, serving;
        asm volatile("ldar %w0, [%1]" : "=r"(next) : "r"(&next_ticket_));
        asm volatile("ldar %w0, [%1]" : "=r"(serving) : "r"(&now_serving_));

        if (next != serving)
        {
            // Lock is held, restore interrupts and return false
            asm volatile("msr daif, %0" ::"r"(daif) : "memory");
            return false;
        }

        // Try to get the ticket
        u32 current;
        u32 new_val;
        u32 status;

        asm volatile("ldaxr   %w0, [%4]       \n"
                     "cmp     %w0, %w3        \n"
                     "b.ne    1f              \n"
                     "add     %w1, %w0, #1    \n"
                     "stxr    %w2, %w1, [%4]  \n"
                     "cbnz    %w2, 1f         \n"
                     "mov     %w2, #0         \n" // Success - use status reg for result
                     "b       2f              \n"
                     "1:                      \n"
                     "mov     %w2, #1         \n" // Failure
                     "2:                      \n"
                     : "=&r"(current), "=&r"(new_val), "=&r"(status)
                     : "r"(next), "r"(&next_ticket_)
                     : "memory", "cc");

        if (status != 0)
        {
            // Failed to acquire, restore interrupts
            asm volatile("msr daif, %0" ::"r"(daif) : "memory");
            return false;
        }

        saved_daif_ = daif;
        return true;
    }

    /**
     * @brief Check if the lock is currently held.
     *
     * @note This is only useful for debugging/assertions, not for
     * synchronization decisions (TOCTOU race).
     */
    bool is_locked() const
    {
        u32 next, serving;
        asm volatile("ldr %w0, [%1]" : "=r"(next) : "r"(&next_ticket_));
        asm volatile("ldr %w0, [%1]" : "=r"(serving) : "r"(&now_serving_));
        return next != serving;
    }

  private:
    volatile u32 next_ticket_; ///< Next ticket to be handed out
    volatile u32 now_serving_; ///< Current ticket being served
    u64 saved_daif_;           ///< Saved interrupt state
};

/**
 * @brief RAII guard for automatic spinlock acquire/release.
 *
 * @details
 * Acquires the lock on construction and releases on destruction,
 * ensuring the lock is always released even on early return or exception.
 */
class SpinlockGuard
{
  public:
    /**
     * @brief Construct guard and acquire lock.
     * @param lock The spinlock to guard.
     */
    explicit SpinlockGuard(Spinlock &lock) : lock_(lock)
    {
        lock_.acquire();
    }

    /**
     * @brief Destruct guard and release lock.
     */
    ~SpinlockGuard()
    {
        lock_.release();
    }

    // Non-copyable, non-movable
    SpinlockGuard(const SpinlockGuard &) = delete;
    SpinlockGuard &operator=(const SpinlockGuard &) = delete;

  private:
    Spinlock &lock_;
};

/**
 * @brief Simple atomic flag for lightweight synchronization.
 *
 * @details
 * A single-bit lock useful for simple cases where ticket fairness
 * isn't needed. More efficient than Spinlock for very short critical sections.
 */
class AtomicFlag
{
  public:
    AtomicFlag() : flag_(0) {}

    /**
     * @brief Test and set the flag atomically.
     * @return Previous value (false if we acquired, true if already set).
     */
    bool test_and_set()
    {
        u32 old_val;
        u32 status;
        asm volatile("1: ldaxr   %w0, [%3]       \n"
                     "   stxr    %w1, %w2, [%3]  \n"
                     "   cbnz    %w1, 1b         \n"
                     : "=&r"(old_val), "=&r"(status)
                     : "r"(1), "r"(&flag_)
                     : "memory");
        return old_val != 0;
    }

    /**
     * @brief Clear the flag.
     */
    void clear()
    {
        asm volatile("stlr wzr, [%0]" ::"r"(&flag_) : "memory");
    }

    /**
     * @brief Spin until flag is clear, then set it.
     */
    void acquire()
    {
        while (test_and_set())
        {
            asm volatile("yield");
        }
    }

    /**
     * @brief Release by clearing the flag.
     */
    void release()
    {
        clear();
    }

  private:
    volatile u32 flag_;
};

} // namespace kernel

// Export to global namespace for convenience
using kernel::AtomicFlag;
using kernel::Spinlock;
using kernel::SpinlockGuard;
