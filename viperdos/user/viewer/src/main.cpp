//===----------------------------------------------------------------------===//
// Viewer - Entry point
//===----------------------------------------------------------------------===//

#include "../include/image.hpp"
#include "../include/view.hpp"

using namespace viewer;

extern "C" int main(int argc, char **argv) {
    if (gui_init() != 0) {
        return 1;
    }

    gui_window_t *win = gui_create_window("Viewer", dims::WIN_WIDTH, dims::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    Image image;
    View view(win);

    // Load image from command line
    if (argc > 1) {
        image.load(argv[1]);
    }

    view.render(image);

    bool running = true;
    bool dragging = false;
    int lastX = 0, lastY = 0;

    while (running) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            bool needsRedraw = false;

            switch (event.type) {
            case GUI_EVENT_CLOSE:
                running = false;
                break;

            case GUI_EVENT_MOUSE:
                if (event.mouse.event_type == 1) {
                    // Mouse button press
                    if (event.mouse.button == 0) {
                        dragging = true;
                        lastX = event.mouse.x;
                        lastY = event.mouse.y;
                    }
                } else if (event.mouse.event_type == 2) {
                    // Mouse button release
                    dragging = false;
                } else if (event.mouse.event_type == 0 && dragging) {
                    // Mouse move while dragging
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
                case 0x09: // F key (fit)
                    view.zoomFit();
                    view.resetPan();
                    needsRedraw = true;
                    break;
                case 0x1E: // 1 key (100%)
                    view.zoom100();
                    view.resetPan();
                    needsRedraw = true;
                    break;
                case 0x50: // Left arrow
                    view.pan(20, 0);
                    needsRedraw = true;
                    break;
                case 0x4F: // Right arrow
                    view.pan(-20, 0);
                    needsRedraw = true;
                    break;
                case 0x52: // Up arrow
                    view.pan(0, 20);
                    needsRedraw = true;
                    break;
                case 0x51: // Down arrow
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

            if (needsRedraw) {
                view.render(image);
            }
        }

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
