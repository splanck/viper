//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_gui.c
// Purpose: Runtime bridge implementation for ViperGUI widget library.
//
//===----------------------------------------------------------------------===//

#include "rt_gui.h"
#include "rt_string.h"

#include "../lib/graphics/include/vgfx.h"
#include "../lib/gui/include/vg_event.h"
#include "../lib/gui/include/vg_font.h"
#include "../lib/gui/include/vg_ide_widgets.h"
#include "../lib/gui/include/vg_layout.h"
#include "../lib/gui/include/vg_theme.h"
#include "../lib/gui/include/vg_widget.h"
#include "../lib/gui/include/vg_widgets.h"

#include <ctype.h> // For toupper
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// For strcasecmp: Windows uses _stricmp, POSIX uses strcasecmp
#ifdef _WIN32
#define strcasecmp _stricmp
#elif defined(__viperdos__)
// ViperDOS: strings.h may not be available yet
static int viperdos_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}
#define strcasecmp viperdos_strcasecmp
#else
#include <strings.h>
#endif

//=============================================================================
// Helper Functions
//=============================================================================

static char *rt_string_to_cstr(rt_string str)
{
    if (!str)
        return NULL;
    size_t len = (size_t)rt_len(str);
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    memcpy(result, rt_string_cstr(str), len);
    result[len] = '\0';
    return result;
}

//=============================================================================
// GUI Application
//=============================================================================

typedef struct
{
    vgfx_window_t window;      // Underlying graphics window
    vg_widget_t *root;         // Root widget container
    vg_font_t *default_font;   // Default font for widgets
    float default_font_size;   // Default font size
    int64_t should_close;      // Close flag
    vg_widget_t *last_clicked; // Widget clicked this frame
    int32_t mouse_x;           // Current mouse X
    int32_t mouse_y;           // Current mouse Y
} rt_gui_app_t;

void *rt_gui_app_new(rt_string title, int64_t width, int64_t height)
{
    rt_gui_app_t *app = calloc(1, sizeof(rt_gui_app_t));
    if (!app)
        return NULL;

    // Create window
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)width;
    params.height = (int32_t)height;
    char *ctitle = rt_string_to_cstr(title);
    if (ctitle)
    {
        params.title = ctitle;
    }
    params.resizable = 1;

    app->window = vgfx_create_window(&params);
    free(ctitle);

    if (!app->window)
    {
        free(app);
        return NULL;
    }

    // Create root container
    app->root = vg_widget_create(VG_WIDGET_CONTAINER);
    if (app->root)
    {
        vg_widget_set_fixed_size(app->root, (float)width, (float)height);
        // Also set actual size (set_fixed_size only sets constraints)
        app->root->width = (float)width;
        app->root->height = (float)height;
    }

    // Set dark theme by default
    vg_theme_set_current(vg_theme_dark());

    return app;
}

void rt_gui_app_destroy(void *app_ptr)
{
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;

    if (app->root)
    {
        vg_widget_destroy(app->root);
    }
    if (app->window)
    {
        vgfx_destroy_window(app->window);
    }
    free(app);
}

int64_t rt_gui_app_should_close(void *app_ptr)
{
    if (!app_ptr)
        return 1;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    return app->should_close;
}

// Forward declarations
static void render_widget_tree(vgfx_window_t window,
                               vg_widget_t *widget,
                               vg_font_t *font,
                               float font_size);
void rt_gui_set_last_clicked(void *widget);

void rt_gui_app_poll(void *app_ptr)
{
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    if (!app->window)
        return;

    // Clear last clicked
    app->last_clicked = NULL;
    rt_gui_set_last_clicked(NULL);

    // Get mouse position
    vgfx_mouse_pos(app->window, &app->mouse_x, &app->mouse_y);

    // Poll events
    vgfx_event_t event;
    while (vgfx_poll_event(app->window, &event))
    {
        if (event.type == VGFX_EVENT_CLOSE)
        {
            app->should_close = 1;
            continue;
        }

        // Convert platform event to GUI event and dispatch to widget tree
        if (app->root)
        {
            vg_event_t gui_event = vg_event_from_platform(&event);

            // Track mouse position from events
            if (event.type == VGFX_EVENT_MOUSE_MOVE)
            {
                app->mouse_x = event.data.mouse_move.x;
                app->mouse_y = event.data.mouse_move.y;
            }

            // Track clicked widget for Button.WasClicked()
            if (event.type == VGFX_EVENT_MOUSE_UP)
            {
                vg_widget_t *hit =
                    vg_widget_hit_test(app->root, (float)app->mouse_x, (float)app->mouse_y);
                if (hit)
                {
                    app->last_clicked = hit;
                    // Also set global for rt_widget_was_clicked
                    rt_gui_set_last_clicked(hit);
                }
            }

            // Dispatch all events to widget tree (handles focus, keyboard, etc.)
            vg_event_dispatch(app->root, &gui_event);

            // Synthesize KEY_CHAR event from KEY_DOWN for printable characters
            // (vgfx doesn't have character input events, only key events)
            if (event.type == VGFX_EVENT_KEY_DOWN && !event.data.key.is_repeat)
            {
                int key = event.data.key.key;
                uint32_t codepoint = 0;

                // Check if it's a printable ASCII character
                if (key >= ' ' && key <= '~')
                {
                    // Letters are uppercase by default, convert to lowercase
                    if (key >= 'A' && key <= 'Z')
                    {
                        codepoint = key + ('a' - 'A'); // Convert to lowercase
                    }
                    else
                    {
                        codepoint = key;
                    }
                }

                if (codepoint != 0)
                {
                    vg_event_t char_event =
                        vg_event_key(VG_EVENT_KEY_CHAR, (vg_key_t)key, codepoint, 0);
                    vg_event_dispatch(app->root, &char_event);
                }
            }
        }
    }
}

// Get spacing for a container (VBox/HBox store layout data in user_data)
static float get_container_spacing(vg_widget_t *widget)
{
    if (widget->user_data)
    {
        // VBox and HBox store vg_vbox_layout_t or vg_hbox_layout_t
        vg_vbox_layout_t *layout = (vg_vbox_layout_t *)widget->user_data;
        return layout->spacing;
    }
    return 8.0f; // Default spacing
}

// Get default height for widget based on type
static float get_widget_default_height(vg_widget_t *widget, float font_size)
{
    if (widget->height > 0)
        return widget->height;

    switch (widget->type)
    {
        case VG_WIDGET_LABEL:
            return font_size + 4;
        case VG_WIDGET_BUTTON:
            return 32;
        case VG_WIDGET_TEXTINPUT:
            return 28;
        case VG_WIDGET_CHECKBOX:
            return 20;
        case VG_WIDGET_CODEEDITOR:
            return 200;
        case VG_WIDGET_CONTAINER:
        {
            // Calculate height from children
            float max_height = 0;
            float total_height = 0;
            float spacing = 8.0f;
            int child_count = 0;
            vg_widget_t *child = widget->first_child;
            while (child)
            {
                float ch = child->height > 0 ? child->height : 32; // estimate
                if (ch > max_height)
                    max_height = ch;
                total_height += ch;
                child_count++;
                child = child->next_sibling;
            }
            // For HBox (buttons), use max height; for VBox use total
            // Heuristic: if all children are buttons, it's HBox
            int button_count = 0;
            child = widget->first_child;
            while (child)
            {
                if (child->type == VG_WIDGET_BUTTON)
                    button_count++;
                child = child->next_sibling;
            }
            if (child_count > 0 && button_count == child_count)
            {
                return max_height + 16; // HBox: max child height + padding
            }
            return total_height + spacing * (child_count > 0 ? child_count - 1 : 0) + 16;
        }
        default:
            return 24;
    }
}

// Check if widget is HBox by comparing vtable (HBox has hbox_arrange)
static bool is_hbox_container(vg_widget_t *widget)
{
    if (!widget || !widget->vtable || widget->type != VG_WIDGET_CONTAINER)
        return false;
    // HBox vtable has arrange function at a different address than VBox
    // We can check by looking at the user_data - HBox uses vg_hbox_layout_t
    // For simplicity, check if arrange function name contains "hbox" behavior
    // Actually, the simplest way is to store a flag or check the vtable pointer
    // For now, we'll use a heuristic: if spacing > 0 and widget has children that
    // are buttons, treat as HBox. Better solution would be to tag the widget type.

    // Check if this container's children are mostly buttons (heuristic for button bar)
    int button_count = 0;
    int child_count = 0;
    vg_widget_t *child = widget->first_child;
    while (child)
    {
        child_count++;
        if (child->type == VG_WIDGET_BUTTON)
            button_count++;
        child = child->next_sibling;
    }
    return (child_count > 0 && button_count == child_count);
}

// Recursively perform layout on widget tree
static void layout_widget_tree(
    vg_widget_t *widget, float rel_x, float rel_y, float parent_width, float font_size)
{
    if (!widget)
        return;

    // Set position (relative to parent)
    widget->x = rel_x;
    widget->y = rel_y;

    // Set default height if not specified
    if (widget->height <= 0)
    {
        widget->height = get_widget_default_height(widget, font_size);
    }

    // Calculate child positions (relative to this widget, starting at padding offset)
    float spacing = get_container_spacing(widget);
    float padding = 8.0f;
    float child_rel_x = padding;
    float child_rel_y = padding;

    // Available width for children (parent width minus padding on both sides)
    float available_width = (widget->width > 0 ? widget->width : parent_width) - padding * 2;

    // Determine if this is horizontal or vertical layout
    bool horizontal = is_hbox_container(widget);

    vg_widget_t *child = widget->first_child;
    while (child)
    {
        // Set default height for child before layout
        if (child->height <= 0)
        {
            child->height = get_widget_default_height(child, font_size);
        }
        // Set default width based on layout type
        if (child->width <= 0)
        {
            if (child->type == VG_WIDGET_BUTTON)
            {
                child->width = 80; // Buttons have fixed width
            }
            else if (!horizontal)
            {
                // In vertical layout, children fill the width
                child->width = available_width;
            }
            else
            {
                // In horizontal layout, use a default
                child->width = 100;
            }
        }

        layout_widget_tree(child, child_rel_x, child_rel_y, available_width, font_size);

        // Advance position based on layout direction
        if (widget->type == VG_WIDGET_CONTAINER)
        {
            if (horizontal)
            {
                child_rel_x += child->width + spacing;
            }
            else
            {
                child_rel_y += child->height + spacing;
            }
        }

        child = child->next_sibling;
    }

    // Update container height to fit all children (for VBox/HBox)
    if (widget->type == VG_WIDGET_CONTAINER && widget->first_child)
    {
        bool horizontal = is_hbox_container(widget);
        if (horizontal)
        {
            // For HBox, height is the max child height + padding
            float max_height = 0;
            for (child = widget->first_child; child; child = child->next_sibling)
            {
                if (child->height > max_height)
                    max_height = child->height;
            }
            float needed_height = max_height + padding * 2;
            if (needed_height > widget->height)
                widget->height = needed_height;
        }
        else
        {
            // For VBox, height is the sum of all children + spacing + padding
            float needed_height =
                child_rel_y + padding; // child_rel_y already includes all children
            if (needed_height > widget->height)
                widget->height = needed_height;
        }
    }
}

void rt_gui_app_render(void *app_ptr)
{
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    if (!app->window)
        return;

    // Try to load a default font if none is set
    if (!app->default_font)
    {
        // Try common system font paths
        const char *font_paths[] = {"/System/Library/Fonts/Menlo.ttc",
                                    "/System/Library/Fonts/SFNSMono.ttf",
                                    "/System/Library/Fonts/Monaco.dfont",
                                    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                                    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                                    NULL};
        for (int i = 0; font_paths[i]; i++)
        {
            app->default_font = vg_font_load_file(font_paths[i]);
            if (app->default_font)
            {
                app->default_font_size = 14.0f;
                break;
            }
        }
    }

    // Perform layout
    float font_size = app->default_font_size > 0 ? app->default_font_size : 14.0f;
    if (app->root)
    {
        layout_widget_tree(app->root, 0, 0, app->root->width, font_size);
    }

    // Clear with theme background
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_cls(app->window, theme ? theme->colors.bg_secondary : 0xFF1E1E1E);

    // Render widget tree
    if (app->root)
    {
        render_widget_tree(app->window, app->root, app->default_font, app->default_font_size);
    }

    // Present
    vgfx_update(app->window);
}

void *rt_gui_app_get_root(void *app_ptr)
{
    if (!app_ptr)
        return NULL;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    return app->root;
}

void rt_gui_app_set_font(void *app_ptr, void *font, double size)
{
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    app->default_font = (vg_font_t *)font;
    app->default_font_size = (float)size;
}

