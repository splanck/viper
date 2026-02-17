//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief VShell - Terminal emulator for ViperDOS (Unix PTY model).
 *
 * Pure terminal emulator following the proven VEdit event loop pattern:
 *   gui_poll_event -> process -> render -> gui_present -> yield
 *
 * Communicates with a separate shell process (shell.prg) via two channels:
 *   input_send  - sends structured ShellInput messages (keys to shell)
 *   output_recv - reads raw text/ANSI bytes (output from shell)
 *
 * The shell process handles all command logic. This process handles all
 * GUI rendering. Clean separation, just like Unix terminals.
 */
//===----------------------------------------------------------------------===//

#include "../../include/viper_colors.h"
#include "../../syscall.hpp"
#include "ansi.hpp"
#include "keymap.hpp"
#include "text_buffer.hpp"
#include <gui.h>

using namespace consoled;

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

namespace {
constexpr uint32_t DEFAULT_FG = VIPER_COLOR_TEXT;
constexpr uint32_t DEFAULT_BG = VIPER_COLOR_CONSOLE_BG;
} // namespace

//===----------------------------------------------------------------------===//
// PTY Protocol (must match shell/main.cpp)
//===----------------------------------------------------------------------===//

/// Input message from terminal emulator to shell.
struct ShellInput {
    uint8_t type;      // 0 = printable char, 1 = special key
    char ch;           // For type 0: the ASCII character
    uint16_t keycode;  // For type 1: raw keycode
    uint8_t modifiers; // For type 1: modifier flags
    uint8_t _pad[3];   // Pad to 8 bytes
};

//===----------------------------------------------------------------------===//
// TerminalApp - VEdit-style GUI application
//===----------------------------------------------------------------------===//

class TerminalApp {
  public:
    TerminalApp()
        : m_window(nullptr), m_winWidth(0), m_winHeight(0),
          m_inputSend(-1), m_outputRecv(-1), m_running(false) {}

    bool init() {
        sys::print("[vshell] Starting terminal emulator...\n");

        if (!waitForDisplayd()) {
            sys::print("[vshell] ERROR: displayd not found\n");
            return false;
        }

        if (gui_init() != 0) {
            sys::print("[vshell] Failed to init GUI\n");
            return false;
        }

        if (!createWindow()) {
            return false;
        }

        if (!initTextBuffer()) {
            return false;
        }

        if (!spawnShell()) {
            sys::print("[vshell] Failed to spawn shell process\n");
            return false;
        }

        // Initial present - show window immediately
        gui_present(m_window);

        sys::print("[vshell] Ready.\n");
        return true;
    }

    void run() {
        m_running = true;

        while (m_running) {
            // 1. Poll GUI events
            gui_event_t event;
            if (gui_poll_event(m_window, &event) == 0) {
                processGuiEvent(event);
            }

            // 2. Read shell output (non-blocking)
            drainShellOutput();

            // 3. Always present — avoids timing windows where content
            //    is rendered to SHM but never composited to screen
            gui_present(m_window);

            // 4. Sleep 5ms (~100Hz) — gives shell process CPU time
            sys::sleep(5);
        }
    }

    void shutdown() {
        // Close channel handles
        if (m_inputSend >= 0)
            sys::channel_close(m_inputSend);
        if (m_outputRecv >= 0)
            sys::channel_close(m_outputRecv);

        if (m_window) {
            gui_destroy_window(m_window);
            m_window = nullptr;
        }
    }

  private:
    // GUI state
    gui_window_t *m_window;
    uint32_t m_winWidth;
    uint32_t m_winHeight;

    // Console rendering
    TextBuffer m_textBuffer;
    AnsiParser m_ansiParser;

    // PTY channels
    int32_t m_inputSend;   // Send keys to shell
    int32_t m_outputRecv;  // Receive output from shell

    bool m_running;

    //=== Initialization helpers =============================================

    bool waitForDisplayd() {
        for (uint32_t attempt = 0; attempt < 100; attempt++) {
            uint32_t handle = 0xFFFFFFFF;
            int64_t result = sys::assign_get("DISPLAY", &handle);
            if (result == 0 && handle != 0xFFFFFFFF) {
                sys::channel_close(static_cast<int32_t>(handle));
                return true;
            }
            sys::sleep(10);
        }
        return false;
    }

    bool createWindow() {
        gui_display_info_t display;
        if (gui_get_display_info(&display) != 0) {
            sys::print("[vshell] Failed to get display info\n");
            return false;
        }

        m_winWidth = (display.width * 70) / 100;
        m_winHeight = (display.height * 60) / 100;

        m_window = gui_create_window("Shell", m_winWidth, m_winHeight);
        if (!m_window) {
            sys::print("[vshell] Failed to create window\n");
            return false;
        }

        gui_set_position(m_window, 20, 20);
        return true;
    }

