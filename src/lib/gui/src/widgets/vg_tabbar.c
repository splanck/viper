// vg_tabbar.c - TabBar widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void tabbar_destroy(vg_widget_t* widget);
static void tabbar_measure(vg_widget_t* widget, float available_width, float available_height);
static void tabbar_paint(vg_widget_t* widget, void* canvas);
static bool tabbar_handle_event(vg_widget_t* widget, vg_event_t* event);
static float get_tab_width(vg_tabbar_t* tabbar, vg_tab_t* tab);

//=============================================================================
// TabBar VTable
//=============================================================================

static vg_widget_vtable_t g_tabbar_vtable = {
    .destroy = tabbar_destroy,
    .measure = tabbar_measure,
    .arrange = NULL,
    .paint = tabbar_paint,
    .handle_event = tabbar_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// Helper Functions
//=============================================================================

static float get_tab_width(vg_tabbar_t* tabbar, vg_tab_t* tab) {
    if (!tabbar->font || !tab->title) {
        return 100.0f;
    }

    vg_text_metrics_t metrics;
    vg_font_measure_text(tabbar->font, tabbar->font_size, tab->title, &metrics);

    float width = metrics.width + tabbar->tab_padding * 2;

    // Add close button width if closable
    if (tab->closable) {
        width += tabbar->close_button_size + 4.0f;
    }

    // Clamp to max
    if (tabbar->max_tab_width > 0 && width > tabbar->max_tab_width) {
        width = tabbar->max_tab_width;
    }

    return width;
}

static vg_tab_t* find_tab_at_x(vg_tabbar_t* tabbar, float x) {
    float tab_x = -tabbar->scroll_x;

    for (vg_tab_t* tab = tabbar->first_tab; tab; tab = tab->next) {
        float width = get_tab_width(tabbar, tab);
        if (x >= tab_x && x < tab_x + width) {
            return tab;
        }
        tab_x += width;
    }
    return NULL;
}

static float get_tab_x(vg_tabbar_t* tabbar, vg_tab_t* target) {
    float x = 0;
    for (vg_tab_t* tab = tabbar->first_tab; tab && tab != target; tab = tab->next) {
        x += get_tab_width(tabbar, tab);
    }
    return x;
}

//=============================================================================
// TabBar Implementation
//=============================================================================

vg_tabbar_t* vg_tabbar_create(vg_widget_t* parent) {
    vg_tabbar_t* tabbar = calloc(1, sizeof(vg_tabbar_t));
    if (!tabbar) return NULL;

    // Initialize base widget
    vg_widget_init(&tabbar->base, VG_WIDGET_TABBAR, &g_tabbar_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Initialize tabbar-specific fields
    tabbar->first_tab = NULL;
    tabbar->last_tab = NULL;
    tabbar->active_tab = NULL;
    tabbar->tab_count = 0;

    tabbar->font = NULL;
    tabbar->font_size = theme->typography.size_normal;

    // Appearance
    tabbar->tab_height = 35.0f;
    tabbar->tab_padding = 12.0f;
    tabbar->close_button_size = 14.0f;
    tabbar->max_tab_width = 200.0f;
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

    // Set size
    tabbar->base.constraints.min_height = tabbar->tab_height;
    tabbar->base.constraints.preferred_height = tabbar->tab_height;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &tabbar->base);
    }

    return tabbar;
}

static void tabbar_destroy(vg_widget_t* widget) {
    vg_tabbar_t* tabbar = (vg_tabbar_t*)widget;

    // Free all tabs
    vg_tab_t* tab = tabbar->first_tab;
    while (tab) {
        vg_tab_t* next = tab->next;
        if (tab->title) free((void*)tab->title);
        if (tab->tooltip) free((void*)tab->tooltip);
        free(tab);
        tab = next;
    }
}

static void tabbar_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_tabbar_t* tabbar = (vg_tabbar_t*)widget;
    (void)available_height;

    // Calculate total width of all tabs
    float total = 0;
    for (vg_tab_t* tab = tabbar->first_tab; tab; tab = tab->next) {
        total += get_tab_width(tabbar, tab);
    }
    tabbar->total_width = total;

    widget->measured_width = available_width > 0 ? available_width : total;
    widget->measured_height = tabbar->tab_height;
}

static void tabbar_paint(vg_widget_t* widget, void* canvas) {
    vg_tabbar_t* tabbar = (vg_tabbar_t*)widget;
    vg_theme_t* theme = vg_theme_get_current();

    // Draw background
    // TODO: Use vgfx primitives
    (void)theme;

    float tab_x = widget->x - tabbar->scroll_x;

    for (vg_tab_t* tab = tabbar->first_tab; tab; tab = tab->next) {
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
        // TODO: Use vgfx primitives
        (void)bg;

        // Draw tab title
        if (tabbar->font && tab->title) {
            vg_font_metrics_t font_metrics;
            vg_font_get_metrics(tabbar->font, tabbar->font_size, &font_metrics);

            float text_x = tab_x + tabbar->tab_padding;
            float text_y = widget->y + (widget->height + font_metrics.ascent - font_metrics.descent) / 2.0f;

            // Add modified indicator
            const char* title = tab->title;
            char modified_title[256];
            if (tab->modified) {
                snprintf(modified_title, sizeof(modified_title), "%s *", tab->title);
                title = modified_title;
            }

            vg_font_draw_text(canvas, tabbar->font, tabbar->font_size,
                              text_x, text_y, title, tabbar->text_color);
        }

        // Draw close button if closable
        if (tab->closable) {
            float close_x = tab_x + width - tabbar->tab_padding - tabbar->close_button_size;
            float close_y = widget->y + (widget->height - tabbar->close_button_size) / 2.0f;

            uint32_t close_color = tabbar->close_color;
            if (tab == tabbar->hovered_tab && tabbar->close_button_hovered) {
                close_color = theme->colors.accent_danger;
            }

            // Draw X button
            // TODO: Draw close button using vgfx primitives
            (void)close_x;
            (void)close_y;
            (void)close_color;
        }

        tab_x += width;
    }
}

