// vg_menubar.c - MenuBar widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widget.h"
#include <stdlib.h>
#include <string.h>

// For strcasecmp: Windows uses _stricmp, POSIX uses strcasecmp
// For strdup: Windows uses _strdup
// For strtok_r: Windows uses strtok_s
#ifdef _WIN32
#define strcasecmp _stricmp
#define strdup _strdup
#define strtok_r strtok_s
#else
#include <strings.h>
#endif

//=============================================================================
// Forward Declarations
//=============================================================================

static void menubar_destroy(vg_widget_t *widget);
static void menubar_measure(vg_widget_t *widget, float available_width, float available_height);
static void menubar_paint(vg_widget_t *widget, void *canvas);
static void menubar_paint_overlay(vg_widget_t *widget, void *canvas);
static bool menubar_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// MenuBar VTable
//=============================================================================

static vg_widget_vtable_t g_menubar_vtable = {.destroy = menubar_destroy,
                                              .measure = menubar_measure,
                                              .arrange = NULL,
                                              .paint = menubar_paint,
                                              .paint_overlay = menubar_paint_overlay,
                                              .handle_event = menubar_handle_event,
                                              .can_focus = NULL,
                                              .on_focus = NULL};

//=============================================================================
// Helper Functions
//=============================================================================

static void free_menu_item(vg_menu_item_t *item)
{
    if (!item)
        return;

    if (item->text)
        free((void *)item->text);
    if (item->shortcut)
        free((void *)item->shortcut);

    // Recursively free submenu
    if (item->submenu)
    {
        vg_menu_item_t *sub = item->submenu->first_item;
        while (sub)
        {
            vg_menu_item_t *next = sub->next;
            free_menu_item(sub);
            sub = next;
        }
        if (item->submenu->title)
            free((void *)item->submenu->title);
        free(item->submenu);
    }

    free(item);
}

static void free_menu(vg_menu_t *menu)
{
    if (!menu)
        return;

    vg_menu_item_t *item = menu->first_item;
    while (item)
    {
        vg_menu_item_t *next = item->next;
        free_menu_item(item);
        item = next;
    }

    if (menu->title)
        free((void *)menu->title);
    free(menu);
}

//=============================================================================
// MenuBar Implementation
//=============================================================================

vg_menubar_t *vg_menubar_create(vg_widget_t *parent)
{
    vg_menubar_t *menubar = calloc(1, sizeof(vg_menubar_t));
    if (!menubar)
        return NULL;

    // Initialize base widget
    vg_widget_init(&menubar->base, VG_WIDGET_MENUBAR, &g_menubar_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize menubar-specific fields
    menubar->first_menu = NULL;
    menubar->last_menu = NULL;
    menubar->menu_count = 0;
    menubar->open_menu = NULL;
    menubar->highlighted = NULL;

    menubar->font = NULL;
    menubar->font_size = theme->typography.size_normal;

    // Appearance — scale pixel constants by ui_scale so the menubar is the
    // correct visual size on HiDPI displays (e.g. 56px physical = 28pt visual
    // on a 2× Retina when ui_scale = 2.0).
    float s = theme->ui_scale > 0.0f ? theme->ui_scale : 1.0f;
    menubar->height = 28.0f * s;
    menubar->menu_padding = 10.0f * s;
    menubar->item_padding =  8.0f * s;
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
    if (parent)
    {
        vg_widget_add_child(parent, &menubar->base);
    }

    return menubar;
}

static void menubar_destroy(vg_widget_t *widget)
{
    vg_menubar_t *menubar = (vg_menubar_t *)widget;

    // Release input capture if this menubar holds it
    if (vg_widget_get_input_capture() == widget)
        vg_widget_release_input_capture();

    vg_menu_t *menu = menubar->first_menu;
    while (menu)
    {
        vg_menu_t *next = menu->next;
        free_menu(menu);
        menu = next;
    }
}

static void menubar_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_menubar_t *menubar = (vg_menubar_t *)widget;
    (void)available_height;

    widget->measured_width = available_width > 0 ? available_width : 400;
    widget->measured_height = menubar->height;
}