// Simple widget rendering (would be expanded for full implementation)
static void render_widget_tree(vgfx_window_t window,
                               vg_widget_t *widget,
                               vg_font_t *font,
                               float font_size)
{
    if (!widget || !widget->visible)
        return;

    // Get screen coordinates (converts relative positions to absolute)
    float x, y, w, h;
    vg_widget_get_screen_bounds(widget, &x, &y, &w, &h);

    vg_theme_t *theme = vg_theme_get_current();
    if (!theme)
        return;

    // Use default font size if not specified
    if (font_size <= 0)
        font_size = 14.0f;

    // Render based on widget type
    switch (widget->type)
    {
        case VG_WIDGET_CONTAINER:
            // Containers are transparent by default, just render children
            break;

        case VG_WIDGET_LABEL:
        {
            vg_label_t *label = (vg_label_t *)widget;
            if (label->text)
            {
                vg_font_t *use_font = label->font ? label->font : font;
                float use_size = label->font ? label->font_size : font_size;
                if (use_font)
                {
                    vg_font_draw_text(window,
                                      use_font,
                                      use_size,
                                      x,
                                      y + use_size,
                                      label->text,
                                      label->text_color);
                }
                // No fallback without font - text won't render
            }
            break;
        }

        case VG_WIDGET_BUTTON:
        {
            vg_button_t *btn = (vg_button_t *)widget;
            uint32_t bg = theme->colors.bg_primary;
            if (widget->state & VG_STATE_HOVERED)
                bg = theme->colors.bg_tertiary;
            if (widget->state & VG_STATE_PRESSED)
                bg = theme->colors.accent_primary;
            vgfx_rect(window, (int)x, (int)y, (int)w, (int)h, bg);
            if (btn->text)
            {
                vg_font_t *use_font = btn->font ? btn->font : font;
                float use_size = btn->font ? btn->font_size : font_size;
                if (use_font)
                {
                    float tw = strlen(btn->text) * use_size * 0.6f;
                    float tx = x + (w - tw) / 2;
                    float ty = y + (h + use_size) / 2 - 2;
                    vg_font_draw_text(
                        window, use_font, use_size, tx, ty, btn->text, theme->colors.fg_primary);
                }
                // No fallback without font
            }
            break;
        }

        case VG_WIDGET_TEXTINPUT:
        {
            vg_textinput_t *input = (vg_textinput_t *)widget;
            uint32_t border_color = theme->colors.fg_tertiary;
            // Draw background
            vgfx_rect(window, (int)x, (int)y, (int)w, (int)h, theme->colors.bg_primary);
            // Draw border
            vgfx_rect(window, (int)x, (int)y, (int)w, 1, border_color);
            vgfx_rect(window, (int)x, (int)(y + h - 1), (int)w, 1, border_color);
            vgfx_rect(window, (int)x, (int)y, 1, (int)h, border_color);
            vgfx_rect(window, (int)(x + w - 1), (int)y, 1, (int)h, border_color);
            // Draw text or placeholder
            const char *display_text =
                (input->text && input->text[0]) ? input->text : input->placeholder;
            uint32_t text_color = (input->text && input->text[0]) ? theme->colors.fg_primary
                                                                  : theme->colors.fg_secondary;
            if (display_text && font)
            {
                vg_font_draw_text(
                    window, font, font_size, x + 4, y + font_size + 2, display_text, text_color);
            }
            break;
        }

        case VG_WIDGET_CODEEDITOR:
        {
            vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;
            uint32_t border_color = theme->colors.fg_tertiary;
            // Draw background
            vgfx_rect(window, (int)x, (int)y, (int)w, (int)h, theme->colors.bg_primary);
            // Draw border
            vgfx_rect(window, (int)x, (int)y, (int)w, 1, border_color);
            vgfx_rect(window, (int)x, (int)(y + h - 1), (int)w, 1, border_color);
            vgfx_rect(window, (int)x, (int)y, 1, (int)h, border_color);
            vgfx_rect(window, (int)(x + w - 1), (int)y, 1, (int)h, border_color);
            // Draw text content (simplified - just first few lines)
            float line_height = font_size + 4;
            float char_width = font_size * 0.6f; // Approximate monospace char width
            const char *text_content = vg_codeeditor_get_text(editor);
            if (text_content && font)
            {
                float ty = y + 4;
                const char *line_start = text_content;
                int max_lines = (int)((h - 8) / line_height);
                for (int i = 0; i < max_lines && *line_start; i++)
                {
                    const char *line_end = strchr(line_start, '\n');
                    int len = line_end ? (int)(line_end - line_start) : (int)strlen(line_start);
                    char line_buf[256];
                    if (len > 255)
                        len = 255;
                    memcpy(line_buf, line_start, len);
                    line_buf[len] = '\0';
                    vg_font_draw_text(window,
                                      font,
                                      font_size,
                                      x + 4,
                                      ty + font_size,
                                      line_buf,
                                      theme->colors.fg_primary);
                    ty += line_height;
                    if (!line_end)
                        break;
                    line_start = line_end + 1;
                }
            }
            // Draw cursor if focused
            if (widget->state & VG_STATE_FOCUSED)
            {
                float cursor_x = x + 4 + editor->cursor_col * char_width;
                float cursor_y = y + 4 + editor->cursor_line * line_height;
                // Draw cursor line
                vgfx_rect(window,
                          (int)cursor_x,
                          (int)cursor_y,
                          2,
                          (int)font_size + 2,
                          theme->colors.fg_primary);
            }
            break;
        }

        case VG_WIDGET_CHECKBOX:
        {
            vg_checkbox_t *cb = (vg_checkbox_t *)widget;
            uint32_t border_color = theme->colors.fg_tertiary;
            // Draw checkbox box
            int box_size = 16;
            vgfx_rect(window, (int)x, (int)y, box_size, box_size, theme->colors.bg_primary);
            vgfx_rect(window, (int)x, (int)y, box_size, 1, border_color);
            vgfx_rect(window, (int)x, (int)(y + box_size - 1), box_size, 1, border_color);
            vgfx_rect(window, (int)x, (int)y, 1, box_size, border_color);
            vgfx_rect(window, (int)(x + box_size - 1), (int)y, 1, box_size, border_color);
            if (cb->checked)
            {
                // Draw checkmark (simplified as filled inner rect)
                vgfx_rect(window,
                          (int)(x + 3),
                          (int)(y + 3),
                          box_size - 6,
                          box_size - 6,
                          theme->colors.accent_primary);
            }
            // Draw label
            if (cb->text && font)
            {
                vg_font_draw_text(window,
                                  font,
                                  font_size,
                                  x + box_size + 6,
                                  y + font_size,
                                  cb->text,
                                  theme->colors.fg_primary);
            }
            break;
        }

        default:
            // For unhandled widgets, just draw a placeholder rect if they have size
            if (w > 0 && h > 0)
            {
                vgfx_rect(window, (int)x, (int)y, (int)w, (int)h, theme->colors.bg_tertiary);
            }
            break;
    }

    // Render children
    vg_widget_t *child = widget->first_child;
    while (child)
    {
        render_widget_tree(window, child, font, font_size);
        child = child->next_sibling;
    }
}

//=============================================================================
// Font Functions
//=============================================================================

void *rt_font_load(rt_string path)
{
    char *cpath = rt_string_to_cstr(path);
    if (!cpath)
        return NULL;

    vg_font_t *font = vg_font_load_file(cpath);
    free(cpath);
    return font;
}

void rt_font_destroy(void *font)
{
    if (font)
    {
        vg_font_destroy((vg_font_t *)font);
    }
}

//=============================================================================
// Widget Functions
//=============================================================================

void rt_widget_destroy(void *widget)
{
    if (widget)
    {
        vg_widget_destroy((vg_widget_t *)widget);
    }
}

void rt_widget_set_visible(void *widget, int64_t visible)
{
    if (widget)
    {
        vg_widget_set_visible((vg_widget_t *)widget, visible != 0);
    }
}

void rt_widget_set_enabled(void *widget, int64_t enabled)
{
    if (widget)
    {
        vg_widget_set_enabled((vg_widget_t *)widget, enabled != 0);
    }
}

void rt_widget_set_size(void *widget, int64_t width, int64_t height)
{
    if (widget)
    {
        vg_widget_set_fixed_size((vg_widget_t *)widget, (float)width, (float)height);
    }
}

void rt_widget_add_child(void *parent, void *child)
{
    if (parent && child)
    {
        vg_widget_add_child((vg_widget_t *)parent, (vg_widget_t *)child);
    }
}

//=============================================================================
// Label Widget
//=============================================================================

void *rt_label_new(void *parent, rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    vg_label_t *label = vg_label_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return label;
}

void rt_label_set_text(void *label, rt_string text)
{
    if (!label)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_label_set_text((vg_label_t *)label, ctext);
    free(ctext);
}

void rt_label_set_font(void *label, void *font, double size)
{
    if (label)
    {
        vg_label_set_font((vg_label_t *)label, (vg_font_t *)font, (float)size);
    }
}

void rt_label_set_color(void *label, int64_t color)
{
    if (label)
    {
        vg_label_set_color((vg_label_t *)label, (uint32_t)color);
    }
}

//=============================================================================
// Button Widget
//=============================================================================

void *rt_button_new(void *parent, rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    vg_button_t *button = vg_button_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return button;
}

void rt_button_set_text(void *button, rt_string text)
{
    if (!button)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_button_set_text((vg_button_t *)button, ctext);
    free(ctext);
}

void rt_button_set_font(void *button, void *font, double size)
{
    if (button)
    {
        vg_button_set_font((vg_button_t *)button, (vg_font_t *)font, (float)size);
    }
}

void rt_button_set_style(void *button, int64_t style)
{
    if (button)
    {
        vg_button_set_style((vg_button_t *)button, (vg_button_style_t)style);
    }
}

//=============================================================================
// TextInput Widget
//=============================================================================

void *rt_textinput_new(void *parent)
{
    return vg_textinput_create((vg_widget_t *)parent);
}

void rt_textinput_set_text(void *input, rt_string text)
{
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_textinput_set_text((vg_textinput_t *)input, ctext);
    free(ctext);
}

