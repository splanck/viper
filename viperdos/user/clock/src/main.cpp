//===----------------------------------------------------------------------===//
// Clock - Entry point
//===----------------------------------------------------------------------===//

#include "../include/clock.hpp"
#include "../include/ui.hpp"

using namespace clockapp;

extern "C" int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (gui_init() != 0) {
        return 1;
    }

    gui_window_t *win = gui_create_window("Clock", dims::WIN_WIDTH, dims::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    UI ui(win);
    Time time;

    // Initial render
    getCurrentTime(time);
    ui.render(time);

    bool running = true;
    int lastSecond = -1;

    while (running) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            switch (event.type) {
            case GUI_EVENT_CLOSE:
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

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