static void menubar_paint(vg_widget_t *widget, void *canvas)
{
    vg_menubar_t *menubar = (vg_menubar_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;

    // Draw menubar background
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   menubar->bg_color);

    if (!menubar->font)
        return;

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(menubar->font, menubar->font_size, &font_metrics);

    float text_y = widget->y + (widget->height + font_metrics.ascent + font_metrics.descent) / 2.0f;
    float menu_x = widget->x;

    // Draw each menu title
    for (vg_menu_t *menu = menubar->first_menu; menu; menu = menu->next)
    {
        if (!menu->title)
            continue;

        vg_text_metrics_t metrics;
        vg_font_measure_text(menubar->font, menubar->font_size, menu->title, &metrics);

        float menu_width = metrics.width + menubar->menu_padding * 2;

        // Draw highlight if this menu is open
        if (menu == menubar->open_menu)
        {
            vgfx_fill_rect(win,
                           (int32_t)menu_x,
                           (int32_t)widget->y,
                           (int32_t)menu_width,
                           (int32_t)widget->height,
                           menubar->highlight_bg);
        }

        // Draw menu title
        float text_x = menu_x + menubar->menu_padding;
        vg_font_draw_text(canvas,
                          menubar->font,
                          menubar->font_size,
                          text_x,
                          text_y,
                          menu->title,
                          menubar->text_color);

        menu_x += menu_width;
    }

    // NOTE: Dropdown is painted in paint_overlay() so it appears on top of other widgets
}

