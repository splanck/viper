//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_playlist.c
// Purpose: Implements a music playlist for the Viper.Audio.Playlist class.
//          Supports sequential and shuffle playback, repeat modes (none/all/one),
//          volume control, and track navigation (Next, Prev, Seek). Delegates
//          actual audio decoding and playback to rt_audio.
//
// Key invariants:
//   - Track indices are zero-based; current == -1 means no track is loaded.
//   - Shuffle mode generates a random permutation of indices at play-start.
//   - Repeat=none stops at end; repeat=all wraps to track 0; repeat=one loops.
//   - Poll must be called periodically to advance to the next track when done.
//   - Volume range is 0-100; values outside this range are clamped.
//   - The shuffle_order sequence is regenerated each time shuffle is toggled on.
//
// Ownership/Lifetime:
//   - The playlist retains a reference to the current Music object while playing.
//   - Track path strings in the tracks sequence are retained by the sequence.
//   - The playlist object is heap-allocated and managed by the runtime GC.
//
// Links: src/runtime/audio/rt_playlist.h (public API),
//        src/runtime/audio/rt_audio.h (music load/play/stop primitives)
//
//===----------------------------------------------------------------------===//

#include "rt_playlist.h"
#include "rt_audio.h"
#include "rt_internal.h"
#include "rt_mixgroup.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    void *tracks;         // Seq of path strings
    int64_t current;      // Current track index (-1 if none)
    void *music;          // Currently loaded Music object
    int64_t volume;       // Playback volume (0-100)
    int8_t shuffle;       // Shuffle mode
    int64_t repeat;       // RT_REPEAT_NONE, RT_REPEAT_ALL, or RT_REPEAT_ONE
    int8_t playing;       // Currently playing
    int8_t paused;        // Paused state
    void *shuffle_order;  // Shuffled index sequence
    int64_t crossfade_ms; // Crossfade duration for track changes (0 = disabled)
} playlist_impl;

//=============================================================================
// Helper Functions
//=============================================================================

static void generate_shuffle_order(playlist_impl *pl) {
    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0)
        return;

    // Release old shuffle order before creating a new one (C-2)
    if (pl->shuffle_order) {
        if (rt_obj_release_check0(pl->shuffle_order))
            rt_obj_free(pl->shuffle_order);
        pl->shuffle_order = NULL;
    }
    pl->shuffle_order = rt_seq_new();

    // Create sequential order first
    int64_t *indices = (int64_t *)malloc(count * sizeof(int64_t));
    if (!indices)
        rt_trap("rt_playlist: memory allocation failed");
    for (int64_t i = 0; i < count; i++) {
        indices[i] = i;
    }

    // Fisher-Yates shuffle using thread-safe local PRNG
    uint64_t rng_state = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&indices;
    // Warm up the PRNG
    for (int w = 0; w < 5; w++) {
        rng_state ^= rng_state >> 12;
        rng_state ^= rng_state << 25;
        rng_state ^= rng_state >> 27;
        rng_state *= 0x2545F4914F6CDD1DULL;
    }

    for (int64_t i = count - 1; i > 0; i--) {
        // xorshift64star step
        rng_state ^= rng_state >> 12;
        rng_state ^= rng_state << 25;
        rng_state ^= rng_state >> 27;
        uint64_t r = rng_state * 0x2545F4914F6CDD1DULL;
        // Modulo reduction has slight bias for non-power-of-2 ranges,
        // but negligible for playlist shuffle (max ~2^-58 bias per element)
        int64_t j = (int64_t)(r % (uint64_t)(i + 1));
        int64_t tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    // Store in shuffle_order seq
    for (int64_t i = 0; i < count; i++) {
        rt_seq_push(pl->shuffle_order, (void *)indices[i]);
    }

    free(indices);
}

static int64_t get_track_index(playlist_impl *pl, int64_t position) {
    if (pl->shuffle && pl->shuffle_order) {
        int64_t count = rt_seq_len(pl->shuffle_order);
        if (position >= 0 && position < count) {
            return (int64_t)(intptr_t)rt_seq_get(pl->shuffle_order, position);
        }
    }
    return position;
}

static void load_current(playlist_impl *pl) {
    // Stop and free previous music
    if (pl->music) {
        rt_music_stop(pl->music);
        rt_music_destroy(pl->music);
        pl->music = NULL;
    }

    if (pl->current < 0 || pl->current >= rt_seq_len(pl->tracks))
        return;

    int64_t actual_index = get_track_index(pl, pl->current);
    rt_string path = (rt_string)rt_seq_get(pl->tracks, actual_index);

    pl->music = rt_music_load(path);
    if (pl->music) {
        rt_music_set_volume(pl->music, pl->volume);
    }
}