rt_string rt_textinput_get_text(void *input)
{
    if (!input)
        return rt_str_empty();
    const char *text = vg_textinput_get_text((vg_textinput_t *)input);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_textinput_set_placeholder(void *input, rt_string placeholder)
{
    if (!input)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_textinput_set_placeholder((vg_textinput_t *)input, ctext);
    free(ctext);
}

void rt_textinput_set_font(void *input, void *font, double size)
{
    if (input)
    {
        vg_textinput_set_font((vg_textinput_t *)input, (vg_font_t *)font, (float)size);
    }
}

//=============================================================================
// Checkbox Widget
//=============================================================================

void *rt_checkbox_new(void *parent, rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    vg_checkbox_t *checkbox = vg_checkbox_create((vg_widget_t *)parent, ctext);
    free(ctext);
    return checkbox;
}

void rt_checkbox_set_checked(void *checkbox, int64_t checked)
{
    if (checkbox)
    {
        vg_checkbox_set_checked((vg_checkbox_t *)checkbox, checked != 0);
    }
}

int64_t rt_checkbox_is_checked(void *checkbox)
{
    if (!checkbox)
        return 0;
    return vg_checkbox_is_checked((vg_checkbox_t *)checkbox) ? 1 : 0;
}

void rt_checkbox_set_text(void *checkbox, rt_string text)
{
    if (!checkbox)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_checkbox_set_text((vg_checkbox_t *)checkbox, ctext);
    free(ctext);
}

//=============================================================================
// ScrollView Widget
//=============================================================================

void *rt_scrollview_new(void *parent)
{
    return vg_scrollview_create((vg_widget_t *)parent);
}

void rt_scrollview_set_scroll(void *scroll, double x, double y)
{
    if (scroll)
    {
        vg_scrollview_set_scroll((vg_scrollview_t *)scroll, (float)x, (float)y);
    }
}

void rt_scrollview_set_content_size(void *scroll, double width, double height)
{
    if (scroll)
    {
        vg_scrollview_set_content_size((vg_scrollview_t *)scroll, (float)width, (float)height);
    }
}

//=============================================================================
// TreeView Widget
//=============================================================================

void *rt_treeview_new(void *parent)
{
    return vg_treeview_create((vg_widget_t *)parent);
}

void *rt_treeview_add_node(void *tree, void *parent_node, rt_string text)
{
    if (!tree)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_tree_node_t *node =
        vg_treeview_add_node((vg_treeview_t *)tree, (vg_tree_node_t *)parent_node, ctext);
    free(ctext);
    return node;
}

void rt_treeview_remove_node(void *tree, void *node)
{
    if (tree && node)
    {
        vg_treeview_remove_node((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_clear(void *tree)
{
    if (tree)
    {
        vg_treeview_clear((vg_treeview_t *)tree);
    }
}

void rt_treeview_expand(void *tree, void *node)
{
    if (tree && node)
    {
        vg_treeview_expand((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_collapse(void *tree, void *node)
{
    if (tree && node)
    {
        vg_treeview_collapse((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_select(void *tree, void *node)
{
    if (tree)
    {
        vg_treeview_select((vg_treeview_t *)tree, (vg_tree_node_t *)node);
    }
}

void rt_treeview_set_font(void *tree, void *font, double size)
{
    if (tree)
    {
        vg_treeview_set_font((vg_treeview_t *)tree, (vg_font_t *)font, (float)size);
    }
}

//=============================================================================
// TabBar Widget
//=============================================================================

void *rt_tabbar_new(void *parent)
{
    return vg_tabbar_create((vg_widget_t *)parent);
}

void *rt_tabbar_add_tab(void *tabbar, rt_string title, int64_t closable)
{
    if (!tabbar)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_t *tab = vg_tabbar_add_tab((vg_tabbar_t *)tabbar, ctitle, closable != 0);
    free(ctitle);
    return tab;
}

void rt_tabbar_remove_tab(void *tabbar, void *tab)
{
    if (tabbar && tab)
    {
        vg_tabbar_remove_tab((vg_tabbar_t *)tabbar, (vg_tab_t *)tab);
    }
}

void rt_tabbar_set_active(void *tabbar, void *tab)
{
    if (tabbar)
    {
        vg_tabbar_set_active((vg_tabbar_t *)tabbar, (vg_tab_t *)tab);
    }
}

void rt_tab_set_title(void *tab, rt_string title)
{
    if (!tab)
        return;
    char *ctitle = rt_string_to_cstr(title);
    vg_tab_set_title((vg_tab_t *)tab, ctitle);
    free(ctitle);
}

void rt_tab_set_modified(void *tab, int64_t modified)
{
    if (tab)
    {
        vg_tab_set_modified((vg_tab_t *)tab, modified != 0);
    }
}

//=============================================================================
// SplitPane Widget
//=============================================================================

void *rt_splitpane_new(void *parent, int64_t horizontal)
{
    vg_split_direction_t direction = horizontal ? VG_SPLIT_HORIZONTAL : VG_SPLIT_VERTICAL;
    return vg_splitpane_create((vg_widget_t *)parent, direction);
}

void rt_splitpane_set_position(void *split, double position)
{
    if (split)
    {
        vg_splitpane_set_position((vg_splitpane_t *)split, (float)position);
    }
}

void *rt_splitpane_get_first(void *split)
{
    if (!split)
        return NULL;
    return vg_splitpane_get_first((vg_splitpane_t *)split);
}

void *rt_splitpane_get_second(void *split)
{
    if (!split)
        return NULL;
    return vg_splitpane_get_second((vg_splitpane_t *)split);
}

//=============================================================================
// CodeEditor Widget
//=============================================================================

void *rt_codeeditor_new(void *parent)
{
    return vg_codeeditor_create((vg_widget_t *)parent);
}

void rt_codeeditor_set_text(void *editor, rt_string text)
{
    if (!editor)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_codeeditor_set_text((vg_codeeditor_t *)editor, ctext);
    free(ctext);
}

rt_string rt_codeeditor_get_text(void *editor)
{
    if (!editor)
        return rt_str_empty();
    char *text = vg_codeeditor_get_text((vg_codeeditor_t *)editor);
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

void rt_codeeditor_set_cursor(void *editor, int64_t line, int64_t col)
{
    if (editor)
    {
        vg_codeeditor_set_cursor((vg_codeeditor_t *)editor, (int)line, (int)col);
    }
}

void rt_codeeditor_scroll_to_line(void *editor, int64_t line)
{
    if (editor)
    {
        vg_codeeditor_scroll_to_line((vg_codeeditor_t *)editor, (int)line);
    }
}

int64_t rt_codeeditor_get_line_count(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_get_line_count((vg_codeeditor_t *)editor);
}

int64_t rt_codeeditor_is_modified(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_is_modified((vg_codeeditor_t *)editor) ? 1 : 0;
}

void rt_codeeditor_clear_modified(void *editor)
{
    if (editor)
    {
        vg_codeeditor_clear_modified((vg_codeeditor_t *)editor);
    }
}

void rt_codeeditor_set_font(void *editor, void *font, double size)
{
    if (editor)
    {
        vg_codeeditor_set_font((vg_codeeditor_t *)editor, (vg_font_t *)font, (float)size);
    }
}

//=============================================================================
// Theme Functions
//=============================================================================

void rt_theme_set_dark(void)
{
    vg_theme_set_current(vg_theme_dark());
}

void rt_theme_set_light(void)
{
    vg_theme_set_current(vg_theme_light());
}

//=============================================================================
// Layout Functions
//=============================================================================

void *rt_vbox_new(void)
{
    vg_widget_t *container = vg_widget_create(VG_WIDGET_CONTAINER);
    if (container)
    {
        // Set layout to VBox using layout data
        // For now this is a simple container, layout is set on the container
    }
    return container;
}

void *rt_hbox_new(void)
{
    vg_widget_t *container = vg_widget_create(VG_WIDGET_CONTAINER);
    if (container)
    {
        // Set layout to HBox using layout data
    }
    return container;
}

void rt_container_set_spacing(void *container, double spacing)
{
    if (container)
    {
        // Set spacing in layout params
        vg_widget_t *w = (vg_widget_t *)container;
        // The spacing would be stored in layout data
        (void)w;
        (void)spacing;
    }
}

void rt_container_set_padding(void *container, double padding)
{
    if (container)
    {
        vg_widget_set_padding((vg_widget_t *)container, (float)padding);
    }
}

//=============================================================================
// Widget State Functions
//=============================================================================

int64_t rt_widget_is_hovered(void *widget)
{
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_HOVERED) ? 1 : 0;
}

int64_t rt_widget_is_pressed(void *widget)
{
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_PRESSED) ? 1 : 0;
}

int64_t rt_widget_is_focused(void *widget)
{
    if (!widget)
        return 0;
    return (((vg_widget_t *)widget)->state & VG_STATE_FOCUSED) ? 1 : 0;
}

// Global for tracking last clicked widget (set by GUI.App.Poll)
static vg_widget_t *g_last_clicked_widget = NULL;

void rt_gui_set_last_clicked(void *widget)
{
    g_last_clicked_widget = (vg_widget_t *)widget;
}

int64_t rt_widget_was_clicked(void *widget)
{
    if (!widget)
        return 0;
    return (g_last_clicked_widget == widget) ? 1 : 0;
}

void rt_widget_set_position(void *widget, int64_t x, int64_t y)
{
    if (widget)
    {
        vg_widget_t *w = (vg_widget_t *)widget;
        w->x = (float)x;
        w->y = (float)y;
    }
}

//=============================================================================
// Dropdown Widget
//=============================================================================

void *rt_dropdown_new(void *parent)
{
    return vg_dropdown_create((vg_widget_t *)parent);
}

int64_t rt_dropdown_add_item(void *dropdown, rt_string text)
{
    if (!dropdown)
        return -1;
    char *ctext = rt_string_to_cstr(text);
    int64_t index = vg_dropdown_add_item((vg_dropdown_t *)dropdown, ctext);
    free(ctext);
    return index;
}

void rt_dropdown_remove_item(void *dropdown, int64_t index)
{
    if (dropdown)
    {
        vg_dropdown_remove_item((vg_dropdown_t *)dropdown, (int)index);
    }
}

void rt_dropdown_clear(void *dropdown)
{
    if (dropdown)
    {
        vg_dropdown_clear((vg_dropdown_t *)dropdown);
    }
}

void rt_dropdown_set_selected(void *dropdown, int64_t index)
{
    if (dropdown)
    {
        vg_dropdown_set_selected((vg_dropdown_t *)dropdown, (int)index);
    }
}

int64_t rt_dropdown_get_selected(void *dropdown)
{
    if (!dropdown)
        return -1;
    return vg_dropdown_get_selected((vg_dropdown_t *)dropdown);
}

rt_string rt_dropdown_get_selected_text(void *dropdown)
{
    if (!dropdown)
        return rt_str_empty();
    const char *text = vg_dropdown_get_selected_text((vg_dropdown_t *)dropdown);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder)
{
    if (!dropdown)
        return;
    char *ctext = rt_string_to_cstr(placeholder);
    vg_dropdown_set_placeholder((vg_dropdown_t *)dropdown, ctext);
    free(ctext);
}

//=============================================================================
// Slider Widget
//=============================================================================

void *rt_slider_new(void *parent, int64_t horizontal)
{
    vg_slider_orientation_t orient = horizontal ? VG_SLIDER_HORIZONTAL : VG_SLIDER_VERTICAL;
    return vg_slider_create((vg_widget_t *)parent, orient);
}

void rt_slider_set_value(void *slider, double value)
{
    if (slider)
    {
        vg_slider_set_value((vg_slider_t *)slider, (float)value);
    }
}

double rt_slider_get_value(void *slider)
{
    if (!slider)
        return 0.0;
    return (double)vg_slider_get_value((vg_slider_t *)slider);
}

void rt_slider_set_range(void *slider, double min_val, double max_val)
{
    if (slider)
    {
        vg_slider_set_range((vg_slider_t *)slider, (float)min_val, (float)max_val);
    }
}

void rt_slider_set_step(void *slider, double step)
{
    if (slider)
    {
        vg_slider_set_step((vg_slider_t *)slider, (float)step);
    }
}

//=============================================================================
// ProgressBar Widget
//=============================================================================

void *rt_progressbar_new(void *parent)
{
    return vg_progressbar_create((vg_widget_t *)parent);
}

void rt_progressbar_set_value(void *progress, double value)
{
    if (progress)
    {
        vg_progressbar_set_value((vg_progressbar_t *)progress, (float)value);
    }
}

double rt_progressbar_get_value(void *progress)
{
    if (!progress)
        return 0.0;
    return (double)vg_progressbar_get_value((vg_progressbar_t *)progress);
}

//=============================================================================
// ListBox Widget
//=============================================================================

void *rt_listbox_new(void *parent)
{
    return vg_listbox_create((vg_widget_t *)parent);
}

void *rt_listbox_add_item(void *listbox, rt_string text)
{
    if (!listbox)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_listbox_item_t *item = vg_listbox_add_item((vg_listbox_t *)listbox, ctext, NULL);
    free(ctext);
    return item;
}

void rt_listbox_remove_item(void *listbox, void *item)
{
    if (listbox && item)
    {
        vg_listbox_remove_item((vg_listbox_t *)listbox, (vg_listbox_item_t *)item);
    }
}

void rt_listbox_clear(void *listbox)
{
    if (listbox)
    {
        vg_listbox_clear((vg_listbox_t *)listbox);
    }
}

void rt_listbox_select(void *listbox, void *item)
{
    if (listbox)
    {
        vg_listbox_select((vg_listbox_t *)listbox, (vg_listbox_item_t *)item);
    }
}

void *rt_listbox_get_selected(void *listbox)
{
    if (!listbox)
        return NULL;
    return vg_listbox_get_selected((vg_listbox_t *)listbox);
}

//=============================================================================
// RadioButton Widget
//=============================================================================

void *rt_radiogroup_new(void)
{
    return vg_radiogroup_create();
}

void rt_radiogroup_destroy(void *group)
{
    if (group)
    {
        vg_radiogroup_destroy((vg_radiogroup_t *)group);
    }
}

void *rt_radiobutton_new(void *parent, rt_string text, void *group)
{
    char *ctext = rt_string_to_cstr(text);
    vg_radiobutton_t *radio =
        vg_radiobutton_create((vg_widget_t *)parent, ctext, (vg_radiogroup_t *)group);
    free(ctext);
    return radio;
}

int64_t rt_radiobutton_is_selected(void *radio)
{
    if (!radio)
        return 0;
    return vg_radiobutton_is_selected((vg_radiobutton_t *)radio) ? 1 : 0;
}

void rt_radiobutton_set_selected(void *radio, int64_t selected)
{
    if (radio)
    {
        vg_radiobutton_set_selected((vg_radiobutton_t *)radio, selected != 0);
    }
}

//=============================================================================
// Spinner Widget
//=============================================================================

void *rt_spinner_new(void *parent)
{
    return vg_spinner_create((vg_widget_t *)parent);
}

void rt_spinner_set_value(void *spinner, double value)
{
    if (spinner)
    {
        vg_spinner_set_value((vg_spinner_t *)spinner, value);
    }
}

double rt_spinner_get_value(void *spinner)
{
    if (!spinner)
        return 0.0;
    return vg_spinner_get_value((vg_spinner_t *)spinner);
}

void rt_spinner_set_range(void *spinner, double min_val, double max_val)
{
    if (spinner)
    {
        vg_spinner_set_range((vg_spinner_t *)spinner, min_val, max_val);
    }
}

void rt_spinner_set_step(void *spinner, double step)
{
    if (spinner)
    {
        vg_spinner_set_step((vg_spinner_t *)spinner, step);
    }
}

void rt_spinner_set_decimals(void *spinner, int64_t decimals)
{
    if (spinner)
    {
        vg_spinner_set_decimals((vg_spinner_t *)spinner, (int)decimals);
    }
}

//=============================================================================
// Image Widget
//=============================================================================

void *rt_image_new(void *parent)
{
    return vg_image_create((vg_widget_t *)parent);
}

void rt_image_set_pixels(void *image, void *pixels, int64_t width, int64_t height)
{
    if (image && pixels)
    {
        vg_image_set_pixels((vg_image_t *)image, (const uint8_t *)pixels, (int)width, (int)height);
    }
}

void rt_image_clear(void *image)
{
    if (image)
    {
        vg_image_clear((vg_image_t *)image);
    }
}

void rt_image_set_scale_mode(void *image, int64_t mode)
{
    if (image)
    {
        vg_image_set_scale_mode((vg_image_t *)image, (vg_image_scale_t)mode);
    }
}

void rt_image_set_opacity(void *image, double opacity)
{
    if (image)
    {
        vg_image_set_opacity((vg_image_t *)image, (float)opacity);
    }
}

//=============================================================================
// Clipboard Functions (Phase 1)
//=============================================================================

void rt_clipboard_set_text(rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    if (ctext)
    {
        vgfx_clipboard_set_text(ctext);
        free(ctext);
    }
}

rt_string rt_clipboard_get_text(void)
{
    char *text = vgfx_clipboard_get_text();
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

int64_t rt_clipboard_has_text(void)
{
    return vgfx_clipboard_has_format(VGFX_CLIPBOARD_TEXT) ? 1 : 0;
}

void rt_clipboard_clear(void)
{
    vgfx_clipboard_clear();
}

//=============================================================================
// Keyboard Shortcuts (Phase 1)
//=============================================================================

// Internal shortcut storage
typedef struct
{
    char *id;
    char *keys;
    char *description;
    int enabled;
    int triggered; // Set to 1 when shortcut is triggered this frame
} rt_shortcut_t;

#define MAX_SHORTCUTS 256
static rt_shortcut_t g_shortcuts[MAX_SHORTCUTS];
static int g_shortcut_count = 0;
static int g_shortcuts_global_enabled = 1;
static char *g_triggered_shortcut_id = NULL;

// Parse modifier keys from string like "Ctrl+Shift+S"
static int parse_shortcut_keys(const char *keys, int *ctrl, int *shift, int *alt, int *key)
{
    *ctrl = 0;
    *shift = 0;
    *alt = 0;
    *key = 0;

    if (!keys)
        return 0;

    char *copy = strdup(keys);
    if (!copy)
        return 0;

    char *token = strtok(copy, "+");
    while (token)
    {
        // Trim whitespace
        while (*token == ' ')
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ')
            *end-- = '\0';

        if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0)
        {
            *ctrl = 1;
        }
        else if (strcasecmp(token, "Shift") == 0)
        {
            *shift = 1;
        }
        else if (strcasecmp(token, "Alt") == 0)
        {
            *alt = 1;
        }
        else if (strcasecmp(token, "Cmd") == 0 || strcasecmp(token, "Command") == 0)
        {
            *ctrl = 1; // Map Cmd to Ctrl for cross-platform
        }
        else if (strlen(token) == 1)
        {
            // Single character key
            *key = toupper(token[0]);
        }
        else if (token[0] == 'F' && strlen(token) <= 3)
        {
            // Function key (F1-F12)
            int fnum = atoi(token + 1);
            if (fnum >= 1 && fnum <= 12)
            {
                *key = 289 + fnum; // VGFX_KEY_F1 = 290 is approximated
            }
        }
        token = strtok(NULL, "+");
    }

    free(copy);
    return (*key != 0);
}

void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description)
{
    if (g_shortcut_count >= MAX_SHORTCUTS)
        return;

    char *cid = rt_string_to_cstr(id);
    char *ckeys = rt_string_to_cstr(keys);
    char *cdesc = rt_string_to_cstr(description);

    if (!cid)
        return;

    // Check if already registered and update
    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(g_shortcuts[i].keys);
            free(g_shortcuts[i].description);
            g_shortcuts[i].keys = ckeys;
            g_shortcuts[i].description = cdesc;
            free(cid);
            return;
        }
    }

    // Add new shortcut
    g_shortcuts[g_shortcut_count].id = cid;
    g_shortcuts[g_shortcut_count].keys = ckeys;
    g_shortcuts[g_shortcut_count].description = cdesc;
    g_shortcuts[g_shortcut_count].enabled = 1;
    g_shortcuts[g_shortcut_count].triggered = 0;
    g_shortcut_count++;
}

void rt_shortcuts_unregister(rt_string id)
{
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(g_shortcuts[i].id);
            free(g_shortcuts[i].keys);
            free(g_shortcuts[i].description);

            // Shift remaining shortcuts down
            for (int j = i; j < g_shortcut_count - 1; j++)
            {
                g_shortcuts[j] = g_shortcuts[j + 1];
            }
            g_shortcut_count--;
            break;
        }
    }

    free(cid);
}

void rt_shortcuts_clear(void)
{
    for (int i = 0; i < g_shortcut_count; i++)
    {
        free(g_shortcuts[i].id);
        free(g_shortcuts[i].keys);
        free(g_shortcuts[i].description);
    }
    g_shortcut_count = 0;
    g_triggered_shortcut_id = NULL;
}

int64_t rt_shortcuts_was_triggered(rt_string id)
{
    if (!g_shortcuts_global_enabled)
        return 0;

    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(cid);
            return g_shortcuts[i].triggered ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

rt_string rt_shortcuts_get_triggered(void)
{
    if (g_triggered_shortcut_id)
    {
        return rt_string_from_bytes(g_triggered_shortcut_id, strlen(g_triggered_shortcut_id));
    }
    return rt_str_empty();
}

void rt_shortcuts_set_enabled(rt_string id, int64_t enabled)
{
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            g_shortcuts[i].enabled = enabled != 0;
            break;
        }
    }

    free(cid);
}

int64_t rt_shortcuts_is_enabled(rt_string id)
{
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(cid);
            return g_shortcuts[i].enabled ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

void rt_shortcuts_set_global_enabled(int64_t enabled)
{
    g_shortcuts_global_enabled = enabled != 0;
}

int64_t rt_shortcuts_get_global_enabled(void)
{
    return g_shortcuts_global_enabled ? 1 : 0;
}

//=============================================================================
// Window Management (Phase 1)
//=============================================================================

void rt_app_set_title(void *app, rt_string title)
{
    if (!app)
        return;
    // Window title changes are not directly supported in vgfx yet
    // This is a stub for future implementation
    (void)title;
}

rt_string rt_app_get_title(void *app)
{
    if (!app)
        return rt_str_empty();
    // Return empty string until window title tracking is implemented
    return rt_str_empty();
}

void rt_app_set_size(void *app, int64_t width, int64_t height)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->root)
    {
        vg_widget_set_fixed_size(gui_app->root, (float)width, (float)height);
        gui_app->root->width = (float)width;
        gui_app->root->height = (float)height;
    }
}

int64_t rt_app_get_width(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return w;
}

int64_t rt_app_get_height(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return h;
}

void rt_app_set_position(void *app, int64_t x, int64_t y)
{
    // Window positioning not yet supported in vgfx
    (void)app;
    (void)x;
    (void)y;
}

int64_t rt_app_get_x(void *app)
{
    // Window position not yet supported in vgfx
    (void)app;
    return 0;
}

int64_t rt_app_get_y(void *app)
{
    // Window position not yet supported in vgfx
    (void)app;
    return 0;
}

void rt_app_minimize(void *app)
{
    // Window state control not yet supported in vgfx
    (void)app;
}

void rt_app_maximize(void *app)
{
    // Window state control not yet supported in vgfx
    (void)app;
}

void rt_app_restore(void *app)
{
    // Window state control not yet supported in vgfx
    (void)app;
}

int64_t rt_app_is_minimized(void *app)
{
    // Window state not yet supported in vgfx
    (void)app;
    return 0;
}

int64_t rt_app_is_maximized(void *app)
{
    // Window state not yet supported in vgfx
    (void)app;
    return 0;
}

void rt_app_set_fullscreen(void *app, int64_t fullscreen)
{
    // Fullscreen not yet supported in vgfx
    (void)app;
    (void)fullscreen;
}

int64_t rt_app_is_fullscreen(void *app)
{
    // Fullscreen not yet supported in vgfx
    (void)app;
    return 0;
}

void rt_app_focus(void *app)
{
    // Window focus control not yet supported in vgfx
    (void)app;
}

int64_t rt_app_is_focused(void *app)
{
    // Window focus state not yet supported in vgfx
    (void)app;
    return 1; // Assume focused for now
}

void rt_app_set_prevent_close(void *app, int64_t prevent)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    // Store prevent_close flag (would need to add field to rt_gui_app_t)
    (void)gui_app;
    (void)prevent;
}

int64_t rt_app_was_close_requested(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->should_close;
}

//=============================================================================
// Cursor Styles (Phase 1)
//=============================================================================

static int64_t g_current_cursor = RT_CURSOR_ARROW;
static int64_t g_cursor_visible = 1;

void rt_cursor_set(int64_t type)
{
    g_current_cursor = type;
    // Actual cursor setting would require vgfx platform support
}

void rt_cursor_reset(void)
{
    g_current_cursor = RT_CURSOR_ARROW;
}

void rt_cursor_set_visible(int64_t visible)
{
    g_cursor_visible = visible;
    // Actual visibility control would require vgfx platform support
}

void rt_widget_set_cursor(void *widget, int64_t type)
{
    if (!widget)
        return;
    // Widget cursor would be stored in widget data
    // For now, just set global cursor when widget is hovered
    (void)type;
}

void rt_widget_reset_cursor(void *widget)
{
    if (!widget)
        return;
    // Reset widget cursor to default
}

//=============================================================================
// MenuBar Widget (Phase 2)
//=============================================================================

void *rt_menubar_new(void *parent)
{
    return vg_menubar_create((vg_widget_t *)parent);
}

void rt_menubar_destroy(void *menubar)
{
    if (!menubar)
        return;
    vg_widget_destroy((vg_widget_t *)menubar);
}

void *rt_menubar_add_menu(void *menubar, rt_string title)
{
    if (!menubar)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_menu_t *menu = vg_menubar_add_menu((vg_menubar_t *)menubar, ctitle);
    free(ctitle);
    return menu;
}

void rt_menubar_remove_menu(void *menubar, void *menu)
{
    if (!menubar || !menu)
        return;
    // Note: vg_menubar_remove_menu not yet implemented in GUI library
    // Stub for future implementation
    (void)menubar;
    (void)menu;
}

int64_t rt_menubar_get_menu_count(void *menubar)
{
    if (!menubar)
        return 0;
    return ((vg_menubar_t *)menubar)->menu_count;
}

void *rt_menubar_get_menu(void *menubar, int64_t index)
{
    if (!menubar)
        return NULL;
    vg_menubar_t *mb = (vg_menubar_t *)menubar;
    if (index < 0 || index >= mb->menu_count)
        return NULL;

    vg_menu_t *menu = mb->first_menu;
    for (int64_t i = 0; i < index && menu; i++)
    {
        menu = menu->next;
    }
    return menu;
}

void rt_menubar_set_visible(void *menubar, int64_t visible)
{
    if (!menubar)
        return;
    vg_widget_set_visible(&((vg_menubar_t *)menubar)->base, visible != 0);
}

int64_t rt_menubar_is_visible(void *menubar)
{
    if (!menubar)
        return 0;
    return ((vg_menubar_t *)menubar)->base.visible ? 1 : 0;
}

//=============================================================================
// Menu Widget (Phase 2)
//=============================================================================

void *rt_menu_add_item(void *menu, rt_string text)
{
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_menu_item_t *item = vg_menu_add_item((vg_menu_t *)menu, ctext, NULL, NULL, NULL);
    free(ctext);
    return item;
}

void *rt_menu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut)
{
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    char *cshortcut = rt_string_to_cstr(shortcut);
    vg_menu_item_t *item = vg_menu_add_item((vg_menu_t *)menu, ctext, cshortcut, NULL, NULL);
    free(ctext);
    free(cshortcut);
    return item;
}

void *rt_menu_add_separator(void *menu)
{
    if (!menu)
        return NULL;
    return vg_menu_add_separator((vg_menu_t *)menu);
}

void *rt_menu_add_submenu(void *menu, rt_string title)
{
    if (!menu)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_menu_t *submenu = vg_menu_add_submenu((vg_menu_t *)menu, ctitle);
    free(ctitle);
    return submenu;
}

void rt_menu_remove_item(void *menu, void *item)
{
    if (!menu || !item)
        return;
    // Note: vg_menu_remove_item not yet implemented in GUI library
    // Stub for future implementation
    (void)menu;
    (void)item;
}

void rt_menu_clear(void *menu)
{
    if (!menu)
        return;
    // Note: vg_menu_clear not yet implemented in GUI library
    // Stub for future implementation
    (void)menu;
}

void rt_menu_set_title(void *menu, rt_string title)
{
    if (!menu)
        return;
    vg_menu_t *m = (vg_menu_t *)menu;
    free((void *)m->title);
    m->title = rt_string_to_cstr(title);
}

rt_string rt_menu_get_title(void *menu)
{
    if (!menu)
        return rt_str_empty();
    const char *title = ((vg_menu_t *)menu)->title;
    if (!title)
        return rt_str_empty();
    return rt_string_from_bytes(title, strlen(title));
}

int64_t rt_menu_get_item_count(void *menu)
{
    if (!menu)
        return 0;
    return ((vg_menu_t *)menu)->item_count;
}

void *rt_menu_get_item(void *menu, int64_t index)
{
    if (!menu)
        return NULL;
    vg_menu_t *m = (vg_menu_t *)menu;
    if (index < 0 || index >= m->item_count)
        return NULL;

    vg_menu_item_t *item = m->first_item;
    for (int64_t i = 0; i < index && item; i++)
    {
        item = item->next;
    }
    return item;
}

void rt_menu_set_enabled(void *menu, int64_t enabled)
{
    if (!menu)
        return;
    // Menu enabled state not currently tracked in vg_menu struct
    // Stub for future implementation
    (void)enabled;
}

int64_t rt_menu_is_enabled(void *menu)
{
    if (!menu)
        return 0;
    // Menu enabled state not currently tracked in vg_menu struct
    return 1; // Default to enabled
}

//=============================================================================
// MenuItem Widget (Phase 2)
//=============================================================================

void rt_menuitem_set_text(void *item, rt_string text)
{
    if (!item)
        return;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    free((void *)mi->text);
    mi->text = rt_string_to_cstr(text);
}

rt_string rt_menuitem_get_text(void *item)
{
    if (!item)
        return rt_str_empty();
    const char *text = ((vg_menu_item_t *)item)->text;
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_menuitem_set_shortcut(void *item, rt_string shortcut)
{
    if (!item)
        return;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    free((void *)mi->shortcut);
    mi->shortcut = rt_string_to_cstr(shortcut);
}

rt_string rt_menuitem_get_shortcut(void *item)
{
    if (!item)
        return rt_str_empty();
    const char *shortcut = ((vg_menu_item_t *)item)->shortcut;
    if (!shortcut)
        return rt_str_empty();
    return rt_string_from_bytes(shortcut, strlen(shortcut));
}

void rt_menuitem_set_icon(void *item, void *pixels)
{
    if (!item)
        return;
    // Icon support would require extending vg_menu_item_t
    (void)pixels;
}

void rt_menuitem_set_checkable(void *item, int64_t checkable)
{
    if (!item)
        return;
    // Checkable state would need to be added to vg_menu_item_t
    (void)checkable;
}

int64_t rt_menuitem_is_checkable(void *item)
{
    if (!item)
        return 0;
    // Return 0 until checkable support is added
    return 0;
}

void rt_menuitem_set_checked(void *item, int64_t checked)
{
    if (!item)
        return;
    ((vg_menu_item_t *)item)->checked = checked != 0;
}

int64_t rt_menuitem_is_checked(void *item)
{
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->checked ? 1 : 0;
}

void rt_menuitem_set_enabled(void *item, int64_t enabled)
{
    if (!item)
        return;
    ((vg_menu_item_t *)item)->enabled = enabled != 0;
}

int64_t rt_menuitem_is_enabled(void *item)
{
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->enabled ? 1 : 0;
}

int64_t rt_menuitem_is_separator(void *item)
{
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->separator ? 1 : 0;
}

// Track clicked menu items per frame
static vg_menu_item_t *g_clicked_menuitem = NULL;

void rt_gui_set_clicked_menuitem(void *item)
{
    g_clicked_menuitem = (vg_menu_item_t *)item;
}

int64_t rt_menuitem_was_clicked(void *item)
{
    if (!item)
        return 0;
    return (g_clicked_menuitem == item) ? 1 : 0;
}

//=============================================================================
// ContextMenu Widget (Phase 2)
//=============================================================================

void *rt_contextmenu_new(void)
{
    return vg_contextmenu_create();
}

void rt_contextmenu_destroy(void *menu)
{
    if (menu)
    {
        vg_contextmenu_destroy((vg_contextmenu_t *)menu);
    }
}

void *rt_contextmenu_add_item(void *menu, rt_string text)
{
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_menu_item_t *item =
        vg_contextmenu_add_item((vg_contextmenu_t *)menu, ctext, NULL, NULL, NULL);
    free(ctext);
    return item;
}

void *rt_contextmenu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut)
{
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    char *cshortcut = rt_string_to_cstr(shortcut);
    vg_menu_item_t *item =
        vg_contextmenu_add_item((vg_contextmenu_t *)menu, ctext, cshortcut, NULL, NULL);
    free(ctext);
    free(cshortcut);
    return item;
}

void *rt_contextmenu_add_separator(void *menu)
{
    if (!menu)
        return NULL;
    vg_contextmenu_add_separator((vg_contextmenu_t *)menu);
    return NULL; // vg_contextmenu_add_separator returns void
}

void *rt_contextmenu_add_submenu(void *menu, rt_string title)
{
    if (!menu)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    // Context menu submenu support would need vg_contextmenu_add_submenu
    // For now return NULL as placeholder
    free(ctitle);
    return NULL;
}

void rt_contextmenu_clear(void *menu)
{
    if (menu)
    {
        vg_contextmenu_clear((vg_contextmenu_t *)menu);
    }
}

void rt_contextmenu_show(void *menu, int64_t x, int64_t y)
{
    if (menu)
    {
        vg_contextmenu_show_at((vg_contextmenu_t *)menu, (int)x, (int)y);
    }
}

void rt_contextmenu_hide(void *menu)
{
    if (menu)
    {
        vg_contextmenu_dismiss((vg_contextmenu_t *)menu);
    }
}

int64_t rt_contextmenu_is_visible(void *menu)
{
    if (!menu)
        return 0;
    return ((vg_contextmenu_t *)menu)->is_visible ? 1 : 0;
}

void *rt_contextmenu_get_clicked_item(void *menu)
{
    if (!menu)
        return NULL;
    vg_contextmenu_t *cm = (vg_contextmenu_t *)menu;
    if (cm->hovered_index >= 0 && cm->hovered_index < (int)cm->item_count)
    {
        return cm->items[cm->hovered_index];
    }
    return NULL;
}

//=============================================================================
// StatusBar Widget (Phase 3)
//=============================================================================

void *rt_statusbar_new(void *parent)
{
    return vg_statusbar_create((vg_widget_t *)parent);
}

void rt_statusbar_destroy(void *bar)
{
    if (bar)
    {
        vg_widget_destroy((vg_widget_t *)bar);
    }
}

// Internal: helper to get first text item in a zone
static vg_statusbar_item_t *get_zone_text_item(vg_statusbar_t *sb, vg_statusbar_zone_t zone)
{
    vg_statusbar_item_t **items = NULL;
    size_t count = 0;
    switch (zone)
    {
        case VG_STATUSBAR_ZONE_LEFT:
            items = sb->left_items;
            count = sb->left_count;
            break;
        case VG_STATUSBAR_ZONE_CENTER:
            items = sb->center_items;
            count = sb->center_count;
            break;
        case VG_STATUSBAR_ZONE_RIGHT:
            items = sb->right_items;
            count = sb->right_count;
            break;
    }
    for (size_t i = 0; i < count; i++)
    {
        if (items[i] && items[i]->type == VG_STATUSBAR_ITEM_TEXT)
        {
            return items[i];
        }
    }
    return NULL;
}

void rt_statusbar_set_left_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_LEFT);
    if (item)
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_item_set_text(item, ctext);
        free(ctext);
    }
    else
    {
        // Create a new text item
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_LEFT, ctext);
        free(ctext);
    }
}

