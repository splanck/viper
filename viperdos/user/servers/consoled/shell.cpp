//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "shell.hpp"
#include "../../syscall.hpp"
#include "console_protocol.hpp"

using namespace console_protocol;

namespace consoled {

// =============================================================================
// Debug Output
// =============================================================================

static void debug_print(const char *msg) {
    sys::print(msg);
}

static void debug_print_dec(uint64_t val) {
    if (val == 0) {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// =============================================================================
// ShellManager Implementation
// =============================================================================

ShellManager::~ShellManager() {
    close();
}

bool ShellManager::spawn() {
    static uint32_t spawn_count = 0;
    spawn_count++;
    debug_print("[consoled] Spawning shell #");
    debug_print_dec(spawn_count);
    debug_print("...\n");

    // Create input channel pair (consoled sends -> shell receives)
    auto input_ch = sys::channel_create();
    if (input_ch.error != 0) {
        debug_print("[consoled] Failed to create input channel\n");
        return false;
    }
    int32_t input_send = static_cast<int32_t>(input_ch.val0);
    int32_t input_recv = static_cast<int32_t>(input_ch.val1);

    // Create output channel pair (shell sends -> consoled receives)
    auto output_ch = sys::channel_create();
    if (output_ch.error != 0) {
        debug_print("[consoled] Failed to create output channel\n");
        sys::channel_close(input_send);
        sys::channel_close(input_recv);
        return false;
    }
    int32_t output_send = static_cast<int32_t>(output_ch.val0);
    int32_t output_recv = static_cast<int32_t>(output_ch.val1);

    // Spawn vinit (shell) and get bootstrap send handle
    uint64_t pid = 0;
    uint64_t tid = 0;
    uint32_t bootstrap_send = 0xFFFFFFFF;
    int64_t err = sys::spawn("/sys/vinit.sys", nullptr, &pid, &tid, nullptr, &bootstrap_send);

    if (err != 0 || bootstrap_send == 0xFFFFFFFF) {
        debug_print("[consoled] Failed to spawn vinit: ");
        debug_print_dec(static_cast<uint64_t>(-err));
        debug_print("\n");
        sys::channel_close(input_send);
        sys::channel_close(input_recv);
        sys::channel_close(output_send);
        sys::channel_close(output_recv);
        return false;
    }

    // Send the channel handles to vinit via bootstrap
    // Shell receives: input_recv (for keyboard input) and output_send (for console output)
    uint32_t handles[2] = {static_cast<uint32_t>(input_recv), static_cast<uint32_t>(output_send)};
    uint8_t dummy = 0;

    bool sent = false;
    for (uint32_t i = 0; i < 100; i++) {
        int64_t send_err =
            sys::channel_send(static_cast<int32_t>(bootstrap_send), &dummy, 1, handles, 2);
        if (send_err == 0) {
            sent = true;
            break;
        }
        if (send_err == VERR_WOULD_BLOCK) {
            sys::yield();
            continue;
        }
        break; // Other error
    }

    sys::channel_close(static_cast<int32_t>(bootstrap_send));

    if (!sent) {
        debug_print("[consoled] Failed to send bootstrap to shell\n");
        sys::channel_close(input_send);
        sys::channel_close(input_recv);
        sys::channel_close(output_send);
        sys::channel_close(output_recv);
        return false;
    }

    // Store the channels we keep (send to input, receive from output)
    m_shell_pid = static_cast<int64_t>(pid);
    m_input_send = input_send;
    m_output_recv = output_recv;

    // Debug: log channel handles
    debug_print("[consoled] spawn: output_send=");
    debug_print_dec(static_cast<uint64_t>(output_send));
    debug_print(" output_recv=");
    debug_print_dec(static_cast<uint64_t>(output_recv));
    debug_print("\n");

    // NOTE: We don't close input_recv and output_send - we passed them to shell

    debug_print("[consoled] Shell #");
    debug_print_dec(spawn_count);
    debug_print(" spawned (pid ");
    debug_print_dec(pid);
    debug_print("), bootstrap sent OK\n");

    return true;
}

void ShellManager::send_input(char ch, uint16_t keycode, uint8_t modifiers) {
    if (m_input_send < 0)
        return;

    // InputEvent structure matching console_protocol.hpp
    struct {
        uint32_t type;
        char ch;
        uint8_t pressed;
        uint16_t keycode;
        uint8_t modifiers;
        uint8_t _pad[3];
    } event;

    event.type = CON_INPUT;
    event.ch = ch;
    event.pressed = 1;
    event.keycode = keycode;
    event.modifiers = modifiers;
    event._pad[0] = event._pad[1] = event._pad[2] = 0;

    // Send non-blocking (drop if full)
    sys::channel_send(m_input_send, &event, sizeof(event), nullptr, 0);
}

bool ShellManager::poll_output(AnsiParser &parser) {
    if (m_output_recv < 0)
        return false;

    // Debug counters
    static uint32_t recv_success_count = 0;
    static uint64_t last_status_log = 0;
    static uint32_t poll_count = 0;
    poll_count++;

    bool got_any = false;
    constexpr int MAX_DRAIN = 8;

    for (int batch = 0; batch < MAX_DRAIN; batch++) {
        uint8_t buf[4096];
        uint32_t handles[4];
        uint32_t handle_count = 4;

        int64_t n = sys::channel_recv(m_output_recv, buf, sizeof(buf), handles, &handle_count);

        if (n <= 0 || n < static_cast<int64_t>(sizeof(uint32_t))) {
            // Debug: log periodic status during startup
            if (!got_any && batch == 0) {
                uint64_t now = sys::uptime();
                if (recv_success_count == 0 && now - last_status_log >= 1000) {
                    last_status_log = now;
                    debug_print("[consoled] poll_output waiting: ch=");
                    debug_print_dec(static_cast<uint64_t>(m_output_recv));
                    debug_print(" polls=");
                    debug_print_dec(poll_count);
                    debug_print(" time=");
                    debug_print_dec(now);
                    debug_print("ms\n");
                }
            }
            break;
        }

        got_any = true;

        // Debug: log first few successful receives
        if (recv_success_count < 3) {
            recv_success_count++;
            uint64_t now = sys::uptime();
            debug_print("[consoled] poll_output SUCCESS #");
            debug_print_dec(recv_success_count);
            debug_print(" ch=");
            debug_print_dec(static_cast<uint64_t>(m_output_recv));
            debug_print(" n=");
            debug_print_dec(static_cast<uint64_t>(n));
            debug_print(" polls=");
            debug_print_dec(poll_count);
            debug_print(" time=");
            debug_print_dec(now);
            debug_print("ms\n");
        }

        uint32_t msg_type = *reinterpret_cast<uint32_t *>(buf);

        if (msg_type == CON_WRITE) {
            struct {
                uint32_t type;
                uint32_t request_id;
                uint32_t length;
                uint32_t reserved;
            } *req = reinterpret_cast<decltype(req)>(buf);

            const char *text = reinterpret_cast<const char *>(buf + 16);
            size_t text_len = static_cast<size_t>(n) - 16;
            if (text_len > req->length)
                text_len = req->length;

            parser.write(text, text_len);
        }

        // Close any handles received
        for (uint32_t i = 0; i < handle_count; i++) {
            if (handles[i] != 0xFFFFFFFF) {
                sys::channel_close(static_cast<int32_t>(handles[i]));
            }
        }
    }

    return got_any;
}

void ShellManager::close() {
    if (m_input_send >= 0) {
        sys::channel_close(m_input_send);
        m_input_send = -1;
    }
    if (m_output_recv >= 0) {
        sys::channel_close(m_output_recv);
        m_output_recv = -1;
    }
    m_shell_pid = -1;
}

// =============================================================================
// LocalShell Implementation (Legacy Fallback)
// =============================================================================

void LocalShell::init(TextBuffer *buffer, AnsiParser *parser) {
    m_buffer = buffer;
    m_parser = parser;
}

void LocalShell::print_prompt() {
    const char *prompt = "> ";
    m_parser->write(prompt, 2);
}

int64_t LocalShell::spawn_program(const char *path) {
    uint64_t pid = 0, tid = 0;
    int64_t result;

    __asm__ volatile("mov x0, %[path]\n\t"
                     "mov x1, xzr\n\t"   // name = NULL
                     "mov x2, xzr\n\t"   // args = NULL
                     "mov x8, #0x05\n\t" // SYS_TASK_SPAWN
                     "svc #0\n\t"
                     "mov %[result], x0\n\t"
                     "mov %[pid], x1\n\t"
                     "mov %[tid], x2\n\t"
                     : [result] "=r"(result), [pid] "=r"(pid), [tid] "=r"(tid)
                     : [path] "r"(path)
                     : "x0", "x1", "x2", "x8", "memory");

    if (result == 0) {
        return static_cast<int64_t>(pid);
    }
    return result; // Error code (negative)
}

static bool str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++)
            return false;
    }
    return true;
}

