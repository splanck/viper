//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_typewriter.h
// Purpose: Character-by-character text reveal effect for dialogue, lore,
//   tutorials, and narrative text.
//
// Key invariants:
//   - Reveal rate is configurable in ms per character.
//   - Skip() instantly reveals all remaining text.
//   - GetVisibleText() returns the currently revealed portion.
//   - Update() returns 1 on the frame the full text is revealed.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64. Text is strdup'd.
//
// Links: src/runtime/game/rt_typewriter.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_typewriter_impl *rt_typewriter;

rt_typewriter rt_typewriter_new(void);
void rt_typewriter_destroy(void *tw);

// Start revealing text
void rt_typewriter_say(void *tw, const char *text, int64_t rate_ms);

// Per-frame update — returns 1 on the frame text becomes fully revealed
int8_t rt_typewriter_update(void *tw, int64_t dt);

// Skip to end (reveal all)
void rt_typewriter_skip(void *tw);

// Reset (clear text and state)
void rt_typewriter_reset(void *tw);

// Queries (return rt_string for Viper runtime compatibility)
rt_string rt_typewriter_get_visible_text(void *tw);
rt_string rt_typewriter_get_full_text(void *tw);
int8_t rt_typewriter_is_active(void *tw);
int8_t rt_typewriter_is_complete(void *tw);
int64_t rt_typewriter_progress(void *tw);
int64_t rt_typewriter_char_count(void *tw);
int64_t rt_typewriter_total_chars(void *tw);

#ifdef __cplusplus
}
#endif