void rt_statusbar_set_center_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_CENTER);
    if (item)
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_item_set_text(item, ctext);
        free(ctext);
    }
    else
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_CENTER, ctext);
        free(ctext);
    }
}

void rt_statusbar_set_right_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_RIGHT);
    if (item)
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_item_set_text(item, ctext);
        free(ctext);
    }
    else
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_RIGHT, ctext);
        free(ctext);
    }
}

rt_string rt_statusbar_get_left_text(void *bar)
{
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_LEFT);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

rt_string rt_statusbar_get_center_text(void *bar)
{
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_CENTER);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

rt_string rt_statusbar_get_right_text(void *bar)
{
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_RIGHT);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

void *rt_statusbar_add_text(void *bar, rt_string text, int64_t zone)
{
    if (!bar)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_statusbar_item_t *item =
        vg_statusbar_add_text((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone, ctext);
    free(ctext);
    return item;
}

void *rt_statusbar_add_button(void *bar, rt_string text, int64_t zone)
{
    if (!bar)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_statusbar_item_t *item = vg_statusbar_add_button(
        (vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone, ctext, NULL, NULL);
    free(ctext);
    return item;
}

void *rt_statusbar_add_progress(void *bar, int64_t zone)
{
    if (!bar)
        return NULL;
    return vg_statusbar_add_progress((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void *rt_statusbar_add_separator(void *bar, int64_t zone)
{
    if (!bar)
        return NULL;
    return vg_statusbar_add_separator((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void *rt_statusbar_add_spacer(void *bar, int64_t zone)
{
    if (!bar)
        return NULL;
    return vg_statusbar_add_spacer((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void rt_statusbar_remove_item(void *bar, void *item)
{
    if (!bar || !item)
        return;
    vg_statusbar_remove_item((vg_statusbar_t *)bar, (vg_statusbar_item_t *)item);
}

void rt_statusbar_clear(void *bar)
{
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_LEFT);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_CENTER);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_RIGHT);
}

void rt_statusbar_set_visible(void *bar, int64_t visible)
{
    if (!bar)
        return;
    vg_widget_set_visible(&((vg_statusbar_t *)bar)->base, visible != 0);
}

int64_t rt_statusbar_is_visible(void *bar)
{
    if (!bar)
        return 0;
    return ((vg_statusbar_t *)bar)->base.visible ? 1 : 0;
}

//=============================================================================
// StatusBarItem Widget (Phase 3)
//=============================================================================

void rt_statusbaritem_set_text(void *item, rt_string text)
{
    if (!item)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_statusbar_item_set_text((vg_statusbar_item_t *)item, ctext);
    free(ctext);
}

rt_string rt_statusbaritem_get_text(void *item)
{
    if (!item)
        return rt_str_empty();
    vg_statusbar_item_t *sbi = (vg_statusbar_item_t *)item;
    if (sbi->text)
    {
        return rt_string_from_bytes(sbi->text, strlen(sbi->text));
    }
    return rt_str_empty();
}

void rt_statusbaritem_set_tooltip(void *item, rt_string tooltip)
{
    if (!item)
        return;
    char *ctext = rt_string_to_cstr(tooltip);
    vg_statusbar_item_set_tooltip((vg_statusbar_item_t *)item, ctext);
    free(ctext);
}

void rt_statusbaritem_set_progress(void *item, double value)
{
    if (!item)
        return;
    vg_statusbar_item_set_progress((vg_statusbar_item_t *)item, (float)value);
}

double rt_statusbaritem_get_progress(void *item)
{
    if (!item)
        return 0.0;
    return (double)((vg_statusbar_item_t *)item)->progress;
}

void rt_statusbaritem_set_visible(void *item, int64_t visible)
{
    if (!item)
        return;
    vg_statusbar_item_set_visible((vg_statusbar_item_t *)item, visible != 0);
}

// Track clicked status bar item
static vg_statusbar_item_t *g_clicked_statusbar_item = NULL;

void rt_gui_set_clicked_statusbar_item(void *item)
{
    g_clicked_statusbar_item = (vg_statusbar_item_t *)item;
}

int64_t rt_statusbaritem_was_clicked(void *item)
{
    if (!item)
        return 0;
    return (g_clicked_statusbar_item == item) ? 1 : 0;
}

//=============================================================================
// Toolbar Widget (Phase 3)
//=============================================================================

void *rt_toolbar_new(void *parent)
{
    return vg_toolbar_create((vg_widget_t *)parent, VG_TOOLBAR_HORIZONTAL);
}

void *rt_toolbar_new_vertical(void *parent)
{
    return vg_toolbar_create((vg_widget_t *)parent, VG_TOOLBAR_VERTICAL);
}

void rt_toolbar_destroy(void *toolbar)
{
    if (toolbar)
    {
        vg_widget_destroy((vg_widget_t *)toolbar);
    }
}

void *rt_toolbar_add_button(void *toolbar, rt_string icon_path, rt_string tooltip)
{
    if (!toolbar)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    icon.type = VG_ICON_PATH;
    icon.data.path = cicon;

    vg_toolbar_item_t *item =
        vg_toolbar_add_button((vg_toolbar_t *)toolbar, NULL, NULL, icon, NULL, NULL);
    if (item)
    {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(cicon);
    free(ctooltip);
    return item;
}

void *rt_toolbar_add_button_with_text(void *toolbar,
                                      rt_string icon_path,
                                      rt_string text,
                                      rt_string tooltip)
{
    if (!toolbar)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctext = rt_string_to_cstr(text);
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    icon.type = VG_ICON_PATH;
    icon.data.path = cicon;

    vg_toolbar_item_t *item =
        vg_toolbar_add_button((vg_toolbar_t *)toolbar, NULL, ctext, icon, NULL, NULL);
    if (item)
    {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(cicon);
    free(ctext);
    free(ctooltip);
    return item;
}

void *rt_toolbar_add_toggle(void *toolbar, rt_string icon_path, rt_string tooltip)
{
    if (!toolbar)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    icon.type = VG_ICON_PATH;
    icon.data.path = cicon;

    vg_toolbar_item_t *item =
        vg_toolbar_add_toggle((vg_toolbar_t *)toolbar, NULL, NULL, icon, false, NULL, NULL);
    if (item)
    {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(cicon);
    free(ctooltip);
    return item;
}

void *rt_toolbar_add_separator(void *toolbar)
{
    if (!toolbar)
        return NULL;
    return vg_toolbar_add_separator((vg_toolbar_t *)toolbar);
}

void *rt_toolbar_add_spacer(void *toolbar)
{
    if (!toolbar)
        return NULL;
    return vg_toolbar_add_spacer((vg_toolbar_t *)toolbar);
}

void *rt_toolbar_add_dropdown(void *toolbar, rt_string tooltip)
{
    if (!toolbar)
        return NULL;
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    icon.type = VG_ICON_NONE;

    vg_toolbar_item_t *item =
        vg_toolbar_add_dropdown((vg_toolbar_t *)toolbar, NULL, NULL, icon, NULL);
    if (item)
    {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(ctooltip);
    return item;
}

void rt_toolbar_remove_item(void *toolbar, void *item)
{
    if (!toolbar || !item)
        return;
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    if (ti->id)
    {
        vg_toolbar_remove_item((vg_toolbar_t *)toolbar, ti->id);
    }
}

void rt_toolbar_set_icon_size(void *toolbar, int64_t size)
{
    if (!toolbar)
        return;
    vg_toolbar_set_icon_size((vg_toolbar_t *)toolbar, (vg_toolbar_icon_size_t)size);
}

int64_t rt_toolbar_get_icon_size(void *toolbar)
{
    if (!toolbar)
        return RT_TOOLBAR_ICON_MEDIUM;
    return ((vg_toolbar_t *)toolbar)->icon_size;
}

void rt_toolbar_set_style(void *toolbar, int64_t style)
{
    if (!toolbar)
        return;
    vg_toolbar_set_show_labels((vg_toolbar_t *)toolbar, style != RT_TOOLBAR_STYLE_ICON_ONLY);
}

int64_t rt_toolbar_get_item_count(void *toolbar)
{
    if (!toolbar)
        return 0;
    return ((vg_toolbar_t *)toolbar)->item_count;
}

void *rt_toolbar_get_item(void *toolbar, int64_t index)
{
    if (!toolbar)
        return NULL;
    vg_toolbar_t *tb = (vg_toolbar_t *)toolbar;
    if (index < 0 || index >= (int64_t)tb->item_count)
        return NULL;
    return tb->items[index];
}

void rt_toolbar_set_visible(void *toolbar, int64_t visible)
{
    if (!toolbar)
        return;
    ((vg_toolbar_t *)toolbar)->base.visible = visible != 0;
}

int64_t rt_toolbar_is_visible(void *toolbar)
{
    if (!toolbar)
        return 0;
    return ((vg_toolbar_t *)toolbar)->base.visible ? 1 : 0;
}

//=============================================================================
// ToolbarItem Widget (Phase 3)
//=============================================================================

void rt_toolbaritem_set_icon(void *item, rt_string icon_path)
{
    if (!item)
        return;
    char *cicon = rt_string_to_cstr(icon_path);
    vg_icon_t icon = {0};
    icon.type = VG_ICON_PATH;
    icon.data.path = cicon;
    vg_toolbar_item_set_icon((vg_toolbar_item_t *)item, icon);
    free(cicon);
}

void rt_toolbaritem_set_icon_pixels(void *item, void *pixels)
{
    if (!item || !pixels)
        return;
    // Would need to convert pixels to vg_icon_t
    // Stub for now
    (void)pixels;
}

void rt_toolbaritem_set_text(void *item, rt_string text)
{
    if (!item)
        return;
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    free(ti->label);
    ti->label = rt_string_to_cstr(text);
}

void rt_toolbaritem_set_tooltip(void *item, rt_string tooltip)
{
    if (!item)
        return;
    char *ctooltip = rt_string_to_cstr(tooltip);
    vg_toolbar_item_set_tooltip((vg_toolbar_item_t *)item, ctooltip);
    free(ctooltip);
}

void rt_toolbaritem_set_enabled(void *item, int64_t enabled)
{
    if (!item)
        return;
    vg_toolbar_item_set_enabled((vg_toolbar_item_t *)item, enabled != 0);
}

int64_t rt_toolbaritem_is_enabled(void *item)
{
    if (!item)
        return 0;
    return ((vg_toolbar_item_t *)item)->enabled ? 1 : 0;
}

void rt_toolbaritem_set_toggled(void *item, int64_t toggled)
{
    if (!item)
        return;
    vg_toolbar_item_set_checked((vg_toolbar_item_t *)item, toggled != 0);
}

int64_t rt_toolbaritem_is_toggled(void *item)
{
    if (!item)
        return 0;
    return ((vg_toolbar_item_t *)item)->checked ? 1 : 0;
}

// Track clicked toolbar item
static vg_toolbar_item_t *g_clicked_toolbar_item = NULL;

void rt_gui_set_clicked_toolbar_item(void *item)
{
    g_clicked_toolbar_item = (vg_toolbar_item_t *)item;
}

int64_t rt_toolbaritem_was_clicked(void *item)
{
    if (!item)
        return 0;
    return (g_clicked_toolbar_item == item) ? 1 : 0;
}

//=============================================================================
// CodeEditor Enhancements - Syntax Highlighting (Phase 4)
//=============================================================================

void rt_codeeditor_set_language(void *editor, rt_string language)
{
    if (!editor)
        return;
    char *clang = rt_string_to_cstr(language);
    // Store language in editor state (would need vg_codeeditor_set_language)
    // Stub for now - the actual implementation would set up syntax rules
    free(clang);
}

void rt_codeeditor_set_token_color(void *editor, int64_t token_type, int64_t color)
{
    if (!editor)
        return;
    // Would store token colors in editor state
    // Stub for now
    (void)token_type;
    (void)color;
}

void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords)
{
    if (!editor)
        return;
    char *ckw = rt_string_to_cstr(keywords);
    // Would parse and store custom keywords
    // Stub for now
    free(ckw);
}

void rt_codeeditor_clear_highlights(void *editor)
{
    if (!editor)
        return;
    // Would clear all syntax highlight spans
    // Stub for now
}

void rt_codeeditor_add_highlight(void *editor,
                                 int64_t start_line,
                                 int64_t start_col,
                                 int64_t end_line,
                                 int64_t end_col,
                                 int64_t token_type)
{
    if (!editor)
        return;
    // Would add a highlight span to the editor
    // Stub for now
    (void)start_line;
    (void)start_col;
    (void)end_line;
    (void)end_col;
    (void)token_type;
}

void rt_codeeditor_refresh_highlights(void *editor)
{
    if (!editor)
        return;
    // Would trigger a re-render with updated highlights
    // Stub for now
}

//=============================================================================
// CodeEditor Enhancements - Gutter & Line Numbers (Phase 4)
//=============================================================================

void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->show_line_numbers = show != 0;
}

int64_t rt_codeeditor_get_show_line_numbers(void *editor)
{
    if (!editor)
        return 1; // Default to showing
    return ((vg_codeeditor_t *)editor)->show_line_numbers ? 1 : 0;
}

void rt_codeeditor_set_line_number_width(void *editor, int64_t width)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->gutter_width = (int)width * 8; // Approximate char width
}

void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot)
{
    if (!editor)
        return;
    // Would store gutter icon for the line/slot
    // Stub for now
    (void)line;
    (void)pixels;
    (void)slot;
}

void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot)
{
    if (!editor)
        return;
    // Would clear gutter icon for the line/slot
    // Stub for now
    (void)line;
    (void)slot;
}

void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot)
{
    if (!editor)
        return;
    // Would clear all gutter icons for the slot
    // Stub for now
    (void)slot;
}

// Gutter click tracking
static int g_gutter_clicked = 0;
static int64_t g_gutter_clicked_line = -1;
static int64_t g_gutter_clicked_slot = -1;

void rt_gui_set_gutter_click(int64_t line, int64_t slot)
{
    g_gutter_clicked = 1;
    g_gutter_clicked_line = line;
    g_gutter_clicked_slot = slot;
}

void rt_gui_clear_gutter_click(void)
{
    g_gutter_clicked = 0;
    g_gutter_clicked_line = -1;
    g_gutter_clicked_slot = -1;
}

int64_t rt_codeeditor_was_gutter_clicked(void *editor)
{
    if (!editor)
        return 0;
    return g_gutter_clicked ? 1 : 0;
}

int64_t rt_codeeditor_get_gutter_clicked_line(void *editor)
{
    if (!editor)
        return -1;
    return g_gutter_clicked_line;
}

int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor)
{
    if (!editor)
        return -1;
    return g_gutter_clicked_slot;
}

void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show)
{
    if (!editor)
        return;
    // Would enable/disable fold gutter column
    // Stub for now
    (void)show;
}

//=============================================================================
// CodeEditor Enhancements - Code Folding (Phase 4)
//=============================================================================

void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line)
{
    if (!editor)
        return;
    // Would add a foldable region
    // Stub for now
    (void)start_line;
    (void)end_line;
}

void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line)
{
    if (!editor)
        return;
    // Would remove a foldable region
    // Stub for now
    (void)start_line;
}

void rt_codeeditor_clear_fold_regions(void *editor)
{
    if (!editor)
        return;
    // Would clear all fold regions
    // Stub for now
}

void rt_codeeditor_fold(void *editor, int64_t line)
{
    if (!editor)
        return;
    // Would fold the region at line
    // Stub for now
    (void)line;
}

void rt_codeeditor_unfold(void *editor, int64_t line)
{
    if (!editor)
        return;
    // Would unfold the region at line
    // Stub for now
    (void)line;
}

void rt_codeeditor_toggle_fold(void *editor, int64_t line)
{
    if (!editor)
        return;
    // Would toggle fold state at line
    // Stub for now
    (void)line;
}

int64_t rt_codeeditor_is_folded(void *editor, int64_t line)
{
    if (!editor)
        return 0;
    // Would check if line is in a folded region
    // Stub for now
    (void)line;
    return 0;
}

void rt_codeeditor_fold_all(void *editor)
{
    if (!editor)
        return;
    // Would fold all regions
    // Stub for now
}

void rt_codeeditor_unfold_all(void *editor)
{
    if (!editor)
        return;
    // Would unfold all regions
    // Stub for now
}

void rt_codeeditor_set_auto_fold_detection(void *editor, int64_t enable)
{
    if (!editor)
        return;
    // Would enable/disable automatic fold detection
    // Stub for now
    (void)enable;
}

//=============================================================================
// CodeEditor Enhancements - Multiple Cursors (Phase 4)
//=============================================================================

int64_t rt_codeeditor_get_cursor_count(void *editor)
{
    if (!editor)
        return 1;
    // Currently only support single cursor, return 1
    return 1;
}

void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col)
{
    if (!editor)
        return;
    // Would add additional cursor
    // Stub for now - single cursor only
    (void)line;
    (void)col;
}

void rt_codeeditor_remove_cursor(void *editor, int64_t index)
{
    if (!editor)
        return;
    // Would remove cursor at index (except primary)
    // Stub for now
    (void)index;
}

void rt_codeeditor_clear_extra_cursors(void *editor)
{
    if (!editor)
        return;
    // Would clear all cursors except primary
    // Stub for now - only single cursor supported
}

int64_t rt_codeeditor_get_cursor_line_at(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    return ((vg_codeeditor_t *)editor)->cursor_line;
}

int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    return ((vg_codeeditor_t *)editor)->cursor_col;
}

