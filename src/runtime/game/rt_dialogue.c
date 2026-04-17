//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_dialogue.c
// Purpose: Typewriter text reveal system with word wrapping, speaker labels,
//   dialogue queue, and immediate-mode Canvas drawing.
//
// Key invariants:
//   - Typewriter accumulator tracks microseconds for precise character timing.
//   - Word wrapping uses greedy line-break at word boundaries.
//   - Queue is circular: oldest lines dropped when full.
//   - Draw is immediate-mode: call each frame with a Canvas.
//
// Ownership/Lifetime:
//   - Dialogue objects are GC-managed (rt_obj_new_i64).
//
// Links: rt_dialogue.h, rt_graphics.h (canvas text/box APIs)
//
//===----------------------------------------------------------------------===//

#include "rt_dialogue.h"

#include "rt_bitmapfont.h"
#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <string.h>

#define DLG_MAX_LINES 64
#define DLG_MAX_TEXT_LEN 512
#define DLG_DEFAULT_SPEED 30
#define DLG_DEFAULT_PADDING 8
#define DLG_DEFAULT_SCALE 1
#define DLG_DEFAULT_LINE_GAP 2

typedef struct {
    char speaker[64];
    char text[DLG_MAX_TEXT_LEN];
    int32_t speaker_len;
    int32_t text_len_bytes;
    int32_t text_len_chars;
} dlg_line;

typedef struct {
    // Geometry
    int32_t x, y, width, height;
    int32_t padding;
    int32_t text_scale;

    // Colors
    int32_t text_color;
    int32_t speaker_color;
    int32_t bg_color;
    int32_t bg_alpha;
    int32_t border_color;

    // Font
    void *font;

    // Dialogue queue
    dlg_line lines[DLG_MAX_LINES];
    int32_t line_count;
    int32_t current_line;

    // Typewriter state
    int32_t chars_per_second;
    int32_t revealed_chars;
    int64_t accumulator_us;

    // State
    int8_t active;
    int8_t line_complete;
    int8_t waiting_for_input;
    int8_t finished;
} rt_dialogue_impl;

static void rt_dialogue_finalizer(void *obj) {
    rt_dialogue_impl *d = (rt_dialogue_impl *)obj;
    if (!d || !d->font)
        return;
    if (rt_obj_release_check0(d->font))
        rt_obj_free(d->font);
    d->font = NULL;
}

static size_t dlg_utf8_char_len(unsigned char c, size_t remaining) {
    size_t len = 1;
    if ((c & 0x80) == 0)
        len = 1;
    else if ((c & 0xE0) == 0xC0)
        len = 2;
    else if ((c & 0xF0) == 0xE0)
        len = 3;
    else if ((c & 0xF8) == 0xF0)
        len = 4;
    if (len > remaining)
        len = remaining;
    return len;
}

static int32_t dlg_utf8_count_codepoints(const char *text, size_t byte_len) {
    int32_t count = 0;
    size_t index = 0;
    while (index < byte_len) {
        index += dlg_utf8_char_len((unsigned char)text[index], byte_len - index);
        count++;
    }
    return count;
}

static size_t dlg_utf8_prefix_bytes(const char *text, size_t byte_len, int32_t codepoints) {
    size_t index = 0;
    int32_t count = 0;
    while (index < byte_len && count < codepoints) {
        index += dlg_utf8_char_len((unsigned char)text[index], byte_len - index);
        count++;
    }
    return index;
}

