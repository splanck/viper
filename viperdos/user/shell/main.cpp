//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief Standalone shell process for ViperDOS (Unix PTY model).
 *
 * This process implements the shell logic (command parsing, execution,
 * history navigation). It communicates with the terminal emulator (vshell)
 * via two kernel channels:
 *
 *   input_recv  — reads structured ShellInput messages (keys from terminal)
 *   output_send — writes raw text/ANSI bytes (output to terminal)
 *
 * The terminal emulator (vshell) handles all GUI rendering. This process
 * has no GUI dependencies — it is a pure text-mode shell.
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include "embedded_shell.hpp"
#include "keymap.hpp"
#include "shell_cmds.hpp"
#include "shell_io.hpp"

using namespace consoled;

// ============================================================================
// PTY Protocol
// ============================================================================

/// Input message from terminal emulator to shell.
struct ShellInput {
    uint8_t type;      // 0 = printable char, 1 = special key
    char ch;           // For type 0: the ASCII character
    uint16_t keycode;  // For type 1: raw keycode
    uint8_t modifiers; // For type 1: modifier flags
    uint8_t _pad[3];   // Pad to 8 bytes
};

// ============================================================================
// Global State
// ============================================================================

static int32_t g_input_recv = -1;
static int32_t g_output_send = -1;
static int32_t g_consoled_recv = -1; // Receive CON_WRITE from child processes

// CON_WRITE message type (from console_protocol.hpp)
static constexpr uint32_t CON_WRITE_TYPE = 0x1001;
// WriteRequest header is 16 bytes: type(4) + request_id(4) + length(4) + reserved(4)
static constexpr size_t WRITE_REQ_HEADER = 16;

// ============================================================================
// Bootstrap
// ============================================================================

/// Receive PTY channel handles from the terminal emulator via bootstrap.
static bool receive_bootstrap_channels() {
    // Bootstrap channel is at handle 0 (kernel convention)
    constexpr int32_t BOOTSTRAP_RECV = 0;

    uint8_t msg[8];
    uint32_t handles[4];
    uint32_t hcount = 4;

    // Wait for the bootstrap message (terminal sends channel handles)
    for (uint32_t attempt = 0; attempt < 2000; attempt++) {
        hcount = 4;
        int64_t n = sys::channel_recv(BOOTSTRAP_RECV, msg, sizeof(msg),
                                       handles, &hcount);
        if (n >= 0 && hcount >= 2) {
            // Got the channels
            g_input_recv = static_cast<int32_t>(handles[0]);
            g_output_send = static_cast<int32_t>(handles[1]);
            sys::channel_close(BOOTSTRAP_RECV);
            return true;
        }
        if (n == VERR_WOULD_BLOCK) {
            sys::yield();
            continue;
        }
        // Other error
        break;
    }

    return false;
}

// ============================================================================
// CONSOLED Service — proxy child process output to vshell
// ============================================================================

/// Register this shell as the CONSOLED service so child processes'
/// libc stdout/stderr routes through us to the terminal emulator.
static void register_consoled_service() {
    auto ch = sys::channel_create();
    if (ch.error != 0)
        return;

    int32_t send_ch = static_cast<int32_t>(ch.val0);
    g_consoled_recv = static_cast<int32_t>(ch.val1);

    int64_t r = sys::assign_set("CONSOLED", static_cast<uint32_t>(send_ch));
    if (r < 0) {
        // Another CONSOLED already registered — clean up
        sys::channel_close(send_ch);
        sys::channel_close(g_consoled_recv);
        g_consoled_recv = -1;
    }
}

/// Drain CON_WRITE messages from child processes and forward text to vshell.
static void drain_consoled_output() {
    if (g_consoled_recv < 0)
        return;

    uint8_t buf[4097]; // Extra byte for null terminator
    for (int i = 0; i < 32; i++) {
        uint32_t hcount = 0;
        int64_t n = sys::channel_recv(g_consoled_recv, buf, 4096, nullptr, &hcount);
        if (n <= 0)
            break;

        if (static_cast<size_t>(n) < WRITE_REQ_HEADER)
            continue; // Too small for WriteRequest header

        uint32_t msg_type = *reinterpret_cast<uint32_t *>(buf);
        if (msg_type == CON_WRITE_TYPE) {
            buf[n] = '\0'; // Null-terminate the text payload
            shell_print(reinterpret_cast<const char *>(buf + WRITE_REQ_HEADER));
        }
    }
}

// ============================================================================
// Main Entry Point
// ============================================================================

extern "C" int main() {
    sys::print("[shell] Starting...\n");

    // 1. Receive channel handles from terminal emulator
    if (!receive_bootstrap_channels()) {
        sys::print("[shell] ERROR: Failed to receive bootstrap channels\n");
        return 1;
    }
    sys::print("[shell] Bootstrap complete\n");

    // 2. Register as CONSOLED service so child processes' output routes here
    register_consoled_service();

    // 3. Initialize shell I/O to write output via channel
    shell_io_init_pty(g_output_send);

    // 4. Initialize shell in PTY mode (no TextBuffer/AnsiParser)
    EmbeddedShell shell;
    shell.init_pty();
    shell_set_instance(&shell);

    // 5. Print banner and initial prompt
    shell.print_banner();
    shell.print_prompt();

    sys::print("[shell] Ready\n");

    // 6. Main loop: read input from terminal, execute commands
    while (true) {
        // Drain child process output (CONSOLED service → PTY → vshell)
        drain_consoled_output();

        ShellInput input;
        uint32_t hcount = 0;
        int64_t n = sys::channel_recv(g_input_recv, &input, sizeof(input),
                                       nullptr, &hcount);

        if (n > 0) {
            if (shell.is_foreground()) {
                // Forward input to foreground child process
                if (input.type == 0 && input.ch != 0) {
                    shell.forward_to_foreground(input.ch);
                } else if (input.type == 1) {
                    shell.forward_special_key(input.keycode);
                }
            } else {
                // Handle shell input
                if (input.type == 1) {
                    shell.handle_special_key(input.keycode, input.modifiers);
                }
                if (input.type == 0 && input.ch != 0) {
                    shell.handle_char(input.ch);
                }
            }
        } else if (n == VERR_WOULD_BLOCK) {
            // No input — check foreground process
            if (shell.is_foreground()) {
                shell.check_foreground();
            }
            sys::sleep(2);
        } else {
            // Channel error (closed?) — exit
            break;
        }
    }

    sys::print("[shell] Exiting\n");
    return 0;
}
