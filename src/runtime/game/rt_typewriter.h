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

/// @brief Create an empty typewriter (call `_say` to begin revealing text).
rt_typewriter rt_typewriter_new(void);
/// @brief Free the typewriter and any owned text (also reclaimed by GC).
void rt_typewriter_destroy(void *tw);

/// @brief Begin revealing @p text one character at a time, one char every @p rate_ms milliseconds.
void rt_typewriter_say(void *tw, const char *text, int64_t rate_ms);

/// @brief Advance by @p dt milliseconds. Returns 1 on the frame the full text is revealed.
int8_t rt_typewriter_update(void *tw, int64_t dt);

/// @brief Instantly reveal all remaining characters (skip the typewriter animation).
void rt_typewriter_skip(void *tw);

/// @brief Discard the active text and reset the reveal counter.
void rt_typewriter_reset(void *tw);

/// @brief Get the currently-revealed prefix of the active text (empty until first Update tick).
rt_string rt_typewriter_get_visible_text(void *tw);
/// @brief Get the complete text (revealed and pending).
rt_string rt_typewriter_get_full_text(void *tw);
/// @brief True if any text is currently being revealed (between Say and full reveal).
int8_t rt_typewriter_is_active(void *tw);
/// @brief True if all characters have been revealed.
int8_t rt_typewriter_is_complete(void *tw);
/// @brief Reveal progress as a percentage (0–100).
int64_t rt_typewriter_progress(void *tw);
/// @brief Number of characters revealed so far.
int64_t rt_typewriter_char_count(void *tw);
/// @brief Total characters in the active text.
int64_t rt_typewriter_total_chars(void *tw);

#ifdef __cplusplus
}
#endif
