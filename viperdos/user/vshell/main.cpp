//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief VShell — standalone GUI shell for ViperDOS.
 *
 * A brand-new .prg shell program that follows the proven VEdit pattern:
 * - extern "C" int main()  — full CRT initialization via crt0
 * - gui_present()           — synchronous present (guaranteed compositing)
 * - gui_poll_event() + yield — cooperative event loop
 *
 * Reuses consoled's proven components (TextBuffer, AnsiParser, EmbeddedShell,
 * keymap, shell_cmds, shell_io, RequestHandler) without duplicating them.
 */
//===----------------------------------------------------------------------===//

#include "../../include/viper_colors.h"
#include "../../syscall.hpp"
#include "ansi.hpp"
#include "console_protocol.hpp"
#include "embedded_shell.hpp"
#include "keymap.hpp"
#include "request.hpp"
#include "shell_cmds.hpp"
#include "shell_io.hpp"
#include "text_buffer.hpp"
#include <gui.h>

using namespace console_protocol;
using namespace consoled;

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

namespace {
constexpr uint32_t DEFAULT_FG = VIPER_COLOR_TEXT;
constexpr uint32_t DEFAULT_BG = VIPER_COLOR_CONSOLE_BG;
} // namespace

//===----------------------------------------------------------------------===//
// ShellApp — VEdit-style GUI application
//===----------------------------------------------------------------------===//

class ShellApp {
  public:
    ShellApp()
        : m_window(nullptr), m_winWidth(0), m_winHeight(0), m_serviceChannel(-1),
          m_isPrimary(false), m_running(false) {}

    bool init() {
        sys::print("[vshell] Starting...\n");

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

        if (!initComponents()) {
            return false;
        }

        registerService();
        sys::print("[vshell] Ready.\n");
        return true;
    }

    void run() {
        m_running = true;

        while (m_running) {
            bool needsPresent = false;

            // 1. Process GUI events (drain all pending)
            gui_event_t event;
            if (gui_poll_event(m_window, &event) == 0) {
                needsPresent |= processEvent(event);
            }

            // 2. Drain IPC messages from child processes (non-blocking)
            if (m_isPrimary && m_serviceChannel >= 0) {
                needsPresent |= processIpc();
            }

            // 3. Check foreground process exit (non-blocking)
            if (m_shell.is_foreground()) {
                bool ended = m_shell.check_foreground();
                needsPresent |= ended;
            }

            // 4. Present SYNCHRONOUSLY if anything changed — the key fix
            if (needsPresent || m_textBuffer.needs_present()) {
                gui_present(m_window);
                m_textBuffer.clear_needs_present();
            }

            // 5. Yield CPU — identical to VEdit and all working GUI apps
            __asm__ volatile("mov x8, #0x00\n\tsvc #0" ::: "x8");
        }
    }

    void shutdown() {
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

    // Console components
    TextBuffer m_textBuffer;
    AnsiParser m_ansiParser;
    EmbeddedShell m_shell;
    RequestHandler m_requestHandler;

    // Service state
    int32_t m_serviceChannel;
    bool m_isPrimary;
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

        // Check for existing consoled/vshell instance
        uint32_t existingHandle = 0xFFFFFFFF;
        bool shellExists =
            (sys::assign_get("CONSOLED", &existingHandle) == 0 && existingHandle != 0xFFFFFFFF);
        if (shellExists) {
            sys::channel_close(static_cast<int32_t>(existingHandle));
        }

        // Build window title
        char windowTitle[32] = "Shell";
        if (shellExists) {
            char *p = windowTitle + 5;
            *p++ = ' ';
            *p++ = '#';
            uint32_t id = sys::uptime() % 1000;
            char digits[4];
            int di = 0;
            do {
                digits[di++] = '0' + (id % 10);
                id /= 10;
            } while (id > 0 && di < 4);
            while (di > 0)
                *p++ = digits[--di];
            *p = '\0';
        }

        m_window = gui_create_window(windowTitle, m_winWidth, m_winHeight);
        if (!m_window) {
            sys::print("[vshell] Failed to create window\n");
            return false;
        }

        int32_t winX = 20 + (shellExists ? 40 : 0);
        int32_t winY = 20 + (shellExists ? 40 : 0);
        gui_set_position(m_window, winX, winY);
        return true;
    }

