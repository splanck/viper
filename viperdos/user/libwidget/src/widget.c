//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/widget.c
// Purpose: Core widget system implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Keycode to character conversion
//===----------------------------------------------------------------------===//

static char keycode_to_char(uint16_t keycode, uint8_t modifiers) {
    bool shift = (modifiers & 1) != 0;
    char ch = 0;

    // Letters (evdev: Q=16..P=25, A=30..L=38, Z=44..M=50)
    if (keycode >= 16 && keycode <= 25) {
        ch = "qwertyuiop"[keycode - 16];
    } else if (keycode >= 30 && keycode <= 38) {
        ch = "asdfghjkl"[keycode - 30];
    } else if (keycode >= 44 && keycode <= 50) {
        ch = "zxcvbnm"[keycode - 44];
    } else if (keycode >= 2 && keycode <= 10) {
        ch = shift ? "!@#$%^&*("[keycode - 2] : '1' + (keycode - 2);
    } else if (keycode == 11) {
        ch = shift ? ')' : '0';
    } else if (keycode == 57) {
        ch = ' ';
    } else if (keycode == 12) {
        ch = shift ? '_' : '-';
    } else if (keycode == 13) {
        ch = shift ? '+' : '=';
    } else if (keycode == 52) {
        ch = shift ? '>' : '.';
    } else if (keycode == 51) {
        ch = shift ? '<' : ',';
    }

    if (ch && shift && ch >= 'a' && ch <= 'z') {
        ch = ch - 'a' + 'A';
    }

    return ch;
}

//===----------------------------------------------------------------------===//
// Internal Helpers
//===----------------------------------------------------------------------===//

static void widget_init_base(widget_t *w, widget_type_t type, widget_t *parent) {
    memset(w, 0, sizeof(widget_t));
    w->type = type;
    w->parent = parent;
    w->visible = true;
    w->enabled = true;
    w->bg_color = WB_GRAY_LIGHT;
    w->fg_color = WB_BLACK;

    if (parent) {
        widget_add_child(parent, w);
    }
}

static void widget_free_children(widget_t *w) {
    if (w->children) {
        for (int i = 0; i < w->child_count; i++) {
            widget_destroy(w->children[i]);
        }
        free(w->children);
        w->children = NULL;
        w->child_count = 0;
        w->child_capacity = 0;
    }
}

//===----------------------------------------------------------------------===//
// Core Widget Functions
//===----------------------------------------------------------------------===//

widget_t *widget_create(widget_type_t type, widget_t *parent) {
    widget_t *w = (widget_t *)malloc(sizeof(widget_t));
    if (!w)
        return NULL;

    widget_init_base(w, type, parent);
    return w;
}

void widget_destroy(widget_t *w) {
    if (!w)
        return;

    // Remove from parent
    if (w->parent) {
        widget_remove_child(w->parent, w);
    }

    // Free children
    widget_free_children(w);

    // Free layout
    if (w->layout) {
        layout_destroy(w->layout);
    }

    free(w);
}

void widget_set_position(widget_t *w, int x, int y) {
    if (w) {
        w->x = x;
        w->y = y;
    }
}

void widget_set_size(widget_t *w, int width, int height) {
    if (w) {
        w->width = width;
        w->height = height;
    }
}

void widget_set_geometry(widget_t *w, int x, int y, int width, int height) {
    if (w) {
        w->x = x;
        w->y = y;
        w->width = width;
        w->height = height;
    }
}

void widget_get_geometry(widget_t *w, int *x, int *y, int *width, int *height) {
    if (w) {
        if (x)
            *x = w->x;
        if (y)
            *y = w->y;
        if (width)
            *width = w->width;
        if (height)
            *height = w->height;
    }
}

void widget_set_visible(widget_t *w, bool visible) {
    if (w) {
        w->visible = visible;
    }
}

void widget_set_enabled(widget_t *w, bool enabled) {
    if (w) {
        w->enabled = enabled;
    }
}

bool widget_is_visible(widget_t *w) {
    return w ? w->visible : false;
}

bool widget_is_enabled(widget_t *w) {
    return w ? w->enabled : false;
}

void widget_set_colors(widget_t *w, uint32_t fg, uint32_t bg) {
    if (w) {
        w->fg_color = fg;
        w->bg_color = bg;
    }
}

void widget_set_focus(widget_t *w) {
    if (w) {
        // Clear focus from siblings
        if (w->parent) {
            for (int i = 0; i < w->parent->child_count; i++) {
                widget_t *sibling = w->parent->children[i];
                if (sibling->focused && sibling != w) {
                    sibling->focused = false;
                    if (sibling->on_focus) {
                        sibling->on_focus(sibling, false);
                    }
                }
            }
        }
        w->focused = true;
        if (w->on_focus) {
            w->on_focus(w, true);
        }
    }
}

bool widget_has_focus(widget_t *w) {
    return w ? w->focused : false;
}

void widget_add_child(widget_t *parent, widget_t *child) {
    if (!parent || !child)
        return;

    // Grow array if needed
    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity ? parent->child_capacity * 2 : 4;
        widget_t **new_children = (widget_t **)realloc(parent->children, new_cap * sizeof(widget_t *));
        if (!new_children)
            return;
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

void widget_remove_child(widget_t *parent, widget_t *child) {
    if (!parent || !child)
        return;

    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            // Shift remaining children
            for (int j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            child->parent = NULL;
            break;
        }
    }
}

widget_t *widget_get_parent(widget_t *w) {
    return w ? w->parent : NULL;
}

int widget_get_child_count(widget_t *w) {
    return w ? w->child_count : 0;
}

