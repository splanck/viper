//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief Display server (displayd) - window management and compositing.
 *
 * @details
 * Provides display and window management services:
 * - Maps the framebuffer into its address space
 * - Manages window surfaces (create, destroy, composite)
 * - Renders window decorations (title bar, borders, scrollbars)
 * - Renders a mouse cursor
 * - Routes input events to focused windows
 */

#include "compositor.hpp"
#include "cursor.hpp"
#include "input.hpp"
#include "ipc.hpp"
#include "state.hpp"

using namespace displayd;

// ============================================================================
// Bootstrap
// ============================================================================

static void recv_bootstrap_caps() {
    constexpr int32_t BOOTSTRAP_RECV = 0;
    uint8_t dummy[1];
    uint32_t handles[4];
    uint32_t handle_count = 4;

    for (uint32_t i = 0; i < 2000; i++) {
        handle_count = 4;
        int64_t n = sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
        if (n >= 0) {
            sys::channel_close(BOOTSTRAP_RECV);
            return;
        }
        if (n == VERR_WOULD_BLOCK) {
            sys::yield();
            continue;
        }
        return;
    }
}

// ============================================================================
// Main Entry Point
// ============================================================================

extern "C" void _start() {
    // Reset console colors to defaults (white on blue)
    sys::print("\033[0m");

    debug_print("[displayd] Starting display server...\n");

    // Receive bootstrap capabilities
    recv_bootstrap_caps();

    // Map framebuffer
    sys::FramebufferInfo fb_info;
    if (sys::map_framebuffer(&fb_info) != 0) {
        debug_print("[displayd] Failed to map framebuffer\n");
        sys::exit(1);
    }

    g_fb = reinterpret_cast<uint32_t *>(fb_info.address);
    g_fb_width = fb_info.width;
    g_fb_height = fb_info.height;
    g_fb_pitch = fb_info.pitch;

    debug_print("[displayd] Framebuffer: ");
    debug_print_dec(g_fb_width);
    debug_print("x");
    debug_print_dec(g_fb_height);
    debug_print(" at 0x");
    debug_print_hex(fb_info.address);
    debug_print("\n");

    // Allocate back buffer for double buffering (eliminates flicker)
    uint64_t back_buffer_size = static_cast<uint64_t>(g_fb_pitch) * g_fb_height;
    auto back_buffer_result = sys::shm_create(back_buffer_size);
    if (back_buffer_result.error != 0) {
        debug_print("[displayd] Failed to allocate back buffer\n");
        sys::exit(1);
    }
    g_back_buffer = reinterpret_cast<uint32_t *>(back_buffer_result.virt_addr);
    g_draw_target = g_fb; // Default to front buffer

    debug_print("[displayd] Double buffering enabled\n");

    // Set mouse bounds
    sys::set_mouse_bounds(g_fb_width, g_fb_height);

    // Initialize cursor to center
    g_cursor_x = static_cast<int32_t>(g_fb_width / 2);
    g_cursor_y = static_cast<int32_t>(g_fb_height / 2);

    // Sync last mouse position with kernel's current position
    sys::MouseState init_state;
    if (sys::get_mouse_state(&init_state) == 0) {
        g_last_mouse_x = init_state.x;
        g_last_mouse_y = init_state.y;
        g_cursor_x = init_state.x;
        g_cursor_y = init_state.y;
        debug_print("[displayd] Initial mouse pos: (");
        debug_print_dec(init_state.x);
        debug_print(",");
        debug_print_dec(init_state.y);
        debug_print(")\n");
    }

    // Try to set up hardware cursor
    setup_hardware_cursor();

    // Initialize surfaces
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        g_surfaces[i].in_use = false;
        g_surfaces[i].event_queue.init();
    }

    // Initial composite (draw desktop)
    composite();

    // Create service channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0) {
        debug_print("[displayd] Failed to create service channel\n");
        sys::exit(1);
    }
    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);
    g_service_channel = recv_ch;

    // Create poll set for efficient message waiting
    g_poll_set = sys::poll_create();
    if (g_poll_set < 0) {
        debug_print("[displayd] Failed to create poll set\n");
        sys::exit(1);
    }

    // Add service channel to poll set (wake when messages arrive)
    if (sys::poll_add(static_cast<uint32_t>(g_poll_set),
                      static_cast<uint32_t>(recv_ch),
                      sys::POLL_CHANNEL_READ) != 0) {
        debug_print("[displayd] Failed to add channel to poll set\n");
        sys::exit(1);
    }

    // Register as DISPLAY
    if (sys::assign_set("DISPLAY", send_ch) < 0) {
        debug_print("[displayd] Failed to register DISPLAY assign\n");
        sys::exit(1);
    }
    debug_print("[displayd] registered DISPLAY assign\n");

    debug_print("[displayd] init complete, entering main loop\n");

    // Main event loop
    constexpr size_t MAX_PAYLOAD = 4096;
    uint8_t msg_buf[MAX_PAYLOAD];
    uint32_t handles[4];

    // Process messages in batches to avoid starving input polling
    constexpr uint32_t MAX_MESSAGES_PER_BATCH = 16;

    while (true) {
        uint32_t messages_processed = 0;

        while (messages_processed < MAX_MESSAGES_PER_BATCH) {
            uint32_t handle_count = 4;
            int64_t n = sys::channel_recv(
                g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);

            if (n > 0) {
                messages_processed++;

                if (handle_count > 0) {
                    // Message with reply channel - handle and respond
                    int32_t client_ch = static_cast<int32_t>(handles[0]);

                    handle_request(
                        client_ch, msg_buf, static_cast<size_t>(n), handles + 1, handle_count - 1);

                    // Close client reply channel after responding
                    sys::channel_close(client_ch);
                } else {
                    // Fire-and-forget message (no reply channel)
                    handle_request(-1, msg_buf, static_cast<size_t>(n), nullptr, 0);
                }
            } else {
                break; // No more messages in this batch
            }
        }

        // Always poll input devices
        poll_mouse();
        poll_keyboard();

        // Always composite — guarantees screen reflects latest SHM pixel
        // data from all client apps. Individual DISP_PRESENT handlers also
        // composite, but this unconditional call is the safety net that
        // ensures updates are never missed.
        composite();

        // If idle, sleep briefly to avoid busy-looping
        if (messages_processed == 0) {
            sys::sleep(5);
        }
    }

    sys::exit(0);
}