    bool initComponents() {
        uint32_t cols = (m_winWidth - 2 * PADDING) / FONT_WIDTH;
        uint32_t rows = (m_winHeight - 2 * PADDING) / FONT_HEIGHT;

        if (!m_textBuffer.init(m_window, cols, rows, DEFAULT_FG, DEFAULT_BG)) {
            sys::print("[vshell] Failed to allocate text buffer\n");
            return false;
        }

        m_ansiParser.init(&m_textBuffer, DEFAULT_FG, DEFAULT_BG);
        m_requestHandler.init(&m_textBuffer, &m_ansiParser);

        shell_io_init(&m_ansiParser, &m_textBuffer, m_window);
        m_shell.init(&m_textBuffer, &m_ansiParser);
        shell_set_instance(&m_shell);

        // Fill background and draw initial content
        gui_fill_rect(m_window, 0, 0, m_winWidth, m_winHeight, DEFAULT_BG);

        m_shell.print_banner();
        m_shell.print_prompt();

        m_textBuffer.redraw_all();
        gui_present(m_window);
        gui_request_focus(m_window);

        return true;
    }

    void registerService() {
        auto chResult = sys::channel_create();
        if (chResult.error != 0) {
            return;
        }

        int32_t sendCh = static_cast<int32_t>(chResult.val0);
        int32_t recvCh = static_cast<int32_t>(chResult.val1);
        m_serviceChannel = recvCh;

        int64_t assignResult = sys::assign_set("CONSOLED", sendCh);

        if (assignResult < 0) {
            m_isPrimary = false;
            sys::channel_close(sendCh);
            sys::channel_close(recvCh);
            m_serviceChannel = -1;
        } else {
            m_isPrimary = true;
        }
    }

    //=== Event processing ===================================================

    /// Process a GUI event. Returns true if the screen needs to be presented.
    bool processEvent(const gui_event_t &event) {
        if (event.type == GUI_EVENT_KEY && event.key.pressed) {
            char c = keycode_to_ascii(event.key.keycode, event.key.modifiers);

            if (m_shell.is_foreground()) {
                if (c != 0) {
                    m_shell.forward_to_foreground(c);
                } else {
                    m_shell.forward_special_key(event.key.keycode);
                }
            } else {
                m_shell.handle_special_key(event.key.keycode, event.key.modifiers);
                if (c != 0) {
                    m_shell.handle_char(c);
                }
            }
            return true;
        } else if (event.type == GUI_EVENT_CLOSE) {
            m_running = false;
            return false;
        }
        return false;
    }

    /// Drain IPC messages. Returns true if any messages were processed.
    bool processIpc() {
        uint8_t msgBuf[MAX_PAYLOAD];
        uint32_t handles[4];
        bool didWork = false;

        for (uint32_t i = 0; i < 64; i++) {
            uint32_t handleCount = 4;
            int64_t n = sys::channel_recv(
                m_serviceChannel, msgBuf, sizeof(msgBuf), handles, &handleCount);

            if (n <= 0)
                break;

            didWork = true;

            int32_t clientCh = (handleCount > 0) ? static_cast<int32_t>(handles[0]) : -1;
            m_requestHandler.handle(
                clientCh, msgBuf, static_cast<size_t>(n), handles, handleCount);

            for (uint32_t j = 0; j < handleCount; j++) {
                if (handles[j] != 0xFFFFFFFF) {
                    sys::channel_close(static_cast<int32_t>(handles[j]));
                }
            }
        }
        return didWork;
    }
};

//===----------------------------------------------------------------------===//
// Entry Point — uses main() for full CRT initialization (like VEdit)
//===----------------------------------------------------------------------===//

extern "C" int main() {
    ShellApp app;
    if (!app.init()) {
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