static int32_t dlg_copy_utf8(char *dst, size_t dst_cap, const char *src) {
    if (!dst || dst_cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    if (!src)
        return 0;

    size_t src_len = strlen(src);
    if (src_len >= dst_cap)
        src_len = dst_cap - 1;

    size_t safe_len = 0;
    while (safe_len < src_len) {
        unsigned char c = (unsigned char)src[safe_len];
        size_t char_len = 1;
        if ((c & 0x80) == 0)
            char_len = 1;
        else if ((c & 0xE0) == 0xC0)
            char_len = 2;
        else if ((c & 0xF0) == 0xE0)
            char_len = 3;
        else if ((c & 0xF8) == 0xF0)
            char_len = 4;
        if (safe_len + char_len > src_len)
            break;
        safe_len += char_len;
    }
    src_len = safe_len;

    if (src_len > 0)
        memcpy(dst, src, src_len);
    dst[src_len] = '\0';
    return (int32_t)src_len;
}

static int32_t dlg_text_height_px(const rt_dialogue_impl *d) {
    int32_t base_height = d->font ? (int32_t)rt_bitmapfont_text_height(d->font)
                                  : (int32_t)rt_canvas_text_height();
    return base_height * d->text_scale;
}

static int32_t dlg_line_advance_px(const rt_dialogue_impl *d) {
    return dlg_text_height_px(d) + DLG_DEFAULT_LINE_GAP * d->text_scale;
}

static int32_t dlg_measure_bytes(const rt_dialogue_impl *d, const char *text, size_t byte_len) {
    if (!text || byte_len == 0)
        return 0;

    rt_string measured = rt_string_from_bytes(text, byte_len);
    if (!measured)
        return 0;

    int64_t width = d->font ? rt_bitmapfont_text_width(d->font, measured) : rt_canvas_text_width(measured);
    rt_str_release_maybe(measured);
    return (int32_t)(width * d->text_scale);
}

static void dlg_draw_bytes(
    const rt_dialogue_impl *d, void *canvas, int32_t x, int32_t y, const char *text, size_t byte_len, int32_t color) {
    if (!canvas || !text || byte_len == 0)
        return;

    rt_string drawn = rt_string_from_bytes(text, byte_len);
    if (!drawn)
        return;

    if (d->font) {
        if (d->text_scale > 1)
            rt_canvas_text_font_scaled(canvas, x, y, drawn, d->font, d->text_scale, color);
        else
            rt_canvas_text_font(canvas, x, y, drawn, d->font, color);
    } else {
        if (d->text_scale > 1)
            rt_canvas_text_scaled(canvas, x, y, drawn, d->text_scale, color);
        else
            rt_canvas_text(canvas, x, y, drawn, color);
    }

    rt_str_release_maybe(drawn);
}

static int8_t dlg_is_space(unsigned char c) {
    return c == ' ' || c == '\t';
}

static void dlg_draw_wrapped_revealed(
    const rt_dialogue_impl *d, void *canvas, const dlg_line *line, int32_t text_x, int32_t text_y, int32_t max_x, int32_t max_y) {
    if (!line || line->text_len_bytes <= 0)
        return;

    size_t shown_bytes =
        dlg_utf8_prefix_bytes(line->text, (size_t)line->text_len_bytes, d->revealed_chars);
    if (shown_bytes == 0)
        return;

    int32_t cx = text_x;
    int32_t cy = text_y;
    int32_t text_height = dlg_text_height_px(d);
    int32_t line_advance = dlg_line_advance_px(d);
    size_t index = 0;

    while (index < shown_bytes) {
        unsigned char c = (unsigned char)line->text[index];
        if (c == '\n') {
            cx = text_x;
            cy += line_advance;
            index++;
            if (cy + text_height > max_y)
                break;
            continue;
        }

        if (dlg_is_space(c)) {
            size_t run_start = index;
            while (index < shown_bytes && dlg_is_space((unsigned char)line->text[index]))
                index++;

            if (cx == text_x)
                continue;

            size_t run_len = index - run_start;
            int32_t run_width = dlg_measure_bytes(d, line->text + run_start, run_len);
            if (cx + run_width > max_x) {
                cx = text_x;
                cy += line_advance;
                if (cy + text_height > max_y)
                    break;
                continue;
            }

            dlg_draw_bytes(d, canvas, cx, cy, line->text + run_start, run_len, d->text_color);
            cx += run_width;
            continue;
        }

        size_t word_start = index;
        while (index < shown_bytes) {
            unsigned char wc = (unsigned char)line->text[index];
            if (wc == '\n' || dlg_is_space(wc))
                break;
            index += dlg_utf8_char_len(wc, shown_bytes - index);
        }

        size_t word_len = index - word_start;
        int32_t word_width = dlg_measure_bytes(d, line->text + word_start, word_len);
        if (cx > text_x && cx + word_width > max_x) {
            cx = text_x;
            cy += line_advance;
            if (cy + text_height > max_y)
                break;
        }

        if (word_width <= max_x - text_x) {
            dlg_draw_bytes(d, canvas, cx, cy, line->text + word_start, word_len, d->text_color);
            cx += word_width;
            continue;
        }

        size_t char_index = word_start;
        while (char_index < word_start + word_len) {
            size_t char_len = dlg_utf8_char_len((unsigned char)line->text[char_index],
                                                word_start + word_len - char_index);
            int32_t char_width = dlg_measure_bytes(d, line->text + char_index, char_len);
            if (cx > text_x && cx + char_width > max_x) {
                cx = text_x;
                cy += line_advance;
                if (cy + text_height > max_y)
                    return;
            }

            dlg_draw_bytes(d, canvas, cx, cy, line->text + char_index, char_len, d->text_color);
            cx += char_width;
            char_index += char_len;
        }
    }
}

//=============================================================================
// Construction
//=============================================================================

/// @brief Create a new dialogue box at the given screen position and size.
/// @details The dialogue supports queued lines with typewriter text reveal,
///          speaker names, and configurable styling (font, colors, padding).
void *rt_dialogue_new(int64_t x, int64_t y, int64_t width, int64_t height) {
    rt_dialogue_impl *d = (rt_dialogue_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_dialogue_impl));
    if (!d)
        return NULL;

    d->x = (int32_t)x;
    d->y = (int32_t)y;
    d->width = (int32_t)width;
    d->height = (int32_t)height;
    d->padding = DLG_DEFAULT_PADDING;
    d->text_scale = DLG_DEFAULT_SCALE;
    d->text_color = 0xFFFFFF;    // white
    d->speaker_color = 0xFFFF00; // yellow
    d->bg_color = 0x000000;      // black
    d->bg_alpha = 200;
    d->border_color = 0xFFFFFF;
    d->font = NULL;
    d->line_count = 0;
    d->current_line = 0;
    d->chars_per_second = DLG_DEFAULT_SPEED;
    d->revealed_chars = 0;
    d->accumulator_us = 0;
    d->active = 0;
    d->line_complete = 0;
    d->waiting_for_input = 0;
    d->finished = 0;
    memset(d->lines, 0, sizeof(d->lines));
    rt_obj_set_finalizer(d, rt_dialogue_finalizer);
    return d;
}

