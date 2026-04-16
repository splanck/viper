//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_tabbar.c
//
//===----------------------------------------------------------------------===//
// vg_tabbar.c - TabBar widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABBAR_DEFAULT_HEIGHT 35.0f
#define TABBAR_DEFAULT_PADDING 12.0f
#define TABBAR_DEFAULT_CLOSE_SIZE 14.0f
#define TABBAR_DEFAULT_MAX_WIDTH 200.0f
#define TABBAR_CLOSE_GAP 4.0f

//=============================================================================
// Forward Declarations
//=============================================================================

static void tabbar_destroy(vg_widget_t *widget);
static void tabbar_measure(vg_widget_t *widget, float available_width, float available_height);
static void tabbar_paint(vg_widget_t *widget, void *canvas);
static bool tabbar_handle_event(vg_widget_t *widget, vg_event_t *event);
static float get_tab_width(vg_tabbar_t *tabbar, vg_tab_t *tab);
static bool tab_close_button_hit(vg_tabbar_t *tabbar, vg_tab_t *tab, float local_x, float local_y);

//=============================================================================
// TabBar VTable
//=============================================================================

static vg_widget_vtable_t g_tabbar_vtable = {.destroy = tabbar_destroy,
                                             .measure = tabbar_measure,
                                             .arrange = NULL,
                                             .paint = tabbar_paint,
                                             .handle_event = tabbar_handle_event,
                                             .can_focus = NULL,
                                             .on_focus = NULL};

//=============================================================================
// Helper Functions
//=============================================================================

static float get_tab_width(vg_tabbar_t *tabbar, vg_tab_t *tab) {
    if (!tabbar->font || !tab->title) {
        return tabbar->max_tab_width > 0.0f ? tabbar->max_tab_width : 100.0f;
    }

    vg_text_metrics_t metrics;
    vg_font_measure_text(tabbar->font, tabbar->font_size, tab->title, &metrics);

    float width = metrics.width + tabbar->tab_padding * 2;

    // Add close button width if closable
    if (tab->closable) {
        float close_gap =
            (tabbar->close_button_size / TABBAR_DEFAULT_CLOSE_SIZE) * TABBAR_CLOSE_GAP;
        width += tabbar->close_button_size + close_gap;
    }

    // Clamp to max
    if (tabbar->max_tab_width > 0 && width > tabbar->max_tab_width) {
        width = tabbar->max_tab_width;
    }

    return width;
}

static char *make_tab_title(const vg_tab_t *tab) {
    if (!tab || !tab->title)
        return strdup("");

    if (!tab->modified)
        return strdup(tab->title);

    size_t len = strlen(tab->title);
    char *title = (char *)malloc(len + 3); /* " *\0" */
    if (!title)
        return NULL;
    memcpy(title, tab->title, len);
    title[len] = ' ';
    title[len + 1] = '*';
    title[len + 2] = '\0';
    return title;
}

static char *fit_tab_title(vg_tabbar_t *tabbar, const char *title, float max_width) {
    if (!title)
        return strdup("");
    if (!tabbar->font || max_width <= 0.0f)
        return strdup("");

    vg_text_metrics_t metrics;
    vg_font_measure_text(tabbar->font, tabbar->font_size, title, &metrics);
    if (metrics.width <= max_width)
        return strdup(title);

    vg_text_metrics_t ellipsis_metrics;
    vg_font_measure_text(tabbar->font, tabbar->font_size, "...", &ellipsis_metrics);
    if (ellipsis_metrics.width > max_width)
        return strdup("");

    size_t len = strlen(title);
    char *buf = (char *)malloc(len + 4); /* original text + "...\0" */
    if (!buf)
        return NULL;

    while (len > 0) {
        memcpy(buf, title, len);
        memcpy(buf + len, "...", 4);
        vg_font_measure_text(tabbar->font, tabbar->font_size, buf, &metrics);
        if (metrics.width <= max_width)
            return buf;
        len--;
    }

    memcpy(buf, "...", 4);
    return buf;
}

