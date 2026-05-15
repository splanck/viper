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
//   - Sound references and key strings are retained while stored; released on removal/clear.
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
#include <stdint.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Internal Structure
//===----------------------------------------------------------------------===//

#define BANK_MAX_ENTRIES 64
#define RT_SOUNDBANK_CLASS_ID INT64_C(-0x730101)
typedef struct {
    rt_string name;
    void *sound; /* retained rt_sound wrapper */
    int in_use;
} bank_entry_t;

typedef struct {
    void *vptr;
    bank_entry_t entries[BANK_MAX_ENTRIES];
    int count;
} rt_soundbank_impl;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

/// @brief Find an entry by name. Returns index or -1.
static int find_entry(const rt_soundbank_impl *bank, rt_string name) {
    for (int i = 0; i < BANK_MAX_ENTRIES; i++) {
        if (bank->entries[i].in_use && rt_str_eq(bank->entries[i].name, name))
            return i;
    }
    return -1;
}

/// @brief Find a free slot. Returns index or -1.
static int find_free(const rt_soundbank_impl *bank) {
    for (int i = 0; i < BANK_MAX_ENTRIES; i++) {
        if (!bank->entries[i].in_use)
            return i;
    }
    return -1;
}

static rt_soundbank_impl *as_soundbank(void *bank_ptr) {
    if (rt_obj_class_id(bank_ptr) != RT_SOUNDBANK_CLASS_ID)
        return NULL;
    return (rt_soundbank_impl *)bank_ptr;
}

/// @brief Release a sound reference in an entry.
static void release_entry(bank_entry_t *entry) {
    rt_str_release_maybe(entry->name);
    entry->name = NULL;
    if (entry->sound) {
        if (rt_obj_release_check0(entry->sound))
            rt_obj_free(entry->sound);
        entry->sound = NULL;
    }
    entry->in_use = 0;
}

//===----------------------------------------------------------------------===//
// Finalizer
//===----------------------------------------------------------------------===//

/// @brief GC finalizer: release every in-use entry's name + sound reference.
/// @details Installed by @ref rt_soundbank_new so when the SoundBank's
///          refcount hits zero, every named sound it owns is properly
///          released and not leaked.
static void rt_soundbank_finalize(void *obj) {
    if (!obj)
        return;

    rt_soundbank_impl *bank = as_soundbank(obj);
    if (!bank)
        return;
    for (int i = 0; i < BANK_MAX_ENTRIES; i++) {
        if (bank->entries[i].in_use)
            release_entry(&bank->entries[i]);
    }
}

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

/// @brief Create a new sound bank for organizing named sound effects.
/// @details A sound bank maps string names to loaded Sound handles, enabling
///          sounds to be played by name (e.g., bank.Play("jump")). Supports
///          up to BANK_MAX_ENTRIES sounds.
void *rt_soundbank_new(void) {
    rt_soundbank_impl *bank =
        (rt_soundbank_impl *)rt_obj_new_i64(RT_SOUNDBANK_CLASS_ID, (int64_t)sizeof(rt_soundbank_impl));
    if (!bank)
        return NULL;

    bank->vptr = NULL;
    bank->count = 0;
    memset(bank->entries, 0, sizeof(bank->entries));
    rt_obj_set_finalizer(bank, rt_soundbank_finalize);

    return bank;
}

/// @brief Load a sound from a file path and register it under the given name.
int64_t rt_soundbank_register(void *bank_ptr, rt_string name, rt_string path) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank || !name || !path)
        return 0;

    int replacing;

    if (!rt_audio_is_available())
        return 0;

    /* Load the sound from file */
    void *sound = rt_sound_load(path);
    if (!sound)
        return 0;

    /* Check if name already exists — replace */
    int idx = find_entry(bank, name);
    replacing = idx >= 0;
    if (idx < 0) {
        idx = find_free(bank);
        if (idx < 0) {
            /* Bank full — free the loaded sound */
            if (rt_obj_release_check0(sound))
                rt_obj_free(sound);
            return 0;
        }
    }

    rt_str_retain_maybe(name);
    if (replacing)
        release_entry(&bank->entries[idx]);
    else
        bank->count++;

    /* Store: sound already has refcount 1 from rt_sound_load */
    bank->entries[idx].name = name;
    bank->entries[idx].sound = sound;
    bank->entries[idx].in_use = 1;

    return 1;
}

/// @brief Register a pre-loaded Sound handle under the given name (retains the sound).
int64_t rt_soundbank_register_sound(void *bank_ptr, rt_string name, void *sound) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank || !name || !sound)
        return 0;
    if (!rt_sound_is_handle(sound))
        return 0;

    int replacing;

    /* Check if name already exists — replace */
    int idx = find_entry(bank, name);
    replacing = idx >= 0;
    if (idx < 0) {
        idx = find_free(bank);
        if (idx < 0)
            return 0; /* Bank full */
    }

    /* Retain first so replacing an entry with the same name/sound cannot free it early. */
    rt_obj_retain_maybe(sound);
    rt_str_retain_maybe(name);
    if (replacing)
        release_entry(&bank->entries[idx]);
    else
        bank->count++;

    bank->entries[idx].name = name;
    bank->entries[idx].sound = sound;
    bank->entries[idx].in_use = 1;

    return 1;
}

/// @brief Play a sound by name at default volume. Returns a voice ID or -1 if not found.
int64_t rt_soundbank_play(void *bank_ptr, rt_string name) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank || !name)
        return -1;

    int idx = find_entry(bank, name);
    if (idx < 0)
        return -1;

    return rt_sound_play(bank->entries[idx].sound);
}

/// @brief Play a sound by name with explicit volume and pan. Returns a voice ID.
int64_t rt_soundbank_play_ex(void *bank_ptr, rt_string name, int64_t volume, int64_t pan) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank || !name)
        return -1;

    int idx = find_entry(bank, name);
    if (idx < 0)
        return -1;

    return rt_sound_play_ex(bank->entries[idx].sound, volume, pan);
}

/// @brief Check whether a sound with the given name exists in the bank.
int64_t rt_soundbank_has(void *bank_ptr, rt_string name) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank || !name)
        return 0;

    return find_entry(bank, name) >= 0 ? 1 : 0;
}

/// @brief Get the Sound handle for a name (retained — caller must release). NULL if not found.
void *rt_soundbank_get(void *bank_ptr, rt_string name) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank || !name)
        return NULL;

    int idx = find_entry(bank, name);
    if (idx < 0)
        return NULL;

    void *sound = bank->entries[idx].sound;
    rt_obj_retain_maybe(sound);
    return sound;
}

/// @brief Remove a sound from the bank by name and release its reference.
void rt_soundbank_remove(void *bank_ptr, rt_string name) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank || !name)
        return;

    int idx = find_entry(bank, name);
    if (idx < 0)
        return;

    release_entry(&bank->entries[idx]);
    bank->count--;
}

/// @brief Remove and release all sounds from the bank.
void rt_soundbank_clear(void *bank_ptr) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank)
        return;

    for (int i = 0; i < BANK_MAX_ENTRIES; i++) {
        if (bank->entries[i].in_use)
            release_entry(&bank->entries[i]);
    }
    bank->count = 0;
}

/// @brief Get the number of sounds currently registered in the bank.
int64_t rt_soundbank_count(void *bank_ptr) {
    rt_soundbank_impl *bank = as_soundbank(bank_ptr);
    if (!bank)
        return 0;

    return bank->count;
}
