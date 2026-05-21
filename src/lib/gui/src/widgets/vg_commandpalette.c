//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_commandpalette.c
// Purpose: Fuzzy-search command palette overlay widget. Maintains a master
//          commands[] array and a filtered[] view that is rebuilt on every
//          query change. Activated via show/toggle and dismissed via Escape
//          or command execution.
// Key invariants:
//   - filtered[] is rebuilt from scratch on every query change; it is a view
//     into commands[], not a copy (pointers only).
//   - filtered_count <= command_count always; filtered_capacity grows separately.
//   - selected_index is reset to 0 (or -1 when empty) after each filter pass.
//   - first_visible_index is clamped by commandpalette_ensure_selection_visible
//     so the selected row is always within the visible window.
//   - is_visible and base.visible are kept in sync; both are cleared on hide.
// Ownership/Lifetime:
//   - commands[] are heap-allocated inside add_command and freed in destroy.
//   - filtered[] holds borrowed pointers into commands[]; never freed individually.
// Links: lib/gui/include/vg_ide_widgets.h,
//        lib/gui/include/vg_widget.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <ctype.h>
#include <stdint.h>
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

/// @brief Decode the next UTF-8 codepoint at @p *cursor, advancing it past
///        the consumed bytes. Returns 0 at end of string, U+FFFD on a
///        malformed/overlong/surrogate sequence (advancing one byte so the
///        fuzzy matcher always makes forward progress).
static uint32_t commandpalette_decode_utf8(const char **cursor) {
    const unsigned char *s = (const unsigned char *)*cursor;
    if (!s || !*s)
        return 0;
    size_t remaining = strlen((const char *)s);
    if (s[0] < 0x80) {
        *cursor += 1;
        return s[0];
    }
    if (remaining >= 2 && (s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *cursor += 2;
        return cp >= 0x80 ? cp : 0xFFFD;
    }
    if (remaining >= 3 && (s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) |
                      (uint32_t)(s[2] & 0x3F);
        *cursor += 3;
        return cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF) ? cp : 0xFFFD;
    }
    if (remaining >= 4 && (s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
                      ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
        *cursor += 4;
        return cp >= 0x10000 && cp <= 0x10FFFF ? cp : 0xFFFD;
    }
    *cursor += 1;
    return 0xFFFD;
}

/// @brief ASCII case-fold a codepoint (A-Z → a-z) for case-insensitive
///        fuzzy matching; non-ASCII passes through unchanged.
static uint32_t commandpalette_fold_codepoint(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z')
        return cp + ('a' - 'A');
    return cp;
}

/// @brief True if @p cp starts a new word (NUL, space, '_' or '-'); used to
///        award the fuzzy matcher a bonus for word-initial character hits.
static bool commandpalette_is_word_boundary(uint32_t cp) {
    return cp == 0 || cp == ' ' || cp == '_' || cp == '-';
}

/// @brief Compute a fuzzy-match score for pattern against text; returns 0 (no match) or a positive score (higher = better).
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
    uint32_t prev_tc = 0;
    int text_pos = 0;

    while (*p && *t) {
        const char *p_next = p;
        const char *t_next = t;
        uint32_t pc = commandpalette_fold_codepoint(commandpalette_decode_utf8(&p_next));
        uint32_t tc = commandpalette_fold_codepoint(commandpalette_decode_utf8(&t_next));

        if (pc == tc) {
            // Found a match
            score += consecutive ? 10 : 5;

            // Bonus for matching at word boundaries
            if (t == text || commandpalette_is_word_boundary(prev_tc)) {
                score += 15;
            }

            // Bonus for consecutive matches
            if (text_pos == last_match_pos + 1) {
                score += 5;
            }

            last_match_pos = text_pos;
            p = p_next;
            consecutive = true;
        } else {
            consecutive = false;
        }
        prev_tc = tc;
        t = t_next;
        text_pos++;
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

/// @brief Rebuild filtered[] from commands[] using current_query, reset selection to the top.
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
                if (new_cap <= palette->filtered_capacity ||
                    new_cap > SIZE_MAX / sizeof(vg_command_t *))
                    continue;
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

/// @brief Clamp first_visible_index so that selected_index is within the visible window.
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

/// @brief Encode a Unicode codepoint to UTF-8 in out[]; returns byte length (1-4) or 0 for invalid codepoints.
static size_t utf8_encode_codepoint(uint32_t codepoint, char out[4]) {
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
        return 0;
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

/// @brief Append a single Unicode codepoint to current_query and re-run the filter pass.
static void append_query_char(vg_commandpalette_t *palette, uint32_t codepoint) {
    char encoded[4];
    size_t encoded_len = 0;
    if (!palette || codepoint < 0x20 || codepoint == 0x7F)
        return;
    encoded_len = utf8_encode_codepoint(codepoint, encoded);
    if (encoded_len == 0)
        return;
    size_t old_len = palette->current_query ? strlen(palette->current_query) : 0;
    if (old_len > SIZE_MAX - encoded_len - 1u)
        return;
    char *next = realloc(palette->current_query, old_len + encoded_len + 1);
    if (!next)
        return;
    memcpy(next + old_len, encoded, encoded_len);
    next[old_len + encoded_len] = '\0';
    palette->current_query = next;
    filter_commands(palette);
}

/// @brief Remove the last UTF-8 character from current_query (backspace), then re-filter.
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

/// @brief Create and initialise a command palette widget with default VS-Code-style colours.
///
/// @return Newly allocated palette (invisible, no commands), or NULL on allocation failure.
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

/// @brief Free all heap strings inside cmd and the cmd struct itself.
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

/// @brief vtable destroy — free all commands, the filtered view, and string fields.
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

/// @brief Destroy the palette, freeing all commands and internal allocations.
///
/// @param palette Palette to destroy; may be NULL (no-op).
void vg_commandpalette_destroy(vg_commandpalette_t *palette) {
    if (!palette)
        return;
    vg_widget_destroy(&palette->base);
}

/// @brief vtable measure — compute desired size as fixed width × (search bar + visible_count × item_height).
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

/// @brief vtable paint — draw the search bar (query or placeholder) and the visible filtered result rows.
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

/// @brief vtable handle_event — dispatch key input (typing, backspace, arrows, Enter, Escape) and mouse clicks/hover/scroll.
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

/// @brief Register a new command and re-filter the display if the palette is currently visible.
///
/// @param palette   Palette to add to; may be NULL (returns NULL).
/// @param id        Unique command identifier; required.
/// @param label     Display text shown in the results list; required.
/// @param shortcut  Optional keyboard shortcut string (e.g. "Ctrl+P"); may be NULL.
/// @param action    Callback invoked when the command is executed; may be NULL.
/// @param user_data Opaque pointer forwarded to action.
/// @return          The new command struct, or NULL on allocation failure.
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
    if (!cmd->id || !cmd->label || (shortcut && !cmd->shortcut)) {
        free_command(cmd);
        return NULL;
    }
    cmd->action = action;
    cmd->user_data = user_data;
    cmd->enabled = true;

    // Add to array
    if (palette->command_count >= palette->command_capacity) {
        size_t new_cap = palette->command_capacity * 2;
        if (new_cap < 32)
            new_cap = 32;
        if (new_cap <= palette->command_capacity ||
            new_cap > SIZE_MAX / sizeof(vg_command_t *)) {
            free_command(cmd);
            return NULL;
        }
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

/// @brief Remove the command with the given id, shifting the commands[] array and re-filtering.
///
/// @param palette Palette to modify; may be NULL (no-op).
/// @param id      String identifier of the command to remove; may be NULL (no-op).
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

/// @brief Look up a registered command by its string id.
///
/// @param palette Palette to search; may be NULL (returns NULL).
/// @param id      String identifier to match; may be NULL (returns NULL).
/// @return        Matching command pointer, or NULL if not found.
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

/// @brief Show the palette, clearing any previous query and rebuilding the filtered list from all commands.
///
/// @param palette Palette to show; may be NULL (no-op).
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

/// @brief Hide the palette and invoke the on_dismiss callback if set.
///
/// @param palette Palette to hide; may be NULL (no-op).
void vg_commandpalette_hide(vg_commandpalette_t *palette) {
    if (!palette)
        return;

    palette->is_visible = false;
    palette->base.visible = false;

    if (palette->on_dismiss) {
        palette->on_dismiss(palette, palette->user_data);
    }
}

/// @brief Toggle visibility — show if hidden, hide if visible.
///
/// @param palette Palette to toggle; may be NULL (no-op).
void vg_commandpalette_toggle(vg_commandpalette_t *palette) {
    if (!palette)
        return;

    if (palette->is_visible) {
        vg_commandpalette_hide(palette);
    } else {
        vg_commandpalette_show(palette);
    }
}

/// @brief Execute the currently selected command, invoke on_execute, then hide the palette.
///
/// @param palette Palette whose selection to execute; may be NULL (no-op).
void vg_commandpalette_execute_selected(vg_commandpalette_t *palette) {
    if (!palette)
        return;
    if (palette->selected_index < 0 || palette->selected_index >= (int)palette->filtered_count)
        return;

    vg_command_t *cmd = palette->filtered[palette->selected_index];
    if (!cmd || !cmd->enabled)
        return;

    vg_command_t snapshot = *cmd;
    snapshot.id = cmd->id ? strdup(cmd->id) : NULL;
    snapshot.label = cmd->label ? strdup(cmd->label) : NULL;
    snapshot.description = cmd->description ? strdup(cmd->description) : NULL;
    snapshot.shortcut = cmd->shortcut ? strdup(cmd->shortcut) : NULL;
    snapshot.category = cmd->category ? strdup(cmd->category) : NULL;
    if ((cmd->id && !snapshot.id) || (cmd->label && !snapshot.label) ||
        (cmd->description && !snapshot.description) ||
        (cmd->category && !snapshot.category) ||
        (cmd->shortcut && !snapshot.shortcut)) {
        free(snapshot.id);
        free(snapshot.label);
        free(snapshot.description);
        free(snapshot.shortcut);
        free(snapshot.category);
        return;
    }
    void (*action)(vg_command_t *, void *) = cmd->action;
    void *action_user_data = cmd->user_data;
    void (*on_execute)(vg_commandpalette_t *, vg_command_t *, void *) = palette->on_execute;
    void *on_execute_user_data = palette->user_data;

    // Execute action
    if (action) {
        action(&snapshot, action_user_data);
    }

    // Notify callback
    if (vg_widget_is_live(&palette->base) && on_execute) {
        on_execute(palette, &snapshot, on_execute_user_data);
    }

    // Hide palette
    if (vg_widget_is_live(&palette->base))
        vg_commandpalette_hide(palette);
    free(snapshot.id);
    free(snapshot.label);
    free(snapshot.description);
    free(snapshot.shortcut);
    free(snapshot.category);
}

/// @brief Register lifecycle callbacks invoked when a command is executed or the palette is dismissed.
///
/// @param palette    Palette to configure; may be NULL (no-op).
/// @param on_execute Called after executing a command, before hiding; may be NULL.
/// @param on_dismiss Called when the palette hides (Escape or post-execute); may be NULL.
/// @param user_data  Opaque pointer forwarded to both callbacks.
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

/// @brief Set the font and point size used for query text, placeholder, and result labels.
///
/// @param palette Palette to configure; may be NULL (no-op).
/// @param font    Font to use for all text rendering.
/// @param size    Point size.
void vg_commandpalette_set_font(vg_commandpalette_t *palette, vg_font_t *font, float size) {
    if (!palette)
        return;

    palette->font = font;
    palette->font_size = size;
    palette->base.needs_paint = true;
}

/// @brief Set the placeholder string shown in the search bar when the query is empty.
///
/// @param palette Palette to configure; may be NULL (no-op).
/// @param text    Placeholder string, duplicated internally; may be NULL to clear.
void vg_commandpalette_set_placeholder(vg_commandpalette_t *palette, const char *text) {
    if (!palette)
        return;
    free(palette->placeholder_text);
    palette->placeholder_text = text ? strdup(text) : NULL;
    palette->base.needs_paint = true;
}

/// @brief Free all registered commands and reset selection state, keeping allocations for reuse.
///
/// @param palette Palette to clear; may be NULL (no-op).
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
