//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_commandpalette.c
//
//===----------------------------------------------------------------------===//
// vg_commandpalette.c - Command Palette widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void commandpalette_destroy(vg_widget_t *widget);
static void commandpalette_measure(vg_widget_t *widget,
                                   float available_width,
                                   float available_height);
static void commandpalette_paint(vg_widget_t *widget, void *canvas);
static bool commandpalette_handle_event(vg_widget_t *widget, vg_event_t *event);
static void commandpalette_ensure_selection_visible(vg_commandpalette_t *palette);

//=============================================================================
// CommandPalette VTable
//=============================================================================

static vg_widget_vtable_t g_commandpalette_vtable = {.destroy = commandpalette_destroy,
                                                     .measure = commandpalette_measure,
                                                     .arrange = NULL,
                                                     .paint = commandpalette_paint,
                                                     .handle_event = commandpalette_handle_event,
                                                     .can_focus = NULL,
                                                     .on_focus = NULL};

//=============================================================================
// Fuzzy Matching
//=============================================================================

/// Simple fuzzy match score - returns 0 for no match, higher for better match
static int fuzzy_match_score(const char *pattern, const char *text) {
    if (!pattern || !text)
        return 0;
    if (!pattern[0])
        return 1; // Empty pattern matches everything

    int score = 0;
    const char *p = pattern;
    const char *t = text;
    bool consecutive = true;
    int last_match_pos = -1;

    while (*p && *t) {
        char pc = (char)tolower((unsigned char)*p);
        char tc = (char)tolower((unsigned char)*t);

        if (pc == tc) {
            // Found a match
            score += consecutive ? 10 : 5;

            // Bonus for matching at word boundaries
            if (t == text || *(t - 1) == ' ' || *(t - 1) == '_' || *(t - 1) == '-') {
                score += 15;
            }

            // Bonus for consecutive matches
            if ((int)(t - text) == last_match_pos + 1) {
                score += 5;
            }

            last_match_pos = (int)(t - text);
            p++;
            consecutive = true;
        } else {
            consecutive = false;
        }
        t++;
    }

    // Pattern must be fully matched
    if (*p)
        return 0;

    // Bonus for shorter text (more specific match)
    int len = (int)strlen(text);
    if (len > 0) {
        score += 100 / len;
    }

    return score;
}

static void filter_commands(vg_commandpalette_t *palette) {
    if (!palette)
        return;

    palette->filtered_count = 0;

    // Get query from search input
    const char *query = palette->current_query;

    for (size_t i = 0; i < palette->command_count; i++) {
        vg_command_t *cmd = palette->commands[i];
        if (!cmd || !cmd->enabled)
            continue;

        // Check if matches
        int score = fuzzy_match_score(query, cmd->label);
        if (score > 0 || !query || !query[0]) {
            // Add to filtered list
            if (palette->filtered_count >= palette->filtered_capacity) {
                size_t new_cap = palette->filtered_capacity * 2;
                if (new_cap < 32)
                    new_cap = 32;
                vg_command_t **new_filtered =
                    realloc(palette->filtered, new_cap * sizeof(vg_command_t *));
                if (!new_filtered)
                    continue;
                palette->filtered = new_filtered;
                palette->filtered_capacity = new_cap;
            }
            palette->filtered[palette->filtered_count++] = cmd;
        }
    }

    // Reset selection
    palette->selected_index = palette->filtered_count > 0 ? 0 : -1;
    palette->first_visible_index = 0;
    palette->base.needs_paint = true;
}

