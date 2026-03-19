# Plan: Audio Crossfading & Mix Groups

## 1. Summary & Objective

Add music crossfading and audio mix groups to the Viper audio runtime. Crossfading enables smooth music transitions between scenes. Mix groups allow independent volume control for SFX vs music — a standard game settings feature that players expect.

**Why:** Playlist `Next()`/`Jump()` hard-cut between tracks. Only master volume exists — cannot let players turn down music without affecting SFX. These are the two most common audio complaints in game dev.

## 2. Scope

**In scope:**
- Music crossfading: fade out current track while fading in next
- Two built-in mix groups: MUSIC (0) and SFX (1)
- Per-group volume control (0-100)
- Group volume multiplied with individual sound/music volume
- Master volume still applies on top
- Playlist auto-crossfade option
- Sound.PlayEx gains optional group parameter

**Out of scope:**
- User-defined mix groups (only MUSIC and SFX)
- Audio bus routing / effect chains
- Per-voice real-time DSP (reverb, EQ, etc.)
- Spatial/positional audio
- Audio ducking (automatic volume reduction)
- MIDI / sequencer

## 3. Zero-Dependency Implementation Strategy

Crossfading uses two music slots: the currently playing track fades out while a second slot fades in. Volume interpolation is linear (no curve). Mix groups are just two volume multipliers stored in the audio system state and applied during the mixing step. ~150 LOC added across existing audio files.

## 4. Technical Requirements

### Modified Files
- `src/runtime/audio/rt_audio.c` — add mix group state, crossfade logic
- New: `src/runtime/audio/rt_mixgroup.h` — mix group API declarations

### C API Additions

```c
// Mix group constants
#define RT_MIXGROUP_MUSIC  0
#define RT_MIXGROUP_SFX    1

// === Mix Groups (extensions to rt_audio) ===
void    rt_audio_set_group_volume(int64_t group, int64_t volume);  // 0-100
int64_t rt_audio_get_group_volume(int64_t group);                   // 0-100

// === Music Crossfade ===
void    rt_music_crossfade_to(void *current_music, void *new_music,
                               int64_t duration_ms);
int8_t  rt_music_is_crossfading(void);                              // Global state
void    rt_music_crossfade_update(int64_t dt_ms);                   // Called internally from music update

// === Playlist Crossfade ===
void    rt_playlist_set_crossfade(void *playlist, int64_t duration_ms); // 0 = disabled
int64_t rt_playlist_get_crossfade(void *playlist);

// === Sound Group Assignment ===
int64_t rt_sound_play_in_group(void *sound, int64_t group);                          // Play in group
int64_t rt_sound_play_ex_in_group(void *sound, int64_t volume, int64_t pan,
                                   int64_t group);
int64_t rt_sound_play_loop_in_group(void *sound, int64_t volume, int64_t pan,
                                     int64_t group);
```

### Internal Changes

In the audio mixer (ViperAUD integration):
- Each voice gains a `group` field (0=MUSIC, 1=SFX)
- Effective volume = `(voice_volume / 100.0) * (group_volume / 100.0) * (master_volume / 100.0)`
- Crossfade state: `struct { void *fade_out; void *fade_in; int64_t elapsed; int64_t duration; }`
- `rt_music_crossfade_update()` called from `rt_playlist_update()` and internally from music step

## 5. runtime.def Registration

