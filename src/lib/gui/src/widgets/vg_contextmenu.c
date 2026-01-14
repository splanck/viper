// vg_contextmenu.c - ContextMenu widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void contextmenu_destroy(vg_widget_t* widget);
static void contextmenu_measure(vg_widget_t* widget, float available_width, float available_height);
static void contextmenu_paint(vg_widget_t* widget, void* canvas);
static bool contextmenu_handle_event(vg_widget_t* widget, vg_event_t* event);

//=============================================================================
// ContextMenu VTable
//=============================================================================

static vg_widget_vtable_t g_contextmenu_vtable = {
    .destroy = contextmenu_destroy,
    .measure = contextmenu_measure,
    .arrange = NULL,
    .paint = contextmenu_paint,
    .handle_event = contextmenu_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// Constants
//=============================================================================

#define ITEM_HEIGHT 28.0f
#define ITEM_PADDING_X 12.0f
#define ITEM_PADDING_Y 4.0f
#define SEPARATOR_HEIGHT 9.0f
#define SUBMENU_ARROW_WIDTH 20.0f
#define SHORTCUT_GAP 30.0f
#define SUBMENU_DELAY_MS 200

//=============================================================================
// Helper Functions
//=============================================================================

static vg_menu_item_t* create_menu_item(const char* label, const char* shortcut,
                                         void (*action)(void*), void* user_data) {
    vg_menu_item_t* item = calloc(1, sizeof(vg_menu_item_t));
    if (!item) return NULL;

    item->text = label ? strdup(label) : NULL;
    item->shortcut = shortcut ? strdup(shortcut) : NULL;
    item->action = action;
    item->action_data = user_data;
    item->enabled = true;
    item->checked = false;
    item->separator = false;
    item->submenu = NULL;

    return item;
}

static void free_menu_item(vg_menu_item_t* item) {
    if (item) {
        free((void*)item->text);
        free((void*)item->shortcut);
        free(item);
    }
}

static float get_item_height(vg_menu_item_t* item) {
    return item->separator ? SEPARATOR_HEIGHT : ITEM_HEIGHT;
}

static float calculate_menu_height(vg_contextmenu_t* menu) {
    float height = ITEM_PADDING_Y * 2; // Top and bottom padding
    for (size_t i = 0; i < menu->item_count; i++) {
        height += get_item_height(menu->items[i]);
    }
    return height;
}

static float calculate_menu_width(vg_contextmenu_t* menu) {
    float max_width = (float)menu->min_width;
    vg_font_t* font = menu->font;
    float font_size = menu->font_size;

    if (!font) return max_width;

    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t* item = menu->items[i];
        if (item->separator) continue;

        float width = ITEM_PADDING_X * 2;

        if (item->text) {
            vg_text_metrics_t metrics;
            vg_font_measure_text(font, font_size, item->text, &metrics);
            width += metrics.width;
        }

        if (item->shortcut) {
            vg_text_metrics_t metrics;
            vg_font_measure_text(font, font_size, item->shortcut, &metrics);
            width += SHORTCUT_GAP + metrics.width;
        }

        if (item->submenu) {
            width += SUBMENU_ARROW_WIDTH;
        }

        if (width > max_width) max_width = width;
    }

    return max_width;
}

static int get_item_at_y(vg_contextmenu_t* menu, float y) {
    float current_y = ITEM_PADDING_Y;
    for (size_t i = 0; i < menu->item_count; i++) {
        float item_height = get_item_height(menu->items[i]);
        if (y >= current_y && y < current_y + item_height) {
            return (int)i;
        }
        current_y += item_height;
    }
    return -1;
}

//=============================================================================
// ContextMenu Implementation
//=============================================================================