static bool str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a++ != *b++)
            return false;
    }
    return *a == *b;
}

void LocalShell::handle_command(const char *cmd, size_t len) {
    // Skip leading whitespace
    while (len > 0 && (*cmd == ' ' || *cmd == '\t')) {
        cmd++;
        len--;
    }

    // Skip trailing whitespace
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) {
        len--;
    }

    if (len == 0) {
        print_prompt();
        return;
    }

    // Built-in commands
    if (str_equal(cmd, "clear") || str_equal(cmd, "cls")) {
        m_buffer->clear();
        m_buffer->set_cursor(0, 0);
        m_buffer->redraw_all();
        print_prompt();
        return;
    }

    if (str_equal(cmd, "exit") || str_equal(cmd, "quit")) {
        sys::exit(0);
    }

    if (str_equal(cmd, "help") || str_equal(cmd, "?")) {
        m_parser->write("Commands:\n", 10);
        m_parser->write("  clear     - Clear screen\n", 27);
        m_parser->write("  exit      - Close this console\n", 33);
        m_parser->write("  help      - Show this help\n", 29);
        m_parser->write("  run PATH  - Run a program\n", 28);
        m_parser->write("  /sys/X    - Run /sys/X directly\n", 34);
        m_parser->write("  /c/X      - Run /c/X directly\n", 32);
        print_prompt();
        return;
    }

    // Run command
    const char *path = nullptr;
    char path_buf[128];

    if (str_starts_with(cmd, "run ")) {
        path = cmd + 4;
        while (*path == ' ')
            path++;
    } else if (cmd[0] == '/') {
        // Direct path
        path = cmd;
    } else {
        // Try as program name in /c/
        size_t i = 0;
        path_buf[i++] = '/';
        path_buf[i++] = 'c';
        path_buf[i++] = '/';
        for (size_t j = 0; j < len && i < 120; j++) {
            path_buf[i++] = cmd[j];
        }
        // Add .prg extension if not present
        if (i > 4 && path_buf[i - 4] != '.') {
            path_buf[i++] = '.';
            path_buf[i++] = 'p';
            path_buf[i++] = 'r';
            path_buf[i++] = 'g';
        }
        path_buf[i] = '\0';
        path = path_buf;
    }

    if (path && *path) {
        m_parser->write("Launching: ", 11);
        for (const char *p = path; *p; p++)
            m_parser->write(p, 1);
        m_parser->write("\n", 1);

        int64_t result = spawn_program(path);
        if (result < 0) {
            m_parser->write("Error: Failed to spawn (", 24);
            char num[16];
            int64_t v = -result;
            int ni = 15;
            num[ni] = '\0';
            do {
                num[--ni] = '0' + (v % 10);
                v /= 10;
            } while (v > 0);
            m_parser->write(&num[ni], 16 - ni);
            m_parser->write(")\n", 2);
        } else {
            m_parser->write("Started (pid ", 13);
            char num[16];
            int64_t v = result;
            int ni = 15;
            num[ni] = '\0';
            do {
                num[--ni] = '0' + (v % 10);
                v /= 10;
            } while (v > 0);
            m_parser->write(&num[ni], 16 - ni);
            m_parser->write(")\n", 2);
        }
    } else {
        m_parser->write("Unknown command: ", 17);
        m_parser->write(cmd, len);
        m_parser->write("\n", 1);
    }

    print_prompt();
}

void LocalShell::handle_input(char c) {
    if (c == '\r' || c == '\n') {
        // Enter pressed - process command
        m_parser->write("\n", 1);
        m_input_buf[m_input_len] = '\0';
        handle_command(m_input_buf, m_input_len);
        m_input_len = 0;
    } else if (c == '\b') {
        // Backspace
        if (m_input_len > 0) {
            m_input_len--;
            m_parser->write("\b", 1); // Moves cursor back and clears
        }
    } else if (c >= 0x20 && c < 0x7F) {
        // Printable character
        if (m_input_len < INPUT_BUF_SIZE - 1) {
            m_input_buf[m_input_len++] = c;
            char str[2] = {c, '\0'};
            m_parser->write(str, 1);
        }
    }
}

} // namespace consoled