static vg_tab_t *find_tab_at_x(vg_tabbar_t *tabbar, float x) {
    float tab_x = -tabbar->scroll_x;

    for (vg_tab_t *tab = tabbar->first_tab; tab; tab = tab->next) {
        float width = get_tab_width(tabbar, tab);
        if (x >= tab_x && x < tab_x + width) {
            return tab;
        }
        tab_x += width;
    }
    return NULL;
}

static float get_tab_x(vg_tabbar_t *tabbar, vg_tab_t *target) {
    float x = 0;
    for (vg_tab_t *tab = tabbar->first_tab; tab && tab != target; tab = tab->next) {
        x += get_tab_width(tabbar, tab);
    }
    return x;
}

static int tabbar_target_index_from_x(vg_tabbar_t *tabbar, float local_x) {
    if (!tabbar || tabbar->tab_count <= 1)
        return 0;

    float tab_x = -tabbar->scroll_x;
    int index = 0;
    for (vg_tab_t *tab = tabbar->first_tab; tab; tab = tab->next, index++) {
        float width = get_tab_width(tabbar, tab);
        if (local_x < tab_x + width * 0.5f)
            return index;
        tab_x += width;
    }
    return tabbar->tab_count - 1;
}

static bool tabbar_move_tab_to_index(vg_tabbar_t *tabbar, vg_tab_t *tab, int new_index) {
    if (!tabbar || !tab || tabbar->tab_count < 2)
        return false;

    int current_index = vg_tabbar_get_tab_index(tabbar, tab);
    if (current_index < 0)
        return false;
    if (new_index < 0)
        new_index = 0;
    if (new_index >= tabbar->tab_count)
        new_index = tabbar->tab_count - 1;
    if (new_index == current_index)
        return false;

    if (tab->prev) {
        tab->prev->next = tab->next;
    } else {
        tabbar->first_tab = tab->next;
    }
    if (tab->next) {
        tab->next->prev = tab->prev;
    } else {
        tabbar->last_tab = tab->prev;
    }

    tab->prev = NULL;
    tab->next = NULL;

    if (new_index <= 0 || !tabbar->first_tab) {
        tab->next = tabbar->first_tab;
        if (tabbar->first_tab)
            tabbar->first_tab->prev = tab;
        tabbar->first_tab = tab;
        if (!tabbar->last_tab)
            tabbar->last_tab = tab;
    } else {
        vg_tab_t *insert_before = vg_tabbar_get_tab_at(tabbar, new_index);
        if (!insert_before) {
            tab->prev = tabbar->last_tab;
            if (tabbar->last_tab)
                tabbar->last_tab->next = tab;
            tabbar->last_tab = tab;
            if (!tabbar->first_tab)
                tabbar->first_tab = tab;
        } else {
            tab->next = insert_before;
            tab->prev = insert_before->prev;
            if (insert_before->prev) {
                insert_before->prev->next = tab;
            } else {
                tabbar->first_tab = tab;
            }
            insert_before->prev = tab;
        }
    }

    tabbar->base.needs_layout = true;
    tabbar->base.needs_paint = true;
    return true;
}

static bool tab_close_button_hit(vg_tabbar_t *tabbar, vg_tab_t *tab, float local_x, float local_y) {
    if (!tabbar || !tab || !tab->closable)
        return false;

    float tab_x = get_tab_x(tabbar, tab) - tabbar->scroll_x;
    float width = get_tab_width(tabbar, tab);
    float close_x = tab_x + width - tabbar->tab_padding - tabbar->close_button_size;
    float close_y = (tabbar->tab_height - tabbar->close_button_size) / 2.0f;
    float close_w = tabbar->close_button_size;
    float close_h = tabbar->close_button_size;

    return local_x >= close_x && local_x < close_x + close_w && local_y >= close_y &&
           local_y < close_y + close_h;
}

//=============================================================================
// TabBar Implementation
//=============================================================================