//=============================================================================
// Configuration
//=============================================================================

/// @brief Set the typewriter text reveal speed in characters per second (<= 0 = instant reveal).
void rt_dialogue_set_speed(void *dlg, int64_t cps) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->chars_per_second = cps <= 0 ? 0 : (int32_t)cps;
}

/// @brief Assign a BitmapFont for dialogue text; retains the new font and releases the old one.
void rt_dialogue_set_font(void *dlg, void *font) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    // Retain new font, release old to prevent dangling pointer
    if (font)
        rt_obj_retain_maybe(font);
    if (d->font && rt_obj_release_check0(d->font))
        rt_obj_free(d->font);
    d->font = font;
}

/// @brief Set the text color of the dialogue.
void rt_dialogue_set_text_color(void *dlg, int64_t color) {
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->text_color = (int32_t)color;
}

/// @brief Set the speaker color of the dialogue.
void rt_dialogue_set_speaker_color(void *dlg, int64_t color) {
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->speaker_color = (int32_t)color;
}

/// @brief Set the bg color of the dialogue.
void rt_dialogue_set_bg_color(void *dlg, int64_t color, int64_t alpha) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->bg_color = (int32_t)color;
    d->bg_alpha = (int32_t)alpha;
}

/// @brief Set the border color of the dialogue.
void rt_dialogue_set_border_color(void *dlg, int64_t color) {
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->border_color = (int32_t)color;
}

