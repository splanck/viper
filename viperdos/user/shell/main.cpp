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

    // 2. Initialize shell I/O to write output via channel
    shell_io_init_pty(g_output_send);

    // 3. Initialize shell in PTY mode (no TextBuffer/AnsiParser)
    EmbeddedShell shell;
    shell.init_pty();
    shell_set_instance(&shell);

    // 4. Print banner and initial prompt
    shell.print_banner();
    shell.print_prompt();

    sys::print("[shell] Ready\n");

    // 5. Main loop: read input from terminal, execute commands
    while (true) {
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
