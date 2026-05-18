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
#include "rt_trap.h"

#include <limits.h>
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

/// @brief Safe-cast a handle to the Dialogue impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p dlg is NULL.
static rt_dialogue_impl *checked_dialogue(void *dlg, const char *api) {
    if (!dlg)
        return NULL;
    if (rt_obj_class_id(dlg) != RT_DIALOGUE_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_dialogue_impl *)dlg;
}

/// @brief Clamp an int64 to the int32 range.
static int32_t clamp_i64_to_i32(int64_t value) {
    if (value < INT32_MIN)
        return INT32_MIN;
    if (value > INT32_MAX)
        return INT32_MAX;
    return (int32_t)value;
}

/// @brief Clamp a positive int64 to int32; non-positive yields @p fallback.
static int32_t clamp_positive_i64_to_i32(int64_t value, int32_t fallback) {
    if (value <= 0)
        return fallback;
    if (value > INT32_MAX)
        return INT32_MAX;
    return (int32_t)value;
}

/// @brief Clamp an int64 to a valid 0..255 alpha byte.
static int32_t clamp_alpha_i64(int64_t value) {
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return (int32_t)value;
}

/// @brief GC finalizer: release the dialogue's referenced font.
static void rt_dialogue_finalizer(void *obj) {
    rt_dialogue_impl *d = (rt_dialogue_impl *)obj;
    if (!d || !d->font)
        return;
    if (rt_obj_release_check0(d->font))
        rt_obj_free(d->font);
    d->font = NULL;
}

/// @brief Byte length (1–4) of the UTF-8 codepoint starting with lead byte
///        @p c, clamped so it never exceeds @p remaining bytes.
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

/// @brief Count UTF-8 codepoints in the first @p byte_len bytes of @p text
///        (used so the typewriter reveal advances per character).
static int32_t dlg_utf8_count_codepoints(const char *text, size_t byte_len) {
    int32_t count = 0;
    size_t index = 0;
    while (index < byte_len) {
        index += dlg_utf8_char_len((unsigned char)text[index], byte_len - index);
        count++;
    }
    return count;
}

/// @brief Byte length of the first @p codepoints UTF-8 characters of @p text
///        (so a partially-revealed line cuts on a character boundary).
static size_t dlg_utf8_prefix_bytes(const char *text, size_t byte_len, int32_t codepoints) {
    size_t index = 0;
    int32_t count = 0;
    while (index < byte_len && count < codepoints) {
        index += dlg_utf8_char_len((unsigned char)text[index], byte_len - index);
        count++;
    }
    return index;
}

/// @brief Copy @p src into @p dst (cap @p dst_cap), truncating on a UTF-8
///        character boundary so no partial multibyte sequence is left.
/// @return Number of bytes written (excluding the NUL terminator).
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

/// @brief Rendered text height in pixels for the dialogue's font and scale.
static int32_t dlg_text_height_px(const rt_dialogue_impl *d) {
    int32_t base_height = d->font ? (int32_t)rt_bitmapfont_text_height(d->font)
                                  : (int32_t)rt_canvas_text_height();
    return base_height * d->text_scale;
}

/// @brief Vertical advance between text lines (text height + scaled line gap).
static int32_t dlg_line_advance_px(const rt_dialogue_impl *d) {
    return dlg_text_height_px(d) + DLG_DEFAULT_LINE_GAP * d->text_scale;
}

/// @brief Rendered pixel width of a byte run, scaled by the dialogue's text
///        scale (0 for empty input).
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

/// @brief Draw a byte run of text at (x, y) on @p canvas using the dialogue's
///        font/scale (falls back to the canvas default font when none set).
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

/// @brief True if @p c is an inline whitespace separator (space or tab).
static int8_t dlg_is_space(unsigned char c) {
    return c == ' ' || c == '\t';
}

/// @brief Draw one word-wrapped line, revealing only the first
///        d->revealed_chars codepoints (typewriter effect), clipped to the
///        (max_x, max_y) bounds.
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
    rt_dialogue_impl *d =
        (rt_dialogue_impl *)rt_obj_new_i64(RT_DIALOGUE_CLASS_ID, (int64_t)sizeof(rt_dialogue_impl));
    if (!d)
        return NULL;

    d->x = clamp_i64_to_i32(x);
    d->y = clamp_i64_to_i32(y);
    d->width = clamp_positive_i64_to_i32(width, 1);
    d->height = clamp_positive_i64_to_i32(height, 1);
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
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.SetSpeed: expected Viper.Game.Dialogue");
    if (!d)
        return;
    d->chars_per_second = cps <= 0 ? 0 : clamp_positive_i64_to_i32(cps, DLG_DEFAULT_SPEED);
}

/// @brief Assign a BitmapFont for dialogue text; retains the new font and releases the old one.
void rt_dialogue_set_font(void *dlg, void *font) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.SetFont: expected Viper.Game.Dialogue");
    if (!d)
        return;
    // Retain new font, release old to prevent dangling pointer
    if (font)
        rt_obj_retain_maybe(font);
    if (d->font && rt_obj_release_check0(d->font))
        rt_obj_free(d->font);
    d->font = font;
}

/// @brief Set the text color of the dialogue.
void rt_dialogue_set_text_color(void *dlg, int64_t color) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.SetTextColor: expected Viper.Game.Dialogue");
    if (d)
        d->text_color = clamp_i64_to_i32(color);
}

/// @brief Set the speaker color of the dialogue.
void rt_dialogue_set_speaker_color(void *dlg, int64_t color) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.SetSpeakerColor: expected Viper.Game.Dialogue");
    if (d)
        d->speaker_color = clamp_i64_to_i32(color);
}

