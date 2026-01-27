//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "tty.hpp"
#include "../console/gcon.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../sched/wait.hpp"

/**
 * @file tty.cpp
 * @brief Kernel TTY buffer implementation.
 *
 * @details
 * Simple ring buffer with blocking read support. consoled pushes chars,
 * clients read them via syscall. No IPC complexity needed.
 */
namespace tty {

// Input buffer (ring buffer)
constexpr u32 INPUT_BUFFER_SIZE = 256;
static char input_buffer[INPUT_BUFFER_SIZE];
static u32 input_head = 0; // Next write position
static u32 input_tail = 0; // Next read position
static u32 input_count = 0;

// Spinlock for buffer access
static Spinlock tty_lock;

// Wait queue for blocked readers
static sched::WaitQueue read_waiters;

void init() {
    serial::puts("[tty] Initializing TTY subsystem\n");

    input_head = 0;
    input_tail = 0;
    input_count = 0;

    sched::wait_init(&read_waiters);

    serial::puts("[tty] TTY subsystem initialized\n");
}

bool has_input() {
    SpinlockGuard guard(tty_lock);
    return input_count > 0;
}

void push_input(char c) {
    SpinlockGuard guard(tty_lock);

    // Add to buffer if space available
    if (input_count < INPUT_BUFFER_SIZE) {
        input_buffer[input_head] = c;
        input_head = (input_head + 1) % INPUT_BUFFER_SIZE;
        input_count++;
    }
    // else: buffer full, drop character (could log this)

    // Wake one blocked reader
    sched::wait_wake_one(&read_waiters);
}

i64 read(void *buf, u32 size) {
    if (!buf || size == 0) {
        return -1; // VERR_INVALID_ARG
    }

    char *dest = static_cast<char *>(buf);
    u32 bytes_read = 0;

    while (bytes_read < size) {
        // Try to get characters from buffer
        {
            SpinlockGuard guard(tty_lock);

            while (input_count > 0 && bytes_read < size) {
                dest[bytes_read++] = input_buffer[input_tail];
                input_tail = (input_tail + 1) % INPUT_BUFFER_SIZE;
                input_count--;
            }
        }

        // If we got at least one character, return
        if (bytes_read > 0) {
            return static_cast<i64>(bytes_read);
        }

        // Buffer empty - block until input arrives
        task::Task *current = task::current();
        if (!current) {
            // No current task (shouldn't happen in normal operation)
            return -1;
        }

        // Add to wait queue and yield
        {
            SpinlockGuard guard(tty_lock);

            // Double-check: maybe input arrived while we were setting up
            if (input_count > 0) {
                continue; // Go back and read it
            }

            // Block
            sched::wait_enqueue(&read_waiters, current);
        }

        // Yield to scheduler - will return when woken by push_input
        task::yield();

        // After waking, loop back and try to read again
    }

    return static_cast<i64>(bytes_read);
}

i64 write(const void *buf, u32 size) {
    if (!buf || size == 0) {
        return 0;
    }

    // Write directly to framebuffer via gcon (bypasses GUI mode check).
    // Serial output removed - it was causing significant slowdown due to
    // UART FIFO wait loops. Debug output still goes to serial via serial::puts.
    const char *src = static_cast<const char *>(buf);
    for (u32 i = 0; i < size; i++) {
        gcon::putc_force(src[i]);
    }

    return static_cast<i64>(size);
}

} // namespace tty
