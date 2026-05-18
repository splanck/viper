//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_typewriter.c
// Purpose: Character-by-character text reveal effect. Accumulates ms via
//   Update(dt), revealing one character per rate_ms interval. GetVisibleText()
//   returns a null-terminated substring of the source text. Skip() reveals all.
//
// Key invariants:
//   - Full text is strdup'd; visible buffer is a separate copy.
//   - char_index tracks revealed bytes; visible_chars tracks revealed UTF-8 codepoints.
//   - Update() returns 1 on the exact frame the byte index reaches strlen.
//   - After completion, is_complete=1 and further Update() calls are no-ops.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64. Text buffers are heap-allocated.
//
// Links: src/runtime/game/rt_typewriter.h
//
//===----------------------------------------------------------------------===//

#include "rt_typewriter.h"
#include "rt_object.h"
#include "rt_trap.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct rt_typewriter_impl {
    char *full_text;
    char *visible_buf;
    int64_t total_len;
    int64_t char_index;
    int64_t total_chars;
    int64_t visible_chars;
    int64_t rate_ms;
    int64_t accum;
    int8_t active;
    int8_t complete;
};

/// @brief Safe-cast a handle to the Typewriter impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p tw is NULL.
static struct rt_typewriter_impl *checked_typewriter(void *tw, const char *api) {
    if (!tw)
        return NULL;
    if (rt_obj_class_id(tw) != RT_TYPEWRITER_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (struct rt_typewriter_impl *)tw;
}

/// @brief GC finalizer: free the full-text and visible-text buffers.
static void typewriter_finalizer(void *obj) {
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)obj;
    if (!t)
        return;
    free(t->full_text);
    free(t->visible_buf);
    t->full_text = NULL;
    t->visible_buf = NULL;
}

/// @brief True if @p c is a UTF-8 continuation byte (10xxxxxx).
static int8_t typewriter_is_utf8_continuation(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

/// @brief Byte length (1–4) of the UTF-8 codepoint starting at @p index.
/// @details Validates continuation bytes; malformed sequences fall back to 1
///          so the typewriter never stalls. 0 if @p index is out of range.
static int64_t typewriter_utf8_codepoint_len(const char *text, int64_t index, int64_t len) {
    if (!text || index < 0 || index >= len)
        return 0;

    const unsigned char c0 = (unsigned char)text[index];
    if (c0 < 0x80u)
        return 1;
    if ((c0 & 0xE0u) == 0xC0u && index + 1 < len &&
        typewriter_is_utf8_continuation((unsigned char)text[index + 1]))
        return 2;
    if ((c0 & 0xF0u) == 0xE0u && index + 2 < len &&
        typewriter_is_utf8_continuation((unsigned char)text[index + 1]) &&
        typewriter_is_utf8_continuation((unsigned char)text[index + 2]))
        return 3;
    if ((c0 & 0xF8u) == 0xF0u && index + 3 < len &&
        typewriter_is_utf8_continuation((unsigned char)text[index + 1]) &&
        typewriter_is_utf8_continuation((unsigned char)text[index + 2]) &&
        typewriter_is_utf8_continuation((unsigned char)text[index + 3]))
        return 4;

    return 1;
}

/// @brief Count UTF-8 codepoints (not bytes) in the first @p len bytes of
///        @p text — used so reveal speed is per-character.
static int64_t typewriter_utf8_char_count(const char *text, int64_t len) {
    int64_t count = 0;
    for (int64_t i = 0; i < len;) {
        int64_t char_len = typewriter_utf8_codepoint_len(text, i, len);
        if (char_len <= 0)
            break;
        i += char_len;
        count++;
    }
    return count;
}

/// @brief Create a new typewriter text-reveal effect.
/// @details Reveals text one character at a time at a configurable rate, producing
///          the classic RPG dialogue effect. Use say() to load text, update() each
///          frame, and get_visible_text() to read the partially-revealed string.
rt_typewriter rt_typewriter_new(void) {
    struct rt_typewriter_impl *t =
        (struct rt_typewriter_impl *)rt_obj_new_i64(RT_TYPEWRITER_CLASS_ID,
                                                    (int64_t)sizeof(struct rt_typewriter_impl));
    if (!t)
        return NULL;

    t->full_text = NULL;
    t->visible_buf = NULL;
    t->total_len = 0;
    t->char_index = 0;
    t->total_chars = 0;
    t->visible_chars = 0;
    t->rate_ms = 30;
    t->accum = 0;
    t->active = 0;
    t->complete = 0;
    rt_obj_set_finalizer(t, typewriter_finalizer);

    return t;
}

/// @brief Destroy a typewriter and free its text buffers.
void rt_typewriter_destroy(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.Destroy: expected Viper.Game.Typewriter");
    if (t && rt_obj_release_check0(t))
        rt_obj_free(t);
}

/// @brief Load new text and begin the typewriter reveal at the given character rate.
void rt_typewriter_say(void *tw, const char *text, int64_t rate_ms) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.Say: expected Viper.Game.Typewriter");
    if (!t)
        return;

    if (!text)
        text = "";

    size_t len = strlen(text);
    if (len > (size_t)INT64_MAX) {
        rt_trap("Typewriter.Say: text too large");
        return;
    }

    char *full = (char *)malloc(len + 1);
    char *visible = (char *)calloc(len + 1, 1);
    if (!full || !visible) {
        free(full);
        free(visible);
        rt_trap("Typewriter.Say: allocation failed");
        return;
    }
    memcpy(full, text, len + 1);

    free(t->full_text);
    free(t->visible_buf);
    t->full_text = full;
    t->total_len = (int64_t)len;
    t->total_chars = typewriter_utf8_char_count(full, t->total_len);
    t->visible_buf = visible;
    t->char_index = 0;
    t->visible_chars = 0;
    t->rate_ms = (rate_ms > 0) ? rate_ms : 30;
    t->accum = 0;
    t->active = 1;
    t->complete = 0;
    if (t->total_len == 0) {
        t->active = 0;
        t->complete = 1;
    }
}

