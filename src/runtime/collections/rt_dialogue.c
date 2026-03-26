//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_dialogue.c
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
#define DLG_CHAR_WIDTH 8
#define DLG_LINE_HEIGHT 10

typedef struct
{
    char speaker[64];
    char text[DLG_MAX_TEXT_LEN];
    int32_t text_len;
} dlg_line;

typedef struct
{
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

//=============================================================================
// Construction
//=============================================================================

void *rt_dialogue_new(int64_t x, int64_t y, int64_t width, int64_t height)
{
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
    return d;
}

//=============================================================================
// Configuration
//=============================================================================

/// @brief Perform dialogue set speed operation.
/// @param dlg
/// @param cps
void rt_dialogue_set_speed(void *dlg, int64_t cps)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->chars_per_second = cps < 1 ? 1 : (int32_t)cps;
}

/// @brief Perform dialogue set font operation.
/// @param dlg
/// @param font
void rt_dialogue_set_font(void *dlg, void *font)
{
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

/// @brief Perform dialogue set text color operation.
/// @param dlg
/// @param color
void rt_dialogue_set_text_color(void *dlg, int64_t color)
{
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->text_color = (int32_t)color;
}

/// @brief Perform dialogue set speaker color operation.
/// @param dlg
/// @param color
void rt_dialogue_set_speaker_color(void *dlg, int64_t color)
{
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->speaker_color = (int32_t)color;
}

/// @brief Perform dialogue set bg color operation.
/// @param dlg
/// @param color
/// @param alpha
void rt_dialogue_set_bg_color(void *dlg, int64_t color, int64_t alpha)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->bg_color = (int32_t)color;
    d->bg_alpha = (int32_t)alpha;
}

/// @brief Perform dialogue set border color operation.
/// @param dlg
/// @param color
void rt_dialogue_set_border_color(void *dlg, int64_t color)
{
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->border_color = (int32_t)color;
}

/// @brief Perform dialogue set padding operation.
/// @param dlg
/// @param padding
void rt_dialogue_set_padding(void *dlg, int64_t padding)
{
    if (!dlg)
        return;
    ((rt_dialogue_impl *)dlg)->padding = (int32_t)padding;
}

/// @brief Perform dialogue set text scale operation.
/// @param dlg
/// @param scale
void rt_dialogue_set_text_scale(void *dlg, int64_t scale)
{
    if (!dlg)
        return;
    if (scale < 1)
        scale = 1;
    ((rt_dialogue_impl *)dlg)->text_scale = (int32_t)scale;
}

/// @brief Perform dialogue set pos operation.
/// @param dlg
/// @param x
/// @param y
void rt_dialogue_set_pos(void *dlg, int64_t x, int64_t y)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->x = (int32_t)x;
    d->y = (int32_t)y;
}

/// @brief Perform dialogue set size operation.
/// @param dlg
/// @param w
/// @param h
void rt_dialogue_set_size(void *dlg, int64_t w, int64_t h)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    d->width = (int32_t)w;
    d->height = (int32_t)h;
}

//=============================================================================
// Dialogue Queue
//=============================================================================

/// @brief Perform dialogue say operation.
/// @param dlg
/// @param speaker
/// @param text
void rt_dialogue_say(void *dlg, rt_string speaker, rt_string text)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;

    if (d->line_count >= DLG_MAX_LINES)
    {
        // Shift lines down, drop oldest
        for (int i = 0; i < DLG_MAX_LINES - 1; i++)
            d->lines[i] = d->lines[i + 1];
        d->line_count = DLG_MAX_LINES - 1;
        if (d->current_line > 0)
            d->current_line--;
    }

    dlg_line *line = &d->lines[d->line_count];
    memset(line, 0, sizeof(dlg_line));

    if (speaker)
    {
        const char *sp = rt_string_cstr(speaker);
        if (sp)
        {
            size_t len = strlen(sp);
            if (len > 63)
                len = 63;
            memcpy(line->speaker, sp, len);
        }
    }

    if (text)
    {
        const char *tx = rt_string_cstr(text);
        if (tx)
        {
            size_t len = strlen(tx);
            if (len > DLG_MAX_TEXT_LEN - 1)
                len = DLG_MAX_TEXT_LEN - 1;
            memcpy(line->text, tx, len);
            line->text_len = (int32_t)len;
        }
    }

    d->line_count++;
    if (!d->active)
    {
        d->active = 1;
        d->finished = 0;
        d->current_line = 0;
        d->revealed_chars = 0;
        d->accumulator_us = 0;
        d->line_complete = 0;
        d->waiting_for_input = 0;
    }
}

/// @brief Perform dialogue say text operation.
/// @param dlg
/// @param text
void rt_dialogue_say_text(void *dlg, rt_string text)
{
    rt_dialogue_say(dlg, NULL, text);
}

/// @brief Perform dialogue clear operation.
/// @param dlg
void rt_dialogue_clear(void *dlg)
{
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

/// @brief Perform dialogue update operation.
/// @param dlg
/// @param dt_ms
void rt_dialogue_update(void *dlg, int64_t dt_ms)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active || d->waiting_for_input || d->line_complete)
        return;
    if (d->current_line >= d->line_count)
        return;

    dlg_line *line = &d->lines[d->current_line];

    // Speed == 0 means instant reveal
    if (d->chars_per_second == 0)
    {
        d->revealed_chars = line->text_len;
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
    d->revealed_chars += chars_to_add;

    if (d->revealed_chars >= line->text_len)
    {
        d->revealed_chars = line->text_len;
        d->line_complete = 1;
        d->waiting_for_input = 1;
    }
}