void rt_codeeditor_set_cursor_position_at(void *editor, int64_t index, int64_t line, int64_t col)
{
    if (!editor)
        return;
    if (index != 0)
        return; // Only primary cursor supported
    vg_codeeditor_set_cursor((vg_codeeditor_t *)editor, (int)line, (int)col);
}

void rt_codeeditor_set_cursor_selection(void *editor,
                                        int64_t index,
                                        int64_t start_line,
                                        int64_t start_col,
                                        int64_t end_line,
                                        int64_t end_col)
{
    if (!editor)
        return;
    if (index != 0)
        return; // Only primary cursor supported
    // Would set selection for cursor
    // Stub for now
    (void)start_line;
    (void)start_col;
    (void)end_line;
    (void)end_col;
}

int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return ce->has_selection ? 1 : 0;
}

//=============================================================================
// Phase 5: MessageBox Dialog
//=============================================================================

int64_t rt_messagebox_info(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_INFO, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    // In a real implementation, we'd need to run a modal loop
    // For now, just return 0 (OK button)
    return 0;
}

int64_t rt_messagebox_warning(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_WARNING, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    return 0;
}

int64_t rt_messagebox_error(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_ERROR, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    return 0;
}

int64_t rt_messagebox_question(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_QUESTION, VG_DIALOG_BUTTONS_YES_NO);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    // Return 1 for Yes, 0 for No - would need modal loop for real result
    return 1;
}