vg_tabbar_t *vg_tabbar_create(vg_widget_t *parent) {
    vg_tabbar_t *tabbar = calloc(1, sizeof(vg_tabbar_t));
    if (!tabbar)
        return NULL;

    // Initialize base widget
    vg_widget_init(&tabbar->base, VG_WIDGET_TABBAR, &g_tabbar_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize tabbar-specific fields
    tabbar->first_tab = NULL;
    tabbar->last_tab = NULL;
    tabbar->active_tab = NULL;
    tabbar->tab_count = 0;

    tabbar->font = NULL;
    tabbar->font_size = theme->typography.size_normal;

    // Appearance
    float s = theme->ui_scale > 0.0f ? theme->ui_scale : 1.0f;
    tabbar->tab_height = TABBAR_DEFAULT_HEIGHT * s;
    tabbar->tab_padding = TABBAR_DEFAULT_PADDING * s;
    tabbar->close_button_size = TABBAR_DEFAULT_CLOSE_SIZE * s;
    tabbar->max_tab_width = TABBAR_DEFAULT_MAX_WIDTH * s;
    tabbar->active_bg = theme->colors.bg_primary;
    tabbar->inactive_bg = theme->colors.bg_secondary;
    tabbar->text_color = theme->colors.fg_primary;
    tabbar->close_color = theme->colors.fg_secondary;

    // Scrolling
    tabbar->scroll_x = 0;
    tabbar->total_width = 0;

    // Callbacks
    tabbar->on_select = NULL;
    tabbar->on_select_data = NULL;
    tabbar->on_close = NULL;
    tabbar->on_close_data = NULL;
    tabbar->on_reorder = NULL;
    tabbar->on_reorder_data = NULL;

    // State
    tabbar->hovered_tab = NULL;
    tabbar->close_button_hovered = false;
    tabbar->dragging = false;
    tabbar->drag_tab = NULL;
    tabbar->drag_x = 0;

    // Per-frame tracking
    tabbar->prev_active_tab = NULL;
    tabbar->close_clicked_index = -1;
    tabbar->auto_close = true;

    // Set size
    tabbar->base.constraints.min_height = tabbar->tab_height;
    tabbar->base.constraints.preferred_height = tabbar->tab_height;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &tabbar->base);
    }

    return tabbar;
}

static void tabbar_destroy(vg_widget_t *widget) {
    vg_tabbar_t *tabbar = (vg_tabbar_t *)widget;

    if (vg_widget_get_input_capture() == widget)
        vg_widget_release_input_capture();

    // Free all tabs
    vg_tab_t *tab = tabbar->first_tab;
    while (tab) {
        vg_tab_t *next = tab->next;
        if (tab->title)
            free((void *)tab->title);
        if (tab->tooltip)
            free((void *)tab->tooltip);
        free(tab);
        tab = next;
    }
}

static void tabbar_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_tabbar_t *tabbar = (vg_tabbar_t *)widget;
    (void)available_height;

    // Calculate total width of all tabs
    float total = 0;
    for (vg_tab_t *tab = tabbar->first_tab; tab; tab = tab->next) {
        total += get_tab_width(tabbar, tab);
    }
    tabbar->total_width = total;

    widget->measured_width = available_width > 0 ? available_width : total;
    widget->measured_height = tabbar->tab_height;

    // Re-clamp scroll_x against the new total. When tabs are removed, the old
    // scroll_x can exceed the new max and the paint loop culls every tab.
    float visible_width = widget->measured_width;
    float max_scroll = total - visible_width;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (tabbar->scroll_x > max_scroll)
        tabbar->scroll_x = max_scroll;
    if (tabbar->scroll_x < 0.0f)
        tabbar->scroll_x = 0.0f;
}

