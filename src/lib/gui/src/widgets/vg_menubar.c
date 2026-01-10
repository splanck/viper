// vg_menubar.c - MenuBar widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void menubar_destroy(vg_widget_t* widget);
static void menubar_measure(vg_widget_t* widget, float available_width, float available_height);
static void menubar_paint(vg_widget_t* widget, void* canvas);
static bool menubar_handle_event(vg_widget_t* widget, vg_event_t* event);

//=============================================================================
// MenuBar VTable
//=============================================================================

static vg_widget_vtable_t g_menubar_vtable = {
    .destroy = menubar_destroy,
    .measure = menubar_measure,
    .arrange = NULL,
    .paint = menubar_paint,
    .handle_event = menubar_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// Helper Functions
//=============================================================================

static void free_menu_item(vg_menu_item_t* item) {
    if (!item) return;

    if (item->text) free((void*)item->text);
    if (item->shortcut) free((void*)item->shortcut);

    // Recursively free submenu
    if (item->submenu) {
        vg_menu_item_t* sub = item->submenu->first_item;
        while (sub) {
            vg_menu_item_t* next = sub->next;
            free_menu_item(sub);
            sub = next;
        }
        if (item->submenu->title) free((void*)item->submenu->title);
        free(item->submenu);
    }

    free(item);
}

static void free_menu(vg_menu_t* menu) {
    if (!menu) return;

    vg_menu_item_t* item = menu->first_item;
    while (item) {
        vg_menu_item_t* next = item->next;
        free_menu_item(item);
        item = next;
    }

    if (menu->title) free((void*)menu->title);
    free(menu);
}

//=============================================================================
// MenuBar Implementation
//=============================================================================

vg_menubar_t* vg_menubar_create(vg_widget_t* parent) {
    vg_menubar_t* menubar = calloc(1, sizeof(vg_menubar_t));
    if (!menubar) return NULL;

    // Initialize base widget
    vg_widget_init(&menubar->base, VG_WIDGET_MENUBAR, &g_menubar_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Initialize menubar-specific fields
    menubar->first_menu = NULL;
    menubar->last_menu = NULL;
    menubar->menu_count = 0;
    menubar->open_menu = NULL;
    menubar->highlighted = NULL;

    menubar->font = NULL;
    menubar->font_size = theme->typography.size_normal;

    // Appearance
    menubar->height = 28.0f;
    menubar->menu_padding = 10.0f;
    menubar->item_padding = 8.0f;
    menubar->bg_color = theme->colors.bg_secondary;
    menubar->text_color = theme->colors.fg_primary;
    menubar->highlight_bg = theme->colors.bg_selected;
    menubar->disabled_color = theme->colors.fg_disabled;

    // State
    menubar->menu_active = false;

    // Set size
    menubar->base.constraints.min_height = menubar->height;
    menubar->base.constraints.preferred_height = menubar->height;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &menubar->base);
    }

    return menubar;
}

static void menubar_destroy(vg_widget_t* widget) {
    vg_menubar_t* menubar = (vg_menubar_t*)widget;

    vg_menu_t* menu = menubar->first_menu;
    while (menu) {
        vg_menu_t* next = menu->next;
        free_menu(menu);
        menu = next;
    }
}

static void menubar_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_menubar_t* menubar = (vg_menubar_t*)widget;
    (void)available_height;

    widget->measured_width = available_width > 0 ? available_width : 400;
    widget->measured_height = menubar->height;
}

