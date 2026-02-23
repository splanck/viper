//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_playlist.h
// Purpose: Queue-based music playlist manager providing sequential track playback with shuffle, repeat, and queue manipulation (add, remove, skip) built on top of the audio bridge.
//
// Key invariants:
//   - Track queue is FIFO by default; shuffle randomizes playback order.
//   - Repeat modes: none (stop at end), one (replay current track), all (loop queue).
//   - Skip and previous navigate relative to the current playback position.
//   - Maximum playlist capacity is fixed at compile time.
//
// Ownership/Lifetime:
//   - Caller owns the playlist handle; destroy with rt_playlist_destroy.
//   - Track strings added to the playlist are copied; caller retains ownership of inputs.
//
// Links: src/runtime/audio/rt_playlist.c (implementation), src/runtime/audio/rt_audio.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_PLAYLIST_H
#define VIPER_RT_PLAYLIST_H

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // Viper.Audio.Playlist
    //=============================================================================

    /// @brief Create a new empty playlist.
    /// @return Playlist object.
    void *rt_playlist_new(void);

    /// @brief Add a music file to the end of the playlist.
    /// @param playlist Playlist object.
    /// @param path Path to music file.
    void rt_playlist_add(void *playlist, rt_string path);

    /// @brief Insert a music file at a specific position.
    /// @param playlist Playlist object.
    /// @param index Position to insert (0 = first).
    /// @param path Path to music file.
    void rt_playlist_insert(void *playlist, int64_t index, rt_string path);

    /// @brief Remove a track from the playlist by index.
    /// @param playlist Playlist object.
    /// @param index Position to remove.
    void rt_playlist_remove(void *playlist, int64_t index);

    /// @brief Clear all tracks from the playlist.
    /// @param playlist Playlist object.
    void rt_playlist_clear(void *playlist);

    /// @brief Get the number of tracks in the playlist.
    /// @param playlist Playlist object.
    /// @return Number of tracks.
    int64_t rt_playlist_len(void *playlist);

    /// @brief Get the path of a track at a given index.
    /// @param playlist Playlist object.
    /// @param index Track index.
    /// @return Path string, or empty if index out of bounds.
    rt_string rt_playlist_get(void *playlist, int64_t index);

    //=============================================================================
    // Playback Control
    //=============================================================================

    /// @brief Start playing from the beginning or resume.
    /// @param playlist Playlist object.
    void rt_playlist_play(void *playlist);

    /// @brief Pause playback.
    /// @param playlist Playlist object.
    void rt_playlist_pause(void *playlist);

    /// @brief Stop playback and reset to beginning.
    /// @param playlist Playlist object.
    void rt_playlist_stop(void *playlist);

    /// @brief Skip to the next track.
    /// @param playlist Playlist object.
    void rt_playlist_next(void *playlist);

    /// @brief Go back to the previous track.
    /// @param playlist Playlist object.
    void rt_playlist_prev(void *playlist);

    /// @brief Jump to a specific track by index.
    /// @param playlist Playlist object.
    /// @param index Track index to play.
    void rt_playlist_jump(void *playlist, int64_t index);

    //=============================================================================
    // Properties
    //=============================================================================

    /// @brief Get the current track index.
    /// @param playlist Playlist object.
    /// @return Current track index, or -1 if empty.
    int64_t rt_playlist_get_current(void *playlist);

    /// @brief Check if the playlist is currently playing.
    /// @param playlist Playlist object.
    /// @return 1 if playing, 0 otherwise.
    int8_t rt_playlist_is_playing(void *playlist);

    /// @brief Check if the playlist is paused.
    /// @param playlist Playlist object.
    /// @return 1 if paused, 0 otherwise.
    int8_t rt_playlist_is_paused(void *playlist);

    /// @brief Get the playback volume.
    /// @param playlist Playlist object.
    /// @return Volume (0-100).
    int64_t rt_playlist_get_volume(void *playlist);

    /// @brief Set the playback volume.
    /// @param playlist Playlist object.
    /// @param volume Volume (0-100).
    void rt_playlist_set_volume(void *playlist, int64_t volume);

    //=============================================================================
    // Playback Modes
    //=============================================================================

    /// @brief Enable or disable shuffle mode.
    /// @param playlist Playlist object.
    /// @param shuffle 1 to enable shuffle, 0 to disable.
    void rt_playlist_set_shuffle(void *playlist, int8_t shuffle);

    /// @brief Check if shuffle mode is enabled.
    /// @param playlist Playlist object.
    /// @return 1 if shuffle is enabled, 0 otherwise.
    int8_t rt_playlist_get_shuffle(void *playlist);

    /// @brief Set the repeat mode.
    /// @param playlist Playlist object.
    /// @param mode 0 = no repeat, 1 = repeat all, 2 = repeat one.
    void rt_playlist_set_repeat(void *playlist, int64_t mode);

    /// @brief Get the current repeat mode.
    /// @param playlist Playlist object.
    /// @return Repeat mode (0, 1, or 2).
    int64_t rt_playlist_get_repeat(void *playlist);

    //=============================================================================
    // Update (for auto-advance)
    //=============================================================================

    /// @brief Update the playlist state (call each frame).
    /// @details Handles auto-advance to next track when current track ends.
    /// @param playlist Playlist object.
    void rt_playlist_update(void *playlist);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_PLAYLIST_H