static void commandpalette_ensure_selection_visible(vg_commandpalette_t *palette) {
    int max_visible = 0;

    if (!palette || palette->max_visible == 0)
        return;

    max_visible = (int)palette->max_visible;
    if (palette->selected_index < 0) {
        palette->first_visible_index = 0;
        return;
    }

    if (palette->first_visible_index < 0)
        palette->first_visible_index = 0;
    if (palette->selected_index < palette->first_visible_index)
        palette->first_visible_index = palette->selected_index;
    if (palette->selected_index >= palette->first_visible_index + max_visible) {
        palette->first_visible_index = palette->selected_index - max_visible + 1;
    }

    if (palette->filtered_count <= (size_t)max_visible) {
        palette->first_visible_index = 0;
    } else {
        int max_first = (int)palette->filtered_count - max_visible;
        if (palette->first_visible_index > max_first)
            palette->first_visible_index = max_first;
    }
}

static size_t utf8_encode_codepoint(uint32_t codepoint, char out[4]) {
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

static void append_query_char(vg_commandpalette_t *palette, uint32_t codepoint) {
    char encoded[4];
    size_t encoded_len = 0;
    if (!palette || codepoint < 0x20 || codepoint == 0x7F)
        return;
    encoded_len = utf8_encode_codepoint(codepoint, encoded);
    if (encoded_len == 0)
        return;
    size_t old_len = palette->current_query ? strlen(palette->current_query) : 0;
    char *next = realloc(palette->current_query, old_len + encoded_len + 1);
    if (!next)
        return;
    memcpy(next + old_len, encoded, encoded_len);
    next[old_len + encoded_len] = '\0';
    palette->current_query = next;
    filter_commands(palette);
}

static void remove_query_char(vg_commandpalette_t *palette) {
    if (!palette || !palette->current_query)
        return;
    size_t len = strlen(palette->current_query);
    if (len == 0)
        return;
    size_t new_len = len;
    do {
        new_len--;
    } while (new_len > 0 && (((unsigned char)palette->current_query[new_len] & 0xC0) == 0x80));
    palette->current_query[new_len] = '\0';
    if (new_len == 0) {
        free(palette->current_query);
        palette->current_query = NULL;
    }
    filter_commands(palette);
}

//=============================================================================
// CommandPalette Implementation
//=============================================================================

vg_commandpalette_t *vg_commandpalette_create(void) {
    vg_commandpalette_t *palette = calloc(1, sizeof(vg_commandpalette_t));
    if (!palette)
        return NULL;

    vg_widget_init(&palette->base, VG_WIDGET_CUSTOM, &g_commandpalette_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    palette->item_height = 32;
    palette->max_visible = 10;
    palette->width = 500;
    palette->bg_color = 0xFF252526;
    palette->selected_bg = 0xFF094771;
    palette->text_color = 0xFFCCCCCC;
    palette->shortcut_color = 0xFF808080;

    palette->font_size = theme->typography.size_normal;
    palette->is_visible = false;
    palette->selected_index = -1;
    palette->hovered_index = -1;
    palette->first_visible_index = 0;
    palette->placeholder_text = strdup("Type to search...");

    return palette;
}

static void free_command(vg_command_t *cmd) {
    if (!cmd)
        return;
    free(cmd->id);
    free(cmd->label);
    free(cmd->description);
    free(cmd->shortcut);
    free(cmd->category);
    free(cmd);
}

static void commandpalette_destroy(vg_widget_t *widget) {
    vg_commandpalette_t *palette = (vg_commandpalette_t *)widget;

    for (size_t i = 0; i < palette->command_count; i++) {
        free_command(palette->commands[i]);
    }
    free(palette->commands);
    free(palette->filtered);
    free(palette->placeholder_text);
    free(palette->current_query);
}

/// @brief Commandpalette destroy.
void vg_commandpalette_destroy(vg_commandpalette_t *palette) {
    if (!palette)
        return;
    vg_widget_destroy(&palette->base);
}

static void commandpalette_measure(vg_widget_t *widget,
                                   float available_width,
                                   float available_height) {
    vg_commandpalette_t *palette = (vg_commandpalette_t *)widget;
    (void)available_width;
    (void)available_height;

    // Search input height + visible items
    float search_height = 36;
    size_t visible = palette->filtered_count;
    if (visible > palette->max_visible)
        visible = palette->max_visible;

    widget->measured_width = palette->width;
    widget->measured_height = search_height + visible * palette->item_height;
}

static void commandpalette_paint(vg_widget_t *widget, void *canvas) {
    vg_commandpalette_t *palette = (vg_commandpalette_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;

    if (!palette->is_visible)
        return;

    float search_height = 36.0f;
    float x = widget->x;
    float y = widget->y;
    float w = widget->width > 0.0f ? widget->width : palette->width;
    float h = widget->height;

    vgfx_fill_rect(win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, palette->bg_color);
    vgfx_rect(win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, 0xFF3C3C3C);
    vgfx_fill_rect(
        win, (int32_t)x, (int32_t)(y + search_height - 1.0f), (int32_t)w, 1, 0xFF3C3C3C);

    if (palette->font) {
        const char *query =
            (palette->current_query && palette->current_query[0])
                ? palette->current_query
                : (palette->placeholder_text ? palette->placeholder_text : "Type to search...");
        uint32_t query_color = (palette->current_query && palette->current_query[0])
                                   ? palette->text_color
                                   : palette->shortcut_color;
        vg_font_draw_text(canvas,
                          palette->font,
                          palette->font_size,
                          x + 12.0f,
                          y + 24.0f,
                          query,
                          query_color);
    }

    // Draw filtered results
    float item_y = widget->y + search_height;
    size_t visible = palette->filtered_count;
    if (visible > palette->max_visible)
        visible = palette->max_visible;

    commandpalette_ensure_selection_visible(palette);

    for (size_t i = 0; i < visible; i++) {
        size_t filtered_index = (size_t)palette->first_visible_index + i;
        vg_command_t *cmd =
            filtered_index < palette->filtered_count ? palette->filtered[filtered_index] : NULL;
        if (!cmd)
            continue;

        // Draw item background
        if ((int)filtered_index == palette->selected_index) {
            vgfx_fill_rect(win,
                           (int32_t)(x + 1.0f),
                           (int32_t)item_y,
                           (int32_t)(w - 2.0f),
                           (int32_t)palette->item_height,
                           palette->selected_bg);
        }

        // Draw label and shortcut
        if (palette->font && cmd->label) {
            vg_font_draw_text(canvas,
                              palette->font,
                              palette->font_size,
                              x + 12.0f,
                              item_y + 22.0f,
                              cmd->label,
                              palette->text_color);

            if (cmd->shortcut) {
                vg_text_metrics_t shortcut_metrics;
                vg_font_measure_text(
                    palette->font, palette->font_size, cmd->shortcut, &shortcut_metrics);
                vg_font_draw_text(canvas,
                                  palette->font,
                                  palette->font_size,
                                  x + w - 16.0f - shortcut_metrics.width,
                                  item_y + 22.0f,
                                  cmd->shortcut,
                                  palette->shortcut_color);
            }
        }

        item_y += palette->item_height;
    }
}

static bool commandpalette_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_commandpalette_t *palette = (vg_commandpalette_t *)widget;

    if (!palette->is_visible)
        return false;

    if (event->type == VG_EVENT_KEY_DOWN) {
        switch (event->key.key) {
            case VG_KEY_ESCAPE:
                vg_commandpalette_hide(palette);
                return true;

            case VG_KEY_ENTER:
                vg_commandpalette_execute_selected(palette);
                return true;

            case VG_KEY_UP:
                if (palette->selected_index > 0) {
                    palette->selected_index--;
                    commandpalette_ensure_selection_visible(palette);
                    palette->base.needs_paint = true;
                }
                return true;

            case VG_KEY_DOWN:
                if (palette->selected_index < (int)palette->filtered_count - 1) {
                    palette->selected_index++;
                    commandpalette_ensure_selection_visible(palette);
                    palette->base.needs_paint = true;
                }
                return true;

            case VG_KEY_BACKSPACE:
                remove_query_char(palette);
                return true;

            default:
                break;
        }
    }

    if (event->type == VG_EVENT_KEY_CHAR) {
        append_query_char(palette, event->key.codepoint);
        return true;
    }

    if (event->type == VG_EVENT_MOUSE_WHEEL) {
        int direction = 0;
        if (event->wheel.delta_y > 0.0f)
            direction = -1;
        else if (event->wheel.delta_y < 0.0f)
            direction = 1;

        if (direction != 0 && palette->filtered_count > palette->max_visible) {
            palette->first_visible_index += direction;
            commandpalette_ensure_selection_visible(palette);
            if (palette->selected_index < palette->first_visible_index)
                palette->selected_index = palette->first_visible_index;
            if (palette->selected_index >= palette->first_visible_index + (int)palette->max_visible) {
                palette->selected_index =
                    palette->first_visible_index + (int)palette->max_visible - 1;
            }
            palette->base.needs_paint = true;
            return true;
        }
    }

    if (event->type == VG_EVENT_MOUSE_MOVE) {
        float local_y = event->mouse.y - 36.0f;
        if (local_y >= 0.0f) {
            int hovered = palette->first_visible_index + (int)(local_y / (float)palette->item_height);
            if (hovered >= 0 && hovered < (int)palette->filtered_count &&
                hovered < palette->first_visible_index + (int)palette->max_visible) {
                if (palette->hovered_index != hovered) {
                    palette->hovered_index = hovered;
                    palette->selected_index = hovered;
                    commandpalette_ensure_selection_visible(palette);
                    palette->base.needs_paint = true;
                }
                return true;
            }
        }
    }

    if (event->type == VG_EVENT_MOUSE_DOWN || event->type == VG_EVENT_CLICK) {
        float local_y = event->mouse.y - 36.0f;
        if (local_y >= 0.0f) {
            int clicked = palette->first_visible_index + (int)(local_y / (float)palette->item_height);
            if (clicked >= 0 && clicked < (int)palette->filtered_count &&
                clicked < palette->first_visible_index + (int)palette->max_visible) {
                palette->selected_index = clicked;
                commandpalette_ensure_selection_visible(palette);
                palette->base.needs_paint = true;
                if (event->type == VG_EVENT_CLICK)
                    vg_commandpalette_execute_selected(palette);
                return true;
            }
        }
    }

    return false;
}

vg_command_t *vg_commandpalette_add_command(vg_commandpalette_t *palette,
                                            const char *id,
                                            const char *label,
                                            const char *shortcut,
                                            void (*action)(vg_command_t *, void *),
                                            void *user_data) {
    if (!palette || !id || !label)
        return NULL;

    vg_command_t *cmd = calloc(1, sizeof(vg_command_t));
    if (!cmd)
        return NULL;

    cmd->id = strdup(id);
    cmd->label = strdup(label);
    if (shortcut)
        cmd->shortcut = strdup(shortcut);
    cmd->action = action;
    cmd->user_data = user_data;
    cmd->enabled = true;

    // Add to array
    if (palette->command_count >= palette->command_capacity) {
        size_t new_cap = palette->command_capacity * 2;
        if (new_cap < 32)
            new_cap = 32;
        vg_command_t **new_cmds = realloc(palette->commands, new_cap * sizeof(vg_command_t *));
        if (!new_cmds) {
            free_command(cmd);
            return NULL;
        }
        palette->commands = new_cmds;
        palette->command_capacity = new_cap;
    }

    palette->commands[palette->command_count++] = cmd;

    // Re-filter if visible
    if (palette->is_visible) {
        filter_commands(palette);
    }

    return cmd;
}

/// @brief Commandpalette remove command.
void vg_commandpalette_remove_command(vg_commandpalette_t *palette, const char *id) {
    if (!palette || !id)
        return;

    for (size_t i = 0; i < palette->command_count; i++) {
        if (palette->commands[i] && strcmp(palette->commands[i]->id, id) == 0) {
            free_command(palette->commands[i]);
            // Shift remaining
            for (size_t j = i; j < palette->command_count - 1; j++) {
                palette->commands[j] = palette->commands[j + 1];
            }
            palette->command_count--;

            if (palette->is_visible) {
                filter_commands(palette);
            }
            return;
        }
    }
}

vg_command_t *vg_commandpalette_get_command(vg_commandpalette_t *palette, const char *id) {
    if (!palette || !id)
        return NULL;

    for (size_t i = 0; i < palette->command_count; i++) {
        if (palette->commands[i] && strcmp(palette->commands[i]->id, id) == 0) {
            return palette->commands[i];
        }
    }
    return NULL;
}

/// @brief Commandpalette show.
void vg_commandpalette_show(vg_commandpalette_t *palette) {
    if (!palette)
        return;

    palette->is_visible = true;
    palette->base.visible = true;

    // Clear search and filter all
    free(palette->current_query);
    palette->current_query = NULL;
    filter_commands(palette);
    palette->first_visible_index = 0;

    palette->base.needs_paint = true;
    palette->base.needs_layout = true;
}

/// @brief Commandpalette hide.
void vg_commandpalette_hide(vg_commandpalette_t *palette) {
    if (!palette)
        return;

    palette->is_visible = false;
    palette->base.visible = false;

    if (palette->on_dismiss) {
        palette->on_dismiss(palette, palette->user_data);
    }
}

/// @brief Commandpalette toggle.
void vg_commandpalette_toggle(vg_commandpalette_t *palette) {
    if (!palette)
        return;

    if (palette->is_visible) {
        vg_commandpalette_hide(palette);
    } else {
        vg_commandpalette_show(palette);
    }
}

/// @brief Commandpalette execute selected.
void vg_commandpalette_execute_selected(vg_commandpalette_t *palette) {
    if (!palette)
        return;
    if (palette->selected_index < 0 || palette->selected_index >= (int)palette->filtered_count)
        return;

    vg_command_t *cmd = palette->filtered[palette->selected_index];
    if (!cmd || !cmd->enabled)
        return;

    // Execute action
    if (cmd->action) {
        cmd->action(cmd, cmd->user_data);
    }

    // Notify callback
    if (palette->on_execute) {
        palette->on_execute(palette, cmd, palette->user_data);
    }

    // Hide palette
    vg_commandpalette_hide(palette);
}

/// @brief Commandpalette set callbacks.
void vg_commandpalette_set_callbacks(vg_commandpalette_t *palette,
                                     void (*on_execute)(vg_commandpalette_t *,
                                                        vg_command_t *,
                                                        void *),
                                     void (*on_dismiss)(vg_commandpalette_t *, void *),
                                     void *user_data) {
    if (!palette)
        return;

    palette->on_execute = on_execute;
    palette->on_dismiss = on_dismiss;
    palette->user_data = user_data;
}

/// @brief Commandpalette set font.
void vg_commandpalette_set_font(vg_commandpalette_t *palette, vg_font_t *font, float size) {
    if (!palette)
        return;

    palette->font = font;
    palette->font_size = size;
    palette->base.needs_paint = true;
}

void vg_commandpalette_set_placeholder(vg_commandpalette_t *palette, const char *text) {
    if (!palette)
        return;
    free(palette->placeholder_text);
    palette->placeholder_text = text ? strdup(text) : NULL;
    palette->base.needs_paint = true;
}

/// @brief Commandpalette clear.
void vg_commandpalette_clear(vg_commandpalette_t *palette) {
    if (!palette)
        return;

    for (size_t i = 0; i < palette->command_count; i++)
        free_command(palette->commands[i]);

    palette->command_count = 0;
    /* Keep the allocation; clear filtered list */
    palette->filtered_count = 0;
    palette->selected_index = -1;
    palette->hovered_index = -1;
    palette->first_visible_index = 0;
    palette->base.needs_paint = true;
}