/// @brief Perform dialogue advance operation.
/// @param dlg
void rt_dialogue_advance(void *dlg)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active)
        return;

    if (!d->line_complete)
    {
        // Skip to end of current line
        if (d->current_line < d->line_count)
        {
            d->revealed_chars = d->lines[d->current_line].text_len;
            d->line_complete = 1;
            d->waiting_for_input = 1;
        }
    }
    else
    {
        // Move to next line
        d->current_line++;
        if (d->current_line >= d->line_count)
        {
            d->active = 0;
            d->finished = 1;
        }
        else
        {
            d->revealed_chars = 0;
            d->accumulator_us = 0;
            d->line_complete = 0;
            d->waiting_for_input = 0;
        }
    }
}

/// @brief Perform dialogue skip operation.
/// @param dlg
void rt_dialogue_skip(void *dlg)
{
    if (!dlg)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active || d->current_line >= d->line_count)
        return;
    d->revealed_chars = d->lines[d->current_line].text_len;
    d->line_complete = 1;
    d->waiting_for_input = 1;
}

//=============================================================================
// State Queries
//=============================================================================

/// @brief Perform dialogue is active operation.
/// @param dlg
/// @return Result value.
int8_t rt_dialogue_is_active(void *dlg)
{
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->active;
}

/// @brief Perform dialogue is line complete operation.
/// @param dlg
/// @return Result value.
int8_t rt_dialogue_is_line_complete(void *dlg)
{
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->line_complete;
}

/// @brief Perform dialogue is finished operation.
/// @param dlg
/// @return Result value.
int8_t rt_dialogue_is_finished(void *dlg)
{
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->finished;
}

/// @brief Perform dialogue is waiting operation.
/// @param dlg
/// @return Result value.
int8_t rt_dialogue_is_waiting(void *dlg)
{
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->waiting_for_input;
}

/// @brief Perform dialogue get line count operation.
/// @param dlg
/// @return Result value.
int64_t rt_dialogue_get_line_count(void *dlg)
{
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->line_count;
}

/// @brief Perform dialogue get current line operation.
/// @param dlg
/// @return Result value.
int64_t rt_dialogue_get_current_line(void *dlg)
{
    if (!dlg)
        return 0;
    return ((rt_dialogue_impl *)dlg)->current_line;
}

/// @brief Perform dialogue get speaker operation.
/// @param dlg
/// @return Result value.
rt_string rt_dialogue_get_speaker(void *dlg)
{
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

/// @brief Perform dialogue draw operation.
/// @param dlg
/// @param canvas
void rt_dialogue_draw(void *dlg, void *canvas)
{
    if (!dlg || !canvas)
        return;
    rt_dialogue_impl *d = (rt_dialogue_impl *)dlg;
    if (!d->active || d->current_line >= d->line_count)
        return;

    // Draw background panel
    rt_canvas_box_alpha(canvas, d->x, d->y, d->width, d->height, d->bg_color, d->bg_alpha);
    rt_canvas_frame(canvas, d->x, d->y, d->width, d->height, d->border_color);

    dlg_line *line = &d->lines[d->current_line];
    int32_t scale = d->text_scale;
    int32_t cw = DLG_CHAR_WIDTH * scale;
    int32_t lh = DLG_LINE_HEIGHT * scale;
    int32_t pad = d->padding;
    int32_t text_x = d->x + pad;
    int32_t text_y = d->y + pad;

    // Draw speaker name
    if (line->speaker[0] != '\0')
    {
        rt_string sp = rt_const_cstr(line->speaker);
        rt_canvas_text_scaled(canvas, text_x, text_y, sp, scale, d->speaker_color);
        text_y += lh + 2;
    }

    // Draw revealed text with word wrapping
    int32_t max_x = d->x + d->width - pad;
    int32_t max_y = d->y + d->height - pad;
    int32_t cx = text_x;
    int32_t cy = text_y;
    int32_t shown = d->revealed_chars;
    if (shown > line->text_len)
        shown = line->text_len;

    // Build a temporary string of the revealed portion
    char buf[DLG_MAX_TEXT_LEN + 1];
    if (shown > 0)
        memcpy(buf, line->text, (size_t)shown);
    buf[shown] = '\0';

    // Simple word-wrapping character draw using single-char strings
    for (int32_t i = 0; i < shown; i++)
    {
        // Check word wrap at word boundaries
        if (buf[i] == ' ' && cx > text_x)
        {
            // Look ahead to measure next word
            int32_t wlen = 0;
            for (int32_t j = i + 1; j < shown && buf[j] != ' '; j++)
                wlen++;
            if (cx + (wlen + 1) * cw > max_x)
            {
                cx = text_x;
                cy += lh;
                if (cy + lh > max_y)
                    break;
                continue; // skip the space at wrap point
            }
        }

        if (buf[i] == '\n')
        {
            cx = text_x;
            cy += lh;
            if (cy + lh > max_y)
                break;
            continue;
        }

        if (cy + lh > max_y)
            break;

        // Draw single character (copy to heap to avoid dangling stack pointer)
        char ch_buf[2] = {buf[i], '\0'};
        rt_string ch = rt_string_from_bytes(ch_buf, 1);
        rt_canvas_text_scaled(canvas, cx, cy, ch, scale, d->text_color);
        rt_str_release_maybe(ch);
        cx += cw;

        if (cx >= max_x)
        {
            cx = text_x;
            cy += lh;
        }
    }

    // Draw "..." indicator when waiting
    if (d->waiting_for_input)
    {
        rt_string indicator = rt_const_cstr("...");
        int32_t ind_x = d->x + d->width - pad - 3 * cw;
        int32_t ind_y = d->y + d->height - pad - lh;
        rt_canvas_text_scaled(canvas, ind_x, ind_y, indicator, scale, d->text_color);
    }
}