//=============================================================================
// Creation
//=============================================================================

// C-1: Finalizer — releases all GC-tracked resources when a Playlist is collected.
static void playlist_finalize(void *obj) {
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->music) {
        rt_music_stop(pl->music);
        rt_music_destroy(pl->music);
        pl->music = NULL;
    }

    if (pl->shuffle_order) {
        if (rt_obj_release_check0(pl->shuffle_order))
            rt_obj_free(pl->shuffle_order);
        pl->shuffle_order = NULL;
    }

    if (pl->tracks) {
        if (rt_obj_release_check0(pl->tracks))
            rt_obj_free(pl->tracks);
        pl->tracks = NULL;
    }
}

void *rt_playlist_new(void) {
    playlist_impl *pl = (playlist_impl *)rt_obj_new_i64(0, (int64_t)sizeof(playlist_impl));
    memset(pl, 0, sizeof(playlist_impl));

    pl->tracks = rt_seq_new();
    pl->current = -1;
    pl->music = NULL;
    pl->volume = 100;
    pl->shuffle = 0;
    pl->repeat = RT_REPEAT_NONE;
    pl->playing = 0;
    pl->paused = 0;
    pl->shuffle_order = NULL;

    rt_obj_set_finalizer(pl, playlist_finalize);

    return pl;
}

//=============================================================================
// Track Management
//=============================================================================

/// @brief Append a music track to the end of the playlist.
/// @details Retains a reference to the path string and appends it to the
///          tracks sequence. If shuffle mode is active, the shuffle order is
///          regenerated to include the new track.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param path Path to the music file to add.
void rt_playlist_add(void *obj, rt_string path) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    // Copy the path string
    rt_string copy = path ? rt_string_ref(path) : rt_const_cstr("");
    rt_seq_push(pl->tracks, (void *)copy);

    // Regenerate shuffle order if needed
    if (pl->shuffle) {
        generate_shuffle_order(pl);
    }
}

/// @brief Insert a music track at a specific position in the playlist.
/// @details Shifts existing tracks at and after @p index to make room. If the
///          insertion point is before the current track, the current index is
///          adjusted to keep it pointing at the same track.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param index Zero-based insertion position.
/// @param path Path to the music file to insert.
void rt_playlist_insert(void *obj, int64_t index, rt_string path) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    rt_string copy = path ? rt_string_ref(path) : rt_const_cstr("");
    rt_seq_insert(pl->tracks, index, (void *)copy);

    // Adjust current if needed
    if (pl->current >= index) {
        pl->current++;
    }

    if (pl->shuffle) {
        generate_shuffle_order(pl);
    }
}

/// @brief Remove a track from the playlist by index.
/// @details If the removed track is before the current track, the current index
///          is decremented. If the current track itself is removed, playback is
///          stopped and the next available track becomes current.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param index Zero-based index of the track to remove.
void rt_playlist_remove(void *obj, int64_t index) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (index < 0 || index >= count)
        return;

    rt_seq_remove(pl->tracks, index);

    // Adjust current
    if (index < pl->current) {
        pl->current--;
    } else if (index == pl->current) {
        // Current track removed
        if (pl->music) {
            rt_music_stop(pl->music);
            rt_music_destroy(pl->music);
            pl->music = NULL;
        }
        pl->playing = 0;
        pl->paused = 0;

        // Try to load next (or wrap)
        if (rt_seq_len(pl->tracks) > 0) {
            if (pl->current >= rt_seq_len(pl->tracks)) {
                pl->current = 0;
            }
        } else {
            pl->current = -1;
        }
    }

    if (pl->shuffle) {
        generate_shuffle_order(pl);
    }
}

/// @brief Remove all tracks from the playlist and stop playback.
/// @details Stops the current music, releases all track path strings from the
///          sequence, resets the current index to -1, and frees the shuffle
///          order if one exists.
/// @param obj Playlist object pointer; no-op if NULL.
void rt_playlist_clear(void *obj) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->music) {
        rt_music_stop(pl->music);
        rt_music_destroy(pl->music);
        pl->music = NULL;
    }

    // Clear tracks
    while (rt_seq_len(pl->tracks) > 0) {
        rt_seq_pop(pl->tracks);
    }

    pl->current = -1;
    pl->playing = 0;
    pl->paused = 0;

    // C-3: Release shuffle_order instead of leaking it (C-3)
    if (pl->shuffle_order) {
        if (rt_obj_release_check0(pl->shuffle_order))
            rt_obj_free(pl->shuffle_order);
        pl->shuffle_order = NULL;
    }
}