/// @brief Set the inner padding (pixels) between the box edge and text.
void rt_dialogue_set_padding(void *dlg, int64_t padding) {
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->padding = (int32_t)padding;
}

/// @brief Set the text scale of the dialogue.
void rt_dialogue_set_text_scale(void *dlg, int64_t scale) {
    if (!dlg)
        return;
    if (scale < 1)
        scale = 1;
    ((rt_dialogue_impl *)dlg)->text_scale = (int32_t)scale;
}

/// @brief Reposition the dialogue box to screen coordinates (x, y).
void rt_dialogue_set_pos(void *dlg, int64_t x, int64_t y) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->x = (int32_t)x;
    d->y = (int32_t)y;
}

/// @brief Set the dialogue box width and height in pixels.
void rt_dialogue_set_size(void *dlg, int64_t w, int64_t h) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->width = (int32_t)w;
    d->height = (int32_t)h;
}

//=============================================================================
// Dialogue Queue
//=============================================================================

/// @brief Say the dialogue.
void rt_dialogue_say(void *dlg, rt_string speaker, rt_string text) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;

    if (d->line_count >= DLG_MAX_LINES) {
        // Shift lines down, drop oldest
        for (int i = 0; i < DLG_MAX_LINES - 1; i++)
            d->lines[i] = d->lines[i + 1];
        d->line_count = DLG_MAX_LINES - 1;
        if (d->current_line > 0)
            d->current_line--;
    }

    dlg_line *line = &d->lines[d->line_count];
    memset(line, 0, sizeof(dlg_line));

    if (speaker) {
        const char *sp = rt_string_cstr(speaker);
        line->speaker_len = dlg_copy_utf8(line->speaker, sizeof(line->speaker), sp);
    }

    if (text) {
        const char *tx = rt_string_cstr(text);
        if (tx) {
            line->text_len_bytes = dlg_copy_utf8(line->text, sizeof(line->text), tx);
            line->text_len_chars =
                dlg_utf8_count_codepoints(line->text, (size_t)line->text_len_bytes);
        }
    }

    d->line_count++;
    if (!d->active) {
        d->active = 1;
        d->finished = 0;
        d->current_line = 0;
        d->revealed_chars = 0;
        d->accumulator_us = 0;
        d->line_complete = 0;
        d->waiting_for_input = 0;
    }
}

/// @brief Begin displaying a new line of dialogue with typewriter text reveal.
void rt_dialogue_say_text(void *dlg, rt_string text) {
    rt_dialogue_say(dlg, NULL, text);
}

/// @brief Remove all entries from the dialogue.
void rt_dialogue_clear(void *dlg) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->line_count = 0;
    d->current_line = 0;
    d->revealed_chars = 0;
    d->accumulator_us = 0;
    d->active = 0;
    d->line_complete = 0;
    d->waiting_for_input = 0;
    d->finished = 0;
}

//=============================================================================
// Playback
//=============================================================================

/// @brief Update the dialogue state (called per frame/tick).
void rt_dialogue_update(void *dlg, int64_t dt_ms) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active || d->waiting_for_input || d->line_complete)
        return;
    if (d->current_line >= d->line_count)
        return;

    dlg_line *line = &d->lines[d->current_line];

    // Speed == 0 means instant reveal
    if (d->chars_per_second == 0) {
        d->revealed_chars = line->text_len_chars;
        d->line_complete = 1;
        d->waiting_for_input = 1;
        return;
    }

    // Accumulate microseconds
    d->accumulator_us += (int64_t)(dt_ms * 1000);
    int64_t us_per_char = 1000000 / d->chars_per_second;
    if (us_per_char < 1)
        us_per_char = 1;

    int64_t chars_to_add = d->accumulator_us / us_per_char;
    d->accumulator_us %= us_per_char;
    d->revealed_chars += (int32_t)chars_to_add;

    if (d->revealed_chars >= line->text_len_chars) {
        d->revealed_chars = line->text_len_chars;
        d->line_complete = 1;
        d->waiting_for_input = 1;
    }
}