vg_contextmenu_t* vg_contextmenu_create(void) {
    vg_contextmenu_t* menu = calloc(1, sizeof(vg_contextmenu_t));
    if (!menu) return NULL;

    // Initialize base widget
    vg_widget_init(&menu->base, VG_WIDGET_CONTAINER, &g_contextmenu_vtable);

    vg_theme_t* theme = vg_theme_get_current();

    // Initialize context menu fields
    menu->items = NULL;
    menu->item_count = 0;
    menu->item_capacity = 0;

    menu->anchor_x = 0;
    menu->anchor_y = 0;

    menu->is_visible = false;
    menu->hovered_index = -1;
    menu->active_submenu = NULL;
    menu->parent_menu = NULL;

    menu->min_width = 150;
    menu->max_height = 400;

    menu->font = NULL;
    menu->font_size = theme->typography.size_normal;

    menu->bg_color = theme->colors.bg_primary;
    menu->hover_color = theme->colors.bg_hover;
    menu->text_color = theme->colors.fg_primary;
    menu->disabled_color = theme->colors.fg_secondary;
    menu->border_color = theme->colors.border_primary;
    menu->separator_color = theme->colors.border_secondary;

    menu->user_data = NULL;
    menu->on_select = NULL;
    menu->on_dismiss = NULL;

    return menu;
}

static void contextmenu_destroy(vg_widget_t* widget) {
    vg_contextmenu_t* menu = (vg_contextmenu_t*)widget;

    // Free all items
    for (size_t i = 0; i < menu->item_count; i++) {
        free_menu_item(menu->items[i]);
    }
    free(menu->items);
}

void vg_contextmenu_destroy(vg_contextmenu_t* menu) {
    if (menu) {
        contextmenu_destroy(&menu->base);
        free(menu);
    }
}

static void contextmenu_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_contextmenu_t* menu = (vg_contextmenu_t*)widget;
    (void)available_width;
    (void)available_height;

    widget->measured_width = calculate_menu_width(menu);
    widget->measured_height = calculate_menu_height(menu);

    // Apply max height
    if (menu->max_height > 0 && widget->measured_height > menu->max_height) {
        widget->measured_height = (float)menu->max_height;
    }
}

static void contextmenu_paint(vg_widget_t* widget, void* canvas) {
    vg_contextmenu_t* menu = (vg_contextmenu_t*)widget;
    vg_theme_t* theme = vg_theme_get_current();

    if (!menu->is_visible) return;

    float x = widget->x;
    float y = widget->y;
    float w = widget->width;
    float h = widget->height;

    // Draw shadow (offset)
    // TODO: Use vgfx primitives for shadow
    (void)theme;

    // Draw background
    // TODO: Use vgfx primitives
    (void)x;
    (void)y;
    (void)w;
    (void)h;

    // Draw border
    // TODO: Use vgfx primitives

    // Draw items
    float item_y = y + ITEM_PADDING_Y;
    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t* item = menu->items[i];
        float item_height = get_item_height(item);

        if (item->separator) {
            // Draw separator line
            float sep_y = item_y + item_height / 2;
            // TODO: Draw horizontal line at sep_y
            (void)sep_y;
        } else {
            // Draw hover background
            if ((int)i == menu->hovered_index && item->enabled) {
                // TODO: Draw hover background rectangle
            }

            // Draw text
            if (menu->font && item->text) {
                uint32_t text_color = item->enabled ? menu->text_color : menu->disabled_color;

                vg_font_metrics_t font_metrics;
                vg_font_get_metrics(menu->font, menu->font_size, &font_metrics);
                float text_y = item_y + (item_height + font_metrics.ascent - font_metrics.descent) / 2;

                // Draw checkmark if checked
                float text_x = x + ITEM_PADDING_X;
                if (item->checked) {
                    vg_font_draw_text(canvas, menu->font, menu->font_size,
                                      text_x, text_y, "\u2713", text_color);
                    text_x += 20;
                }

                // Draw label
                vg_font_draw_text(canvas, menu->font, menu->font_size,
                                  text_x, text_y, item->text, text_color);

                // Draw shortcut
                if (item->shortcut) {
                    vg_text_metrics_t shortcut_metrics;
                    vg_font_measure_text(menu->font, menu->font_size, item->shortcut, &shortcut_metrics);
                    float shortcut_x = x + w - ITEM_PADDING_X - shortcut_metrics.width;
                    if (item->submenu) shortcut_x -= SUBMENU_ARROW_WIDTH;

                    vg_font_draw_text(canvas, menu->font, menu->font_size,
                                      shortcut_x, text_y, item->shortcut, menu->disabled_color);
                }

                // Draw submenu arrow
                if (item->submenu) {
                    float arrow_x = x + w - ITEM_PADDING_X - 10;
                    vg_font_draw_text(canvas, menu->font, menu->font_size,
                                      arrow_x, text_y, ">", text_color);
                }
            }
        }

        item_y += item_height;
    }
}

