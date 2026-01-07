//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/examples/basic_draw.c
// Purpose: ViperGFX example showing window creation, primitive drawing, and
//          basic event handling.
// Key invariants: Keeps example code minimal and side-effect free beyond the
//                 graphics system; exits cleanly on close/ESC.
// Ownership/Lifetime: Demonstration program; resources owned and released by
//                     the example.
// Links: docs/vgfx.md
//
//===----------------------------------------------------------------------===//

/*
 * ViperGFX Example: Basic Drawing
 * Demonstrates window creation, drawing primitives, and event handling
 */

#include <stdio.h>
#include <vgfx.h>

/// What: Entry point for the Basic Drawing example.
/// Why:  Demonstrates ViperGFX window lifecycle and primitive rendering.
/// How:  Creates a window, draws shapes, and processes events until exit.
int main(void)
{
    printf("ViperGFX v%d.%d.%d - Basic Drawing Example\n",
           VGFX_VERSION_MAJOR,
           VGFX_VERSION_MINOR,
           VGFX_VERSION_PATCH);

    /* Create window with default parameters */
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = 640;
    params.height = 480;
    params.title = "ViperGFX - Basic Drawing";
    params.resizable = 1;

    vgfx_window_t win = vgfx_create_window(&params);
    if (!win)
    {
        fprintf(stderr, "Failed to create window: %s\n", vgfx_get_last_error());
        return 1;
    }

    printf("Window created: %dx%d\n", params.width, params.height);

    /* Set FPS limit */
    vgfx_set_fps(win, 60);

    /* Clear screen to dark blue */
    vgfx_cls(win, VGFX_RGB(0, 0, 64));

    /* Draw some shapes */
    vgfx_fill_rect(win, 50, 50, 100, 100, VGFX_RED);
    vgfx_rect(win, 45, 45, 110, 110, VGFX_WHITE);

    vgfx_fill_circle(win, 400, 240, 80, VGFX_GREEN);
    vgfx_circle(win, 400, 240, 85, VGFX_WHITE);

    vgfx_line(win, 0, 0, 639, 479, VGFX_YELLOW);
    vgfx_line(win, 639, 0, 0, 479, VGFX_YELLOW);

    /* Event loop */
    int running = 1;
    while (running)
    {
        vgfx_event_t event;
        while (vgfx_poll_event(win, &event))
        {
            if (event.type == VGFX_EVENT_CLOSE)
            {
                printf("Close event received\n");
                running = 0;
            }
            else if (event.type == VGFX_EVENT_KEY_DOWN)
            {
                if (event.data.key.key == VGFX_KEY_ESCAPE)
                {
                    printf("ESC pressed, exiting\n");
                    running = 0;
                }
            }
            else if (event.type == VGFX_EVENT_RESIZE)
            {
                printf(
                    "Window resized to %dx%d\n", event.data.resize.width, event.data.resize.height);
            }
        }

        /* Update display */
        if (!vgfx_update(win))
        {
            fprintf(stderr, "Update failed: %s\n", vgfx_get_last_error());
            break;
        }
    }

    /* Cleanup */
    vgfx_destroy_window(win);
    printf("Window destroyed\n");

    return 0;
}
