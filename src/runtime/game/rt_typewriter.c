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
//   - char_index tracks how many characters are revealed (0 to strlen).
//   - Update() returns 1 on the exact frame char_index reaches strlen.
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
#include <stdlib.h>
#include <string.h>

struct rt_typewriter_impl
{
    char *full_text;
    char *visible_buf;
    int64_t total_len;
    int64_t char_index;
    int64_t rate_ms;
    int64_t accum;
    int8_t active;
    int8_t complete;
};

rt_typewriter rt_typewriter_new(void)
{
    struct rt_typewriter_impl *t =
        (struct rt_typewriter_impl *)rt_obj_new_i64(
            0, (int64_t)sizeof(struct rt_typewriter_impl));
    if (!t)
        return NULL;

    t->full_text = NULL;
    t->visible_buf = NULL;
    t->total_len = 0;
    t->char_index = 0;
    t->rate_ms = 30;
    t->accum = 0;
    t->active = 0;
    t->complete = 0;

    return t;
}

void rt_typewriter_destroy(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t)
        return;
    free(t->full_text);
    free(t->visible_buf);
    if (rt_obj_release_check0(t))
        rt_obj_free(t);
}

void rt_typewriter_say(void *tw, const char *text, int64_t rate_ms)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t)
        return;

    free(t->full_text);
    free(t->visible_buf);

    if (!text)
        text = "";

    t->full_text = strdup(text);
    t->total_len = (int64_t)strlen(text);
    t->visible_buf = (char *)calloc((size_t)(t->total_len + 1), 1);
    t->char_index = 0;
    t->rate_ms = (rate_ms > 0) ? rate_ms : 30;
    t->accum = 0;
    t->active = 1;
    t->complete = 0;
}

int8_t rt_typewriter_update(void *tw, int64_t dt)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t || !t->active || t->complete)
        return 0;

    t->accum += dt;

    int8_t just_completed = 0;
    while (t->accum >= t->rate_ms && t->char_index < t->total_len)
    {
        t->accum -= t->rate_ms;
        t->visible_buf[t->char_index] = t->full_text[t->char_index];
        t->char_index++;
        t->visible_buf[t->char_index] = '\0';

        if (t->char_index >= t->total_len)
        {
            t->complete = 1;
            t->active = 0;
            just_completed = 1;
        }
    }

    return just_completed;
}

void rt_typewriter_skip(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t || !t->full_text)
        return;

    if (t->visible_buf && t->full_text)
    {
        memcpy(t->visible_buf, t->full_text, (size_t)t->total_len);
        t->visible_buf[t->total_len] = '\0';
    }
    t->char_index = t->total_len;
    t->complete = 1;
    t->active = 0;
}

void rt_typewriter_reset(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t)
        return;
    free(t->full_text);
    free(t->visible_buf);
    t->full_text = NULL;
    t->visible_buf = NULL;
    t->total_len = 0;
    t->char_index = 0;
    t->accum = 0;
    t->active = 0;
    t->complete = 0;
}

rt_string rt_typewriter_get_visible_text(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t || !t->visible_buf)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(t->visible_buf, (int64_t)strlen(t->visible_buf));
}

rt_string rt_typewriter_get_full_text(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t || !t->full_text)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(t->full_text, (int64_t)strlen(t->full_text));
}

int8_t rt_typewriter_is_active(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    return (t && t->active) ? 1 : 0;
}

int8_t rt_typewriter_is_complete(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    return (t && t->complete) ? 1 : 0;
}

int64_t rt_typewriter_progress(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    if (!t || t->total_len == 0)
        return 0;
    return t->char_index * 100 / t->total_len;
}

int64_t rt_typewriter_char_count(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    return t ? t->char_index : 0;
}

int64_t rt_typewriter_total_chars(void *tw)
{
    struct rt_typewriter_impl *t = (struct rt_typewriter_impl *)tw;
    return t ? t->total_len : 0;
}
