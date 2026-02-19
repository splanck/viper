//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_playlist.c
/// @brief Playlist implementation for sequential music playback.
///
//===----------------------------------------------------------------------===//

#include "rt_playlist.h"
#include "rt_audio.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct
{
    void *tracks;        // Seq of path strings
    int64_t current;     // Current track index (-1 if none)
    void *music;         // Currently loaded Music object
    int64_t volume;      // Playback volume (0-100)
    int8_t shuffle;      // Shuffle mode
    int64_t repeat;      // 0=none, 1=all, 2=one
    int8_t playing;      // Currently playing
    int8_t paused;       // Paused state
    void *shuffle_order; // Shuffled index sequence
} playlist_impl;

//=============================================================================
// Helper Functions
//=============================================================================

static void generate_shuffle_order(playlist_impl *pl)
{
    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0)
        return;

    // Clear old shuffle order
    if (pl->shuffle_order)
    {
        // Just create a new one
    }
    pl->shuffle_order = rt_seq_new();

    // Create sequential order first
    int64_t *indices = (int64_t *)malloc(count * sizeof(int64_t));
    for (int64_t i = 0; i < count; i++)
    {
        indices[i] = i;
    }

    // Fisher-Yates shuffle
    static int seeded = 0;
    if (!seeded)
    {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    for (int64_t i = count - 1; i > 0; i--)
    {
        int64_t j = rand() % (i + 1);
        int64_t tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    // Store in shuffle_order seq
    for (int64_t i = 0; i < count; i++)
    {
        rt_seq_push(pl->shuffle_order, (void *)indices[i]);
    }

    free(indices);
}

static int64_t get_track_index(playlist_impl *pl, int64_t position)
{
    if (pl->shuffle && pl->shuffle_order)
    {
        int64_t count = rt_seq_len(pl->shuffle_order);
        if (position >= 0 && position < count)
        {
            return (int64_t)(intptr_t)rt_seq_get(pl->shuffle_order, position);
        }
    }
    return position;
}

static void load_current(playlist_impl *pl)
{
    // Stop and free previous music
    if (pl->music)
    {
        rt_music_stop(pl->music);
        rt_music_free(pl->music);
        pl->music = NULL;
    }

    if (pl->current < 0 || pl->current >= rt_seq_len(pl->tracks))
        return;

    int64_t actual_index = get_track_index(pl, pl->current);
    rt_string path = (rt_string)rt_seq_get(pl->tracks, actual_index);

    pl->music = rt_music_load(path);
    if (pl->music)
    {
        rt_music_set_volume(pl->music, pl->volume);
    }
}

//=============================================================================
// Creation
//=============================================================================

void *rt_playlist_new(void)
{
    playlist_impl *pl = (playlist_impl *)rt_obj_new_i64(0, (int64_t)sizeof(playlist_impl));
    memset(pl, 0, sizeof(playlist_impl));

    pl->tracks = rt_seq_new();
    pl->current = -1;
    pl->music = NULL;
    pl->volume = 100;
    pl->shuffle = 0;
    pl->repeat = 0;
    pl->playing = 0;
    pl->paused = 0;
    pl->shuffle_order = NULL;

    return pl;
}

//=============================================================================
// Track Management
//=============================================================================

void rt_playlist_add(void *obj, rt_string path)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    // Copy the path string
    const char *str = rt_string_cstr(path);
    rt_string copy = str ? rt_string_from_bytes(str, strlen(str)) : rt_const_cstr("");
    rt_seq_push(pl->tracks, (void *)copy);

    // Regenerate shuffle order if needed
    if (pl->shuffle)
    {
        generate_shuffle_order(pl);
    }
}

void rt_playlist_insert(void *obj, int64_t index, rt_string path)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    const char *str = rt_string_cstr(path);
    rt_string copy = str ? rt_string_from_bytes(str, strlen(str)) : rt_const_cstr("");
    rt_seq_insert(pl->tracks, index, (void *)copy);

    // Adjust current if needed
    if (pl->current >= index)
    {
        pl->current++;
    }

    if (pl->shuffle)
    {
        generate_shuffle_order(pl);
    }
}

void rt_playlist_remove(void *obj, int64_t index)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (index < 0 || index >= count)
        return;

    rt_seq_remove(pl->tracks, index);

    // Adjust current
    if (index < pl->current)
    {
        pl->current--;
    }
    else if (index == pl->current)
    {
        // Current track removed
        if (pl->music)
        {
            rt_music_stop(pl->music);
            rt_music_free(pl->music);
            pl->music = NULL;
        }
        pl->playing = 0;
        pl->paused = 0;

        // Try to load next (or wrap)
        if (rt_seq_len(pl->tracks) > 0)
        {
            if (pl->current >= rt_seq_len(pl->tracks))
            {
                pl->current = 0;
            }
        }
        else
        {
            pl->current = -1;
        }
    }

    if (pl->shuffle)
    {
        generate_shuffle_order(pl);
    }
}