/// @brief Set the bg color of the dialogue.
void rt_dialogue_set_bg_color(void *dlg, int64_t color, int64_t alpha) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.SetBgColor: expected Viper.Game.Dialogue");
    if (!d)
        return;
    d->bg_color = clamp_i64_to_i32(color);
    d->bg_alpha = clamp_alpha_i64(alpha);
}

/// @brief Set the border color of the dialogue.
void rt_dialogue_set_border_color(void *dlg, int64_t color) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.SetBorderColor: expected Viper.Game.Dialogue");
    if (d)
        d->border_color = clamp_i64_to_i32(color);
}

/// @brief Set the inner padding (pixels) between the box edge and text.
void rt_dialogue_set_padding(void *dlg, int64_t padding) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.SetPadding: expected Viper.Game.Dialogue");
    if (d)
        d->padding = padding <= 0 ? 0 : clamp_i64_to_i32(padding);
}

/// @brief Set the text scale of the dialogue.
void rt_dialogue_set_text_scale(void *dlg, int64_t scale) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.SetTextScale: expected Viper.Game.Dialogue");
    if (!d)
        return;
    if (scale < 1)
        scale = 1;
    d->text_scale = clamp_positive_i64_to_i32(scale, 1);
}

/// @brief Reposition the dialogue box to screen coordinates (x, y).
void rt_dialogue_set_pos(void *dlg, int64_t x, int64_t y) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.SetPos: expected Viper.Game.Dialogue");
    if (!d)
        return;
    d->x = clamp_i64_to_i32(x);
    d->y = clamp_i64_to_i32(y);
}

/// @brief Set the dialogue box width and height in pixels.
void rt_dialogue_set_size(void *dlg, int64_t w, int64_t h) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.SetSize: expected Viper.Game.Dialogue");
    if (!d)
        return;
    d->width = clamp_positive_i64_to_i32(w, d->width > 0 ? d->width : 1);
    d->height = clamp_positive_i64_to_i32(h, d->height > 0 ? d->height : 1);
}

//=============================================================================
// Dialogue Queue
//=============================================================================

/// @brief Say the dialogue.
void rt_dialogue_say(void *dlg, rt_string speaker, rt_string text) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.Say: expected Viper.Game.Dialogue");
    if (!d)
        return;

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
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.Clear: expected Viper.Game.Dialogue");
    if (!d)
        return;
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
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.Update: expected Viper.Game.Dialogue");
    if (!d || dt_ms <= 0)
        return;
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
    int64_t delta_us = dt_ms > INT64_MAX / 1000 ? INT64_MAX : dt_ms * 1000;
    d->accumulator_us =
        d->accumulator_us > INT64_MAX - delta_us ? INT64_MAX : d->accumulator_us + delta_us;
    int64_t us_per_char = 1000000 / d->chars_per_second;
    if (us_per_char < 1)
        us_per_char = 1;

    int64_t chars_to_add = d->accumulator_us / us_per_char;
    d->accumulator_us %= us_per_char;
    if (chars_to_add > INT32_MAX - d->revealed_chars)
        d->revealed_chars = INT32_MAX;
    else
        d->revealed_chars += (int32_t)chars_to_add;

    if (d->revealed_chars >= line->text_len_chars) {
        d->revealed_chars = line->text_len_chars;
        d->line_complete = 1;
        d->waiting_for_input = 1;
    }
}

/// @brief Advance the dialogue.
void rt_dialogue_advance(void *dlg) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.Advance: expected Viper.Game.Dialogue");
    if (!d)
        return;
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
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.Skip: expected Viper.Game.Dialogue");
    if (!d)
        return;
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
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.IsActive: expected Viper.Game.Dialogue");
    return d ? d->active : 0;
}

/// @brief Check whether the current line of dialogue has been fully displayed.
int8_t rt_dialogue_is_line_complete(void *dlg) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.IsLineComplete: expected Viper.Game.Dialogue");
    return d ? d->line_complete : 0;
}

/// @brief Check whether the typewriter effect has fully revealed all text.
int8_t rt_dialogue_is_finished(void *dlg) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.IsFinished: expected Viper.Game.Dialogue");
    return d ? d->finished : 0;
}

/// @brief Check whether the dialogue is waiting for the player to dismiss it.
int8_t rt_dialogue_is_waiting(void *dlg) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.IsWaiting: expected Viper.Game.Dialogue");
    return d ? d->waiting_for_input : 0;
}

/// @brief Return the count of elements in the dialogue.
int64_t rt_dialogue_get_line_count(void *dlg) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.LineCount: expected Viper.Game.Dialogue");
    return d ? d->line_count : 0;
}

/// @brief Get the current line of the dialogue.
int64_t rt_dialogue_get_current_line(void *dlg) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.CurrentLine: expected Viper.Game.Dialogue");
    return d ? d->current_line : 0;
}

/// @brief Return the name of the current speaker, or empty string if none.
rt_string rt_dialogue_get_speaker(void *dlg) {
    rt_dialogue_impl *d =
        checked_dialogue(dlg, "Dialogue.GetSpeaker: expected Viper.Game.Dialogue");
    if (!d)
        return rt_const_cstr("");
    if (d->current_line >= d->line_count)
        return rt_const_cstr("");
    return rt_const_cstr(d->lines[d->current_line].speaker);
}

//=============================================================================
// Rendering
//=============================================================================

/// @brief Draw the dialogue.
void rt_dialogue_draw(void *dlg, void *canvas) {
    rt_dialogue_impl *d = checked_dialogue(dlg, "Dialogue.Draw: expected Viper.Game.Dialogue");
    if (!d || !canvas)
        return;
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