static void menubar_paint(vg_widget_t* widget, void* canvas) {
    vg_menubar_t* menubar = (vg_menubar_t*)widget;

    // Draw background
    // TODO: Use vgfx primitives
    (void)menubar->bg_color;

    if (!menubar->font) return;

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(menubar->font, menubar->font_size, &font_metrics);

    float text_y = widget->y + (widget->height + font_metrics.ascent - font_metrics.descent) / 2.0f;
    float menu_x = widget->x;

    // Draw each menu title
    for (vg_menu_t* menu = menubar->first_menu; menu; menu = menu->next) {
        if (!menu->title) continue;

        vg_text_metrics_t metrics;
        vg_font_measure_text(menubar->font, menubar->font_size, menu->title, &metrics);

        float menu_width = metrics.width + menubar->menu_padding * 2;

        // Draw highlight if this menu is open
        if (menu == menubar->open_menu) {
            // Draw highlight background
            // TODO: Use vgfx primitives
            (void)menubar->highlight_bg;
        }

        // Draw menu title
        float text_x = menu_x + menubar->menu_padding;
        vg_font_draw_text(canvas, menubar->font, menubar->font_size,
                          text_x, text_y, menu->title, menubar->text_color);

        menu_x += menu_width;
    }

    // Draw open menu dropdown
    if (menubar->open_menu) {
        // Find position of open menu
        float dropdown_x = widget->x;
        for (vg_menu_t* menu = menubar->first_menu; menu != menubar->open_menu; menu = menu->next) {
            if (menu->title) {
                vg_text_metrics_t metrics;
                vg_font_measure_text(menubar->font, menubar->font_size, menu->title, &metrics);
                dropdown_x += metrics.width + menubar->menu_padding * 2;
            }
        }

        // Calculate dropdown dimensions
        float dropdown_y = widget->y + widget->height;
        float dropdown_width = 200.0f;
        float item_height = 24.0f;
        float dropdown_height = menubar->open_menu->item_count * item_height;

        // Draw dropdown background
        // TODO: Use vgfx primitives

        // Draw menu items
        float item_y = dropdown_y;
        for (vg_menu_item_t* item = menubar->open_menu->first_item; item; item = item->next) {
            if (item->separator) {
                // Draw separator line
                // TODO: Use vgfx primitives
            } else {
                // Draw highlight if highlighted
                if (item == menubar->highlighted) {
                    // TODO: Use vgfx primitives
                }

                // Draw item text
                if (item->text) {
                    float item_text_y = item_y + (item_height + font_metrics.ascent - font_metrics.descent) / 2.0f;
                    uint32_t color = item->enabled ? menubar->text_color : menubar->disabled_color;
                    vg_font_draw_text(canvas, menubar->font, menubar->font_size,
                                      dropdown_x + menubar->item_padding, item_text_y,
                                      item->text, color);

                    // Draw shortcut if present
                    if (item->shortcut) {
                        vg_text_metrics_t shortcut_metrics;
                        vg_font_measure_text(menubar->font, menubar->font_size, item->shortcut, &shortcut_metrics);
                        float shortcut_x = dropdown_x + dropdown_width - shortcut_metrics.width - menubar->item_padding;
                        vg_font_draw_text(canvas, menubar->font, menubar->font_size,
                                          shortcut_x, item_text_y, item->shortcut,
                                          menubar->disabled_color);
                    }

                    // Draw check mark if checked
                    if (item->checked) {
                        // TODO: Draw check mark
                    }

                    // Draw submenu arrow if has submenu
                    if (item->submenu) {
                        // TODO: Draw arrow
                    }
                }
            }

            item_y += item_height;
        }

        (void)dropdown_width;
        (void)dropdown_height;
    }
}

static vg_menu_t* find_menu_at_x(vg_menubar_t* menubar, float x) {
    if (!menubar->font) return NULL;

    float menu_x = 0;
    for (vg_menu_t* menu = menubar->first_menu; menu; menu = menu->next) {
        if (!menu->title) continue;

        vg_text_metrics_t metrics;
        vg_font_measure_text(menubar->font, menubar->font_size, menu->title, &metrics);
        float menu_width = metrics.width + menubar->menu_padding * 2;

        if (x >= menu_x && x < menu_x + menu_width) {
            return menu;
        }

        menu_x += menu_width;
    }
    return NULL;
}