/// @brief Return the number of tracks in the playlist.
/// @param obj Playlist object pointer; returns 0 if NULL.
/// @return Track count.
int64_t rt_playlist_len(void *obj) {
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return rt_seq_len(pl->tracks);
}

/// @brief Return the file path of the track at the given index.
/// @param obj Playlist object pointer; returns empty string if NULL.
/// @param index Zero-based track index.
/// @return Track path string, or empty string if index is out of bounds.
rt_string rt_playlist_get(void *obj, int64_t index) {
    if (!obj)
        return rt_const_cstr("");
    playlist_impl *pl = (playlist_impl *)obj;

    if (index < 0 || index >= rt_seq_len(pl->tracks))
        return rt_const_cstr("");

    return (rt_string)rt_seq_get(pl->tracks, index);
}

//=============================================================================
// Playback Control
//=============================================================================

/// @brief Start or resume playlist playback.
/// @details If paused, resumes the current track. If stopped, loads the
///          current track (defaulting to index 0) and begins playback.
///          Repeat-one mode passes the loop flag to the music backend.
/// @param obj Playlist object pointer; no-op if NULL or empty.
void rt_playlist_play(void *obj) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (rt_seq_len(pl->tracks) == 0)
        return;

    if (pl->paused && pl->music) {
        rt_music_resume(pl->music);
        pl->paused = 0;
        pl->playing = 1;
        return;
    }

    if (pl->current < 0) {
        pl->current = 0;
    }

    load_current(pl);
    if (pl->music) {
        int loop = (pl->repeat == RT_REPEAT_ONE) ? 1 : 0;
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

/// @brief Pause the currently playing track without resetting position.
/// @param obj Playlist object pointer; no-op if NULL or not playing.
void rt_playlist_pause(void *obj) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->music && pl->playing) {
        rt_music_pause(pl->music);
        pl->paused = 1;
        pl->playing = 0;
    }
}

/// @brief Stop playback and reset the current position to the beginning.
/// @param obj Playlist object pointer; no-op if NULL.
void rt_playlist_stop(void *obj) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->music) {
        rt_music_stop(pl->music);
    }

    pl->playing = 0;
    pl->paused = 0;
    pl->current = 0;
}

/// @brief Advance to the next track in the playlist.
/// @details Handles repeat-all (wraps to track 0 and re-shuffles if needed)
///          and repeat-none (stops at the last track). If playback was active,
///          the new track is loaded and started automatically.
/// @param obj Playlist object pointer; no-op if NULL or empty.
void rt_playlist_next(void *obj) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0)
        return;

    pl->current++;

    if (pl->current >= count) {
        if (pl->repeat == RT_REPEAT_ALL) {
            // Repeat all: go back to start
            pl->current = 0;
            if (pl->shuffle) {
                generate_shuffle_order(pl);
            }
        } else {
            // No repeat: stop at end
            pl->current = count - 1;
            rt_playlist_stop(obj);
            return;
        }
    }

    int was_playing = pl->playing || pl->paused;
    load_current(pl);
    if (was_playing && pl->music) {
        int loop = (pl->repeat == RT_REPEAT_ONE) ? 1 : 0;
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

/// @brief Go back to the previous track in the playlist.
/// @details With repeat-all, wrapping past the first track goes to the last.
///          Without repeat, the position clamps at track 0.
/// @param obj Playlist object pointer; no-op if NULL or empty.
void rt_playlist_prev(void *obj) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0)
        return;

    pl->current--;

    if (pl->current < 0) {
        if (pl->repeat == RT_REPEAT_ALL) {
            // Repeat all: go to end
            pl->current = count - 1;
        } else {
            pl->current = 0;
        }
    }

    int was_playing = pl->playing || pl->paused;
    load_current(pl);
    if (was_playing && pl->music) {
        int loop = (pl->repeat == RT_REPEAT_ONE) ? 1 : 0;
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

/// @brief Jump to a specific track by index and begin playback if active.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param index Zero-based track index to jump to; out-of-range is ignored.
void rt_playlist_jump(void *obj, int64_t index) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0 || index < 0 || index >= count)
        return;

    pl->current = index;

    int was_playing = pl->playing || pl->paused;
    load_current(pl);
    if (was_playing && pl->music) {
        int loop = (pl->repeat == RT_REPEAT_ONE) ? 1 : 0;
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

//=============================================================================
// Properties
//=============================================================================

/// @brief Return the real track index of the currently playing/loaded track.
/// @details In shuffle mode, maps the internal position through the shuffle
///          order to return the actual track index in the original sequence.
/// @param obj Playlist object pointer; returns -1 if NULL.
/// @return Zero-based track index, or -1 if no track is loaded.
int64_t rt_playlist_get_current(void *obj) {
    if (!obj)
        return -1;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->shuffle && pl->current >= 0) {
        return get_track_index(pl, pl->current);
    }
    return pl->current;
}