    bool initTextBuffer() {
        uint32_t cols = (m_winWidth - 2 * PADDING) / FONT_WIDTH;
        uint32_t rows = (m_winHeight - 2 * PADDING) / FONT_HEIGHT;

        if (!m_textBuffer.init(m_window, cols, rows, DEFAULT_FG, DEFAULT_BG)) {
            sys::print("[vshell] Failed to allocate text buffer\n");
            return false;
        }

        m_ansiParser.init(&m_textBuffer, DEFAULT_FG, DEFAULT_BG);

        // Fill background
        gui_fill_rect(m_window, 0, 0, m_winWidth, m_winHeight, DEFAULT_BG);
        m_textBuffer.redraw_all();

        return true;
    }

    bool spawnShell() {
        // Create input channel (terminal -> shell)
        auto inputCh = sys::channel_create();
        if (inputCh.error != 0)
            return false;
        m_inputSend = static_cast<int32_t>(inputCh.val0);  // Keep send end
        uint32_t inputRecv = static_cast<uint32_t>(inputCh.val1);

        // Create output channel (shell -> terminal)
        auto outputCh = sys::channel_create();
        if (outputCh.error != 0) {
            sys::channel_close(m_inputSend);
            sys::channel_close(static_cast<int32_t>(inputRecv));
            m_inputSend = -1;
            return false;
        }
        m_outputRecv = static_cast<int32_t>(outputCh.val1); // Keep recv end
        uint32_t outputSend = static_cast<uint32_t>(outputCh.val0);

        // Spawn shell process
        uint32_t bootstrapSend = 0xFFFFFFFF;
        int64_t err = sys::spawn("/c/shell.prg", "shell",
                                  nullptr, nullptr, nullptr, &bootstrapSend);
        if (err != 0) {
            sys::print("[vshell] spawn failed: ");
            // Clean up channels
            sys::channel_close(m_inputSend);
            sys::channel_close(static_cast<int32_t>(inputRecv));
            sys::channel_close(m_outputRecv);
            sys::channel_close(static_cast<int32_t>(outputSend));
            m_inputSend = -1;
            m_outputRecv = -1;
            return false;
        }

        // Send [input_recv, output_send] to shell via bootstrap channel
        uint8_t dummy = 0;
        uint32_t handles[2] = {inputRecv, outputSend};
        int64_t send_err = sys::channel_send(
            static_cast<int32_t>(bootstrapSend), &dummy, 1, handles, 2);
        if (send_err != 0) {
            sys::print("[vshell] ERROR: Bootstrap send failed\n");
        }
        sys::channel_close(static_cast<int32_t>(bootstrapSend));

        // Note: inputRecv and outputSend handles are now transferred to the
        // shell process (channel_send transfers ownership). We only keep
        // m_inputSend and m_outputRecv.

        sys::print("[vshell] Shell process spawned\n");
        return true;
    }

    //=== Event processing ===================================================

    /// Process a GUI event. Returns true if the screen needs to be presented.
    bool processGuiEvent(const gui_event_t &event) {
        if (event.type == GUI_EVENT_KEY && event.key.pressed) {
            // Convert keypress to ShellInput and send to shell
            ShellInput input = {};
            char c = keycode_to_ascii(event.key.keycode, event.key.modifiers);

            if (c != 0) {
                input.type = 0;
                input.ch = c;
            } else {
                input.type = 1;
                input.keycode = event.key.keycode;
                input.modifiers = event.key.modifiers;
            }

            sys::channel_send(m_inputSend, &input, sizeof(input),
                              nullptr, 0);

            // Don't present yet - wait for shell echo via output channel
            return false;
        }

        if (event.type == GUI_EVENT_CLOSE) {
            m_running = false;
            return false;
        }

        // Any other event (focus, resize, etc.) — present to refresh display
        return true;
    }

    /// Read output from shell process and render it. Returns true if any
    /// output was received.
    bool drainShellOutput() {
        bool didWork = false;

        // Read up to 32 messages per loop iteration
        for (int i = 0; i < 32; i++) {
            uint8_t buf[4096];
            uint32_t hcount = 0;
            int64_t n = sys::channel_recv(m_outputRecv, buf, sizeof(buf),
                                           nullptr, &hcount);
            if (n <= 0)
                break;

            didWork = true;

            // Feed raw bytes to AnsiParser -> TextBuffer -> pixels
            m_ansiParser.write(reinterpret_cast<const char *>(buf),
                               static_cast<size_t>(n));
        }

        return didWork;
    }
};

//===----------------------------------------------------------------------===//
// Entry Point - uses main() for full CRT initialization (like VEdit)
//===----------------------------------------------------------------------===//

extern "C" int main() {
    TerminalApp app;
    if (!app.init()) {
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