widget_t *widget_get_child(widget_t *w, int index) {
    if (!w || index < 0 || index >= w->child_count)
        return NULL;
    return w->children[index];
}

void widget_repaint(widget_t *w) {
    // This is a placeholder - actual repaint needs window context
    (void)w;
}

void widget_paint(widget_t *w, gui_window_t *win) {
    if (!w || !w->visible)
        return;

    // Call custom paint handler if set
    if (w->on_paint) {
        w->on_paint(w, win);
    }

    // Paint children
    widget_paint_children(w, win);
}

void widget_paint_children(widget_t *w, gui_window_t *win) {
    if (!w)
        return;

    for (int i = 0; i < w->child_count; i++) {
        widget_paint(w->children[i], win);
    }
}

bool widget_handle_mouse(widget_t *w, int x, int y, int button, int event_type) {
    if (!w || !w->visible || !w->enabled)
        return false;

    // Check if point is inside widget
    if (x < w->x || x >= w->x + w->width || y < w->y || y >= w->y + w->height) {
        return false;
    }

    // Try children first (reverse order for z-order)
    for (int i = w->child_count - 1; i >= 0; i--) {
        if (widget_handle_mouse(w->children[i], x, y, button, event_type)) {
            return true;
        }
    }

    // Handle ourselves
    if (w->on_click && event_type == 1) { // Button down
        w->on_click(w, x - w->x, y - w->y, button);
        return true;
    }

    return false;
}

bool widget_handle_key(widget_t *w, int keycode, char ch) {
    if (!w || !w->visible || !w->enabled)
        return false;

    // Find focused widget
    if (w->focused && w->on_key) {
        w->on_key(w, keycode, ch);
        return true;
    }

    // Check children
    for (int i = 0; i < w->child_count; i++) {
        if (widget_handle_key(w->children[i], keycode, ch)) {
            return true;
        }
    }

    return false;
}

widget_t *widget_find_at(widget_t *root, int x, int y) {
    if (!root || !root->visible)
        return NULL;

    // Check if point is inside
    if (x < root->x || x >= root->x + root->width || y < root->y || y >= root->y + root->height) {
        return NULL;
    }

    // Check children (reverse order for z-order)
    for (int i = root->child_count - 1; i >= 0; i--) {
        widget_t *found = widget_find_at(root->children[i], x, y);
        if (found) {
            return found;
        }
    }

    return root;
}

void widget_set_user_data(widget_t *w, void *data) {
    if (w) {
        w->user_data = data;
    }
}

void *widget_get_user_data(widget_t *w) {
    return w ? w->user_data : NULL;
}

//===----------------------------------------------------------------------===//
// Widget Application
//===----------------------------------------------------------------------===//

widget_app_t *widget_app_create(const char *title, int width, int height) {
    if (gui_init() != 0) {
        return NULL;
    }

    widget_app_t *app = (widget_app_t *)malloc(sizeof(widget_app_t));
    if (!app) {
        gui_shutdown();
        return NULL;
    }

    memset(app, 0, sizeof(widget_app_t));

    app->window = gui_create_window(title, width, height);
    if (!app->window) {
        free(app);
        gui_shutdown();
        return NULL;
    }

    app->running = true;
    return app;
}

void widget_app_destroy(widget_app_t *app) {
    if (!app)
        return;

    if (app->root) {
        widget_destroy(app->root);
    }

    if (app->active_menu) {
        menu_destroy(app->active_menu);
    }

    if (app->window) {
        gui_destroy_window(app->window);
    }

    gui_shutdown();
    free(app);
}

void widget_app_set_root(widget_app_t *app, widget_t *root) {
    if (app) {
        app->root = root;
    }
}

void widget_app_run(widget_app_t *app) {
    if (!app)
        return;

    // Initial paint
    widget_app_repaint(app);

    while (app->running) {
        gui_event_t event;
        if (gui_poll_event(app->window, &event) == 0) {
            switch (event.type) {
            case GUI_EVENT_CLOSE:
                app->running = false;
                break;

            case GUI_EVENT_MOUSE:
                if (app->active_menu && menu_is_visible(app->active_menu)) {
                    if (menu_handle_mouse(app->active_menu, event.mouse.x, event.mouse.y,
                                          event.mouse.button, event.mouse.event_type)) {
                        widget_app_repaint(app);
                        break;
                    }
                }

                if (app->root) {
                    if (widget_handle_mouse(app->root, event.mouse.x, event.mouse.y,
                                            event.mouse.button, event.mouse.event_type)) {
                        widget_app_repaint(app);
                    }
                }
                break;

            case GUI_EVENT_KEY:
                if (app->root && event.key.pressed) {
                    char ch = keycode_to_char(event.key.keycode, event.key.modifiers);
                    if (widget_handle_key(app->root, event.key.keycode, ch)) {
                        widget_app_repaint(app);
                    }
                }
                break;

            default:
                break;
            }
        }

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }
}

void widget_app_quit(widget_app_t *app) {
    if (app) {
        app->running = false;
    }
}

void widget_app_repaint(widget_app_t *app) {
    if (!app || !app->window)
        return;

    // Clear background
    int w = gui_get_width(app->window);
    int h = gui_get_height(app->window);
    gui_fill_rect(app->window, 0, 0, w, h, WB_GRAY_LIGHT);

    // Paint widgets
    if (app->root) {
        widget_paint(app->root, app->window);
    }

    // Paint active menu on top
    if (app->active_menu && menu_is_visible(app->active_menu)) {
        menu_paint(app->active_menu, app->window);
    }

    gui_present(app->window);
}
