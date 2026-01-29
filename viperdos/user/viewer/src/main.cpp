//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief Image viewer application entry point and event loop.
 *
 * This file contains the main function for the ViperDOS image viewer.
 * The viewer allows browsing BMP and PPM images with zoom and pan controls.
 *
 * ## Application Structure
 *
 * The viewer is organized into components:
 * - **main.cpp** (this file): Event loop and input handling
 * - **image.hpp/cpp**: Image loading (BMP, PPM formats)
 * - **view.hpp/cpp**: Rendering and zoom/pan management
 *
 * ## Controls
 *
 * ### Mouse
 *
 * | Action          | Effect               |
 * |-----------------|----------------------|
 * | Click + Drag    | Pan the image        |
 *
 * ### Keyboard
 *
 * | Key          | Effect                    |
 * |--------------|---------------------------|
 * | + (equals)   | Zoom in                   |
 * | - (minus)    | Zoom out                  |
 * | F            | Fit image to window       |
 * | 1            | 100% zoom (actual pixels) |
 * | Arrow keys   | Pan 20 pixels             |
 *
 * ## Event Loop
 *
 * The main loop processes:
 * 1. **Close events**: Terminate the application
 * 2. **Mouse events**: Drag for panning
 * 3. **Keyboard events**: Zoom and pan controls
 *
 * ## Command Line
 *
 * ```
 * viewer [filename]
 * ```
 *
 * - `filename`: Optional path to an image file to open
 * - If no filename is provided, the viewer opens empty
 *
 * @see image.hpp for supported formats
 * @see view.hpp for rendering
 */
//===----------------------------------------------------------------------===//

#include "../include/image.hpp"
#include "../include/view.hpp"

using namespace viewer;

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

/**
 * @brief Application entry point for the image viewer.
 *
 * Initializes the GUI, creates the viewer window, and runs the main event
 * loop. Optionally loads an image specified on the command line.
 *
 * ## Initialization
 *
 * 1. Initialize GUI library (connect to displayd)
 * 2. Create window with fixed dimensions (640x480)
 * 3. Create Image and View instances
 * 4. Load image from command line if provided
 * 5. Render initial display
 *
 * ## Mouse Drag Handling
 *
 * The event loop tracks mouse drag state:
 * - Button press: Start dragging, record position
 * - Button release: Stop dragging
 * - Mouse move during drag: Calculate delta, pan image
 *
 * This provides smooth, responsive panning of the image.
 *
 * ## Keycode Reference
 *
 * | Keycode | Key     | Action     |
 * |---------|---------|------------|
 * | 0x2D    | = (+)   | Zoom in    |
 * | 0x2E    | - (_)   | Zoom out   |
 * | 0x09    | F       | Fit        |
 * | 0x1E    | 1       | 100% zoom  |
 * | 0x50    | Left    | Pan left   |
 * | 0x4F    | Right   | Pan right  |
 * | 0x52    | Up      | Pan up     |
 * | 0x51    | Down    | Pan down   |
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of argument strings. argv[1] is the image path.
 * @return 0 on normal exit, 1 on initialization failure.
 */
extern "C" int main(int argc, char **argv) {
    // Initialize GUI library
    if (gui_init() != 0) {
        return 1;
    }

    // Create viewer window
    gui_window_t *win = gui_create_window("Viewer", dims::WIN_WIDTH, dims::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Initialize image and view
    Image image;
    View view(win);

    // Load image from command line
    if (argc > 1) {
        image.load(argv[1]);
    }

    // Initial render
    view.render(image);

    bool running = true;
    bool dragging = false;
    int lastX = 0, lastY = 0;

    // Main event loop
    while (running) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            bool needsRedraw = false;

            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    // Window close button clicked
                    running = false;
                    break;

                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1) {
                        // Mouse button press - start dragging
                        if (event.mouse.button == 0) {
                            dragging = true;
                            lastX = event.mouse.x;
                            lastY = event.mouse.y;
                        }
                    } else if (event.mouse.event_type == 2) {
                        // Mouse button release - stop dragging
                        dragging = false;
                    } else if (event.mouse.event_type == 0 && dragging) {
                        // Mouse move while dragging - pan image
                        int dx = event.mouse.x - lastX;
                        int dy = event.mouse.y - lastY;
                        view.pan(dx, dy);
                        lastX = event.mouse.x;
                        lastY = event.mouse.y;
                        needsRedraw = true;
                    }
                    break;

                case GUI_EVENT_KEY:
                    switch (event.key.keycode) {
                        case 0x2D: // + key (zoom in)
                            view.zoomIn();
                            needsRedraw = true;
                            break;

                        case 0x2E: // - key (zoom out)
                            view.zoomOut();
                            needsRedraw = true;
                            break;

                        case 0x09: // F key (fit to window)
                            view.zoomFit();
                            view.resetPan();
                            needsRedraw = true;
                            break;

                        case 0x1E: // 1 key (100% zoom)
                            view.zoom100();
                            view.resetPan();
                            needsRedraw = true;
                            break;

                        case 0x50: // Left arrow (pan left)
                            view.pan(20, 0);
                            needsRedraw = true;
                            break;

                        case 0x4F: // Right arrow (pan right)
                            view.pan(-20, 0);
                            needsRedraw = true;
                            break;

                        case 0x52: // Up arrow (pan up)
                            view.pan(0, 20);
                            needsRedraw = true;
                            break;

                        case 0x51: // Down arrow (pan down)
                            view.pan(0, -20);
                            needsRedraw = true;
                            break;

                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }

            // Re-render if state changed
            if (needsRedraw) {
                view.render(image);
            }
        }

        // Yield CPU to prevent busy-waiting
        // System call 0x00 = SYS_TASK_YIELD
        __asm__ volatile("mov x8, #0x00\n\tsvc #0" ::: "x8");
    }

    // Cleanup
    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