int64_t rt_messagebox_confirm(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_QUESTION, VG_DIALOG_BUTTONS_OK_CANCEL);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    // Return 1 for OK, 0 for Cancel - would need modal loop for real result
    return 1;
}

// Custom MessageBox structure for tracking state
typedef struct
{
    vg_dialog_t *dialog;
    int64_t result;
    int64_t default_button;
} rt_messagebox_data_t;

void *rt_messagebox_new(rt_string title, rt_string message, int64_t type)
{
    char *ctitle = rt_string_to_cstr(title);
    vg_dialog_t *dlg = vg_dialog_create(ctitle);
    if (ctitle)
        free(ctitle);
    if (!dlg)
        return NULL;

    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_set_message(dlg, cmsg);
    if (cmsg)
        free(cmsg);

    vg_dialog_icon_t icon = VG_DIALOG_ICON_INFO;
    switch (type)
    {
        case RT_MESSAGEBOX_INFO:
            icon = VG_DIALOG_ICON_INFO;
            break;
        case RT_MESSAGEBOX_WARNING:
            icon = VG_DIALOG_ICON_WARNING;
            break;
        case RT_MESSAGEBOX_ERROR:
            icon = VG_DIALOG_ICON_ERROR;
            break;
        case RT_MESSAGEBOX_QUESTION:
            icon = VG_DIALOG_ICON_QUESTION;
            break;
    }
    vg_dialog_set_icon(dlg, icon);
    vg_dialog_set_buttons(dlg, VG_DIALOG_BUTTONS_NONE);

    rt_messagebox_data_t *data = (rt_messagebox_data_t *)malloc(sizeof(rt_messagebox_data_t));
    if (!data)
        return NULL;
    data->dialog = dlg;
    data->result = -1;
    data->default_button = 0;

    return data;
}

void rt_messagebox_add_button(void *box, rt_string text, int64_t id)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    // In a full implementation, we'd track custom buttons
    // For now, stub - the dialog system uses presets
    (void)data;
    (void)text;
    (void)id;
}

void rt_messagebox_set_default_button(void *box, int64_t id)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    data->default_button = id;
}

int64_t rt_messagebox_show(void *box)
{
    if (!box)
        return -1;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    vg_dialog_show(data->dialog);
    // Would need modal loop to get actual result
    return data->default_button;
}

void rt_messagebox_destroy(void *box)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    if (data->dialog)
    {
        vg_widget_destroy((vg_widget_t *)data->dialog);
    }
    free(data);
}

//=============================================================================
// Phase 5: FileDialog
//=============================================================================

rt_string rt_filedialog_open(rt_string title, rt_string default_path, rt_string filter)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);
    char *cfilter = rt_string_to_cstr(filter);

    // vg_filedialog_open_file expects: title, path, filter_name, filter_pattern
    char *result = vg_filedialog_open_file(ctitle, cpath, "Files", cfilter);

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);
    char *cfilter = rt_string_to_cstr(filter);

    vg_filedialog_t *dlg = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    if (!dlg)
    {
        if (ctitle)
            free(ctitle);
        if (cpath)
            free(cpath);
        if (cfilter)
            free(cfilter);
        return rt_string_from_bytes("", 0);
    }

    vg_filedialog_set_title(dlg, ctitle);
    vg_filedialog_set_initial_path(dlg, cpath);
    vg_filedialog_set_multi_select(dlg, true);
    if (cfilter && cfilter[0])
    {
        vg_filedialog_add_filter(dlg, "Files", cfilter);
    }

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    vg_filedialog_show(dlg);

    size_t count = 0;
    char **paths = vg_filedialog_get_selected_paths(dlg, &count);

    rt_string result = rt_string_from_bytes("", 0);
    if (paths && count > 0)
    {
        // Join paths with semicolon
        size_t total_len = 0;
        for (size_t i = 0; i < count; i++)
        {
            total_len += strlen(paths[i]) + 1;
        }
        char *joined = (char *)malloc(total_len);
        if (joined)
        {
            joined[0] = '\0';
            for (size_t i = 0; i < count; i++)
            {
                if (i > 0)
                    strcat(joined, ";");
                strcat(joined, paths[i]);
            }
            result = rt_string_from_bytes(joined, strlen(joined));
            free(joined);
        }
        for (size_t i = 0; i < count; i++)
        {
            free(paths[i]);
        }
        free(paths);
    }

    vg_filedialog_destroy(dlg);
    return result;
}

rt_string rt_filedialog_save(rt_string title,
                             rt_string default_path,
                             rt_string filter,
                             rt_string default_name)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);
    char *cfilter = rt_string_to_cstr(filter);
    char *cname = rt_string_to_cstr(default_name);

    // vg_filedialog_save_file expects: title, path, default_name, filter_name, filter_pattern
    char *result = vg_filedialog_save_file(ctitle, cpath, cname, "Files", cfilter);

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);
    if (cname)
        free(cname);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);

    char *result = vg_filedialog_select_folder(ctitle, cpath);

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

// Custom FileDialog structure
typedef struct
{
    vg_filedialog_t *dialog;
    char **selected_paths;
    size_t selected_count;
    int64_t result;
} rt_filedialog_data_t;

