//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/servers/consoled/main.cpp
// Purpose: Console server (consoled) - GUI-based terminal emulator.
// Key invariants: Renders text in a GUI window via displayd.
// Ownership/Lifetime: Long-running service process.
//
//===----------------------------------------------------------------------===//

/**
 * @file main.cpp
 * @brief Console server (consoled) main entry point.
 *
 * @details
 * This server provides console output services to user-space processes via IPC.
 * Text is rendered in a GUI window managed by displayd.
 */

#include "ansi.hpp"
#include "keymap.hpp"
#include "request.hpp"
#include "shell.hpp"
#include "text_buffer.hpp"
#include "../../include/viper_colors.h"
#include "../../syscall.hpp"
#include "console_protocol.hpp"
#include <gui.h>

using namespace console_protocol;
using namespace consoled;

// =============================================================================
// Constants
// =============================================================================

// Colors (from centralized viper_colors.h)
static constexpr uint32_t DEFAULT_FG = VIPER_COLOR_TEXT;
static constexpr uint32_t DEFAULT_BG = VIPER_COLOR_CONSOLE_BG;

// Frame rate limiting - target ~60 FPS (16ms frame time)
static constexpr uint64_t FRAME_INTERVAL_MS = 16;

// Message batching
static constexpr uint32_t MAX_MESSAGES_PER_BATCH = 256;

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
// Global State
// =============================================================================

// GUI window
static gui_window_t *g_window = nullptr;
static uint32_t g_window_width = 0;
static uint32_t g_window_height = 0;

// Console components
static TextBuffer g_text_buffer;
static AnsiParser g_ansi_parser;
static ShellManager g_shell_manager;
static LocalShell g_local_shell;
static RequestHandler g_request_handler;

// Service channel
static int32_t g_service_channel = -1;

// Multi-instance support
static bool g_is_primary = false;
static uint32_t g_instance_id = 0;

// Presentation timing
static uint64_t g_last_present_time = 0;

// =============================================================================
// BSS Section
// =============================================================================

extern "C" char __bss_start[];
extern "C" char __bss_end[];