// Paint overlay - called after all widgets are painted to draw popups on top
static void menubar_paint_overlay(vg_widget_t *widget, void *canvas)
{
    vg_menubar_t *menubar = (vg_menubar_t *)widget;

    // Only draw if a menu is open
    if (!menubar->open_menu || !menubar->font)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(menubar->font, menubar->font_size, &font_metrics);

    // Find position of open menu
    float dropdown_x = widget->x;
    for (vg_menu_t *menu = menubar->first_menu; menu != menubar->open_menu; menu = menu->next)
    {
        if (menu->title)
        {
            vg_text_metrics_t metrics;
            vg_font_measure_text(menubar->font, menubar->font_size, menu->title, &metrics);
            dropdown_x += metrics.width + menubar->menu_padding * 2;
        }
    }

    // Calculate dropdown dimensions — scale by ui_scale for HiDPI.
    float dropdown_y = widget->y + widget->height;
    float _ds = vg_theme_get_current()->ui_scale;
    if (_ds <= 0.0f) _ds = 1.0f;
    float dropdown_width = 200.0f * _ds;
    float item_height = 28.0f * _ds;
    float dropdown_height = menubar->open_menu->item_count * item_height;

    // Get theme for colors
    vg_theme_t *theme = vg_theme_get_current();

    // Draw dropdown background (dark panel)
    vgfx_fill_rect(win,
                   (int32_t)dropdown_x,
                   (int32_t)dropdown_y,
                   (int32_t)dropdown_width,
                   (int32_t)dropdown_height,
                   menubar->bg_color);

    // Draw dropdown border
    uint32_t border_color = theme->colors.border_primary;
    // Top border
    vgfx_fill_rect(
        win, (int32_t)dropdown_x, (int32_t)dropdown_y, (int32_t)dropdown_width, 1, border_color);
    // Left border
    vgfx_fill_rect(
        win, (int32_t)dropdown_x, (int32_t)dropdown_y, 1, (int32_t)dropdown_height, border_color);
    // Right border
    vgfx_fill_rect(win,
                   (int32_t)(dropdown_x + dropdown_width - 1),
                   (int32_t)dropdown_y,
                   1,
                   (int32_t)dropdown_height,
                   border_color);
    // Bottom border
    vgfx_fill_rect(win,
                   (int32_t)dropdown_x,
                   (int32_t)(dropdown_y + dropdown_height - 1),
                   (int32_t)dropdown_width,
                   1,
                   border_color);

    // Draw menu items
    float item_y = dropdown_y;
    for (vg_menu_item_t *item = menubar->open_menu->first_item; item; item = item->next)
    {
        if (item->separator)
        {
            // Draw separator line
            int32_t sep_margin = 8;
            int32_t sep_y = (int32_t)(item_y + item_height / 2);
            vgfx_fill_rect(win,
                           (int32_t)dropdown_x + sep_margin,
                           sep_y,
                           (int32_t)dropdown_width - sep_margin * 2,
                           1,
                           theme->colors.border_secondary);
        }
        else
        {
            // Draw highlight if highlighted
            if (item == menubar->highlighted)
            {
                vgfx_fill_rect(win,
                               (int32_t)dropdown_x + 1,
                               (int32_t)item_y,
                               (int32_t)dropdown_width - 2,
                               (int32_t)item_height,
                               menubar->highlight_bg);
            }

            // Draw item text
            if (item->text)
            {
                float item_text_y =
                    item_y + (item_height + font_metrics.ascent + font_metrics.descent) / 2.0f;
                uint32_t color = item->enabled ? menubar->text_color : menubar->disabled_color;
                vg_font_draw_text(canvas,
                                  menubar->font,
                                  menubar->font_size,
                                  dropdown_x + menubar->item_padding,
                                  item_text_y,
                                  item->text,
                                  color);

                // Draw shortcut if present
                if (item->shortcut)
                {
                    vg_text_metrics_t shortcut_metrics;
                    vg_font_measure_text(
                        menubar->font, menubar->font_size, item->shortcut, &shortcut_metrics);
                    float shortcut_x = dropdown_x + dropdown_width - shortcut_metrics.width -
                                       menubar->item_padding;
                    vg_font_draw_text(canvas,
                                      menubar->font,
                                      menubar->font_size,
                                      shortcut_x,
                                      item_text_y,
                                      item->shortcut,
                                      menubar->disabled_color);
                }

                // Draw check mark if checked
                if (item->checked)
                {
                    float check_x = dropdown_x + 4;
                    float check_y = item_y + item_height / 2;
                    vgfx_fill_rect(
                        win, (int32_t)check_x, (int32_t)check_y, 3, 1, menubar->text_color);
                    vgfx_fill_rect(win,
                                   (int32_t)(check_x + 2),
                                   (int32_t)(check_y - 3),
                                   1,
                                   4,
                                   menubar->text_color);
                }

                // Draw submenu arrow if has submenu
                if (item->submenu)
                {
                    float arrow_x = dropdown_x + dropdown_width - 12;
                    float arrow_y = item_y + item_height / 2;
                    vgfx_fill_rect(
                        win, (int32_t)arrow_x, (int32_t)(arrow_y - 2), 1, 5, menubar->text_color);
                    vgfx_fill_rect(win,
                                   (int32_t)(arrow_x + 1),
                                   (int32_t)(arrow_y - 1),
                                   1,
                                   3,
                                   menubar->text_color);
                    vgfx_fill_rect(
                        win, (int32_t)(arrow_x + 2), (int32_t)arrow_y, 1, 1, menubar->text_color);
                }
            }
        }

        item_y += item_height;
    }
}

static vg_menu_t *find_menu_at_x(vg_menubar_t *menubar, float x)
{
    if (!menubar->font)
        return NULL;

    float menu_x = 0;
    for (vg_menu_t *menu = menubar->first_menu; menu; menu = menu->next)
    {
        if (!menu->title)
            continue;

        vg_text_metrics_t metrics;
        vg_font_measure_text(menubar->font, menubar->font_size, menu->title, &metrics);
        float menu_width = metrics.width + menubar->menu_padding * 2;

        if (x >= menu_x && x < menu_x + menu_width)
        {
            return menu;
        }

        menu_x += menu_width;
    }
    return NULL;
}

