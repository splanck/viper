//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ansi.hpp"
#include "text_buffer.hpp"
#include <stddef.h>
#include <stdint.h>

namespace consoled {

/// Embedded shell that runs commands in-process, writing directly to
/// the TextBuffer via AnsiParser. Eliminates the IPC pipeline that
/// caused the shell prompt visibility bug.
class EmbeddedShell {
  public:
    void init(TextBuffer *buffer, AnsiParser *parser);

    /// Handle a printable character, Enter, Backspace, or control char.
    void handle_char(char c);

    /// Handle special keys (arrows, Home, End, Delete).
    void handle_special_key(uint16_t keycode, uint8_t modifiers);

    /// Print the shell prompt ("SYS:/path> ").
    void print_prompt();

    /// Print the startup banner.
    void print_banner();

    /// Returns true after a command was executed (cleared on next input).
    bool command_just_ran() const {
        return m_command_ran;
    }

    /// Returns true if a foreground process is running.
    bool is_foreground() const { return m_fg_pid != 0; }

    /// Check if foreground process has exited (non-blocking).
    /// Returns true if foreground mode ended.
    bool check_foreground();

    /// Forward a keyboard character to the foreground process via kernel TTY.
    void forward_to_foreground(char c);

    /// Forward a special key (arrow, home, etc.) as ANSI escape sequence to foreground.
    void forward_special_key(uint16_t keycode);

    /// Enter foreground mode for a spawned child process.
    void enter_foreground(uint64_t pid, uint64_t task_id);

  private:
    void execute_command();
    void clear_input_line();
    void redraw_input_line();
    void history_add(const char *line);
    void history_navigate(int direction);

    AnsiParser *m_parser = nullptr;
    TextBuffer *m_buffer = nullptr;

    // Input state
    static constexpr size_t INPUT_BUF_SIZE = 512;
    char m_input_buf[INPUT_BUF_SIZE] = {};
    size_t m_input_len = 0;
    size_t m_cursor_pos = 0;
    bool m_command_ran = false;

    // Prompt tracking (to know how far back to erase)
    size_t m_prompt_len = 0;

    // History
    static constexpr size_t HISTORY_SIZE = 16;
    static constexpr size_t HISTORY_LINE_LEN = 256;
    char m_history[HISTORY_SIZE][HISTORY_LINE_LEN] = {};
    size_t m_history_count = 0;
    size_t m_history_index = 0; // Write index (circular)
    size_t m_history_browse = 0;
    bool m_browsing_history = false;

    // Foreground process state
    uint64_t m_fg_pid = 0;     // Child viper ID (0 = no foreground process)
    uint64_t m_fg_task_id = 0; // Child task ID (for kill)
};

} // namespace consoled
