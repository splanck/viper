//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ansi.hpp"
#include "text_buffer.hpp" // Includes gui.h which has stdint types

namespace consoled {

// =============================================================================
// ShellManager Class
// =============================================================================

/**
 * @brief Manages the shell process for a console instance.
 *
 * Each consoled spawns and manages its own shell (vinit) process with
 * private I/O channels. This enables independent multi-window support.
 */
class ShellManager {
  public:
    ShellManager() = default;
    ~ShellManager();

    /**
     * @brief Spawn a shell process with private I/O channels.
     * @return true on success, false on failure
     */
    bool spawn();

    /**
     * @brief Send keyboard input to the shell process.
     * @param ch ASCII character (0 for special keys)
     * @param keycode Raw keycode (for special keys)
     * @param modifiers Key modifiers
     */
    void send_input(char ch, uint16_t keycode, uint8_t modifiers);

    /**
     * @brief Poll for output from the shell process.
     * @param parser ANSI parser to process output
     */
    void poll_output(AnsiParser &parser);

    /**
     * @brief Close shell channels and clean up.
     */
    void close();

    // Accessors
    bool has_shell() const {
        return m_shell_pid >= 0;
    }

    int64_t shell_pid() const {
        return m_shell_pid;
    }

    int32_t input_channel() const {
        return m_input_send;
    }

    int32_t output_channel() const {
        return m_output_recv;
    }

  private:
    int64_t m_shell_pid = -1;   // PID of child shell process
    int32_t m_input_send = -1;  // Channel to send input to shell
    int32_t m_output_recv = -1; // Channel to receive output from shell
};

// =============================================================================
// Legacy Local Shell (fallback for secondary instances)
// =============================================================================

/**
 * @brief Legacy interactive shell for when no shell process is available.
 *
 * Provides basic command processing for secondary instances that
 * cannot spawn their own shell process.
 */
class LocalShell {
  public:
    LocalShell() = default;

    /**
     * @brief Initialize with output buffer and parser.
     */
    void init(TextBuffer *buffer, AnsiParser *parser);

    /**
     * @brief Handle a keyboard character.
     * @param c ASCII character
     */
    void handle_input(char c);

    /**
     * @brief Print the command prompt.
     */
    void print_prompt();

  private:
    void handle_command(const char *cmd, size_t len);
    int64_t spawn_program(const char *path);

    TextBuffer *m_buffer = nullptr;
    AnsiParser *m_parser = nullptr;

    static constexpr size_t INPUT_BUF_SIZE = 256;
    char m_input_buf[INPUT_BUF_SIZE] = {};
    size_t m_input_len = 0;
};

} // namespace consoled