static bool contextmenu_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_contextmenu_t* menu = (vg_contextmenu_t*)widget;

    if (!menu->is_visible) return false;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            float local_x = event->mouse.x - widget->x;
            float local_y = event->mouse.y - widget->y;

            // Check if inside menu
            if (local_x >= 0 && local_x < widget->width &&
                local_y >= 0 && local_y < widget->height) {

                int new_hover = get_item_at_y(menu, local_y);
                if (new_hover != menu->hovered_index) {
                    menu->hovered_index = new_hover;
                    widget->needs_paint = true;

                    // Close active submenu when moving to different item
                    if (menu->active_submenu) {
                        vg_contextmenu_dismiss(menu->active_submenu);
                        menu->active_submenu = NULL;
                    }

                    // Open submenu if hovering over submenu item
                    if (new_hover >= 0 && (size_t)new_hover < menu->item_count) {
                        vg_menu_item_t* item = menu->items[new_hover];
                        if (item->submenu && item->enabled) {
                            // Calculate submenu position
                            float item_y = ITEM_PADDING_Y;
                            for (int j = 0; j < new_hover; j++) {
                                item_y += get_item_height(menu->items[j]);
                            }
                            vg_contextmenu_t* submenu = (vg_contextmenu_t*)item->submenu;
                            vg_contextmenu_show_at(submenu,
                                (int)(widget->x + widget->width),
                                (int)(widget->y + item_y));
                            submenu->parent_menu = menu;
                            menu->active_submenu = submenu;
                        }
                    }
                }
                return true;
            } else {
                if (menu->hovered_index != -1) {
                    menu->hovered_index = -1;
                    widget->needs_paint = true;
                }
            }
            return false;
        }

        case VG_EVENT_MOUSE_DOWN: {
            float local_x = event->mouse.x - widget->x;
            float local_y = event->mouse.y - widget->y;

            // Check if inside menu
            if (local_x >= 0 && local_x < widget->width &&
                local_y >= 0 && local_y < widget->height) {

                int clicked = get_item_at_y(menu, local_y);
                if (clicked >= 0 && (size_t)clicked < menu->item_count) {
                    vg_menu_item_t* item = menu->items[clicked];

                    if (!item->separator && item->enabled && !item->submenu) {
                        // Invoke action
                        if (item->action) {
                            item->action(item->action_data);
                        }

                        // Invoke on_select callback
                        if (menu->on_select) {
                            menu->on_select(menu, item, menu->user_data);
                        }

                        // Dismiss entire menu chain
                        vg_contextmenu_t* root = menu;
                        while (root->parent_menu) {
                            root = root->parent_menu;
                        }
                        vg_contextmenu_dismiss(root);

                        return true;
                    }
                }
                return true;
            } else {
                // Clicked outside - dismiss
                // Find root menu
                vg_contextmenu_t* root = menu;
                while (root->parent_menu) {
                    root = root->parent_menu;
                }
                vg_contextmenu_dismiss(root);
                return false;
            }
        }

        case VG_EVENT_KEY_DOWN: {
            if (event->key.key == VG_KEY_ESCAPE) {
                vg_contextmenu_t* root = menu;
                while (root->parent_menu) {
                    root = root->parent_menu;
                }
                vg_contextmenu_dismiss(root);
                return true;
            }

            if (event->key.key == VG_KEY_UP) {
                // Move selection up
                int new_index = menu->hovered_index - 1;
                while (new_index >= 0 && menu->items[new_index]->separator) {
                    new_index--;
                }
                if (new_index >= 0) {
                    menu->hovered_index = new_index;
                    widget->needs_paint = true;
                }
                return true;
            }

            if (event->key.key == VG_KEY_DOWN) {
                // Move selection down
                int new_index = menu->hovered_index + 1;
                while ((size_t)new_index < menu->item_count && menu->items[new_index]->separator) {
                    new_index++;
                }
                if ((size_t)new_index < menu->item_count) {
                    menu->hovered_index = new_index;
                    widget->needs_paint = true;
                }
                return true;
            }

            if (event->key.key == VG_KEY_ENTER) {
                if (menu->hovered_index >= 0 && (size_t)menu->hovered_index < menu->item_count) {
                    vg_menu_item_t* item = menu->items[menu->hovered_index];
                    if (!item->separator && item->enabled && !item->submenu) {
                        if (item->action) {
                            item->action(item->action_data);
                        }
                        if (menu->on_select) {
                            menu->on_select(menu, item, menu->user_data);
                        }
                        vg_contextmenu_t* root = menu;
                        while (root->parent_menu) {
                            root = root->parent_menu;
                        }
                        vg_contextmenu_dismiss(root);
                    }
                }
                return true;
            }

            if (event->key.key == VG_KEY_RIGHT) {
                // Open submenu
                if (menu->hovered_index >= 0 && (size_t)menu->hovered_index < menu->item_count) {
                    vg_menu_item_t* item = menu->items[menu->hovered_index];
                    if (item->submenu && item->enabled) {
                        float item_y = ITEM_PADDING_Y;
                        for (int j = 0; j < menu->hovered_index; j++) {
                            item_y += get_item_height(menu->items[j]);
                        }
                        vg_contextmenu_t* submenu = (vg_contextmenu_t*)item->submenu;
                        vg_contextmenu_show_at(submenu,
                            (int)(widget->x + widget->width),
                            (int)(widget->y + item_y));
                        submenu->parent_menu = menu;
                        menu->active_submenu = submenu;
                        // Move focus to submenu
                        submenu->hovered_index = 0;
                    }
                }
                return true;
            }

            if (event->key.key == VG_KEY_LEFT) {
                // Close submenu / return to parent
                if (menu->parent_menu) {
                    vg_contextmenu_dismiss(menu);
                }
                return true;
            }

            return false;
        }

        default:
            break;
    }

    return false;
}

