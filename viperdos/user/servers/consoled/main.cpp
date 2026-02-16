//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief Console server (consoled) main entry point.
 *
 * Uses an embedded shell that runs commands in-process, writing directly
 * to the TextBuffer via AnsiParser. This matches the architecture of every
 * other working GUI app in ViperDOS (direct drawing + synchronous present).
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
constexpr uint64_t FRAME_INTERVAL_MS = 16;
constexpr uint32_t MAX_MESSAGES_PER_BATCH = 256;
} // namespace

//===----------------------------------------------------------------------===//
// Debug utilities
//===----------------------------------------------------------------------===//

class Debug {
  public:
    static void print(const char *msg) {
        sys::print(msg);
    }

    static void printDec(uint64_t val) {
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
};

//===----------------------------------------------------------------------===//
// BSS Initialization
//===----------------------------------------------------------------------===//

extern "C" char __bss_start[];
extern "C" char __bss_end[];

static void clearBss() {
    for (char *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
}

//===----------------------------------------------------------------------===//
// ConsoleServer - Main server class
//===----------------------------------------------------------------------===//

class ConsoleServer {
  public:
    ConsoleServer()
        : m_window(nullptr), m_windowWidth(0), m_windowHeight(0), m_serviceChannel(-1),
          m_isPrimary(false), m_instanceId(0), m_lastPresentTime(0) {}

    bool init() {
        sys::print("\033[0m"); // Reset console colors
        Debug::print("[consoled] Starting console server (GUI mode)...\n");

        if (!receiveBootstrapCaps()) {
            // Not fatal - continue anyway
        }

        if (!waitForDisplayd()) {
            Debug::print("[consoled] ERROR: displayd not found after 1 second\n");
            return false;
        }

        if (!initGui()) {
            return false;
        }

        if (!createWindow()) {
            return false;
        }

        if (!initComponents()) {
            return false;
        }

        registerService();

        return true;
    }

    void run() {
        uint8_t msgBuf[MAX_PAYLOAD];
        uint32_t handles[4];
        gui_event_t event;

        m_lastPresentTime = sys::uptime();

        while (true) {
            bool didWork = false;

            // 1. Check foreground process exit (non-blocking)
            if (m_shell.is_foreground()) {
                if (m_shell.check_foreground()) {
                    didWork = true;
                }
            }

            // 2. Process IPC messages (child process output via CONSOLED service)
            if (m_isPrimary && m_serviceChannel >= 0) {
                didWork |= processIpcMessages(msgBuf, handles);
            }

            // 3. Process GUI events (keyboard input)
            while (gui_poll_event(m_window, &event) == 0) {
                didWork = true;
                if (!handleEvent(event)) {
                    return; // Window closed
                }
            }

            // 4. Present with frame rate limiting (AFTER events, synchronous)
            uint64_t now = sys::uptime();
            if (m_textBuffer.needs_present() && (now - m_lastPresentTime) >= FRAME_INTERVAL_MS) {
                gui_present(m_window);
                m_textBuffer.clear_needs_present();
                m_lastPresentTime = now;
            }

            // 5. Sleep if no work
            if (!didWork) {
                if (m_textBuffer.needs_present()) {
                    // Need to present soon — sleep until next frame
                    uint64_t elapsed = now - m_lastPresentTime;
                    if (elapsed < FRAME_INTERVAL_MS) {
                        sys::sleep(FRAME_INTERVAL_MS - elapsed);
                    }
                } else {
                    sys::sleep(5); // Brief sleep to let other tasks run
                }
            }
        }
    }

  private:
    // GUI state
    gui_window_t *m_window;
    uint32_t m_windowWidth;
    uint32_t m_windowHeight;

    // Console components
    TextBuffer m_textBuffer;
    AnsiParser m_ansiParser;
    EmbeddedShell m_shell;
    RequestHandler m_requestHandler;

    // Service state
    int32_t m_serviceChannel;
    bool m_isPrimary;
    uint32_t m_instanceId;
    uint64_t m_lastPresentTime;

    bool receiveBootstrapCaps() {
        Debug::print("[consoled] Checking bootstrap channel...\n");

        constexpr int32_t BOOTSTRAP_RECV = 0;
        uint8_t dummy[1];
        uint32_t handles[4];
        uint32_t handleCount = 4;

        for (uint32_t i = 0; i < 50; i++) {
            handleCount = 4;
            int64_t n =
                sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handleCount);
            if (n >= 0) {
                Debug::print("[consoled] Received bootstrap caps\n");
                sys::channel_close(BOOTSTRAP_RECV);
                return true;
            }
            if (n != VERR_WOULD_BLOCK) {
                Debug::print("[consoled] No bootstrap channel (secondary instance)\n");
                return false;
            }
            sys::yield();
        }
        return false;
    }

    bool waitForDisplayd() {
        Debug::print("[consoled] Waiting for displayd...\n");

        for (uint32_t attempt = 0; attempt < 100; attempt++) {
            uint32_t handle = 0xFFFFFFFF;
            int64_t result = sys::assign_get("DISPLAY", &handle);
            if (result == 0 && handle != 0xFFFFFFFF) {
                sys::channel_close(static_cast<int32_t>(handle));
                Debug::print("[consoled] Found displayd after ");
                Debug::printDec(attempt);
                Debug::print(" attempts\n");
                return true;
            }
            sys::sleep(10);
        }
        return false;
    }

    bool initGui() {
        Debug::print("[consoled] Initializing GUI...\n");
        if (gui_init() != 0) {
            Debug::print("[consoled] Failed to initialize GUI library\n");
            return false;
        }
        Debug::print("[consoled] GUI initialized\n");
        return true;
    }

    bool createWindow() {
        gui_display_info_t display;
        if (gui_get_display_info(&display) != 0) {
            Debug::print("[consoled] Failed to get display info\n");
            return false;
        }

        Debug::print("[consoled] Display: ");
        Debug::printDec(display.width);
        Debug::print("x");
        Debug::printDec(display.height);
        Debug::print("\n");

        m_windowWidth = (display.width * 70) / 100;
        m_windowHeight = (display.height * 60) / 100;

        uint32_t cols = (m_windowWidth - 2 * PADDING) / FONT_WIDTH;
        uint32_t rows = (m_windowHeight - 2 * PADDING) / FONT_HEIGHT;

        Debug::print("[consoled] Console: ");
        Debug::printDec(cols);
        Debug::print(" cols x ");
        Debug::printDec(rows);
        Debug::print(" rows\n");

        // Check for existing consoled instance
        uint32_t existingHandle = 0xFFFFFFFF;
        bool consoledExists =
            (sys::assign_get("CONSOLED", &existingHandle) == 0 && existingHandle != 0xFFFFFFFF);
        if (consoledExists) {
            sys::channel_close(static_cast<int32_t>(existingHandle));
            m_instanceId = sys::uptime() % 1000;
        }

        // Build window title
        char windowTitle[32] = "Console";
        if (consoledExists) {
            char *p = windowTitle + 7;
            *p++ = ' ';
            *p++ = '#';
            uint32_t id = m_instanceId;
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

        Debug::print("[consoled] Creating window: ");
        Debug::print(windowTitle);
        Debug::print("\n");

        m_window = gui_create_window(windowTitle, m_windowWidth, m_windowHeight);
        if (!m_window) {
            Debug::print("[consoled] Failed to create console window\n");
            return false;
        }

        int32_t winX = 20 + (consoledExists ? 40 : 0);
        int32_t winY = 20 + (consoledExists ? 40 : 0);
        gui_set_position(m_window, winX, winY);

        Debug::print("[consoled] Window created successfully\n");
        return true;
    }

    bool initComponents() {
        uint32_t cols = (m_windowWidth - 2 * PADDING) / FONT_WIDTH;
        uint32_t rows = (m_windowHeight - 2 * PADDING) / FONT_HEIGHT;

        if (!m_textBuffer.init(m_window, cols, rows, DEFAULT_FG, DEFAULT_BG)) {
            Debug::print("[consoled] Failed to allocate text buffer\n");
            return false;
        }

        m_ansiParser.init(&m_textBuffer, DEFAULT_FG, DEFAULT_BG);
        m_requestHandler.init(&m_textBuffer, &m_ansiParser);

        // Initialize embedded shell I/O and print banner + prompt
        shell_io_init(&m_ansiParser, &m_textBuffer, m_window);
        m_shell.init(&m_textBuffer, &m_ansiParser);
        shell_set_instance(&m_shell);

        // Fill background and draw initial content
        gui_fill_rect(m_window, 0, 0, m_windowWidth, m_windowHeight, DEFAULT_BG);

        // Print banner and prompt (writes to text buffer via AnsiParser)
        m_shell.print_banner();
        m_shell.print_prompt();

        // Redraw with prompt content and present synchronously
        m_textBuffer.redraw_all();
        gui_present(m_window); // Synchronous — prompt visible immediately

        // Ensure we have keyboard focus (critical when launched from workbench)
        gui_request_focus(m_window);

        Debug::print("[consoled] Shell initialized, prompt displayed\n");
        return true;
    }

    void registerService() {
        auto chResult = sys::channel_create();
        if (chResult.error != 0) {
            Debug::print("[consoled] Failed to create service channel\n");
            return;
        }

        int32_t sendCh = static_cast<int32_t>(chResult.val0);
        int32_t recvCh = static_cast<int32_t>(chResult.val1);
        m_serviceChannel = recvCh;

        Debug::print("[consoled] Attempting to register as CONSOLED service...\n");
        int64_t assignResult = sys::assign_set("CONSOLED", sendCh);

        if (assignResult < 0) {
            Debug::print("[consoled] assign_set failed with error: ");
            Debug::printDec(static_cast<uint64_t>(-assignResult));
            Debug::print("\n");
            Debug::print("[consoled] Running as secondary instance (interactive mode)\n");
            m_isPrimary = false;
            sys::channel_close(sendCh);
            sys::channel_close(recvCh);
            m_serviceChannel = -1;
        } else {
            Debug::print("[consoled] Service registered as CONSOLED\n");
            m_isPrimary = true;
        }

        Debug::print("[consoled] Ready.\n");
    }

    bool processIpcMessages(uint8_t *msgBuf, uint32_t *handles) {
        uint32_t messagesProcessed = 0;

        while (messagesProcessed < MAX_MESSAGES_PER_BATCH) {
            uint32_t handleCount = 4;
            int64_t n =
                sys::channel_recv(m_serviceChannel, msgBuf, MAX_PAYLOAD, handles, &handleCount);

            if (n > 0) {
                messagesProcessed++;

                int32_t clientCh = (handleCount > 0) ? static_cast<int32_t>(handles[0]) : -1;
                m_requestHandler.handle(
                    clientCh, msgBuf, static_cast<size_t>(n), handles, handleCount);

                for (uint32_t i = 0; i < handleCount; i++) {
                    if (handles[i] != 0xFFFFFFFF) {
                        sys::channel_close(static_cast<int32_t>(handles[i]));
                    }
                }
            } else {
                break;
            }
        }

        return messagesProcessed > 0;
    }

    bool handleEvent(const gui_event_t &event) {
        if (event.type == GUI_EVENT_KEY && event.key.pressed) {
            char c = keycode_to_ascii(event.key.keycode, event.key.modifiers);

            if (m_shell.is_foreground()) {
                // Forward keyboard input to foreground process via kernel TTY
                if (c != 0) {
                    m_shell.forward_to_foreground(c);
                } else {
                    // Forward special keys as ANSI escape sequences
                    m_shell.forward_special_key(event.key.keycode);
                }
            } else {
                // Normal shell input handling
                m_shell.handle_special_key(event.key.keycode, event.key.modifiers);
                if (c != 0) {
                    m_shell.handle_char(c);
                }
            }
        } else if (event.type == GUI_EVENT_CLOSE) {
            Debug::print("[consoled] Closing console...\n");
            if (m_window) {
                gui_destroy_window(m_window);
                m_window = nullptr;
            }
            return false;
        }
        return true;
    }
};

//===----------------------------------------------------------------------===//
// Entry Point
//===----------------------------------------------------------------------===//

extern "C" void _start() {
    clearBss();

    ConsoleServer server;
    if (!server.init()) {
        sys::exit(1);
    }

    server.run();
    sys::exit(0);
}