static bool menubar_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_menubar_t* menubar = (vg_menubar_t*)widget;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            float local_x = event->mouse.x;
            float local_y = event->mouse.y;

            // Check if in menu bar area
            if (local_y < menubar->height) {
                vg_menu_t* menu = find_menu_at_x(menubar, local_x);
                if (menubar->menu_active && menu != menubar->open_menu) {
                    menubar->open_menu = menu;
                    if (menu) menu->open = true;
                    menubar->highlighted = NULL;
                    widget->needs_paint = true;
                }
            } else if (menubar->open_menu) {
                // Check if in dropdown area
                float item_height = 24.0f;
                int item_index = (int)((local_y - menubar->height) / item_height);

                vg_menu_item_t* old_highlight = menubar->highlighted;
                menubar->highlighted = NULL;

                int idx = 0;
                for (vg_menu_item_t* item = menubar->open_menu->first_item; item; item = item->next) {
                    if (idx == item_index && !item->separator) {
                        menubar->highlighted = item;
                        break;
                    }
                    idx++;
                }

                if (old_highlight != menubar->highlighted) {
                    widget->needs_paint = true;
                }
            }
            return false;
        }

        case VG_EVENT_CLICK: {
            float local_x = event->mouse.x;
            float local_y = event->mouse.y;

            // Check if clicking menu bar
            if (local_y < menubar->height) {
                vg_menu_t* menu = find_menu_at_x(menubar, local_x);
                if (menu) {
                    if (menubar->open_menu == menu) {
                        // Close menu
                        menubar->open_menu = NULL;
                        menubar->menu_active = false;
                        menu->open = false;
                    } else {
                        // Open menu
                        if (menubar->open_menu) {
                            menubar->open_menu->open = false;
                        }
                        menubar->open_menu = menu;
                        menubar->menu_active = true;
                        menu->open = true;
                    }
                    widget->needs_paint = true;
                    return true;
                }
            } else if (menubar->open_menu && menubar->highlighted) {
                // Execute highlighted item
                vg_menu_item_t* item = menubar->highlighted;
                if (item->enabled && item->action) {
                    item->action(item->action_data);
                }

                // Close menu
                menubar->open_menu->open = false;
                menubar->open_menu = NULL;
                menubar->menu_active = false;
                menubar->highlighted = NULL;
                widget->needs_paint = true;
                return true;
            }
            return false;
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (!menubar->menu_active) {
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_KEY_DOWN:
            if (menubar->open_menu) {
                switch (event->key.key) {
                    case VG_KEY_ESCAPE:
                        menubar->open_menu->open = false;
                        menubar->open_menu = NULL;
                        menubar->menu_active = false;
                        menubar->highlighted = NULL;
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_UP:
                        // Move highlight up
                        if (menubar->highlighted && menubar->highlighted->prev) {
                            menubar->highlighted = menubar->highlighted->prev;
                            while (menubar->highlighted && menubar->highlighted->separator) {
                                menubar->highlighted = menubar->highlighted->prev;
                            }
                            widget->needs_paint = true;
                        }
                        return true;

                    case VG_KEY_DOWN:
                        // Move highlight down
                        if (menubar->highlighted) {
                            if (menubar->highlighted->next) {
                                menubar->highlighted = menubar->highlighted->next;
                                while (menubar->highlighted && menubar->highlighted->separator) {
                                    menubar->highlighted = menubar->highlighted->next;
                                }
                            }
                        } else if (menubar->open_menu->first_item) {
                            menubar->highlighted = menubar->open_menu->first_item;
                            while (menubar->highlighted && menubar->highlighted->separator) {
                                menubar->highlighted = menubar->highlighted->next;
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_LEFT:
                        // Move to previous menu
                        if (menubar->open_menu->prev) {
                            menubar->open_menu->open = false;
                            menubar->open_menu = menubar->open_menu->prev;
                            menubar->open_menu->open = true;
                            menubar->highlighted = NULL;
                            widget->needs_paint = true;
                        }
                        return true;

                    case VG_KEY_RIGHT:
                        // Move to next menu
                        if (menubar->open_menu->next) {
                            menubar->open_menu->open = false;
                            menubar->open_menu = menubar->open_menu->next;
                            menubar->open_menu->open = true;
                            menubar->highlighted = NULL;
                            widget->needs_paint = true;
                        }
                        return true;

                    case VG_KEY_ENTER:
                        // Execute highlighted item
                        if (menubar->highlighted && menubar->highlighted->enabled) {
                            if (menubar->highlighted->action) {
                                menubar->highlighted->action(menubar->highlighted->action_data);
                            }
                            menubar->open_menu->open = false;
                            menubar->open_menu = NULL;
                            menubar->menu_active = false;
                            menubar->highlighted = NULL;
                            widget->needs_paint = true;
                        }
                        return true;

                    default:
                        break;
                }
            }
            return false;

        default:
            break;
    }

    return false;
}

//=============================================================================
// MenuBar API
//=============================================================================

vg_menu_t* vg_menubar_add_menu(vg_menubar_t* menubar, const char* title) {
    if (!menubar) return NULL;

    vg_menu_t* menu = calloc(1, sizeof(vg_menu_t));
    if (!menu) return NULL;

    menu->title = title ? strdup(title) : strdup("Menu");
    menu->first_item = NULL;
    menu->last_item = NULL;
    menu->item_count = 0;
    menu->open = false;

    // Add to end of list
    if (menubar->last_menu) {
        menubar->last_menu->next = menu;
        menu->prev = menubar->last_menu;
        menubar->last_menu = menu;
    } else {
        menubar->first_menu = menu;
        menubar->last_menu = menu;
    }
    menubar->menu_count++;

    menubar->base.needs_paint = true;

    return menu;
}

vg_menu_item_t* vg_menu_add_item(vg_menu_t* menu, const char* text,
                                  const char* shortcut, void (*action)(void*), void* data) {
    if (!menu) return NULL;

    vg_menu_item_t* item = calloc(1, sizeof(vg_menu_item_t));
    if (!item) return NULL;

    item->text = text ? strdup(text) : NULL;
    item->shortcut = shortcut ? strdup(shortcut) : NULL;
    item->action = action;
    item->action_data = data;
    item->enabled = true;
    item->checked = false;
    item->separator = false;
    item->submenu = NULL;

    // Add to end of list
    if (menu->last_item) {
        menu->last_item->next = item;
        item->prev = menu->last_item;
        menu->last_item = item;
    } else {
        menu->first_item = item;
        menu->last_item = item;
    }
    menu->item_count++;

    return item;
}

vg_menu_item_t* vg_menu_add_separator(vg_menu_t* menu) {
    if (!menu) return NULL;

    vg_menu_item_t* item = calloc(1, sizeof(vg_menu_item_t));
    if (!item) return NULL;

    item->separator = true;
    item->enabled = false;

    // Add to end of list
    if (menu->last_item) {
        menu->last_item->next = item;
        item->prev = menu->last_item;
        menu->last_item = item;
    } else {
        menu->first_item = item;
        menu->last_item = item;
    }
    menu->item_count++;

    return item;
}

vg_menu_t* vg_menu_add_submenu(vg_menu_t* menu, const char* title) {
    if (!menu) return NULL;

    vg_menu_item_t* item = vg_menu_add_item(menu, title, NULL, NULL, NULL);
    if (!item) return NULL;

    item->submenu = calloc(1, sizeof(vg_menu_t));
    if (!item->submenu) return NULL;

    item->submenu->title = title ? strdup(title) : strdup("Submenu");

    return item->submenu;
}

void vg_menu_item_set_enabled(vg_menu_item_t* item, bool enabled) {
    if (item) {
        item->enabled = enabled;
    }
}

void vg_menu_item_set_checked(vg_menu_item_t* item, bool checked) {
    if (item) {
        item->checked = checked;
    }
}

void vg_menubar_set_font(vg_menubar_t* menubar, vg_font_t* font, float size) {
    if (!menubar) return;

    menubar->font = font;
    menubar->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    menubar->base.needs_paint = true;
}