static void tabbar_paint(vg_widget_t *widget, void *canvas) {
    vg_tabbar_t *tabbar = (vg_tabbar_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();

    vgfx_window_t win = (vgfx_window_t)canvas;

    // Draw tab bar background
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   theme->colors.bg_secondary);

    float tab_x = widget->x - tabbar->scroll_x;

    for (vg_tab_t *tab = tabbar->first_tab; tab; tab = tab->next) {
        float width = get_tab_width(tabbar, tab);

        // Skip if not visible
        if (tab_x + width < widget->x || tab_x > widget->x + widget->width) {
            tab_x += width;
            continue;
        }

        // Determine background color
        uint32_t bg = (tab == tabbar->active_tab) ? tabbar->active_bg : tabbar->inactive_bg;
        if (tab == tabbar->hovered_tab && tab != tabbar->active_tab) {
            bg = theme->colors.bg_hover;
        }

        // Draw tab background
        vgfx_fill_rect(
            win, (int32_t)tab_x, (int32_t)widget->y, (int32_t)width, (int32_t)widget->height, bg);

        // Draw tab title
        if (tabbar->font && tab->title) {
            vg_font_metrics_t font_metrics;
            vg_font_get_metrics(tabbar->font, tabbar->font_size, &font_metrics);

            float text_x = tab_x + tabbar->tab_padding;
            float text_y =
                widget->y + (widget->height + font_metrics.ascent + font_metrics.descent) / 2.0f;

            float text_max_width = width - tabbar->tab_padding * 2.0f;
            if (tab->closable) {
                float close_gap =
                    (tabbar->close_button_size / TABBAR_DEFAULT_CLOSE_SIZE) * TABBAR_CLOSE_GAP;
                text_max_width -= tabbar->close_button_size + close_gap;
            }

            char *title = make_tab_title(tab);
            char *fitted_title = fit_tab_title(tabbar, title ? title : "", text_max_width);
            if (fitted_title && fitted_title[0] != '\0') {
                vg_font_draw_text(canvas,
                                  tabbar->font,
                                  tabbar->font_size,
                                  text_x,
                                  text_y,
                                  fitted_title,
                                  tabbar->text_color);
            }
            free(fitted_title);
            free(title);
        }

        // Draw close button if closable
        if (tab->closable) {
            float close_x = tab_x + width - tabbar->tab_padding - tabbar->close_button_size;
            float close_y = widget->y + (widget->height - tabbar->close_button_size) / 2.0f;

            uint32_t close_color = tabbar->close_color;
            if (tab == tabbar->hovered_tab && tabbar->close_button_hovered) {
                close_color = theme->colors.accent_danger;
            }

            // Draw X button as two crossing diagonal lines
            int32_t cx = (int32_t)(close_x + tabbar->close_button_size / 2.0f);
            int32_t cy = (int32_t)(close_y + tabbar->close_button_size / 2.0f);
            int32_t r = (int32_t)(tabbar->close_button_size / 2.0f) - 1;
            vgfx_line(win, cx - r, cy - r, cx + r, cy + r, close_color);
            vgfx_line(win, cx - r, cy + r, cx + r, cy - r, close_color);
        }

        tab_x += width;
    }
}

static bool tabbar_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_tabbar_t *tabbar = (vg_tabbar_t *)widget;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            float local_x = event->mouse.x;
            vg_tab_t *old_hover = tabbar->hovered_tab;
            bool old_close_hover = tabbar->close_button_hovered;

            tabbar->hovered_tab = find_tab_at_x(tabbar, local_x);

            // Check if hovering close button
            tabbar->close_button_hovered =
                tab_close_button_hit(tabbar, tabbar->hovered_tab, local_x, event->mouse.y);

            if (old_hover != tabbar->hovered_tab || old_close_hover != tabbar->close_button_hovered) {
                widget->needs_paint = true;
            }

            // Handle dragging
            if (tabbar->dragging && tabbar->drag_tab) {
                tabbar->drag_x = local_x;
                int new_index = tabbar_target_index_from_x(tabbar, local_x);
                if (tabbar_move_tab_to_index(tabbar, tabbar->drag_tab, new_index) &&
                    tabbar->on_reorder) {
                    tabbar->on_reorder(
                        widget, tabbar->drag_tab, new_index, tabbar->on_reorder_data);
                }
                widget->needs_paint = true;
            }

            return false;
        }

        case VG_EVENT_MOUSE_LEAVE:
            tabbar->hovered_tab = NULL;
            tabbar->close_button_hovered = false;
            widget->needs_paint = true;
            return false;

        case VG_EVENT_MOUSE_DOWN: {
            float local_x = event->mouse.x;
            vg_tab_t *clicked = find_tab_at_x(tabbar, local_x);

            if (clicked) {
                // Check if clicking close button
                if (tab_close_button_hit(tabbar, clicked, local_x, event->mouse.y)) {
                    // Record close-clicked tab for runtime polling before any mutation.
                    tabbar->close_clicked_index = vg_tabbar_get_tab_index(tabbar, clicked);

                    // Close tab
                    bool allow_close = true;
                    if (tabbar->on_close) {
                        allow_close = tabbar->on_close(widget, clicked, tabbar->on_close_data);
                    }
                    if (allow_close && tabbar->auto_close) {
                        vg_tabbar_remove_tab(tabbar, clicked);
                    }
                    return true;
                }

                // Start potential drag
                tabbar->dragging = true;
                tabbar->drag_tab = clicked;
                tabbar->drag_x = local_x;
                vg_widget_set_input_capture(widget);

                // Activate tab
                vg_tabbar_set_active(tabbar, clicked);
                return true;
            }
            return false;
        }

        case VG_EVENT_MOUSE_UP:
            tabbar->dragging = false;
            tabbar->drag_tab = NULL;
            if (vg_widget_get_input_capture() == widget)
                vg_widget_release_input_capture();
            return false;

        case VG_EVENT_MOUSE_WHEEL:
            // Horizontal scroll with mouse wheel
            tabbar->scroll_x -= event->wheel.delta_y * 30.0f;
            if (tabbar->scroll_x < 0)
                tabbar->scroll_x = 0;
            if (tabbar->scroll_x > tabbar->total_width - widget->width) {
                tabbar->scroll_x = tabbar->total_width - widget->width;
                if (tabbar->scroll_x < 0)
                    tabbar->scroll_x = 0;
            }
            widget->needs_paint = true;
            return true;

        default:
            break;
    }

    return false;
}

