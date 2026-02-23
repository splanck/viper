//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_keychord.c
// Purpose: Key chord and sequential combo detection for the Viper input system.
//   Chords require all specified keys held simultaneously; they trigger on the
//   frame the last key is pressed (edge detection). Combos require keys pressed
//   in order within a configurable frame window; they trigger when the final key
//   in the sequence is pressed within the window. Both types are named and
//   stored in a growable array within a GC-managed KeyChord object.
//
// Key invariants:
//   - Chord trigger fires on exactly one frame (the press frame of the last key);
//     it is cleared on the next rt_keychord_update() call.
//   - Combo progress resets if any key in the sequence is pressed out of order
//     or if the inter-key gap exceeds the configured window_frames.
//   - Entry names must be unique within a KeyChord instance; duplicate AddChord/
//     AddCombo calls with the same name are silently ignored.
//   - KC_MAX_KEYS (16) is the maximum number of keys in a single chord or combo.
//   - rt_keychord_update() increments the internal frame counter; it must be
//     called once per frame before querying trigger state.
//
// Ownership/Lifetime:
//   - rt_keychord_impl is allocated via rt_obj_new_i64 (GC heap); the entries
//     array and per-entry name strings are malloc'd and freed in kc_finalizer,
//     which is registered as the GC finalizer at creation.
//
// Links: src/runtime/graphics/rt_keychord.h (public API),
//        src/runtime/graphics/rt_input.h (keyboard state queries),
//        src/runtime/graphics/rt_action.c (action mapping uses BIND_CHORD)
//
//===----------------------------------------------------------------------===//

#include "rt_keychord.h"

#include "rt_input.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#define KC_MAX_KEYS 16
#define KC_INITIAL_CAPACITY 8

typedef enum
{
    KC_TYPE_CHORD = 0,
    KC_TYPE_COMBO = 1
} kc_type;

typedef struct
{
    char *name;
    kc_type type;
    int64_t keys[KC_MAX_KEYS];
    int64_t key_count;
    int64_t window_frames;
    /* Chord state */
    int8_t was_active;
    int8_t is_active;
    int8_t triggered;
    /* Combo state */
    int64_t combo_index;
    int64_t last_match_frame;
} kc_entry;

typedef struct
{
    void *vptr;
    kc_entry *entries;
    int64_t count;
    int64_t capacity;
    int64_t frame_counter;
} rt_keychord_impl;

static kc_entry *find_entry(rt_keychord_impl *kc, const char *name)
{
    int64_t i;
    for (i = 0; i < kc->count; i++)
    {
        if (strcmp(kc->entries[i].name, name) == 0)
            return &kc->entries[i];
    }
    return NULL;
}

static void kc_finalizer(void *obj)
{
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    if (kc)
    {
        int64_t i;
        for (i = 0; i < kc->count; i++)
        {
            free(kc->entries[i].name);
        }
        free(kc->entries);
        kc->entries = NULL;
        kc->count = 0;
    }
}

static void ensure_capacity(rt_keychord_impl *kc)
{
    if (kc->count < kc->capacity)
        return;
    int64_t new_cap = kc->capacity * 2;
    kc_entry *new_entries = (kc_entry *)realloc(kc->entries, (size_t)new_cap * sizeof(kc_entry));
    if (!new_entries)
    {
        rt_trap("KeyChord: memory allocation failed");
        return;
    }
    kc->entries = new_entries;
    kc->capacity = new_cap;
}