void *rt_filedialog_new(int64_t type)
{
    vg_filedialog_mode_t mode;
    switch (type)
    {
        case RT_FILEDIALOG_OPEN:
            mode = VG_FILEDIALOG_OPEN;
            break;
        case RT_FILEDIALOG_SAVE:
            mode = VG_FILEDIALOG_SAVE;
            break;
        case RT_FILEDIALOG_FOLDER:
            mode = VG_FILEDIALOG_SELECT_FOLDER;
            break;
        default:
            mode = VG_FILEDIALOG_OPEN;
            break;
    }

    vg_filedialog_t *dlg = vg_filedialog_create(mode);
    if (!dlg)
        return NULL;

    rt_filedialog_data_t *data = (rt_filedialog_data_t *)malloc(sizeof(rt_filedialog_data_t));
    if (!data)
    {
        vg_filedialog_destroy(dlg);
        return NULL;
    }
    data->dialog = dlg;
    data->selected_paths = NULL;
    data->selected_count = 0;
    data->result = 0;

    return data;
}

void rt_filedialog_set_title(void *dialog, rt_string title)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *ctitle = rt_string_to_cstr(title);
    vg_filedialog_set_title(data->dialog, ctitle);
    if (ctitle)
        free(ctitle);
}

void rt_filedialog_set_path(void *dialog, rt_string path)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cpath = rt_string_to_cstr(path);
    vg_filedialog_set_initial_path(data->dialog, cpath);
    if (cpath)
        free(cpath);
}

void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_clear_filters(data->dialog);
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_set_default_name(void *dialog, rt_string name)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    vg_filedialog_set_filename(data->dialog, cname);
    if (cname)
        free(cname);
}

void rt_filedialog_set_multiple(void *dialog, int64_t multiple)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_set_multi_select(data->dialog, multiple != 0);
}

int64_t rt_filedialog_show(void *dialog)
{
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_show(data->dialog);

    // Get selected paths
    if (data->selected_paths)
    {
        for (size_t i = 0; i < data->selected_count; i++)
        {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
    }
    data->selected_paths = vg_filedialog_get_selected_paths(data->dialog, &data->selected_count);
    data->result = (data->selected_count > 0) ? 1 : 0;

    return data->result;
}

rt_string rt_filedialog_get_path(void *dialog)
{
    if (!dialog)
        return rt_string_from_bytes("", 0);
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && data->selected_count > 0)
    {
        return rt_string_from_bytes(data->selected_paths[0], strlen(data->selected_paths[0]));
    }
    return rt_string_from_bytes("", 0);
}

int64_t rt_filedialog_get_path_count(void *dialog)
{
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    return (int64_t)data->selected_count;
}

rt_string rt_filedialog_get_path_at(void *dialog, int64_t index)
{
    if (!dialog)
        return rt_string_from_bytes("", 0);
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && index >= 0 && (size_t)index < data->selected_count)
    {
        return rt_string_from_bytes(data->selected_paths[index],
                                    strlen(data->selected_paths[index]));
    }
    return rt_string_from_bytes("", 0);
}

void rt_filedialog_destroy(void *dialog)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths)
    {
        for (size_t i = 0; i < data->selected_count; i++)
        {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
    }
    if (data->dialog)
    {
        vg_filedialog_destroy(data->dialog);
    }
    free(data);
}

//=============================================================================
// Phase 6: FindBar (Search & Replace)
//=============================================================================

// FindBar state tracking
typedef struct
{
    vg_findreplacebar_t *bar;
    void *bound_editor;
    char *find_text;
    char *replace_text;
    int64_t case_sensitive;
    int64_t whole_word;
    int64_t regex;
    int64_t replace_mode;
} rt_findbar_data_t;

void *rt_findbar_new(void *parent)
{
    vg_findreplacebar_t *bar = vg_findreplacebar_create();
    if (!bar)
        return NULL;

    rt_findbar_data_t *data = (rt_findbar_data_t *)malloc(sizeof(rt_findbar_data_t));
    if (!data)
    {
        vg_findreplacebar_destroy(bar);
        return NULL;
    }
    data->bar = bar;
    data->bound_editor = NULL;
    data->find_text = NULL;
    data->replace_text = NULL;
    data->case_sensitive = 0;
    data->whole_word = 0;
    data->regex = 0;
    data->replace_mode = 0;

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_findbar_destroy(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->bar)
    {
        vg_findreplacebar_destroy(data->bar);
    }
    if (data->find_text)
        free(data->find_text);
    if (data->replace_text)
        free(data->replace_text);
    free(data);
}

void rt_findbar_bind_editor(void *bar, void *editor)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = editor;
    vg_findreplacebar_set_target(data->bar, (vg_codeeditor_t *)editor);
}

void rt_findbar_unbind_editor(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = NULL;
    vg_findreplacebar_set_target(data->bar, NULL);
}

void rt_findbar_set_replace_mode(void *bar, int64_t replace)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->replace_mode = replace;
    vg_findreplacebar_set_show_replace(data->bar, replace != 0);
}

int64_t rt_findbar_is_replace_mode(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->replace_mode;
}

void rt_findbar_set_find_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text)
        free(data->find_text);
    data->find_text = rt_string_to_cstr(text);
    vg_findreplacebar_set_find_text(data->bar, data->find_text);
}

rt_string rt_findbar_get_find_text(void *bar)
{
    if (!bar)
        return rt_string_from_bytes("", 0);
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text)
    {
        return rt_string_from_bytes(data->find_text, strlen(data->find_text));
    }
    return rt_string_from_bytes("", 0);
}

void rt_findbar_set_replace_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text)
        free(data->replace_text);
    data->replace_text = rt_string_to_cstr(text);
    // vg_findreplacebar doesn't have a set_replace_text - would need to track locally
}

rt_string rt_findbar_get_replace_text(void *bar)
{
    if (!bar)
        return rt_string_from_bytes("", 0);
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text)
    {
        return rt_string_from_bytes(data->replace_text, strlen(data->replace_text));
    }
    return rt_string_from_bytes("", 0);
}

// Helper to update find options
static void rt_findbar_update_options(rt_findbar_data_t *data)
{
    vg_search_options_t opts = {.case_sensitive = data->case_sensitive != 0,
                                .whole_word = data->whole_word != 0,
                                .use_regex = data->regex != 0,
                                .in_selection = false,
                                .wrap_around = true};
    vg_findreplacebar_set_options(data->bar, &opts);
}

void rt_findbar_set_case_sensitive(void *bar, int64_t sensitive)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->case_sensitive = sensitive;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_case_sensitive(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->case_sensitive;
}

void rt_findbar_set_whole_word(void *bar, int64_t whole)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->whole_word = whole;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_whole_word(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->whole_word;
}

void rt_findbar_set_regex(void *bar, int64_t regex)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->regex = regex;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_regex(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->regex;
}

int64_t rt_findbar_find_next(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_next(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

int64_t rt_findbar_find_previous(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_prev(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

int64_t rt_findbar_replace(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_replace_current(data->bar);
    return 1; // Assume success
}

int64_t rt_findbar_replace_all(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    size_t count_before = vg_findreplacebar_get_match_count(data->bar);
    vg_findreplacebar_replace_all(data->bar);
    return (int64_t)count_before;
}

int64_t rt_findbar_get_match_count(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_match_count(data->bar);
}

int64_t rt_findbar_get_current_match(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_current_match(data->bar);
}

void rt_findbar_set_visible(void *bar, int64_t visible)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    // The widget would need visibility control - stub for now
    (void)data;
    (void)visible;
}

int64_t rt_findbar_is_visible(void *bar)
{
    if (!bar)
        return 0;
    // Stub - would need widget visibility query
    (void)bar;
    return 0;
}

void rt_findbar_focus(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_focus(data->bar);
}

//=============================================================================
// Phase 6: CommandPalette
//=============================================================================

// CommandPalette state tracking
typedef struct
{
    vg_commandpalette_t *palette;
    char *selected_command;
    int64_t was_selected;
} rt_commandpalette_data_t;

static void rt_commandpalette_on_execute(vg_commandpalette_t *palette,
                                         vg_command_t *cmd,
                                         void *user_data)
{
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)user_data;
    if (data && cmd && cmd->id)
    {
        if (data->selected_command)
            free(data->selected_command);
        data->selected_command = strdup(cmd->id);
        data->was_selected = 1;
    }
    (void)palette;
}

void *rt_commandpalette_new(void *parent)
{
    vg_commandpalette_t *palette = vg_commandpalette_create();
    if (!palette)
        return NULL;

    rt_commandpalette_data_t *data =
        (rt_commandpalette_data_t *)malloc(sizeof(rt_commandpalette_data_t));
    if (!data)
    {
        vg_commandpalette_destroy(palette);
        return NULL;
    }
    data->palette = palette;
    data->selected_command = NULL;
    data->was_selected = 0;

    vg_commandpalette_set_callbacks(palette, rt_commandpalette_on_execute, NULL, data);

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_commandpalette_destroy(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    if (data->palette)
    {
        vg_commandpalette_destroy(data->palette);
    }
    if (data->selected_command)
        free(data->selected_command);
    free(data);
}

void rt_commandpalette_add_command(void *palette, rt_string id, rt_string label, rt_string category)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    char *clabel = rt_string_to_cstr(label);
    // Note: category is not used by underlying widget - could prepend to label if needed
    (void)category;

    vg_commandpalette_add_command(data->palette, cid, clabel, NULL, NULL, NULL);

    if (cid)
        free(cid);
    if (clabel)
        free(clabel);
}

void rt_commandpalette_add_command_with_shortcut(
    void *palette, rt_string id, rt_string label, rt_string category, rt_string shortcut)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    char *clabel = rt_string_to_cstr(label);
    char *cshort = rt_string_to_cstr(shortcut);
    // Note: category is not used by underlying widget
    (void)category;

    vg_commandpalette_add_command(data->palette, cid, clabel, cshort, NULL, NULL);

    if (cid)
        free(cid);
    if (clabel)
        free(clabel);
    if (cshort)
        free(cshort);
}

void rt_commandpalette_remove_command(void *palette, rt_string id)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    vg_commandpalette_remove_command(data->palette, cid);
    if (cid)
        free(cid);
}

void rt_commandpalette_clear(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    // Would need to iterate and remove all commands - stub for now
    (void)data;
}

void rt_commandpalette_show(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    data->was_selected = 0; // Reset selection state when showing
    vg_commandpalette_show(data->palette);
}

void rt_commandpalette_hide(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    vg_commandpalette_hide(data->palette);
}

int64_t rt_commandpalette_is_visible(void *palette)
{
    if (!palette)
        return 0;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    return data->palette->base.visible ? 1 : 0;
}

void rt_commandpalette_set_placeholder(void *palette, rt_string text)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *ctext = rt_string_to_cstr(text);
    // Would need placeholder support in vg_commandpalette - stub
    (void)data;
    if (ctext)
        free(ctext);
}

rt_string rt_commandpalette_get_selected_command(void *palette)
{
    if (!palette)
        return rt_string_from_bytes("", 0);
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    if (data->selected_command)
    {
        return rt_string_from_bytes(data->selected_command, strlen(data->selected_command));
    }
    return rt_string_from_bytes("", 0);
}

int64_t rt_commandpalette_was_command_selected(void *palette)
{
    if (!palette)
        return 0;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    int64_t result = data->was_selected;
    data->was_selected = 0; // Reset after checking
    return result;
}

//=============================================================================
// Phase 7: Tooltip Implementation
//=============================================================================

// Global tooltip state
static vg_tooltip_t *g_active_tooltip = NULL;
static uint32_t g_tooltip_delay_ms = 500;

void rt_tooltip_show(rt_string text, int64_t x, int64_t y)
{
    char *ctext = rt_string_to_cstr(text);

    // Create tooltip if needed
    if (!g_active_tooltip)
    {
        g_active_tooltip = vg_tooltip_create();
    }

    if (g_active_tooltip && ctext)
    {
        vg_tooltip_set_text(g_active_tooltip, ctext);
        vg_tooltip_show_at(g_active_tooltip, (int)x, (int)y);
    }

    if (ctext)
        free(ctext);
}

void rt_tooltip_show_rich(rt_string title, rt_string body, int64_t x, int64_t y)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cbody = rt_string_to_cstr(body);

    // Create tooltip if needed
    if (!g_active_tooltip)
    {
        g_active_tooltip = vg_tooltip_create();
    }

    if (g_active_tooltip)
    {
        // Combine title and body for now (rich tooltip would need more widget support)
        char combined[1024];
        snprintf(combined, sizeof(combined), "%s\n%s", ctitle ? ctitle : "", cbody ? cbody : "");
        vg_tooltip_set_text(g_active_tooltip, combined);
        vg_tooltip_show_at(g_active_tooltip, (int)x, (int)y);
    }

    if (ctitle)
        free(ctitle);
    if (cbody)
        free(cbody);
}

void rt_tooltip_hide(void)
{
    if (g_active_tooltip)
    {
        vg_tooltip_hide(g_active_tooltip);
    }
}

void rt_tooltip_set_delay(int64_t delay_ms)
{
    g_tooltip_delay_ms = (uint32_t)delay_ms;
    if (g_active_tooltip)
    {
        vg_tooltip_set_timing(g_active_tooltip, g_tooltip_delay_ms, 100, 0);
    }
}

void rt_widget_set_tooltip(void *widget, rt_string text)
{
    if (!widget)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_widget_set_tooltip_text((vg_widget_t *)widget, ctext);
    if (ctext)
        free(ctext);
}

void rt_widget_set_tooltip_rich(void *widget, rt_string title, rt_string body)
{
    if (!widget)
        return;
    // Combine title and body for basic tooltip support
    char *ctitle = rt_string_to_cstr(title);
    char *cbody = rt_string_to_cstr(body);

    char combined[1024];
    snprintf(combined, sizeof(combined), "%s\n%s", ctitle ? ctitle : "", cbody ? cbody : "");
    vg_widget_set_tooltip_text((vg_widget_t *)widget, combined);

    if (ctitle)
        free(ctitle);
    if (cbody)
        free(cbody);
}

void rt_widget_clear_tooltip(void *widget)
{
    if (!widget)
        return;
    vg_widget_set_tooltip_text((vg_widget_t *)widget, NULL);
}