//=============================================================================
// TabBar API
//=============================================================================

vg_tab_t *vg_tabbar_add_tab(vg_tabbar_t *tabbar, const char *title, bool closable) {
    if (!tabbar)
        return NULL;

    vg_tab_t *tab = calloc(1, sizeof(vg_tab_t));
    if (!tab)
        return NULL;

    tab->owner = tabbar;
    tab->title = title ? strdup(title) : strdup("Untitled");
    tab->tooltip = tab->title ? strdup(tab->title) : NULL;
    tab->user_data = NULL;
    tab->closable = closable;
    tab->modified = false;

    // Add to end of list
    if (tabbar->last_tab) {
        tabbar->last_tab->next = tab;
        tab->prev = tabbar->last_tab;
        tabbar->last_tab = tab;
    } else {
        tabbar->first_tab = tab;
        tabbar->last_tab = tab;
    }
    tabbar->tab_count++;

    // Set as active if first tab
    if (!tabbar->active_tab) {
        tabbar->active_tab = tab;
    }

    tabbar->base.needs_layout = true;
    tabbar->base.needs_paint = true;

    return tab;
}

/// @brief Tabbar remove tab.
void vg_tabbar_remove_tab(vg_tabbar_t *tabbar, vg_tab_t *tab) {
    if (!tabbar || !tab)
        return;

    // Update active tab if needed
    if (tabbar->active_tab == tab) {
        if (tab->next) {
            tabbar->active_tab = tab->next;
        } else if (tab->prev) {
            tabbar->active_tab = tab->prev;
        } else {
            tabbar->active_tab = NULL;
        }
    }

    // Update hover if needed
    if (tabbar->hovered_tab == tab) {
        tabbar->hovered_tab = NULL;
    }
    if (tabbar->drag_tab == tab) {
        tabbar->drag_tab = NULL;
        tabbar->dragging = false;
        if (vg_widget_get_input_capture() == &tabbar->base)
            vg_widget_release_input_capture();
    }

    // Remove from list
    if (tab->prev) {
        tab->prev->next = tab->next;
    } else {
        tabbar->first_tab = tab->next;
    }
    if (tab->next) {
        tab->next->prev = tab->prev;
    } else {
        tabbar->last_tab = tab->prev;
    }
    tabbar->tab_count--;

    // Free tab
    if (tab->title)
        free((void *)tab->title);
    if (tab->tooltip)
        free((void *)tab->tooltip);
    free(tab);

    tabbar->base.needs_layout = true;
    tabbar->base.needs_paint = true;
}