static void clear_bss(void) {
    for (char *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================

extern "C" void _start() {
    // Clear BSS first - critical for C++ global initializers
    clear_bss();

    // Reset console colors to defaults
    sys::print("\033[0m");

    debug_print("[consoled] Starting console server (GUI mode)...\n");

    // Receive bootstrap capabilities (optional)
    debug_print("[consoled] Checking bootstrap channel...\n");
    {
        constexpr int32_t BOOTSTRAP_RECV = 0;
        uint8_t dummy[1];
        uint32_t handles[4];
        uint32_t handle_count = 4;

        for (uint32_t i = 0; i < 50; i++) {
            handle_count = 4;
            int64_t n =
                sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
            if (n >= 0) {
                debug_print("[consoled] Received bootstrap caps\n");
                sys::channel_close(BOOTSTRAP_RECV);
                break;
            }
            if (n != VERR_WOULD_BLOCK) {
                debug_print("[consoled] No bootstrap channel (secondary instance)\n");
                break;
            }
            sys::yield();
        }
    }

    // Wait for displayd
    debug_print("[consoled] Waiting for displayd...\n");
    bool displayd_found = false;
    for (uint32_t attempt = 0; attempt < 100; attempt++) {
        uint32_t handle = 0xFFFFFFFF;
        int64_t result = sys::assign_get("DISPLAY", &handle);
        if (result == 0 && handle != 0xFFFFFFFF) {
            sys::channel_close(static_cast<int32_t>(handle));
            displayd_found = true;
            debug_print("[consoled] Found displayd after ");
            debug_print_dec(attempt);
            debug_print(" attempts\n");
            break;
        }
        sys::sleep(10);
    }

    if (!displayd_found) {
        debug_print("[consoled] ERROR: displayd not found after 1 second\n");
        sys::exit(1);
    }

    // Initialize GUI library
    debug_print("[consoled] Initializing GUI...\n");
    if (gui_init() != 0) {
        debug_print("[consoled] Failed to initialize GUI library\n");
        sys::exit(1);
    }
    debug_print("[consoled] GUI initialized\n");

    // Get display information
    gui_display_info_t display;
    if (gui_get_display_info(&display) != 0) {
        debug_print("[consoled] Failed to get display info\n");
        sys::exit(1);
    }

    debug_print("[consoled] Display: ");
    debug_print_dec(display.width);
    debug_print("x");
    debug_print_dec(display.height);
    debug_print("\n");

    // Calculate window size (70% width, 60% height)
    g_window_width = (display.width * 70) / 100;
    g_window_height = (display.height * 60) / 100;

    // Calculate text grid size
    uint32_t cols = (g_window_width - 2 * PADDING) / FONT_WIDTH;
    uint32_t rows = (g_window_height - 2 * PADDING) / FONT_HEIGHT;

    debug_print("[consoled] Console: ");
    debug_print_dec(cols);
    debug_print(" cols x ");
    debug_print_dec(rows);
    debug_print(" rows\n");

    // Check if another consoled is already registered
    uint32_t existing_handle = 0xFFFFFFFF;
    bool consoled_exists =
        (sys::assign_get("CONSOLED", &existing_handle) == 0 && existing_handle != 0xFFFFFFFF);
    if (consoled_exists) {
        sys::channel_close(static_cast<int32_t>(existing_handle));
        g_instance_id = sys::uptime() % 1000;
    }

    // Create console window
    char window_title[32] = "Console";
    if (consoled_exists) {
        char *p = window_title + 7;
        *p++ = ' ';
        *p++ = '#';
        uint32_t id = g_instance_id;
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

    debug_print("[consoled] Creating window: ");
    debug_print(window_title);
    debug_print("\n");

    g_window = gui_create_window(window_title, g_window_width, g_window_height);
    if (!g_window) {
        debug_print("[consoled] Failed to create console window\n");
        sys::exit(1);
    }
    debug_print("[consoled] Window created successfully\n");

    // Position window
    int32_t win_x = 20 + (consoled_exists ? 40 : 0);
    int32_t win_y = 20 + (consoled_exists ? 40 : 0);
    gui_set_position(g_window, win_x, win_y);

    // Initialize text buffer
    if (!g_text_buffer.init(g_window, cols, rows, DEFAULT_FG, DEFAULT_BG)) {
        debug_print("[consoled] Failed to allocate text buffer\n");
        sys::exit(1);
    }

    // Initialize ANSI parser
    g_ansi_parser.init(&g_text_buffer, DEFAULT_FG, DEFAULT_BG);

    // Initialize request handler
    g_request_handler.init(&g_text_buffer, &g_ansi_parser);

    // Initialize local shell (legacy fallback)
    g_local_shell.init(&g_text_buffer, &g_ansi_parser);

    // Fill window background
    gui_fill_rect(g_window, 0, 0, g_window_width, g_window_height, DEFAULT_BG);

    // Initial draw
    g_text_buffer.redraw_all();
    gui_present(g_window);

    // Create service channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0) {
        debug_print("[consoled] Failed to create service channel\n");
        sys::exit(1);
    }
    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);
    g_service_channel = recv_ch;

    // Register with assign system
    debug_print("[consoled] Attempting to register as CONSOLED service...\n");
    int64_t assign_result = sys::assign_set("CONSOLED", send_ch);
    if (assign_result < 0) {
        debug_print("[consoled] assign_set failed with error: ");
        debug_print_dec(static_cast<uint64_t>(-assign_result));
        debug_print("\n");
        debug_print("[consoled] Running as secondary instance (interactive mode)\n");
        g_is_primary = false;
        sys::channel_close(send_ch);
        sys::channel_close(recv_ch);
        g_service_channel = -1;
    } else {
        debug_print("[consoled] Service registered as CONSOLED\n");
        g_is_primary = true;
    }
    debug_print("[consoled] Ready.\n");

    // Spawn shell process
    if (!g_shell_manager.spawn()) {
        debug_print("[consoled] Failed to spawn shell, will use legacy mode\n");
    }

    // Main event loop
    uint8_t msg_buf[MAX_PAYLOAD];
    uint32_t handles[4];
    gui_event_t event;

    g_last_present_time = sys::uptime();

    while (true) {
        bool did_work = false;
        uint64_t now = sys::uptime();

        // STEP 1: Process IPC messages (primary instance only)
        if (g_is_primary && g_service_channel >= 0) {
            uint32_t messages_processed = 0;

            while (messages_processed < MAX_MESSAGES_PER_BATCH) {
                uint32_t handle_count = 4;
                int64_t n = sys::channel_recv(
                    g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);

                if (n > 0) {
                    did_work = true;
                    messages_processed++;

                    int32_t client_ch = (handle_count > 0) ? static_cast<int32_t>(handles[0]) : -1;
                    g_request_handler.handle(
                        client_ch, msg_buf, static_cast<size_t>(n), handles, handle_count);

                    // Close received handles
                    for (uint32_t i = 0; i < handle_count; i++) {
                        if (handles[i] != 0xFFFFFFFF) {
                            sys::channel_close(static_cast<int32_t>(handles[i]));
                        }
                    }
                } else {
                    break;
                }
            }
        }

        // STEP 2: Poll shell output
        if (g_shell_manager.has_shell()) {
            g_shell_manager.poll_output(g_ansi_parser);
            did_work = true;
        }

        // STEP 3: Present with frame rate limiting
        now = sys::uptime();
        uint64_t time_since_present = now - g_last_present_time;

        if (g_text_buffer.needs_present() && time_since_present >= FRAME_INTERVAL_MS) {
            gui_present_async(g_window);
            g_text_buffer.clear_needs_present();
            g_last_present_time = now;
        }

        // STEP 4: Process GUI events
        while (gui_poll_event(g_window, &event) == 0) {
            did_work = true;

            if (event.type == GUI_EVENT_KEY && event.key.pressed) {
                char c = keycode_to_ascii(event.key.keycode, event.key.modifiers);

                if (g_shell_manager.has_shell()) {
                    g_shell_manager.send_input(c, event.key.keycode, event.key.modifiers);
                } else if (c != 0 && !g_is_primary) {
                    g_local_shell.handle_input(c);
                }
            } else if (event.type == GUI_EVENT_CLOSE) {
                debug_print("[consoled] Closing console...\n");
                g_shell_manager.close();
                if (g_window) {
                    gui_destroy_window(g_window);
                    g_window = nullptr;
                }
                sys::exit(0);
            }
        }

        // STEP 5: Yield if no work was done
        if (!did_work) {
            if (g_text_buffer.needs_present()) {
                uint64_t remaining = FRAME_INTERVAL_MS - time_since_present;
                if (remaining > 0 && remaining <= FRAME_INTERVAL_MS) {
                    sys::sleep(remaining);
                }
            } else {
                sys::yield();
            }
        }
    }

    sys::exit(0);
}
