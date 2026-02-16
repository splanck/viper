//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file embedded_shell.cpp
 * @brief Embedded shell implementation for consoled.
 *
 * Handles character input, command dispatch, history navigation,
 * and cursor movement. Commands execute in-process, writing directly
 * to the TextBuffer via AnsiParser.
 */
//===----------------------------------------------------------------------===//

#include "embedded_shell.hpp"
#include "../../syscall.hpp"
#include "keymap.hpp"
#include "shell_cmds.hpp"
#include "shell_io.hpp"

namespace consoled {

void EmbeddedShell::init(TextBuffer *buffer, AnsiParser *parser) {
    m_buffer = buffer;
    m_parser = parser;
}

void EmbeddedShell::print_banner() {
    shell_print("ViperDOS Shell\n\n");
}

void EmbeddedShell::print_prompt() {
    const char *dir = shell_current_dir();

    // Build prompt: "SYS:/path> "
    shell_print("SYS:");
    shell_print(dir);
    shell_print("> ");

    // Calculate prompt length for cursor tracking
    m_prompt_len = 4; // "SYS:"
    const char *p = dir;
    while (*p) {
        m_prompt_len++;
        p++;
    }
    m_prompt_len += 2; // "> "
}

void EmbeddedShell::handle_char(char c) {
    m_command_ran = false;

    if (c == '\r' || c == '\n') {
        // Submit command
        shell_print("\n");
        m_input_buf[m_input_len] = '\0';

        if (m_input_len > 0) {
            history_add(m_input_buf);
            m_buffer->begin_batch();
            execute_command();
            m_command_ran = true;
        }

        m_input_len = 0;
        m_cursor_pos = 0;
        m_browsing_history = false;

        // Don't print prompt if we just entered foreground mode
        if (!is_foreground()) {
            print_prompt();
        }
        m_buffer->end_batch();
        return;
    }

    if (c == '\b' || c == 127) {
        // Backspace
        if (m_cursor_pos > 0) {
            // Shift buffer left
            for (size_t i = m_cursor_pos - 1; i < m_input_len - 1; i++) {
                m_input_buf[i] = m_input_buf[i + 1];
            }
            m_input_len--;
            m_cursor_pos--;

            // Redraw from cursor position
            clear_input_line();
            redraw_input_line();
        }
        return;
    }

    if (c == 0x15) {
        // Ctrl+U — clear entire line
        clear_input_line();
        m_input_len = 0;
        m_cursor_pos = 0;
        redraw_input_line();
        return;
    }

    if (c == 0x0B) {
        // Ctrl+K — kill to end of line
        clear_input_line();
        m_input_len = m_cursor_pos;
        m_input_buf[m_input_len] = '\0';
        redraw_input_line();
        return;
    }

    // Printable character
    if (c >= 0x20 && c <= 0x7E) {
        if (m_input_len >= INPUT_BUF_SIZE - 1)
            return; // Buffer full

        // Insert at cursor position
        for (size_t i = m_input_len; i > m_cursor_pos; i--) {
            m_input_buf[i] = m_input_buf[i - 1];
        }
        m_input_buf[m_cursor_pos] = c;
        m_input_len++;
        m_cursor_pos++;

        if (m_cursor_pos == m_input_len) {
            // Appending at end — just echo the character
            shell_print_char(c);
        } else {
            // Inserted in middle — redraw from cursor
            clear_input_line();
            redraw_input_line();
        }
    }
}

void EmbeddedShell::handle_special_key(uint16_t keycode, uint8_t /*modifiers*/) {
    m_command_ran = false;

    switch (keycode) {
        case KEY_UP:
            history_navigate(-1);
            break;

        case KEY_DOWN:
            history_navigate(1);
            break;

        case KEY_LEFT:
            if (m_cursor_pos > 0) {
                m_cursor_pos--;
                m_buffer->move_cursor(-1, 0);
            }
            break;

        case KEY_RIGHT:
            if (m_cursor_pos < m_input_len) {
                m_cursor_pos++;
                m_buffer->move_cursor(1, 0);
            }
            break;

        case KEY_HOME:
            if (m_cursor_pos > 0) {
                int32_t delta = -static_cast<int32_t>(m_cursor_pos);
                m_cursor_pos = 0;
                m_buffer->move_cursor(delta, 0);
            }
            break;

        case KEY_END:
            if (m_cursor_pos < m_input_len) {
                int32_t delta = static_cast<int32_t>(m_input_len - m_cursor_pos);
                m_cursor_pos = m_input_len;
                m_buffer->move_cursor(delta, 0);
            }
            break;

        case KEY_DELETE:
            if (m_cursor_pos < m_input_len) {
                for (size_t i = m_cursor_pos; i < m_input_len - 1; i++) {
                    m_input_buf[i] = m_input_buf[i + 1];
                }
                m_input_len--;
                clear_input_line();
                redraw_input_line();
            }
            break;

        default:
            break;
    }
}

void EmbeddedShell::clear_input_line() {
    // Jump cursor directly to start of input area and clear to end of line
    m_buffer->set_cursor(m_prompt_len, m_buffer->cursor_y());
    m_buffer->clear_to_eol();
}

void EmbeddedShell::redraw_input_line() {
    // Print the entire input buffer
    for (size_t i = 0; i < m_input_len; i++) {
        shell_print_char(m_input_buf[i]);
    }

    // Position cursor directly instead of \b loop
    if (m_cursor_pos < m_input_len) {
        m_buffer->set_cursor(
            static_cast<uint32_t>(m_prompt_len + m_cursor_pos), m_buffer->cursor_y());
    }
}

void EmbeddedShell::execute_command() {
    // Skip leading whitespace
    const char *cmd = m_input_buf;
    while (*cmd == ' ')
        cmd++;

    if (*cmd == '\0')
        return;

    // Find end of command word
    const char *cmd_end = cmd;
    while (*cmd_end && *cmd_end != ' ')
        cmd_end++;

    // Extract args (skip spaces after command)
    const char *args = cmd_end;
    while (*args == ' ')
        args++;
    if (*args == '\0')
        args = nullptr;

    // Command dispatch (case-insensitive)
    if (shell_strcaseeq(cmd, "help") ||
        (cmd_end - cmd == 4 && shell_strcasestart(cmd, "help"))) {
        cmd_help();
    } else if (shell_strcaseeq(cmd, "cls") ||
               (cmd_end - cmd == 3 && shell_strcasestart(cmd, "cls"))) {
        cmd_clear();
    } else if (shell_strcasestart(cmd, "echo") && (cmd[4] == ' ' || cmd[4] == '\0')) {
        cmd_echo(args);
    } else if (shell_strcaseeq(cmd, "version") ||
               (cmd_end - cmd == 7 && shell_strcasestart(cmd, "version"))) {
        cmd_version();
    } else if (shell_strcaseeq(cmd, "uptime") ||
               (cmd_end - cmd == 6 && shell_strcasestart(cmd, "uptime"))) {
        cmd_uptime();
    } else if (shell_strcaseeq(cmd, "why") ||
               (cmd_end - cmd == 3 && shell_strcasestart(cmd, "why"))) {
        cmd_why();
    } else if (shell_strcasestart(cmd, "cd") && (cmd[2] == ' ' || cmd[2] == '\0')) {
        cmd_cd(args);
    } else if (shell_strcasestart(cmd, "chdir") && (cmd[5] == ' ' || cmd[5] == '\0')) {
        cmd_cd(args);
    } else if (shell_strcaseeq(cmd, "pwd") || shell_strcaseeq(cmd, "cwd") ||
               (cmd_end - cmd == 3 && (shell_strcasestart(cmd, "pwd") ||
                                        shell_strcasestart(cmd, "cwd")))) {
        cmd_pwd();
    } else if (shell_strcasestart(cmd, "dir") && (cmd[3] == ' ' || cmd[3] == '\0')) {
        cmd_dir(args);
    } else if (shell_strcasestart(cmd, "list") && (cmd[4] == ' ' || cmd[4] == '\0')) {
        cmd_list(args);
    } else if (shell_strcasestart(cmd, "type") && (cmd[4] == ' ' || cmd[4] == '\0')) {
        cmd_type(args);
    } else if (shell_strcasestart(cmd, "copy") && (cmd[4] == ' ' || cmd[4] == '\0')) {
        cmd_copy(args);
    } else if (shell_strcasestart(cmd, "delete") && (cmd[6] == ' ' || cmd[6] == '\0')) {
        cmd_delete(args);
    } else if (shell_strcasestart(cmd, "makedir") && (cmd[7] == ' ' || cmd[7] == '\0')) {
        cmd_makedir(args);
    } else if (shell_strcasestart(cmd, "rename") && (cmd[6] == ' ' || cmd[6] == '\0')) {
        cmd_rename(args);
    } else if (shell_strcasestart(cmd, "run") && (cmd[3] == ' ' || cmd[3] == '\0')) {
        cmd_run(args);
    } else {
        // Unknown command — print the command word only
        shell_print("Unknown command: ");
        for (const char *p = cmd; p < cmd_end; p++) {
            shell_print_char(*p);
        }
        shell_print("\nType 'Help' for available commands.\n");
    }
}

void EmbeddedShell::history_add(const char *line) {
    if (!line || line[0] == '\0')
        return;

    // Deduplicate: don't add if same as last entry
    if (m_history_count > 0) {
        size_t last_idx = (m_history_index + HISTORY_SIZE - 1) % HISTORY_SIZE;
        if (shell_streq(m_history[last_idx], line))
            return;
    }

    // Copy into circular buffer
    shell_strcpy(m_history[m_history_index], line, HISTORY_LINE_LEN);
    m_history_index = (m_history_index + 1) % HISTORY_SIZE;
    if (m_history_count < HISTORY_SIZE)
        m_history_count++;

    m_browsing_history = false;
}

void EmbeddedShell::history_navigate(int direction) {
    if (m_history_count == 0)
        return;

    if (!m_browsing_history) {
        // Start browsing from most recent
        m_history_browse = m_history_index;
        m_browsing_history = true;
    }

    if (direction < 0) {
        // Navigate backwards (older)
        size_t prev = (m_history_browse + HISTORY_SIZE - 1) % HISTORY_SIZE;
        // Check if we've gone past the oldest entry
        size_t oldest = (m_history_count < HISTORY_SIZE)
                            ? 0
                            : m_history_index;
        if (m_history_browse == oldest)
            return; // Already at oldest

        m_history_browse = prev;
    } else {
        // Navigate forwards (newer)
        if (m_history_browse == m_history_index) {
            // Already at newest — clear input
            clear_input_line();
            m_input_len = 0;
            m_cursor_pos = 0;
            m_input_buf[0] = '\0';
            m_browsing_history = false;
            return;
        }
        m_history_browse = (m_history_browse + 1) % HISTORY_SIZE;
        if (m_history_browse == m_history_index) {
            // Went past newest — clear input
            clear_input_line();
            m_input_len = 0;
            m_cursor_pos = 0;
            m_input_buf[0] = '\0';
            m_browsing_history = false;
            return;
        }
    }

    // Replace current input with history entry
    clear_input_line();
    shell_strcpy(m_input_buf, m_history[m_history_browse], INPUT_BUF_SIZE);
    m_input_len = shell_strlen(m_input_buf);
    m_cursor_pos = m_input_len;
    redraw_input_line();
}

void EmbeddedShell::enter_foreground(uint64_t pid, uint64_t task_id) {
    m_fg_pid = pid;
    m_fg_task_id = task_id;
    // Don't print prompt — we're in foreground mode now
}

bool EmbeddedShell::check_foreground() {
    if (m_fg_pid == 0)
        return false;

    int32_t status = 0;
    int64_t result = sys::waitpid_nohang(m_fg_pid, &status);

    if (result > 0) {
        // Child exited normally
        m_fg_pid = 0;
        m_fg_task_id = 0;
        shell_print("\n");
        print_prompt();
        m_buffer->end_batch();
        return true;
    }
    if (result < 0) {
        // Error (e.g., child already gone or not our child)
        m_fg_pid = 0;
        m_fg_task_id = 0;
        shell_print("\n");
        print_prompt();
        m_buffer->end_batch();
        return true;
    }
    // result == 0: child still running
    return false;
}

void EmbeddedShell::forward_to_foreground(char c) {
    if (m_fg_pid == 0)
        return;

    // Ctrl+C: kill the foreground process
    if (c == 0x03) {
        shell_print("^C\n");
        sys::kill(m_fg_task_id, 9); // SIGKILL
        // Don't clear fg state here; check_foreground will detect exit
        return;
    }

    // Convert '\r' (from keymap Enter) to '\n' for kernel TTY
    if (c == '\r')
        c = '\n';

    sys::tty_push_input(c);
}

void EmbeddedShell::forward_special_key(uint16_t keycode) {
    if (m_fg_pid == 0)
        return;

    const char *seq = nullptr;
    switch (keycode) {
        case KEY_UP:
            seq = "\033[A";
            break;
        case KEY_DOWN:
            seq = "\033[B";
            break;
        case KEY_RIGHT:
            seq = "\033[C";
            break;
        case KEY_LEFT:
            seq = "\033[D";
            break;
        case KEY_HOME:
            seq = "\033[H";
            break;
        case KEY_END:
            seq = "\033[F";
            break;
        case KEY_DELETE:
            seq = "\033[3~";
            break;
        case KEY_PAGEUP:
            seq = "\033[5~";
            break;
        case KEY_PAGEDOWN:
            seq = "\033[6~";
            break;
        default:
            return;
    }

    while (*seq) {
        sys::tty_push_input(*seq++);
    }
}

} // namespace consoled