/// @brief Check whether the playlist is currently playing a track.
/// @param obj Playlist object pointer; returns 0 if NULL.
/// @return 1 if playing, 0 if stopped or paused.
int8_t rt_playlist_is_playing(void *obj) {
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->playing;
}

/// @brief Check whether the playlist is paused.
/// @param obj Playlist object pointer; returns 0 if NULL.
/// @return 1 if paused, 0 otherwise.
int8_t rt_playlist_is_paused(void *obj) {
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->paused;
}

/// @brief Return the current playback volume of the playlist.
/// @param obj Playlist object pointer; returns 0 if NULL.
/// @return Volume in range [0, 100].
int64_t rt_playlist_get_volume(void *obj) {
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->volume;
}

/// @brief Set the playback volume, clamped to [0, 100].
/// @details Also applies the new volume to the currently loaded music track
///          so the change takes effect immediately.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param volume Desired volume (0-100).
void rt_playlist_set_volume(void *obj, int64_t volume) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    pl->volume = volume;

    if (pl->music) {
        rt_music_set_volume(pl->music, volume);
    }
}

//=============================================================================
// Playback Modes
//=============================================================================

/// @brief Enable or disable shuffle mode.
/// @details When enabled, generates a random permutation of track indices
///          using a xorshift64* PRNG seeded from the current time. The
///          permutation is regenerated each time shuffle is toggled on.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param shuffle 1 to enable shuffle, 0 to disable.
void rt_playlist_set_shuffle(void *obj, int8_t shuffle) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    pl->shuffle = shuffle ? 1 : 0;

    if (pl->shuffle) {
        generate_shuffle_order(pl);
    }
}

/// @brief Check whether shuffle mode is currently enabled.
/// @param obj Playlist object pointer; returns 0 if NULL.
/// @return 1 if shuffle is enabled, 0 otherwise.
int8_t rt_playlist_get_shuffle(void *obj) {
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->shuffle;
}

/// @brief Set the repeat mode for the playlist.
/// @details Mode values: RT_REPEAT_NONE (stop at end), RT_REPEAT_ALL (loop),
///          RT_REPEAT_ONE (replay current track). Out-of-range values are clamped.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param mode Repeat mode constant.
void rt_playlist_set_repeat(void *obj, int64_t mode) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (mode < RT_REPEAT_NONE)
        mode = RT_REPEAT_NONE;
    if (mode > RT_REPEAT_ONE)
        mode = RT_REPEAT_ONE;
    pl->repeat = mode;
}

/// @brief Return the current repeat mode.
/// @param obj Playlist object pointer; returns 0 (REPEAT_NONE) if NULL.
/// @return Repeat mode constant (RT_REPEAT_NONE, RT_REPEAT_ALL, RT_REPEAT_ONE).
int64_t rt_playlist_get_repeat(void *obj) {
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->repeat;
}

//=============================================================================
// Update
//=============================================================================

/// @brief Poll the playlist state and auto-advance when the current track ends.
/// @details Should be called once per frame or tick. When the current track
///          finishes, repeat-one restarts it; otherwise rt_playlist_next is
///          invoked to advance (which handles repeat-all and repeat-none).
/// @param obj Playlist object pointer; no-op if NULL or not playing.
void rt_playlist_update(void *obj) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (!pl->playing || !pl->music)
        return;

    // Check if current track has ended
    if (!rt_music_is_playing(pl->music)) {
        // Track ended
        if (pl->repeat == RT_REPEAT_ONE) {
            // Repeat one: restart same track
            rt_music_seek(pl->music, 0);
            rt_music_play(pl->music, 1);
        } else {
            // Move to next
            rt_playlist_next(obj);
        }
    }
}

//=============================================================================
// Playlist Crossfade Settings
//=============================================================================

/// @brief Set the crossfade duration for track transitions.
/// @details A value of 0 disables crossfading (immediate switch). Positive
///          values enable a linear volume crossfade of the specified duration
///          when advancing between tracks.
/// @param obj Playlist object pointer; no-op if NULL.
/// @param duration_ms Crossfade duration in milliseconds (0 = disabled).
void rt_playlist_set_crossfade(void *obj, int64_t duration_ms) {
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;
    pl->crossfade_ms = duration_ms < 0 ? 0 : duration_ms;
}

/// @brief Return the current crossfade duration in milliseconds.
/// @param obj Playlist object pointer; returns 0 if NULL.
/// @return Crossfade duration (0 = disabled).
int64_t rt_playlist_get_crossfade(void *obj) {
    if (!obj)
        return 0;
    return ((playlist_impl *)obj)->crossfade_ms;
}
