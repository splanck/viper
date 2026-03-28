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
void *rt_dialogue_new(int64_t x, int64_t y, int64_t width, int64_t height);

// Configuration
void rt_dialogue_set_speed(void *dlg, int64_t chars_per_second);
void rt_dialogue_set_font(void *dlg, void *font);
void rt_dialogue_set_text_color(void *dlg, int64_t color);
void rt_dialogue_set_speaker_color(void *dlg, int64_t color);
void rt_dialogue_set_bg_color(void *dlg, int64_t color, int64_t alpha);
void rt_dialogue_set_border_color(void *dlg, int64_t color);
void rt_dialogue_set_padding(void *dlg, int64_t padding);
void rt_dialogue_set_text_scale(void *dlg, int64_t scale);
void rt_dialogue_set_pos(void *dlg, int64_t x, int64_t y);
void rt_dialogue_set_size(void *dlg, int64_t w, int64_t h);

// Dialogue queue
void rt_dialogue_say(void *dlg, rt_string speaker, rt_string text);
void rt_dialogue_say_text(void *dlg, rt_string text);
void rt_dialogue_clear(void *dlg);

// Playback
void rt_dialogue_update(void *dlg, int64_t dt_ms);
void rt_dialogue_advance(void *dlg);
void rt_dialogue_skip(void *dlg);

// State queries
int8_t rt_dialogue_is_active(void *dlg);
int8_t rt_dialogue_is_line_complete(void *dlg);
int8_t rt_dialogue_is_finished(void *dlg);
int8_t rt_dialogue_is_waiting(void *dlg);
int64_t rt_dialogue_get_line_count(void *dlg);
int64_t rt_dialogue_get_current_line(void *dlg);
rt_string rt_dialogue_get_speaker(void *dlg);

// Rendering
void rt_dialogue_draw(void *dlg, void *canvas);

#ifdef __cplusplus
}
#endif
