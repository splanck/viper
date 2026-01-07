#pragma once

#include "../include/types.hpp"

/**
 * @file tty.hpp
 * @brief Kernel TTY buffer for text-mode input.
 *
 * @details
 * Provides a simple kernel buffer for console input. consoled pushes keyboard
 * characters into the buffer, and clients read them via blocking syscalls.
 * This eliminates the need for complex IPC channel handoffs between processes.
 */
namespace tty
{

/**
 * @brief Initialize the TTY subsystem.
 */
void init();

/**
 * @brief Read characters from TTY input buffer.
 *
 * @details
 * Blocks the calling task until at least one character is available.
 * Returns immediately if buffer has data.
 *
 * @param buf Destination buffer.
 * @param size Maximum bytes to read.
 * @return Number of bytes read, or negative error code.
 */
i64 read(void *buf, u32 size);

/**
 * @brief Write characters to TTY output.
 *
 * @details
 * Currently forwards to serial output. In the future could be used for
 * kernel-level output buffering.
 *
 * @param buf Source buffer.
 * @param size Number of bytes to write.
 * @return Number of bytes written, or negative error code.
 */
i64 write(const void *buf, u32 size);

/**
 * @brief Check if TTY has input available.
 *
 * @return true if at least one character is available.
 */
bool has_input();

/**
 * @brief Push a character into the TTY input buffer.
 *
 * @details
 * Called by consoled when it receives keyboard input. Wakes any tasks
 * blocked in read().
 *
 * @param c Character to push.
 */
void push_input(char c);

} // namespace tty