//=============================================================================
// Phase 7: Toast/Notifications Implementation
//=============================================================================

// Global notification manager
static vg_notification_manager_t *g_notification_manager = NULL;

// Wrapper to track toast state
typedef struct rt_toast_data
{
    uint32_t id;
    int64_t was_action_clicked;
    int64_t was_dismissed;
} rt_toast_data_t;

static vg_notification_manager_t *rt_get_notification_manager(void)
{
    if (!g_notification_manager)
    {
        g_notification_manager = vg_notification_manager_create();
    }
    return g_notification_manager;
}

static vg_notification_type_t rt_toast_type_to_vg(int64_t type)
{
    switch (type)
    {
        case RT_TOAST_INFO:
            return VG_NOTIFICATION_INFO;
        case RT_TOAST_SUCCESS:
            return VG_NOTIFICATION_SUCCESS;
        case RT_TOAST_WARNING:
            return VG_NOTIFICATION_WARNING;
        case RT_TOAST_ERROR:
            return VG_NOTIFICATION_ERROR;
        default:
            return VG_NOTIFICATION_INFO;
    }
}

static vg_notification_position_t rt_toast_position_to_vg(int64_t position)
{
    switch (position)
    {
        case RT_TOAST_POSITION_TOP_RIGHT:
            return VG_NOTIFICATION_TOP_RIGHT;
        case RT_TOAST_POSITION_TOP_LEFT:
            return VG_NOTIFICATION_TOP_LEFT;
        case RT_TOAST_POSITION_BOTTOM_RIGHT:
            return VG_NOTIFICATION_BOTTOM_RIGHT;
        case RT_TOAST_POSITION_BOTTOM_LEFT:
            return VG_NOTIFICATION_BOTTOM_LEFT;
        case RT_TOAST_POSITION_TOP_CENTER:
            return VG_NOTIFICATION_TOP_CENTER;
        case RT_TOAST_POSITION_BOTTOM_CENTER:
            return VG_NOTIFICATION_BOTTOM_CENTER;
        default:
            return VG_NOTIFICATION_TOP_RIGHT;
    }
}

void rt_toast_info(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_INFO, "Info", cmsg, 3000);
    if (cmsg)
        free(cmsg);
}

void rt_toast_success(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_SUCCESS, "Success", cmsg, 3000);
    if (cmsg)
        free(cmsg);
}

void rt_toast_warning(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_WARNING, "Warning", cmsg, 5000);
    if (cmsg)
        free(cmsg);
}

void rt_toast_error(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_ERROR, "Error", cmsg, 0); // Sticky for errors
    if (cmsg)
        free(cmsg);
}

void *rt_toast_new(rt_string message, int64_t type, int64_t duration_ms)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return NULL;

    char *cmsg = rt_string_to_cstr(message);

    rt_toast_data_t *data = (rt_toast_data_t *)malloc(sizeof(rt_toast_data_t));
    if (!data)
    {
        if (cmsg)
            free(cmsg);
        return NULL;
    }

    data->id =
        vg_notification_show(mgr, rt_toast_type_to_vg(type), NULL, cmsg, (uint32_t)duration_ms);
    data->was_action_clicked = 0;
    data->was_dismissed = 0;

    if (cmsg)
        free(cmsg);
    return data;
}

void rt_toast_set_action(void *toast, rt_string label)
{
    if (!toast)
        return;
    // Note: Would need to modify notification after creation - not directly supported
    // This would require extending vg_notification_manager to support adding actions
    // after notification creation
    (void)label;
}

int64_t rt_toast_was_action_clicked(void *toast)
{
    if (!toast)
        return 0;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    int64_t result = data->was_action_clicked;
    data->was_action_clicked = 0;
    return result;
}

int64_t rt_toast_was_dismissed(void *toast)
{
    if (!toast)
        return 0;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    // Check with manager if notification is still active
    // For now, return stored state
    return data->was_dismissed;
}

void rt_toast_dismiss(void *toast)
{
    if (!toast)
        return;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        vg_notification_dismiss(mgr, data->id);
        data->was_dismissed = 1;
    }
}

void rt_toast_set_position(int64_t position)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        vg_notification_manager_set_position(mgr, rt_toast_position_to_vg(position));
    }
}

void rt_toast_set_max_visible(int64_t count)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        mgr->max_visible = (uint32_t)count;
    }
}

void rt_toast_dismiss_all(void)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        vg_notification_dismiss_all(mgr);
    }
}

//=============================================================================
// Phase 8: Breadcrumb Implementation
//=============================================================================

// Wrapper to track breadcrumb state
typedef struct rt_breadcrumb_data
{
    vg_breadcrumb_t *breadcrumb;
    int64_t clicked_index;
    char *clicked_data;
    int64_t was_clicked;
} rt_breadcrumb_data_t;

// Breadcrumb click callback
static void rt_breadcrumb_on_click(vg_breadcrumb_t *bc, int index, void *user_data)
{
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)user_data;
    if (!data)
        return;

    data->clicked_index = index;
    data->was_clicked = 1;

    // Store the clicked item's data
    if (data->clicked_data)
    {
        free(data->clicked_data);
        data->clicked_data = NULL;
    }

    if (index >= 0 && (size_t)index < bc->item_count)
    {
        if (bc->items[index].user_data)
        {
            data->clicked_data = strdup((const char *)bc->items[index].user_data);
        }
    }
}

void *rt_breadcrumb_new(void *parent)
{
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    if (!bc)
        return NULL;

    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)malloc(sizeof(rt_breadcrumb_data_t));
    if (!data)
    {
        vg_breadcrumb_destroy(bc);
        return NULL;
    }

    data->breadcrumb = bc;
    data->clicked_index = -1;
    data->clicked_data = NULL;
    data->was_clicked = 0;

    vg_breadcrumb_set_on_click(bc, rt_breadcrumb_on_click, data);

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_breadcrumb_destroy(void *crumb)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    if (data->breadcrumb)
    {
        vg_breadcrumb_destroy(data->breadcrumb);
    }
    if (data->clicked_data)
        free(data->clicked_data);
    free(data);
}

void rt_breadcrumb_set_path(void *crumb, rt_string path, rt_string separator)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *cpath = rt_string_to_cstr(path);
    char *csep = rt_string_to_cstr(separator);

    // Clear existing items
    vg_breadcrumb_clear(data->breadcrumb);

    // Parse path and add items
    if (cpath && csep && csep[0])
    {
        char *token = strtok(cpath, csep);
        while (token)
        {
            vg_breadcrumb_push(data->breadcrumb, token, strdup(token));
            token = strtok(NULL, csep);
        }
    }

    if (cpath)
        free(cpath);
    if (csep)
        free(csep);
}

void rt_breadcrumb_set_items(void *crumb, rt_string items)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *citems = rt_string_to_cstr(items);

    // Clear existing items
    vg_breadcrumb_clear(data->breadcrumb);

    // Parse comma-separated items
    if (citems)
    {
        char *token = strtok(citems, ",");
        while (token)
        {
            // Trim whitespace
            while (*token == ' ')
                token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ')
                *end-- = '\0';

            vg_breadcrumb_push(data->breadcrumb, token, strdup(token));
            token = strtok(NULL, ",");
        }
        free(citems);
    }
}

void rt_breadcrumb_add_item(void *crumb, rt_string text, rt_string item_data)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *ctext = rt_string_to_cstr(text);
    char *cdata = rt_string_to_cstr(item_data);

    if (ctext)
    {
        vg_breadcrumb_push(data->breadcrumb, ctext, cdata ? strdup(cdata) : NULL);
        free(ctext);
    }
    if (cdata)
        free(cdata);
}

void rt_breadcrumb_clear(void *crumb)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    vg_breadcrumb_clear(data->breadcrumb);
}

int64_t rt_breadcrumb_was_item_clicked(void *crumb)
{
    if (!crumb)
        return 0;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    int64_t result = data->was_clicked;
    data->was_clicked = 0; // Reset after checking
    return result;
}

int64_t rt_breadcrumb_get_clicked_index(void *crumb)
{
    if (!crumb)
        return -1;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    return data->clicked_index;
}

rt_string rt_breadcrumb_get_clicked_data(void *crumb)
{
    if (!crumb)
        return rt_string_from_bytes("", 0);
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    if (data->clicked_data)
    {
        return rt_string_from_bytes(data->clicked_data, strlen(data->clicked_data));
    }
    return rt_string_from_bytes("", 0);
}

void rt_breadcrumb_set_separator(void *crumb, rt_string sep)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    char *csep = rt_string_to_cstr(sep);
    if (csep)
    {
        vg_breadcrumb_set_separator(data->breadcrumb, csep);
        free(csep);
    }
}

void rt_breadcrumb_set_max_items(void *crumb, int64_t max)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    // Note: vg_breadcrumb doesn't have max_items - would truncate or show "..."
    (void)data;
    (void)max;
}

//=============================================================================
// Phase 8: Minimap Implementation
//=============================================================================

// Wrapper to track minimap state
typedef struct rt_minimap_data
{
    vg_minimap_t *minimap;
    int64_t width;
} rt_minimap_data_t;

void *rt_minimap_new(void *parent)
{
    vg_minimap_t *minimap = vg_minimap_create(NULL);
    if (!minimap)
        return NULL;

    rt_minimap_data_t *data = (rt_minimap_data_t *)malloc(sizeof(rt_minimap_data_t));
    if (!data)
    {
        vg_minimap_destroy(minimap);
        return NULL;
    }

    data->minimap = minimap;
    data->width = 80; // Default width

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_minimap_destroy(void *minimap)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    if (data->minimap)
    {
        vg_minimap_destroy(data->minimap);
    }
    free(data);
}

void rt_minimap_bind_editor(void *minimap, void *editor)
{
    if (!minimap || !editor)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_editor(data->minimap, (vg_codeeditor_t *)editor);
}

void rt_minimap_unbind_editor(void *minimap)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_editor(data->minimap, NULL);
}

void rt_minimap_set_width(void *minimap, int64_t width)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    data->width = width;
    data->minimap->base.width = (float)width;
}

int64_t rt_minimap_get_width(void *minimap)
{
    if (!minimap)
        return 0;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    return data->width;
}

void rt_minimap_set_scale(void *minimap, double scale)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_scale(data->minimap, (float)scale);
}

void rt_minimap_set_show_slider(void *minimap, int64_t show)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_show_viewport(data->minimap, show != 0);
}

void rt_minimap_add_marker(void *minimap, int64_t line, int64_t color, int64_t type)
{
    if (!minimap)
        return;
    // Note: vg_minimap doesn't have marker support - would need to extend
    // For now, store markers separately if needed
    (void)line;
    (void)color;
    (void)type;
}

void rt_minimap_remove_markers(void *minimap, int64_t line)
{
    if (!minimap)
        return;
    // Note: vg_minimap doesn't have marker support
    (void)line;
}

void rt_minimap_clear_markers(void *minimap)
{
    if (!minimap)
        return;
    // Note: vg_minimap doesn't have marker support
}

//=============================================================================
// Phase 8: Drag and Drop Implementation
//=============================================================================

// Drag and drop state per widget (would need to be stored in widget user_data)
typedef struct rt_drag_drop_data
{
    int64_t is_draggable;
    char *drag_type;
    char *drag_data;
    int64_t is_drop_target;
    char *accepted_types;
    int64_t is_being_dragged;
    int64_t is_drag_over;
    int64_t was_dropped;
    char *drop_type;
    char *drop_data;
} rt_drag_drop_data_t;

// Global drag state for simple implementation
static rt_drag_drop_data_t *g_current_drag = NULL;

void rt_widget_set_draggable(void *widget, int64_t draggable)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    // For now, this is a stub that tracks state
    (void)draggable;
}

void rt_widget_set_drag_data(void *widget, rt_string type, rt_string data)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    (void)type;
    (void)data;
}

int64_t rt_widget_is_being_dragged(void *widget)
{
    if (!widget)
        return 0;
    // Note: Would need to extend vg_widget to support drag/drop
    return 0;
}

void rt_widget_set_drop_target(void *widget, int64_t target)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    (void)target;
}

void rt_widget_set_accepted_drop_types(void *widget, rt_string types)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    (void)types;
}

int64_t rt_widget_is_drag_over(void *widget)
{
    if (!widget)
        return 0;
    // Note: Would need to extend vg_widget to support drag/drop
    return 0;
}

int64_t rt_widget_was_dropped(void *widget)
{
    if (!widget)
        return 0;
    // Note: Would need to extend vg_widget to support drag/drop
    return 0;
}

rt_string rt_widget_get_drop_type(void *widget)
{
    if (!widget)
        return rt_string_from_bytes("", 0);
    // Note: Would need to extend vg_widget to support drag/drop
    return rt_string_from_bytes("", 0);
}

rt_string rt_widget_get_drop_data(void *widget)
{
    if (!widget)
        return rt_string_from_bytes("", 0);
    // Note: Would need to extend vg_widget to support drag/drop
    return rt_string_from_bytes("", 0);
}

// File drop state for app
typedef struct rt_file_drop_data
{
    char **files;
    int64_t file_count;
    int64_t was_dropped;
} rt_file_drop_data_t;

static rt_file_drop_data_t g_file_drop = {0};

int64_t rt_app_was_file_dropped(void *app)
{
    (void)app;
    int64_t result = g_file_drop.was_dropped;
    g_file_drop.was_dropped = 0;
    return result;
}

int64_t rt_app_get_dropped_file_count(void *app)
{
    (void)app;
    return g_file_drop.file_count;
}

rt_string rt_app_get_dropped_file(void *app, int64_t index)
{
    (void)app;
    if (index >= 0 && index < g_file_drop.file_count && g_file_drop.files)
    {
        char *file = g_file_drop.files[index];
        if (file)
        {
            return rt_string_from_bytes(file, strlen(file));
        }
    }
    return rt_string_from_bytes("", 0);
}
