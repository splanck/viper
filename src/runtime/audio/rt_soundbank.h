//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_soundbank.h
// Purpose: Named sound registry that maps string names to loaded Sound objects.
//          Games use SoundBank to manage sounds by name (e.g., "jump", "coin")
//          instead of keeping raw Sound handles around.
//
// Key invariants:
//   - Maximum 64 named entries per bank.
//   - Name keys are retained as full runtime strings; long names are not truncated.
//   - Sound references are retained (refcount incremented) while in the bank.
//   - Thread safety: NOT thread-safe; call from main thread only.
//
// Ownership/Lifetime:
//   - Bank is GC-managed; finalizer releases all held sound references.
//   - Sounds stored in the bank have their refcount incremented.
//
// Links: rt_audio.h (sound loading/playback), rt_synth.h (procedural sounds)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a new empty sound bank.
/// @return Opaque sound bank handle.
void *rt_soundbank_new(void);

/// @brief Register a sound from a WAV file under a name.
/// @param bank Sound bank handle.
/// @param name Name to register under.
/// @param path Path to WAV file.
/// @return 1 on success, 0 on failure.
int64_t rt_soundbank_register(void *bank, rt_string name, rt_string path);

/// @brief Register an existing sound object under a name.
/// @param bank Sound bank handle.
/// @param name Name to register under.
/// @param sound Sound object (from Sound.Load or Synth).
/// @return 1 on success, 0 on failure.
int64_t rt_soundbank_register_sound(void *bank, rt_string name, void *sound);

/// @brief Play a named sound with default settings.
/// @param bank Sound bank handle.
/// @param name Name of the sound to play.
/// @return Voice ID, or -1 on failure.
int64_t rt_soundbank_play(void *bank, rt_string name);

/// @brief Play a named sound with volume and pan control.
/// @param bank Sound bank handle.
/// @param name Name of the sound to play.
/// @param volume Volume (0-100).
/// @param pan Pan (-100 to 100).
/// @return Voice ID, or -1 on failure.
int64_t rt_soundbank_play_ex(void *bank, rt_string name, int64_t volume, int64_t pan);

/// @brief Check if a name is registered in the bank.
/// @param bank Sound bank handle.
/// @param name Name to check.
/// @return 1 if registered, 0 if not.
int64_t rt_soundbank_has(void *bank, rt_string name);

/// @brief Get the sound object registered under a name.
/// @param bank Sound bank handle.
/// @param name Name to look up.
/// @return Sound object, or NULL if not found.
void *rt_soundbank_get(void *bank, rt_string name);

/// @brief Remove a named entry from the bank.
/// @param bank Sound bank handle.
/// @param name Name to remove.
void rt_soundbank_remove(void *bank, rt_string name);

/// @brief Remove all entries from the bank.
/// @param bank Sound bank handle.
void rt_soundbank_clear(void *bank);

/// @brief Get the number of registered sounds.
/// @param bank Sound bank handle.
/// @return Number of entries.
int64_t rt_soundbank_count(void *bank);

#ifdef __cplusplus
}
#endif