//=============================================================================
// ContextMenu API
//=============================================================================

vg_menu_item_t* vg_contextmenu_add_item(vg_contextmenu_t* menu,
    const char* label, const char* shortcut,
    void (*action)(void*), void* user_data) {
    if (!menu) return NULL;

    vg_menu_item_t* item = create_menu_item(label, shortcut, action, user_data);
    if (!item) return NULL;

    // Add to array
    if (menu->item_count >= menu->item_capacity) {
        size_t new_cap = menu->item_capacity == 0 ? 8 : menu->item_capacity * 2;
        vg_menu_item_t** new_items = realloc(menu->items, new_cap * sizeof(vg_menu_item_t*));
        if (!new_items) {
            free_menu_item(item);
            return NULL;
        }
        menu->items = new_items;
        menu->item_capacity = new_cap;
    }

    menu->items[menu->item_count++] = item;
    return item;
}

vg_menu_item_t* vg_contextmenu_add_submenu(vg_contextmenu_t* menu,
    const char* label, vg_contextmenu_t* submenu) {
    if (!menu || !submenu) return NULL;

    vg_menu_item_t* item = create_menu_item(label, NULL, NULL, NULL);
    if (!item) return NULL;

    item->submenu = (struct vg_menu*)submenu;

    // Add to array
    if (menu->item_count >= menu->item_capacity) {
        size_t new_cap = menu->item_capacity == 0 ? 8 : menu->item_capacity * 2;
        vg_menu_item_t** new_items = realloc(menu->items, new_cap * sizeof(vg_menu_item_t*));
        if (!new_items) {
            free_menu_item(item);
            return NULL;
        }
        menu->items = new_items;
        menu->item_capacity = new_cap;
    }

    menu->items[menu->item_count++] = item;
    return item;
}