/// @brief Advance the dialogue.
void rt_dialogue_advance(void *dlg) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active)
        return;

    if (!d->line_complete) {
        // Skip to end of current line
        if (d->current_line < d->line_count) {
            d->revealed_chars = d->lines[d->current_line].text_len_chars;
            d->line_complete = 1;
            d->waiting_for_input = 1;
        }
    } else {
        // Move to next line
        d->current_line++;
        if (d->current_line >= d->line_count) {
            d->active = 0;
            d->finished = 1;
        } else {
            d->revealed_chars = 0;
            d->accumulator_us = 0;
            d->line_complete = 0;
            d->waiting_for_input = 0;
        }
    }
}

/// @brief Skip the dialogue.
void rt_dialogue_skip(void *dlg) {
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active || d->current_line >= d->line_count)
        return;
    d->revealed_chars = d->lines[d->current_line].text_len_chars;
    d->line_complete = 1;
    d->waiting_for_input = 1;
}

//=============================================================================
// State Queries
//=============================================================================

/// @brief Check whether the dialogue box is currently visible and displaying text.
int8_t rt_dialogue_is_active(void *dlg) {
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->active;
}

/// @brief Check whether the current line of dialogue has been fully displayed.
int8_t rt_dialogue_is_line_complete(void *dlg) {
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->line_complete;
}

/// @brief Check whether the typewriter effect has fully revealed all text.
int8_t rt_dialogue_is_finished(void *dlg) {
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->finished;
}

/// @brief Check whether the dialogue is waiting for the player to dismiss it.
int8_t rt_dialogue_is_waiting(void *dlg) {
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->waiting_for_input;
}

/// @brief Return the count of elements in the dialogue.
int64_t rt_dialogue_get_line_count(void *dlg) {
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->line_count;
}

/// @brief Get the current line of the dialogue.
int64_t rt_dialogue_get_current_line(void *dlg) {
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->current_line;
}

/// @brief Return the name of the current speaker, or empty string if none.
rt_string rt_dialogue_get_speaker(void *dlg) {
    if (!dlg)
        return rt_const_cstr("");
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (d->current_line >= d->line_count)
        return rt_const_cstr("");
    return rt_const_cstr(d->lines[d->current_line].speaker);
}

//=============================================================================
// Rendering
//=============================================================================

/// @brief Draw the dialogue.
void rt_dialogue_draw(void *dlg, void *canvas) {
    if (!dlg || !canvas)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active || d->current_line >= d->line_count)
        return;

    // Draw background panel
    rt_canvas_box_alpha(canvas, d->x, d->y, d->width, d->height, d->bg_color, d->bg_alpha);
    if (d->border_color >= 0)
        rt_canvas_frame(canvas, d->x, d->y, d->width, d->height, d->border_color);

    dlg_line *line = &d->lines[d->current_line];
    int32_t pad = d->padding;
    int32_t text_x = d->x + pad;
    int32_t text_y = d->y + pad;
    int32_t text_height = dlg_text_height_px(d);
    int32_t line_advance = dlg_line_advance_px(d);

    // Draw speaker name
    if (line->speaker[0] != '\0') {
        dlg_draw_bytes(
            d, canvas, text_x, text_y, line->speaker, (size_t)line->speaker_len, d->speaker_color);
        text_y += line_advance + DLG_DEFAULT_LINE_GAP * d->text_scale;
    }

    // Draw revealed text with word wrapping
    int32_t max_x = d->x + d->width - pad;
    int32_t max_y = d->y + d->height - pad;
    dlg_draw_wrapped_revealed(d, canvas, line, text_x, text_y, max_x, max_y);

    // Draw "..." indicator when waiting
    if (d->waiting_for_input) {
        static const char indicator[] = "...";
        int32_t ind_w = dlg_measure_bytes(d, indicator, sizeof(indicator) - 1);
        int32_t ind_x = d->x + d->width - pad - ind_w;
        int32_t ind_y = d->y + d->height - pad - text_height;
        dlg_draw_bytes(d, canvas, ind_x, ind_y, indicator, sizeof(indicator) - 1, d->text_color);
    }
}