static bool menubar_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_menubar_t *menubar = (vg_menubar_t *)widget;

    switch (event->type)
    {
        case VG_EVENT_MOUSE_MOVE:
        {
            float local_x = event->mouse.x;
            float local_y = event->mouse.y;

            // Check if in menu bar area
            if (local_y < menubar->height)
            {
                vg_menu_t *menu = find_menu_at_x(menubar, local_x);
                if (menubar->menu_active && menu != menubar->open_menu)
                {
                    menubar->open_menu = menu;
                    if (menu)
                        menu->open = true;
                    menubar->highlighted = NULL;
                    widget->needs_paint = true;
                }
            }
            else if (menubar->open_menu)
            {
                // Check if in dropdown area — scale item_height by ui_scale.
                float _hs = vg_theme_get_current()->ui_scale;
                if (_hs <= 0.0f) _hs = 1.0f;
                float item_height = 28.0f * _hs;
                int item_index = (int)((local_y - menubar->height) / item_height);

                vg_menu_item_t *old_highlight = menubar->highlighted;
                menubar->highlighted = NULL;

                int idx = 0;
                for (vg_menu_item_t *item = menubar->open_menu->first_item; item; item = item->next)
                {
                    if (idx == item_index && !item->separator)
                    {
                        menubar->highlighted = item;
                        break;
                    }
                    idx++;
                }

                if (old_highlight != menubar->highlighted)
                {
                    widget->needs_paint = true;
                }
            }
            return false;
        }

        case VG_EVENT_CLICK:
        {
            float local_x = event->mouse.x;
            float local_y = event->mouse.y;

            // Check if clicking menu bar
            if (local_y >= 0 && local_y < menubar->height && local_x >= 0)
            {
                vg_menu_t *menu = find_menu_at_x(menubar, local_x);
                if (menu)
                {
                    if (menubar->open_menu == menu)
                    {
                        // Close menu
                        menubar->open_menu = NULL;
                        menubar->menu_active = false;
                        menu->open = false;
                        vg_widget_release_input_capture();
                    }
                    else
                    {
                        // Open menu
                        if (menubar->open_menu)
                        {
                            menubar->open_menu->open = false;
                        }
                        menubar->open_menu = menu;
                        menubar->menu_active = true;
                        menu->open = true;
                        vg_widget_set_input_capture(widget);
                    }
                    widget->needs_paint = true;
                    return true;
                }
            }

            if (menubar->open_menu && menubar->highlighted)
            {
                // Execute highlighted item
                vg_menu_item_t *item = menubar->highlighted;
                item->was_clicked = true;
                if (item->enabled && item->action)
                {
                    item->action(item->action_data);
                }

                // Close menu
                menubar->open_menu->open = false;
                menubar->open_menu = NULL;
                menubar->menu_active = false;
                menubar->highlighted = NULL;
                vg_widget_release_input_capture();
                widget->needs_paint = true;
                return true;
            }

            // Click outside both menubar and dropdown area — close menu
            if (menubar->open_menu)
            {
                menubar->open_menu->open = false;
                menubar->open_menu = NULL;
                menubar->menu_active = false;
                menubar->highlighted = NULL;
                vg_widget_release_input_capture();
                widget->needs_paint = true;
                return true;
            }

            return false;
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (!menubar->menu_active)
            {
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_KEY_DOWN:
            if (menubar->open_menu)
            {
                switch (event->key.key)
                {
                    case VG_KEY_ESCAPE:
                        menubar->open_menu->open = false;
                        menubar->open_menu = NULL;
                        menubar->menu_active = false;
                        menubar->highlighted = NULL;
                        vg_widget_release_input_capture();
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_UP:
                        // Move highlight up
                        if (menubar->highlighted && menubar->highlighted->prev)
                        {
                            menubar->highlighted = menubar->highlighted->prev;
                            while (menubar->highlighted && menubar->highlighted->separator)
                            {
                                menubar->highlighted = menubar->highlighted->prev;
                            }
                            widget->needs_paint = true;
                        }
                        return true;

                    case VG_KEY_DOWN:
                        // Move highlight down
                        if (menubar->highlighted)
                        {
                            if (menubar->highlighted->next)
                            {
                                menubar->highlighted = menubar->highlighted->next;
                                while (menubar->highlighted && menubar->highlighted->separator)
                                {
                                    menubar->highlighted = menubar->highlighted->next;
                                }
                            }
                        }
                        else if (menubar->open_menu->first_item)
                        {
                            menubar->highlighted = menubar->open_menu->first_item;
                            while (menubar->highlighted && menubar->highlighted->separator)
                            {
                                menubar->highlighted = menubar->highlighted->next;
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_LEFT:
                        // Move to previous menu
                        if (menubar->open_menu->prev)
                        {
                            menubar->open_menu->open = false;
                            menubar->open_menu = menubar->open_menu->prev;
                            menubar->open_menu->open = true;
                            menubar->highlighted = NULL;
                            widget->needs_paint = true;
                        }
                        return true;

                    case VG_KEY_RIGHT:
                        // Move to next menu
                        if (menubar->open_menu->next)
                        {
                            menubar->open_menu->open = false;
                            menubar->open_menu = menubar->open_menu->next;
                            menubar->open_menu->open = true;
                            menubar->highlighted = NULL;
                            widget->needs_paint = true;
                        }
                        return true;

                    case VG_KEY_ENTER:
                        // Execute highlighted item
                        if (menubar->highlighted && menubar->highlighted->enabled)
                        {
                            menubar->highlighted->was_clicked = true;
                            if (menubar->highlighted->action)
                            {
                                menubar->highlighted->action(menubar->highlighted->action_data);
                            }
                            menubar->open_menu->open = false;
                            menubar->open_menu = NULL;
                            menubar->menu_active = false;
                            menubar->highlighted = NULL;
                            vg_widget_release_input_capture();
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

vg_menu_t *vg_menubar_add_menu(vg_menubar_t *menubar, const char *title)
{
    if (!menubar)
        return NULL;

    vg_menu_t *menu = calloc(1, sizeof(vg_menu_t));
    if (!menu)
        return NULL;

    menu->title = title ? strdup(title) : strdup("Menu");
    menu->first_item = NULL;
    menu->last_item = NULL;
    menu->item_count = 0;
    menu->open = false;

    // Add to end of list
    if (menubar->last_menu)
    {
        menubar->last_menu->next = menu;
        menu->prev = menubar->last_menu;
        menubar->last_menu = menu;
    }
    else
    {
        menubar->first_menu = menu;
        menubar->last_menu = menu;
    }
    menubar->menu_count++;

    menubar->base.needs_paint = true;

    return menu;
}

vg_menu_item_t *vg_menu_add_item(
    vg_menu_t *menu, const char *text, const char *shortcut, void (*action)(void *), void *data)
{
    if (!menu)
        return NULL;

    vg_menu_item_t *item = calloc(1, sizeof(vg_menu_item_t));
    if (!item)
        return NULL;

    item->text = text ? strdup(text) : NULL;
    item->shortcut = shortcut ? strdup(shortcut) : NULL;
    item->action = action;
    item->action_data = data;
    item->enabled = true;
    item->checked = false;
    item->separator = false;
    item->submenu = NULL;

    // Add to end of list
    if (menu->last_item)
    {
        menu->last_item->next = item;
        item->prev = menu->last_item;
        menu->last_item = item;
    }
    else
    {
        menu->first_item = item;
        menu->last_item = item;
    }
    menu->item_count++;

    return item;
}

vg_menu_item_t *vg_menu_add_separator(vg_menu_t *menu)
{
    if (!menu)
        return NULL;

    vg_menu_item_t *item = calloc(1, sizeof(vg_menu_item_t));
    if (!item)
        return NULL;

    item->separator = true;
    item->enabled = false;

    // Add to end of list
    if (menu->last_item)
    {
        menu->last_item->next = item;
        item->prev = menu->last_item;
        menu->last_item = item;
    }
    else
    {
        menu->first_item = item;
        menu->last_item = item;
    }
    menu->item_count++;

    return item;
}

vg_menu_t *vg_menu_add_submenu(vg_menu_t *menu, const char *title)
{
    if (!menu)
        return NULL;

    vg_menu_item_t *item = vg_menu_add_item(menu, title, NULL, NULL, NULL);
    if (!item)
        return NULL;

    item->submenu = calloc(1, sizeof(vg_menu_t));
    if (!item->submenu)
        return NULL;

    item->submenu->title = title ? strdup(title) : strdup("Submenu");

    return item->submenu;
}

void vg_menu_item_set_enabled(vg_menu_item_t *item, bool enabled)
{
    if (item)
    {
        item->enabled = enabled;
    }
}

void vg_menu_item_set_checked(vg_menu_item_t *item, bool checked)
{
    if (item)
    {
        item->checked = checked;
    }
}

void vg_menu_remove_item(vg_menu_t *menu, vg_menu_item_t *item)
{
    if (!menu || !item)
        return;

    if (item->prev)
        item->prev->next = item->next;
    else
        menu->first_item = item->next;

    if (item->next)
        item->next->prev = item->prev;
    else
        menu->last_item = item->prev;

    menu->item_count--;
    free_menu_item(item);
}

void vg_menu_clear(vg_menu_t *menu)
{
    if (!menu)
        return;

    vg_menu_item_t *item = menu->first_item;
    while (item)
    {
        vg_menu_item_t *next = item->next;
        free_menu_item(item);
        item = next;
    }

    menu->first_item = menu->last_item = NULL;
    menu->item_count = 0;
}

void vg_menubar_remove_menu(vg_menubar_t *menubar, vg_menu_t *menu)
{
    if (!menubar || !menu)
        return;

    if (menu->prev)
        menu->prev->next = menu->next;
    else
        menubar->first_menu = menu->next;

    if (menu->next)
        menu->next->prev = menu->prev;
    else
        menubar->last_menu = menu->prev;

    menubar->menu_count--;

    if (menubar->open_menu == menu)
        menubar->open_menu = NULL;

    free_menu(menu);
    menubar->base.needs_paint = true;
}

void vg_menubar_set_font(vg_menubar_t *menubar, vg_font_t *font, float size)
{
    if (!menubar)
        return;

    menubar->font = font;
    menubar->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    menubar->base.needs_paint = true;
}

//=============================================================================
// Keyboard Accelerators
//=============================================================================

// Key name to key code mapping
typedef struct
{
    const char *name;
    int key;
} key_mapping_t;

static const key_mapping_t g_key_mappings[] = {
    // Letters
    {"A", VG_KEY_A},
    {"B", VG_KEY_B},
    {"C", VG_KEY_C},
    {"D", VG_KEY_D},
    {"E", VG_KEY_E},
    {"F", VG_KEY_F},
    {"G", VG_KEY_G},
    {"H", VG_KEY_H},
    {"I", VG_KEY_I},
    {"J", VG_KEY_J},
    {"K", VG_KEY_K},
    {"L", VG_KEY_L},
    {"M", VG_KEY_M},
    {"N", VG_KEY_N},
    {"O", VG_KEY_O},
    {"P", VG_KEY_P},
    {"Q", VG_KEY_Q},
    {"R", VG_KEY_R},
    {"S", VG_KEY_S},
    {"T", VG_KEY_T},
    {"U", VG_KEY_U},
    {"V", VG_KEY_V},
    {"W", VG_KEY_W},
    {"X", VG_KEY_X},
    {"Y", VG_KEY_Y},
    {"Z", VG_KEY_Z},
    // Numbers
    {"0", VG_KEY_0},
    {"1", VG_KEY_1},
    {"2", VG_KEY_2},
    {"3", VG_KEY_3},
    {"4", VG_KEY_4},
    {"5", VG_KEY_5},
    {"6", VG_KEY_6},
    {"7", VG_KEY_7},
    {"8", VG_KEY_8},
    {"9", VG_KEY_9},
    // Function keys
    {"F1", VG_KEY_F1},
    {"F2", VG_KEY_F2},
    {"F3", VG_KEY_F3},
    {"F4", VG_KEY_F4},
    {"F5", VG_KEY_F5},
    {"F6", VG_KEY_F6},
    {"F7", VG_KEY_F7},
    {"F8", VG_KEY_F8},
    {"F9", VG_KEY_F9},
    {"F10", VG_KEY_F10},
    {"F11", VG_KEY_F11},
    {"F12", VG_KEY_F12},
    // Special keys
    {"Enter", VG_KEY_ENTER},
    {"Return", VG_KEY_ENTER},
    {"Tab", VG_KEY_TAB},
    {"Escape", VG_KEY_ESCAPE},
    {"Esc", VG_KEY_ESCAPE},
    {"Space", VG_KEY_SPACE},
    {"Backspace", VG_KEY_BACKSPACE},
    {"Delete", VG_KEY_DELETE},
    {"Del", VG_KEY_DELETE},
    {"Insert", VG_KEY_INSERT},
    {"Ins", VG_KEY_INSERT},
    {"Home", VG_KEY_HOME},
    {"End", VG_KEY_END},
    {"PageUp", VG_KEY_PAGE_UP},
    {"PgUp", VG_KEY_PAGE_UP},
    {"PageDown", VG_KEY_PAGE_DOWN},
    {"PgDn", VG_KEY_PAGE_DOWN},
    {"Up", VG_KEY_UP},
    {"Down", VG_KEY_DOWN},
    {"Left", VG_KEY_LEFT},
    {"Right", VG_KEY_RIGHT},
    // Punctuation
    {"-", VG_KEY_MINUS},
    {"=", VG_KEY_EQUAL},
    {"[", VG_KEY_LEFT_BRACKET},
    {"]", VG_KEY_RIGHT_BRACKET},
    {";", VG_KEY_SEMICOLON},
    {"'", VG_KEY_APOSTROPHE},
    {",", VG_KEY_COMMA},
    {".", VG_KEY_PERIOD},
    {"/", VG_KEY_SLASH},
    {"\\", VG_KEY_BACKSLASH},
    {"`", VG_KEY_GRAVE},
    {NULL, 0}};

static int lookup_key(const char *name)
{
    for (const key_mapping_t *m = g_key_mappings; m->name; m++)
    {
        if (strcasecmp(name, m->name) == 0)
        {
            return m->key;
        }
    }
    return VG_KEY_UNKNOWN;
}

bool vg_parse_accelerator(const char *shortcut, vg_accelerator_t *accel)
{
    if (!shortcut || !accel)
        return false;

    accel->key = VG_KEY_UNKNOWN;
    accel->modifiers = 0;

    // Copy string for tokenizing
    char *str = strdup(shortcut);
    if (!str)
        return false;

    char *saveptr;
    char *token = strtok_r(str, "+", &saveptr);

    while (token)
    {
        // Trim whitespace
        while (*token == ' ')
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ')
            *end-- = '\0';

        // Check for modifiers
        if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0)
        {
            accel->modifiers |= VG_MOD_CTRL;
        }
        else if (strcasecmp(token, "Cmd") == 0 || strcasecmp(token, "Command") == 0 ||
                 strcasecmp(token, "Meta") == 0 || strcasecmp(token, "Super") == 0)
        {
            accel->modifiers |= VG_MOD_SUPER;
        }
        else if (strcasecmp(token, "Shift") == 0)
        {
            accel->modifiers |= VG_MOD_SHIFT;
        }
        else if (strcasecmp(token, "Alt") == 0 || strcasecmp(token, "Option") == 0)
        {
            accel->modifiers |= VG_MOD_ALT;
        }
        else
        {
            // Must be the key
            accel->key = lookup_key(token);
        }

        token = strtok_r(NULL, "+", &saveptr);
    }

    free(str);
    return accel->key != VG_KEY_UNKNOWN;
}

void vg_menubar_register_accelerator(vg_menubar_t *menubar,
                                     vg_menu_item_t *item,
                                     const char *shortcut)
{
    if (!menubar || !item || !shortcut)
        return;

    vg_accelerator_t accel;
    if (!vg_parse_accelerator(shortcut, &accel))
        return;

    // Store parsed accelerator in item
    item->accel = accel;

    // Add to accelerator table
    vg_accel_entry_t *entry = calloc(1, sizeof(vg_accel_entry_t));
    if (!entry)
        return;

    entry->accel = accel;
    entry->item = item;
    entry->next = menubar->accel_table;
    menubar->accel_table = entry;
}

static void rebuild_accels_for_menu(vg_menubar_t *menubar, vg_menu_t *menu)
{
    for (vg_menu_item_t *item = menu->first_item; item; item = item->next)
    {
        if (item->shortcut)
        {
            vg_menubar_register_accelerator(menubar, item, item->shortcut);
        }
        if (item->submenu)
        {
            rebuild_accels_for_menu(menubar, item->submenu);
        }
    }
}

void vg_menubar_rebuild_accelerators(vg_menubar_t *menubar)
{
    if (!menubar)
        return;

    // Free existing accelerator table
    vg_accel_entry_t *entry = menubar->accel_table;
    while (entry)
    {
        vg_accel_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }
    menubar->accel_table = NULL;

    // Rebuild from all menus
    for (vg_menu_t *menu = menubar->first_menu; menu; menu = menu->next)
    {
        rebuild_accels_for_menu(menubar, menu);
    }
}

bool vg_menubar_handle_accelerator(vg_menubar_t *menubar, int key, uint32_t modifiers)
{
    if (!menubar)
        return false;

    // Normalize modifiers: treat Ctrl and Super as equivalent on Mac
    uint32_t norm_mods = modifiers & (VG_MOD_CTRL | VG_MOD_SUPER | VG_MOD_SHIFT | VG_MOD_ALT);

    for (vg_accel_entry_t *entry = menubar->accel_table; entry; entry = entry->next)
    {
        // Check for match
        uint32_t entry_mods = entry->accel.modifiers;

        // Allow Ctrl and Super to be interchangeable
        bool ctrl_match = ((norm_mods & (VG_MOD_CTRL | VG_MOD_SUPER)) != 0 &&
                           (entry_mods & (VG_MOD_CTRL | VG_MOD_SUPER)) != 0) ||
                          ((norm_mods & (VG_MOD_CTRL | VG_MOD_SUPER)) == 0 &&
                           (entry_mods & (VG_MOD_CTRL | VG_MOD_SUPER)) == 0);

        bool shift_match = ((norm_mods & VG_MOD_SHIFT) != 0) == ((entry_mods & VG_MOD_SHIFT) != 0);
        bool alt_match = ((norm_mods & VG_MOD_ALT) != 0) == ((entry_mods & VG_MOD_ALT) != 0);

        if (entry->accel.key == key && ctrl_match && shift_match && alt_match)
        {
            vg_menu_item_t *item = entry->item;
            if (item->enabled && item->action)
            {
                item->action(item->action_data);
                return true;
            }
        }
    }

    return false;
}