void vg_contextmenu_add_separator(vg_contextmenu_t* menu) {
    if (!menu) return;

    vg_menu_item_t* item = calloc(1, sizeof(vg_menu_item_t));
    if (!item) return;

    item->separator = true;

    // Add to array
    if (menu->item_count >= menu->item_capacity) {
        size_t new_cap = menu->item_capacity == 0 ? 8 : menu->item_capacity * 2;
        vg_menu_item_t** new_items = realloc(menu->items, new_cap * sizeof(vg_menu_item_t*));
        if (!new_items) {
            free(item);
            return;
        }
        menu->items = new_items;
        menu->item_capacity = new_cap;
    }

    menu->items[menu->item_count++] = item;
}

void vg_contextmenu_clear(vg_contextmenu_t* menu) {
    if (!menu) return;

    for (size_t i = 0; i < menu->item_count; i++) {
        free_menu_item(menu->items[i]);
    }
    menu->item_count = 0;
}

void vg_contextmenu_item_set_enabled(vg_menu_item_t* item, bool enabled) {
    if (item) item->enabled = enabled;
}

void vg_contextmenu_item_set_checked(vg_menu_item_t* item, bool checked) {
    if (item) item->checked = checked;
}

void vg_contextmenu_item_set_icon(vg_menu_item_t* item, vg_icon_t icon) {
    (void)item;
    (void)icon;
    // Icons not yet implemented for menu items
}

void vg_contextmenu_show_at(vg_contextmenu_t* menu, int x, int y) {
    if (!menu) return;

    menu->anchor_x = x;
    menu->anchor_y = y;
    menu->is_visible = true;
    menu->hovered_index = -1;

    // Calculate size
    contextmenu_measure(&menu->base, 0, 0);

    // Position menu
    menu->base.x = (float)x;
    menu->base.y = (float)y;
    menu->base.width = menu->base.measured_width;
    menu->base.height = menu->base.measured_height;

    // TODO: Adjust position if would go off-screen

    menu->base.visible = true;
    menu->base.needs_paint = true;
}

void vg_contextmenu_show_for_widget(vg_contextmenu_t* menu,
    vg_widget_t* widget, int offset_x, int offset_y) {
    if (!menu || !widget) return;

    int x = (int)widget->x + offset_x;
    int y = (int)(widget->y + widget->height) + offset_y;
    vg_contextmenu_show_at(menu, x, y);
}

void vg_contextmenu_dismiss(vg_contextmenu_t* menu) {
    if (!menu) return;

    // Dismiss active submenu first
    if (menu->active_submenu) {
        vg_contextmenu_dismiss(menu->active_submenu);
        menu->active_submenu = NULL;
    }

    menu->is_visible = false;
    menu->hovered_index = -1;
    menu->parent_menu = NULL;
    menu->base.visible = false;

    // Invoke dismiss callback
    if (menu->on_dismiss) {
        menu->on_dismiss(menu, menu->user_data);
    }
}

void vg_contextmenu_set_on_select(vg_contextmenu_t* menu,
    void (*callback)(vg_contextmenu_t*, vg_menu_item_t*, void*), void* user_data) {
    if (!menu) return;
    menu->on_select = callback;
    menu->user_data = user_data;
}

void vg_contextmenu_set_on_dismiss(vg_contextmenu_t* menu,
    void (*callback)(vg_contextmenu_t*, void*), void* user_data) {
    if (!menu) return;
    menu->on_dismiss = callback;
    menu->user_data = user_data;
}

void vg_contextmenu_register_for_widget(vg_widget_t* widget, vg_contextmenu_t* menu) {
    (void)widget;
    (void)menu;
    // TODO: Store menu reference in widget and handle right-click events
}

void vg_contextmenu_unregister_for_widget(vg_widget_t* widget) {
    (void)widget;
    // TODO: Remove menu reference from widget
}

void vg_contextmenu_set_font(vg_contextmenu_t* menu, vg_font_t* font, float size) {
    if (!menu) return;
    menu->font = font;
    if (size > 0) menu->font_size = size;
}
