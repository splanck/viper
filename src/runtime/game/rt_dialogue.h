//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_dialogue.h
// Purpose: Typewriter text reveal system for RPGs, visual novels, and tutorials.
//   Provides character-by-character reveal, word wrapping, speaker labels,
//   dialogue queue, and immediate-mode Canvas drawing.
//
// Key invariants:
//   - Queue holds up to DLG_MAX_LINES (64) lines.
//   - Text per line limited to DLG_MAX_TEXT_LEN (512) characters.
//   - Speaker name limited to 63 characters.
//   - Update(dt_ms) advances reveal; Advance() skips or moves to next line.
//
// Ownership/Lifetime:
//   - Dialogue objects are GC-managed (rt_obj_new_i64).
//
// Links: src/runtime/game/rt_dialogue.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Construction
/// @brief Create a dialogue box widget at (x, y) sized (width × height) with default styling.
void *rt_dialogue_new(int64_t x, int64_t y, int64_t width, int64_t height);

// Configuration
/// @brief Set the typewriter reveal speed (characters per second; <=0 reveals instantly).
void rt_dialogue_set_speed(void *dlg, int64_t chars_per_second);
/// @brief Bind a custom BitmapFont (NULL falls back to built-in 8×8).
void rt_dialogue_set_font(void *dlg, void *font);
/// @brief Set the body-text color.
void rt_dialogue_set_text_color(void *dlg, int64_t color);
/// @brief Set the speaker-name label color.
void rt_dialogue_set_speaker_color(void *dlg, int64_t color);
/// @brief Set background fill color and per-pixel alpha (0–255).
void rt_dialogue_set_bg_color(void *dlg, int64_t color, int64_t alpha);
/// @brief Set the border color (-1 disables the border).
void rt_dialogue_set_border_color(void *dlg, int64_t color);
/// @brief Set inner padding in pixels between the border and the text.
void rt_dialogue_set_padding(void *dlg, int64_t padding);
/// @brief Set integer text scale (1 = native, 2 = double-size, etc.).
void rt_dialogue_set_text_scale(void *dlg, int64_t scale);
/// @brief Move the dialogue box to (x, y).
void rt_dialogue_set_pos(void *dlg, int64_t x, int64_t y);
/// @brief Resize the dialogue box.
void rt_dialogue_set_size(void *dlg, int64_t w, int64_t h);

// Dialogue queue
/// @brief Queue a line of text spoken by @p speaker (the speaker label appears above the text).
void rt_dialogue_say(void *dlg, rt_string speaker, rt_string text);
/// @brief Queue a line of text with no speaker label.
void rt_dialogue_say_text(void *dlg, rt_string text);
/// @brief Discard all queued lines and reset the typewriter.
void rt_dialogue_clear(void *dlg);

// Playback
/// @brief Advance the typewriter by @p dt_ms milliseconds (call once per frame).
void rt_dialogue_update(void *dlg, int64_t dt_ms);
/// @brief Move to the next queued line; if the current line isn't complete, complete it first.
void rt_dialogue_advance(void *dlg);
/// @brief Instantly reveal all characters of the current line (skip typewriter animation).
void rt_dialogue_skip(void *dlg);

// State queries
/// @brief True if there is at least one line queued or currently being revealed.
int8_t rt_dialogue_is_active(void *dlg);
/// @brief True if the current line has finished its typewriter reveal.
int8_t rt_dialogue_is_line_complete(void *dlg);
/// @brief True if the queue is empty and the last line finished.
int8_t rt_dialogue_is_finished(void *dlg);
/// @brief True if waiting for the user to Advance (line complete, more lines queued).
int8_t rt_dialogue_is_waiting(void *dlg);
/// @brief Number of lines currently in the queue (including the active one).
int64_t rt_dialogue_get_line_count(void *dlg);
/// @brief Index of the line currently being revealed.
int64_t rt_dialogue_get_current_line(void *dlg);
/// @brief Get the speaker name for the active line (empty string if none).
rt_string rt_dialogue_get_speaker(void *dlg);

// Rendering
/// @brief Render the dialogue box and revealed text onto @p canvas.
void rt_dialogue_draw(void *dlg, void *canvas);

#ifdef __cplusplus
}
#endif