```c
//=============================================================================
// SOUND - MIX GROUPS
//=============================================================================

RT_FUNC(AudioSetGroupVolume,  rt_audio_set_group_volume,  "Viper.Sound.Audio.SetGroupVolume",  "void(i64,i64)")
RT_FUNC(AudioGetGroupVolume,  rt_audio_get_group_volume,  "Viper.Sound.Audio.GetGroupVolume",  "i64(i64)")

// Music crossfade
RT_FUNC(MusicCrossfadeTo,     rt_music_crossfade_to,      "Viper.Sound.Music.CrossfadeTo",     "void(obj,obj,i64)")
RT_FUNC(MusicIsCrossfading,   rt_music_is_crossfading,    "Viper.Sound.Audio.get_IsCrossfading","i1()")

// Playlist crossfade
RT_FUNC(PlaylistSetCrossfade, rt_playlist_set_crossfade,  "Viper.Sound.Playlist.set_Crossfade","void(obj,i64)")
RT_FUNC(PlaylistGetCrossfade, rt_playlist_get_crossfade,  "Viper.Sound.Playlist.get_Crossfade","i64(obj)")

// Sound group play variants
RT_FUNC(SoundPlayGroup,       rt_sound_play_in_group,      "Viper.Sound.Sound.PlayGroup",       "i64(obj,i64)")
RT_FUNC(SoundPlayExGroup,     rt_sound_play_ex_in_group,   "Viper.Sound.Sound.PlayExGroup",     "i64(obj,i64,i64,i64)")
RT_FUNC(SoundPlayLoopGroup,   rt_sound_play_loop_in_group, "Viper.Sound.Sound.PlayLoopGroup",   "i64(obj,i64,i64,i64)")

// Add to existing Audio static class:
//   RT_METHOD("SetGroupVolume", "void(i64,i64)", AudioSetGroupVolume)
//   RT_METHOD("GetGroupVolume", "i64(i64)", AudioGetGroupVolume)

// Add to existing Music class:
//   RT_METHOD("CrossfadeTo", "void(obj,i64)", MusicCrossfadeTo)

// Add to existing Playlist class:
//   RT_PROP("Crossfade", "i64", PlaylistGetCrossfade, PlaylistSetCrossfade)

// Add to existing Sound class:
//   RT_METHOD("PlayGroup", "i64(i64)", SoundPlayGroup)
//   RT_METHOD("PlayExGroup", "i64(i64,i64,i64)", SoundPlayExGroup)
//   RT_METHOD("PlayLoopGroup", "i64(i64,i64,i64)", SoundPlayLoopGroup)
```

## 6. CMakeLists.txt Changes

No new .c files needed — all changes extend `rt_audio.c` and `rt_playlist.c`.
Add `audio/rt_mixgroup.h` as a header (no source file, just declarations included by rt_audio.c).

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| Invalid group ID (not 0 or 1) | No-op, return default |
| Volume out of 0-100 range | Clamp |
| CrossfadeTo with NULL music | Stop current track with fade-out only |
| CrossfadeTo with duration ≤ 0 | Immediate switch (no fade) |
| CrossfadeTo while already crossfading | Cancel current crossfade, start new one |
| Sound.PlayGroup with invalid group | Default to SFX group |
| SetGroupVolume before Audio.Init() | Stored, applied when init completes |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_audio_mixing.zia`)

1. **Group volume default**
   - Given: Audio initialized
   - When: `Audio.GetGroupVolume(0)` and `Audio.GetGroupVolume(1)`
   - Then: Both return 100 (default full volume)

2. **Group volume set/get round-trip**
   - Given: Audio initialized
   - When: `Audio.SetGroupVolume(1, 50)`
   - Then: `Audio.GetGroupVolume(1) == 50`

3. **Group volume clamping**
   - When: `Audio.SetGroupVolume(0, 150)`
   - Then: `Audio.GetGroupVolume(0) == 100`

4. **Playlist crossfade setting**
   - Given: Playlist created
   - When: `playlist.Crossfade = 500`
   - Then: `playlist.Crossfade == 500`

5. **Crossfade disabled by default**
   - Given: New playlist
   - Then: `playlist.Crossfade == 0`

Note: Actual audio playback tests require audio hardware; these test API state only.

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| UPDATE | `docs/viperlib/audio.md` — add Mix Groups section, Crossfade section, update Sound/Music/Playlist tables |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/audio/rt_audio.c` | **Primary file to extend** — master volume, init/shutdown |
| `src/runtime/audio/rt_playlist.c` | **Extend** — auto-crossfade on track change |
| `src/runtime/audio/rt_soundbank.c` | Pattern: named sound management |
| `src/runtime/audio/rt_synth.c` | Pattern: sound generation pipeline |
| `src/il/runtime/runtime.def` | Registration (extend existing Audio/Music/Playlist/Sound blocks) |