static bool tabbar_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_tabbar_t* tabbar = (vg_tabbar_t*)widget;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            float local_x = event->mouse.x;
            vg_tab_t* old_hover = tabbar->hovered_tab;

            tabbar->hovered_tab = find_tab_at_x(tabbar, local_x);

            // Check if hovering close button
            if (tabbar->hovered_tab && tabbar->hovered_tab->closable) {
                float tab_x = get_tab_x(tabbar, tabbar->hovered_tab) - tabbar->scroll_x;
                float width = get_tab_width(tabbar, tabbar->hovered_tab);
                float close_x = tab_x + width - tabbar->tab_padding - tabbar->close_button_size;

                tabbar->close_button_hovered = local_x >= close_x;
            } else {
                tabbar->close_button_hovered = false;
            }

            if (old_hover != tabbar->hovered_tab) {
                widget->needs_paint = true;
            }

            // Handle dragging
            if (tabbar->dragging && tabbar->drag_tab) {
                tabbar->drag_x = local_x;
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
            vg_tab_t* clicked = find_tab_at_x(tabbar, local_x);

            if (clicked) {
                // Check if clicking close button
                if (clicked->closable) {
                    float tab_x = get_tab_x(tabbar, clicked) - tabbar->scroll_x;
                    float width = get_tab_width(tabbar, clicked);
                    float close_x = tab_x + width - tabbar->tab_padding - tabbar->close_button_size;

                    if (local_x >= close_x) {
                        // Close tab
                        bool allow_close = true;
                        if (tabbar->on_close) {
                            allow_close = tabbar->on_close(widget, clicked, tabbar->on_close_data);
                        }
                        if (allow_close) {
                            vg_tabbar_remove_tab(tabbar, clicked);
                        }
                        return true;
                    }
                }

                // Start potential drag
                tabbar->dragging = true;
                tabbar->drag_tab = clicked;
                tabbar->drag_x = local_x;

                // Activate tab
                vg_tabbar_set_active(tabbar, clicked);
                return true;
            }
            return false;
        }

        case VG_EVENT_MOUSE_UP:
            tabbar->dragging = false;
            tabbar->drag_tab = NULL;
            return false;

        case VG_EVENT_MOUSE_WHEEL:
            // Horizontal scroll with mouse wheel
            tabbar->scroll_x -= event->wheel.delta_y * 30.0f;
            if (tabbar->scroll_x < 0) tabbar->scroll_x = 0;
            if (tabbar->scroll_x > tabbar->total_width - widget->width) {
                tabbar->scroll_x = tabbar->total_width - widget->width;
                if (tabbar->scroll_x < 0) tabbar->scroll_x = 0;
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

vg_tab_t* vg_tabbar_add_tab(vg_tabbar_t* tabbar, const char* title, bool closable) {
    if (!tabbar) return NULL;

    vg_tab_t* tab = calloc(1, sizeof(vg_tab_t));
    if (!tab) return NULL;

    tab->title = title ? strdup(title) : strdup("Untitled");
    tab->tooltip = NULL;
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

void vg_tabbar_remove_tab(vg_tabbar_t* tabbar, vg_tab_t* tab) {
    if (!tabbar || !tab) return;

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
    if (tab->title) free((void*)tab->title);
    if (tab->tooltip) free((void*)tab->tooltip);
    free(tab);

    tabbar->base.needs_layout = true;
    tabbar->base.needs_paint = true;
}

void vg_tabbar_set_active(vg_tabbar_t* tabbar, vg_tab_t* tab) {
    if (!tabbar || tabbar->active_tab == tab) return;

    tabbar->active_tab = tab;
    tabbar->base.needs_paint = true;

    if (tabbar->on_select && tab) {
        tabbar->on_select(&tabbar->base, tab, tabbar->on_select_data);
    }
}

vg_tab_t* vg_tabbar_get_active(vg_tabbar_t* tabbar) {
    return tabbar ? tabbar->active_tab : NULL;
}

void vg_tab_set_title(vg_tab_t* tab, const char* title) {
    if (!tab) return;

    if (tab->title) {
        free((void*)tab->title);
    }
    tab->title = title ? strdup(title) : strdup("Untitled");
}

void vg_tab_set_modified(vg_tab_t* tab, bool modified) {
    if (tab) {
        tab->modified = modified;
    }
}

void vg_tab_set_data(vg_tab_t* tab, void* data) {
    if (tab) {
        tab->user_data = data;
    }
}

void vg_tabbar_set_font(vg_tabbar_t* tabbar, vg_font_t* font, float size) {
    if (!tabbar) return;

    tabbar->font = font;
    tabbar->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    tabbar->base.needs_layout = true;
    tabbar->base.needs_paint = true;
}

void vg_tabbar_set_on_select(vg_tabbar_t* tabbar, vg_tab_select_callback_t callback, void* user_data) {
    if (!tabbar) return;
    tabbar->on_select = callback;
    tabbar->on_select_data = user_data;
}

void vg_tabbar_set_on_close(vg_tabbar_t* tabbar, vg_tab_close_callback_t callback, void* user_data) {
    if (!tabbar) return;
    tabbar->on_close = callback;
    tabbar->on_close_data = user_data;
}

void vg_tabbar_set_on_reorder(vg_tabbar_t* tabbar, vg_tab_reorder_callback_t callback, void* user_data) {
    if (!tabbar) return;
    tabbar->on_reorder = callback;
    tabbar->on_reorder_data = user_data;
}
