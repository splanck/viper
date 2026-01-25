//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief Clock application entry point and event loop.
 *
 * This file contains the main function for the ViperDOS clock application.
 * The clock displays both an analog clock face with moving hands and a
 * digital time readout, with the ability to toggle between 12-hour and
 * 24-hour display modes.
 *
 * ## Application Structure
 *
 * The clock is organized into three source files:
 * - **main.cpp** (this file): Application startup and event loop
 * - **clock.hpp/cpp**: Time retrieval, formatting, and angle calculations
 * - **ui.hpp/cpp**: Visual rendering of the clock display
 *
 * ## Event Loop
 *
 * The main loop runs continuously, performing these operations:
 * 1. Poll for GUI events (non-blocking)
 * 2. Handle close events (exit application)
 * 3. Handle mouse clicks (toggle 12/24 hour mode)
 * 4. Check if the second has changed
 * 5. Re-render the display if time changed
 * 6. Yield CPU to prevent busy-waiting
 *
 * ## Display Updates
 *
 * Rather than re-rendering at a fixed rate, the clock only updates when
 * the second value changes. This reduces CPU usage while still providing
 * accurate second-hand movement. The `lastSecond` variable tracks the
 * previous second to detect changes.
 *
 * ## User Interaction
 *
 * Clicking anywhere in the clock window toggles between 12-hour (AM/PM)
 * and 24-hour (military) time display. This only affects the digital
 * readout; the analog clock is always displayed the same way.
 *
 * @see clock.hpp for time handling functions
 * @see ui.hpp for the UI class and rendering
 */
//===----------------------------------------------------------------------===//

#include "../include/clock.hpp"
#include "../include/ui.hpp"

using namespace clockapp;

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

/**
 * @brief Application entry point.
 *
 * Initializes the GUI system, creates the clock window, and runs the
 * main event loop. The application continues until the user closes
 * the window.
 *
 * ## Initialization Sequence
 *
 * 1. Initialize GUI library (connect to displayd)
 * 2. Create clock window with fixed dimensions (200x240)
 * 3. Create UI instance for rendering
 * 4. Get initial time and render
 * 5. Enter main event loop
 *
 * ## Exit Conditions
 *
 * The application exits when:
 * - User clicks the window's close button (GUI_EVENT_CLOSE)
 * - GUI initialization fails (returns 1)
 * - Window creation fails (returns 1)
 *
 * ## CPU Efficiency
 *
 * The event loop uses a non-blocking poll followed by a yield system call.
 * This allows the application to respond quickly to events while giving
 * other processes CPU time when idle.
 *
 * @param argc Number of command-line arguments (unused).
 * @param argv Command-line argument values (unused).
 * @return 0 on normal exit, 1 on initialization failure.
 */
extern "C" int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Initialize GUI library
    if (gui_init() != 0) {
        return 1;
    }

    // Create clock window
    gui_window_t *win = gui_create_window("Clock", dims::WIN_WIDTH, dims::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Create UI instance and time structure
    UI ui(win);
    Time time;

    // Initial render
    getCurrentTime(time);
    ui.render(time);

    bool running = true;
    int lastSecond = -1;

    // Main event loop
    while (running) {
        // Poll for GUI events (non-blocking)
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            switch (event.type) {
            case GUI_EVENT_CLOSE:
                // User closed the window
                running = false;
                break;

            case GUI_EVENT_MOUSE:
                // Click toggles 12/24 hour mode
                if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                    ui.toggle24Hour();
                    getCurrentTime(time);
                    ui.render(time);
                }
                break;

            default:
                break;
            }
        }

        // Update time display every second
        getCurrentTime(time);
        if (time.seconds != lastSecond) {
            lastSecond = time.seconds;
            ui.render(time);
        }

        // Yield CPU to prevent busy-waiting
        // System call 0x0E = sched_yield()
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    // Cleanup
    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