void rt_playlist_clear(void *obj)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->music)
    {
        rt_music_stop(pl->music);
        rt_music_free(pl->music);
        pl->music = NULL;
    }

    // Clear tracks
    while (rt_seq_len(pl->tracks) > 0)
    {
        rt_seq_pop(pl->tracks);
    }

    pl->current = -1;
    pl->playing = 0;
    pl->paused = 0;
    pl->shuffle_order = NULL;
}

int64_t rt_playlist_len(void *obj)
{
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return rt_seq_len(pl->tracks);
}

rt_string rt_playlist_get(void *obj, int64_t index)
{
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

void rt_playlist_play(void *obj)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (rt_seq_len(pl->tracks) == 0)
        return;

    if (pl->paused && pl->music)
    {
        rt_music_resume(pl->music);
        pl->paused = 0;
        pl->playing = 1;
        return;
    }

    if (pl->current < 0)
    {
        pl->current = 0;
    }

    load_current(pl);
    if (pl->music)
    {
        int loop = (pl->repeat == 2) ? 1 : 0; // Repeat one = loop
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

void rt_playlist_pause(void *obj)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->music && pl->playing)
    {
        rt_music_pause(pl->music);
        pl->paused = 1;
        pl->playing = 0;
    }
}

void rt_playlist_stop(void *obj)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->music)
    {
        rt_music_stop(pl->music);
    }

    pl->playing = 0;
    pl->paused = 0;
    pl->current = 0;
}

void rt_playlist_next(void *obj)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0)
        return;

    pl->current++;

    if (pl->current >= count)
    {
        if (pl->repeat == 1)
        {
            // Repeat all: go back to start
            pl->current = 0;
            if (pl->shuffle)
            {
                generate_shuffle_order(pl);
            }
        }
        else
        {
            // No repeat: stop at end
            pl->current = count - 1;
            rt_playlist_stop(obj);
            return;
        }
    }

    int was_playing = pl->playing || pl->paused;
    load_current(pl);
    if (was_playing && pl->music)
    {
        int loop = (pl->repeat == 2) ? 1 : 0;
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

void rt_playlist_prev(void *obj)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0)
        return;

    pl->current--;

    if (pl->current < 0)
    {
        if (pl->repeat == 1)
        {
            // Repeat all: go to end
            pl->current = count - 1;
        }
        else
        {
            pl->current = 0;
        }
    }

    int was_playing = pl->playing || pl->paused;
    load_current(pl);
    if (was_playing && pl->music)
    {
        int loop = (pl->repeat == 2) ? 1 : 0;
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

void rt_playlist_jump(void *obj, int64_t index)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    int64_t count = rt_seq_len(pl->tracks);
    if (count == 0 || index < 0 || index >= count)
        return;

    pl->current = index;

    int was_playing = pl->playing || pl->paused;
    load_current(pl);
    if (was_playing && pl->music)
    {
        int loop = (pl->repeat == 2) ? 1 : 0;
        rt_music_play(pl->music, loop);
        pl->playing = 1;
        pl->paused = 0;
    }
}

//=============================================================================
// Properties
//=============================================================================

int64_t rt_playlist_get_current(void *obj)
{
    if (!obj)
        return -1;
    playlist_impl *pl = (playlist_impl *)obj;

    if (pl->shuffle && pl->current >= 0)
    {
        return get_track_index(pl, pl->current);
    }
    return pl->current;
}

int8_t rt_playlist_is_playing(void *obj)
{
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->playing;
}

int8_t rt_playlist_is_paused(void *obj)
{
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->paused;
}

int64_t rt_playlist_get_volume(void *obj)
{
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->volume;
}

void rt_playlist_set_volume(void *obj, int64_t volume)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    pl->volume = volume;

    if (pl->music)
    {
        rt_music_set_volume(pl->music, volume);
    }
}

//=============================================================================
// Playback Modes
//=============================================================================

void rt_playlist_set_shuffle(void *obj, int8_t shuffle)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    pl->shuffle = shuffle ? 1 : 0;

    if (pl->shuffle)
    {
        generate_shuffle_order(pl);
    }
}

int8_t rt_playlist_get_shuffle(void *obj)
{
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->shuffle;
}

void rt_playlist_set_repeat(void *obj, int64_t mode)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (mode < 0)
        mode = 0;
    if (mode > 2)
        mode = 2;
    pl->repeat = mode;
}

int64_t rt_playlist_get_repeat(void *obj)
{
    if (!obj)
        return 0;
    playlist_impl *pl = (playlist_impl *)obj;
    return pl->repeat;
}

//=============================================================================
// Update
//=============================================================================

void rt_playlist_update(void *obj)
{
    if (!obj)
        return;
    playlist_impl *pl = (playlist_impl *)obj;

    if (!pl->playing || !pl->music)
        return;

    // Check if current track has ended
    if (!rt_music_is_playing(pl->music))
    {
        // Track ended
        if (pl->repeat == 2)
        {
            // Repeat one: restart same track
            rt_music_seek(pl->music, 0);
            rt_music_play(pl->music, 1);
        }
        else
        {
            // Move to next
            rt_playlist_next(obj);
        }
    }
}