/// @brief Tabbar set active.
void vg_tabbar_set_active(vg_tabbar_t *tabbar, vg_tab_t *tab) {
    if (!tabbar || tabbar->active_tab == tab)
        return;

    tabbar->active_tab = tab;
    tabbar->base.needs_paint = true;

    if (tabbar->on_select && tab) {
        tabbar->on_select(&tabbar->base, tab, tabbar->on_select_data);
    }
}

vg_tab_t *vg_tabbar_get_active(vg_tabbar_t *tabbar) {
    return tabbar ? tabbar->active_tab : NULL;
}

/// @brief Tabbar get tab index.
int vg_tabbar_get_tab_index(vg_tabbar_t *tabbar, vg_tab_t *tab) {
    if (!tabbar || !tab)
        return -1;
    int index = 0;
    for (vg_tab_t *t = tabbar->first_tab; t; t = t->next) {
        if (t == tab)
            return index;
        index++;
    }
    return -1;
}

vg_tab_t *vg_tabbar_get_tab_at(vg_tabbar_t *tabbar, int index) {
    if (!tabbar || index < 0)
        return NULL;
    int i = 0;
    for (vg_tab_t *t = tabbar->first_tab; t; t = t->next) {
        if (i == index)
            return t;
        i++;
    }
    return NULL;
}

/// @brief Tab set title.
void vg_tab_set_title(vg_tab_t *tab, const char *title) {
    if (!tab)
        return;

    bool tooltip_tracks_title = false;
    if (!tab->tooltip) {
        tooltip_tracks_title = true;
    } else if (!tab->title) {
        tooltip_tracks_title = false;
    } else if (strcmp(tab->tooltip, tab->title) == 0) {
        tooltip_tracks_title = true;
    }

    if (tab->title) {
        free((void *)tab->title);
    }
    tab->title = title ? strdup(title) : strdup("Untitled");

    if (tooltip_tracks_title) {
        if (tab->tooltip) {
            free((void *)tab->tooltip);
        }
        tab->tooltip = tab->title ? strdup(tab->title) : NULL;
    }
    if (tab->owner) {
        tab->owner->base.needs_layout = true;
        tab->owner->base.needs_paint = true;
    }
}

/// @brief Tab set modified.
void vg_tab_set_modified(vg_tab_t *tab, bool modified) {
    if (tab) {
        tab->modified = modified;
        if (tab->owner)
            tab->owner->base.needs_paint = true;
    }
}

/// @brief Tab set tooltip.
void vg_tab_set_tooltip(vg_tab_t *tab, const char *tooltip) {
    if (!tab)
        return;

    if (tab->tooltip)
        free((void *)tab->tooltip);
    tab->tooltip = tooltip ? strdup(tooltip) : NULL;
    if (tab->owner)
        tab->owner->base.needs_paint = true;
}

/// @brief Tab set data.
void vg_tab_set_data(vg_tab_t *tab, void *data) {
    if (tab) {
        tab->user_data = data;
    }
}

/// @brief Tabbar set font.
void vg_tabbar_set_font(vg_tabbar_t *tabbar, vg_font_t *font, float size) {
    if (!tabbar)
        return;

    tabbar->font = font;
    tabbar->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    tabbar->base.needs_layout = true;
    tabbar->base.needs_paint = true;
}

/// @brief Tabbar set on select.
void vg_tabbar_set_on_select(vg_tabbar_t *tabbar,
                             vg_tab_select_callback_t callback,
                             void *user_data) {
    if (!tabbar)
        return;
    tabbar->on_select = callback;
    tabbar->on_select_data = user_data;
}

/// @brief Tabbar set on close.
void vg_tabbar_set_on_close(vg_tabbar_t *tabbar,
                            vg_tab_close_callback_t callback,
                            void *user_data) {
    if (!tabbar)
        return;
    tabbar->on_close = callback;
    tabbar->on_close_data = user_data;
}

/// @brief Tabbar set on reorder.
void vg_tabbar_set_on_reorder(vg_tabbar_t *tabbar,
                              vg_tab_reorder_callback_t callback,
                              void *user_data) {
    if (!tabbar)
        return;
    tabbar->on_reorder = callback;
    tabbar->on_reorder_data = user_data;
}
