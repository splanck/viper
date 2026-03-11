//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_soundbank.c
// Purpose: Named sound registry mapping string names to loaded Sound objects.
//          Supports loading from files or registering synthesized sounds.
//
// Key invariants:
//   - Linear scan over fixed array (max 64 entries) — simple and cache-friendly.
//   - Sound references are retained while stored; released on removal/clear.
//   - Finalizer releases all held references when bank is garbage collected.
//
// Ownership/Lifetime:
//   - Bank is GC-managed (rt_obj_new_i64 + finalizer).
//   - Each stored sound has its refcount incremented by 1.
//
// Links: rt_soundbank.h, rt_audio.h, rt_object.h
//
//===----------------------------------------------------------------------===//

#include "rt_soundbank.h"
#include "rt_audio.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stddef.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Internal Structure
//===----------------------------------------------------------------------===//

#define BANK_MAX_ENTRIES 64
#define BANK_NAME_LEN 32

typedef struct
{
    char name[BANK_NAME_LEN];
    void *sound; /* retained rt_sound wrapper */
    int in_use;
} bank_entry_t;

typedef struct
{
    void *vptr;
    bank_entry_t entries[BANK_MAX_ENTRIES];
    int count;
} rt_soundbank_impl;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

/// @brief Copy an rt_string name into a fixed buffer, truncating if needed.
static void copy_name(char *dst, rt_string name)
{
    const char *src = rt_string_cstr(name);
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= BANK_NAME_LEN)
        len = BANK_NAME_LEN - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/// @brief Find an entry by name. Returns index or -1.
static int find_entry(const rt_soundbank_impl *bank, const char *name)
{
    for (int i = 0; i < BANK_MAX_ENTRIES; i++)
    {
        if (bank->entries[i].in_use && strcmp(bank->entries[i].name, name) == 0)
            return i;
    }
    return -1;
}

/// @brief Find a free slot. Returns index or -1.
static int find_free(const rt_soundbank_impl *bank)
{
    for (int i = 0; i < BANK_MAX_ENTRIES; i++)
    {
        if (!bank->entries[i].in_use)
            return i;
    }
    return -1;
}

/// @brief Release a sound reference in an entry.
static void release_entry(bank_entry_t *entry)
{
    if (entry->sound)
    {
        if (rt_obj_release_check0(entry->sound))
            rt_obj_free(entry->sound);
        entry->sound = NULL;
    }
    entry->in_use = 0;
    entry->name[0] = '\0';
}

//===----------------------------------------------------------------------===//
// Finalizer
//===----------------------------------------------------------------------===//

static void rt_soundbank_finalize(void *obj)
{
    if (!obj)
        return;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)obj;
    for (int i = 0; i < BANK_MAX_ENTRIES; i++)
    {
        if (bank->entries[i].in_use)
            release_entry(&bank->entries[i]);
    }
}

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

void *rt_soundbank_new(void)
{
    rt_soundbank_impl *bank =
        (rt_soundbank_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_soundbank_impl));
    if (!bank)
        return NULL;

    bank->vptr = NULL;
    bank->count = 0;
    memset(bank->entries, 0, sizeof(bank->entries));
    rt_obj_set_finalizer(bank, rt_soundbank_finalize);

    return bank;
}

int64_t rt_soundbank_register(void *bank_ptr, rt_string name, rt_string path)
{
    if (!bank_ptr || !name || !path)
        return 0;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;

    /* Load the sound from file */
    void *sound = rt_sound_load(path);
    if (!sound)
        return 0;

    /* Get name as C string */
    char namebuf[BANK_NAME_LEN];
    copy_name(namebuf, name);

    /* Check if name already exists — replace */
    int idx = find_entry(bank, namebuf);
    if (idx >= 0)
    {
        release_entry(&bank->entries[idx]);
        bank->count--;
    }
    else
    {
        idx = find_free(bank);
        if (idx < 0)
        {
            /* Bank full — free the loaded sound */
            if (rt_obj_release_check0(sound))
                rt_obj_free(sound);
            return 0;
        }
    }

    /* Store: sound already has refcount 1 from rt_sound_load */
    memcpy(bank->entries[idx].name, namebuf, BANK_NAME_LEN);
    bank->entries[idx].sound = sound;
    bank->entries[idx].in_use = 1;
    bank->count++;

    return 1;
}

int64_t rt_soundbank_register_sound(void *bank_ptr, rt_string name, void *sound)
{
    if (!bank_ptr || !name || !sound)
        return 0;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;

    char namebuf[BANK_NAME_LEN];
    copy_name(namebuf, name);

    /* Check if name already exists — replace */
    int idx = find_entry(bank, namebuf);
    if (idx >= 0)
    {
        release_entry(&bank->entries[idx]);
        bank->count--;
    }
    else
    {
        idx = find_free(bank);
        if (idx < 0)
            return 0; /* Bank full */
    }

    /* Retain the sound (caller also holds a reference) */
    rt_obj_retain_maybe(sound);

    memcpy(bank->entries[idx].name, namebuf, BANK_NAME_LEN);
    bank->entries[idx].sound = sound;
    bank->entries[idx].in_use = 1;
    bank->count++;

    return 1;
}

int64_t rt_soundbank_play(void *bank_ptr, rt_string name)
{
    if (!bank_ptr || !name)
        return -1;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;
    char namebuf[BANK_NAME_LEN];
    copy_name(namebuf, name);

    int idx = find_entry(bank, namebuf);
    if (idx < 0)
        return -1;

    return rt_sound_play(bank->entries[idx].sound);
}

int64_t rt_soundbank_play_ex(void *bank_ptr, rt_string name, int64_t volume, int64_t pan)
{
    if (!bank_ptr || !name)
        return -1;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;
    char namebuf[BANK_NAME_LEN];
    copy_name(namebuf, name);

    int idx = find_entry(bank, namebuf);
    if (idx < 0)
        return -1;

    return rt_sound_play_ex(bank->entries[idx].sound, volume, pan);
}

int64_t rt_soundbank_has(void *bank_ptr, rt_string name)
{
    if (!bank_ptr || !name)
        return 0;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;
    char namebuf[BANK_NAME_LEN];
    copy_name(namebuf, name);

    return find_entry(bank, namebuf) >= 0 ? 1 : 0;
}

void *rt_soundbank_get(void *bank_ptr, rt_string name)
{
    if (!bank_ptr || !name)
        return NULL;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;
    char namebuf[BANK_NAME_LEN];
    copy_name(namebuf, name);

    int idx = find_entry(bank, namebuf);
    if (idx < 0)
        return NULL;

    return bank->entries[idx].sound;
}

void rt_soundbank_remove(void *bank_ptr, rt_string name)
{
    if (!bank_ptr || !name)
        return;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;
    char namebuf[BANK_NAME_LEN];
    copy_name(namebuf, name);

    int idx = find_entry(bank, namebuf);
    if (idx < 0)
        return;

    release_entry(&bank->entries[idx]);
    bank->count--;
}

void rt_soundbank_clear(void *bank_ptr)
{
    if (!bank_ptr)
        return;

    rt_soundbank_impl *bank = (rt_soundbank_impl *)bank_ptr;
    for (int i = 0; i < BANK_MAX_ENTRIES; i++)
    {
        if (bank->entries[i].in_use)
            release_entry(&bank->entries[i]);
    }
    bank->count = 0;
}

int64_t rt_soundbank_count(void *bank_ptr)
{
    if (!bank_ptr)
        return 0;

    return ((rt_soundbank_impl *)bank_ptr)->count;
}