static void add_entry(
    rt_keychord_impl *kc, const char *name, kc_type type, void *keys, int64_t window_frames)
{
    int64_t key_count = rt_seq_len(keys);
    if (key_count <= 0 || key_count > KC_MAX_KEYS)
        return;

    /* Remove existing entry with same name */
    kc_entry *existing = find_entry(kc, name);
    if (existing)
    {
        free(existing->name);
        /* Shift entries to remove gap */
        int64_t idx = (int64_t)(existing - kc->entries);
        int64_t i;
        for (i = idx; i < kc->count - 1; i++)
        {
            kc->entries[i] = kc->entries[i + 1];
        }
        kc->count--;
    }

    ensure_capacity(kc);

    kc_entry *e = &kc->entries[kc->count];
    memset(e, 0, sizeof(kc_entry));

    size_t name_len = strlen(name);
    e->name = (char *)malloc(name_len + 1);
    if (!e->name)
        return;
    memcpy(e->name, name, name_len + 1);

    e->type = type;
    e->key_count = key_count;
    e->window_frames = window_frames;

    {
        int64_t i;
        for (i = 0; i < key_count; i++)
        {
            void *item = rt_seq_get(keys, i);
            e->keys[i] = (int64_t)(intptr_t)item;
        }
    }

    kc->count++;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_keychord_new(void)
{
    rt_keychord_impl *kc = (rt_keychord_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_keychord_impl));
    if (!kc)
    {
        rt_trap("KeyChord: memory allocation failed");
        return NULL;
    }
    kc->vptr = NULL;
    kc->count = 0;
    kc->capacity = KC_INITIAL_CAPACITY;
    kc->frame_counter = 0;
    kc->entries = (kc_entry *)calloc((size_t)KC_INITIAL_CAPACITY, sizeof(kc_entry));
    if (!kc->entries)
    {
        rt_trap("KeyChord: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(kc, kc_finalizer);
    return kc;
}

void rt_keychord_define(void *obj, rt_string name, void *keys)
{
    if (!obj || !keys)
        return;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return;
    add_entry(kc, cstr, KC_TYPE_CHORD, keys, 0);
}

void rt_keychord_define_combo(void *obj, rt_string name, void *keys, int64_t window_frames)
{
    if (!obj || !keys)
        return;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return;
    if (window_frames <= 0)
        window_frames = 15; /* default ~250ms at 60fps */
    add_entry(kc, cstr, KC_TYPE_COMBO, keys, window_frames);
}

void rt_keychord_update(void *obj)
{
    if (!obj)
        return;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    int64_t i;

    kc->frame_counter++;

    for (i = 0; i < kc->count; i++)
    {
        kc_entry *e = &kc->entries[i];
        e->triggered = 0;

        if (e->type == KC_TYPE_CHORD)
        {
            /* Check if all chord keys are currently held */
            int8_t all_down = 1;
            int8_t any_just_pressed = 0;
            int64_t k;
            for (k = 0; k < e->key_count; k++)
            {
                if (!rt_keyboard_is_down(e->keys[k]))
                {
                    all_down = 0;
                    break;
                }
                if (rt_keyboard_was_pressed(e->keys[k]))
                    any_just_pressed = 1;
            }

            e->was_active = e->is_active;
            e->is_active = all_down;

            /* Trigger on the frame the chord becomes active */
            if (all_down && (!e->was_active || any_just_pressed))
                e->triggered = 1;
        }
        else /* KC_TYPE_COMBO */
        {
            /* Check for timeout */
            if (e->combo_index > 0 && (kc->frame_counter - e->last_match_frame) > e->window_frames)
            {
                e->combo_index = 0;
            }

            /* Check if the next expected key was pressed this frame */
            if (e->combo_index < e->key_count)
            {
                int64_t expected_key = e->keys[e->combo_index];
                if (rt_keyboard_was_pressed(expected_key))
                {
                    e->combo_index++;
                    e->last_match_frame = kc->frame_counter;

                    if (e->combo_index >= e->key_count)
                    {
                        e->triggered = 1;
                        e->combo_index = 0;
                    }
                }
            }
        }
    }
}

int8_t rt_keychord_active(void *obj, rt_string name)
{
    if (!obj)
        return 0;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;
    kc_entry *e = find_entry(kc, cstr);
    if (!e)
        return 0;
    return e->is_active;
}

int8_t rt_keychord_triggered(void *obj, rt_string name)
{
    if (!obj)
        return 0;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;
    kc_entry *e = find_entry(kc, cstr);
    if (!e)
        return 0;
    return e->triggered;
}

int64_t rt_keychord_progress(void *obj, rt_string name)
{
    if (!obj)
        return 0;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;
    kc_entry *e = find_entry(kc, cstr);
    if (!e)
        return 0;
    if (e->type == KC_TYPE_CHORD)
        return e->is_active ? e->key_count : 0;
    return e->combo_index;
}

int8_t rt_keychord_remove(void *obj, rt_string name)
{
    if (!obj)
        return 0;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    const char *cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;
    kc_entry *e = find_entry(kc, cstr);
    if (!e)
        return 0;

    free(e->name);
    int64_t idx = (int64_t)(e - kc->entries);
    int64_t j;
    for (j = idx; j < kc->count - 1; j++)
    {
        kc->entries[j] = kc->entries[j + 1];
    }
    kc->count--;
    return 1;
}

void rt_keychord_clear(void *obj)
{
    if (!obj)
        return;
    rt_keychord_impl *kc = (rt_keychord_impl *)obj;
    int64_t i;
    for (i = 0; i < kc->count; i++)
    {
        free(kc->entries[i].name);
    }
    kc->count = 0;
}

int64_t rt_keychord_count(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_keychord_impl *)obj)->count;
}