/// @brief Advance the typewriter by dt milliseconds. Returns 1 if it just completed.
int8_t rt_typewriter_update(void *tw, int64_t dt) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.Update: expected Viper.Game.Typewriter");
    if (!t || !t->active || t->complete)
        return 0;
    if (dt <= 0 || !t->visible_buf || !t->full_text)
        return 0;

    if (t->accum > INT64_MAX - dt)
        t->accum = INT64_MAX;
    else
        t->accum += dt;

    int8_t just_completed = 0;
    while (t->accum >= t->rate_ms && t->char_index < t->total_len) {
        t->accum -= t->rate_ms;
        int64_t char_len = typewriter_utf8_codepoint_len(t->full_text, t->char_index, t->total_len);
        if (char_len <= 0)
            break;
        memcpy(t->visible_buf + t->char_index, t->full_text + t->char_index, (size_t)char_len);
        t->char_index += char_len;
        t->visible_chars++;
        t->visible_buf[t->char_index] = '\0';

        if (t->char_index >= t->total_len) {
            t->complete = 1;
            t->active = 0;
            just_completed = 1;
        }
    }

    return just_completed;
}

/// @brief Instantly reveal all remaining text (skip the animation).
void rt_typewriter_skip(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.Skip: expected Viper.Game.Typewriter");
    if (!t || !t->full_text)
        return;

    if (t->visible_buf && t->full_text) {
        memcpy(t->visible_buf, t->full_text, (size_t)t->total_len);
        t->visible_buf[t->total_len] = '\0';
    }
    t->char_index = t->total_len;
    t->visible_chars = t->total_chars;
    t->complete = 1;
    t->active = 0;
}

/// @brief Clear all text and reset to the initial idle state.
void rt_typewriter_reset(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.Reset: expected Viper.Game.Typewriter");
    if (!t)
        return;
    free(t->full_text);
    free(t->visible_buf);
    t->full_text = NULL;
    t->visible_buf = NULL;
    t->total_len = 0;
    t->char_index = 0;
    t->total_chars = 0;
    t->visible_chars = 0;
    t->accum = 0;
    t->active = 0;
    t->complete = 0;
}

/// @brief Get the currently revealed portion of the text.
rt_string rt_typewriter_get_visible_text(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.GetVisibleText: expected Viper.Game.Typewriter");
    if (!t || !t->visible_buf)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(t->visible_buf, (int64_t)strlen(t->visible_buf));
}

/// @brief Get the complete text that was loaded with say().
rt_string rt_typewriter_get_full_text(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.GetFullText: expected Viper.Game.Typewriter");
    if (!t || !t->full_text)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(t->full_text, (int64_t)strlen(t->full_text));
}

/// @brief Check whether the typewriter is currently revealing text.
int8_t rt_typewriter_is_active(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.IsActive: expected Viper.Game.Typewriter");
    return (t && t->active) ? 1 : 0;
}

/// @brief Check whether all text has been fully revealed.
int8_t rt_typewriter_is_complete(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.IsComplete: expected Viper.Game.Typewriter");
    return (t && t->complete) ? 1 : 0;
}

/// @brief Get the reveal progress as a percentage (0–100).
int64_t rt_typewriter_progress(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.Progress: expected Viper.Game.Typewriter");
    if (!t)
        return 0;
    if (t->total_chars == 0)
        return t->complete ? 100 : 0;
    long double pct = ((long double)t->visible_chars * 100.0L) / (long double)t->total_chars;
    if (pct >= 100.0L)
        return 100;
    return pct <= 0.0L ? 0 : (int64_t)pct;
}

/// @brief Get the number of characters revealed so far.
int64_t rt_typewriter_char_count(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.CharCount: expected Viper.Game.Typewriter");
    return t ? t->visible_chars : 0;
}

/// @brief Get the total number of characters in the loaded text.
int64_t rt_typewriter_total_chars(void *tw) {
    struct rt_typewriter_impl *t =
        checked_typewriter(tw, "Typewriter.TotalChars: expected Viper.Game.Typewriter");
    return t ? t->total_chars : 0;
}
