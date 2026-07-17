# ZannaAUD - Cross-Platform Audio Library

**Version:** 1.0.0
**Status:** Complete (All Platforms)

ZannaAUD is a cross-platform audio library written in C11 that provides sound effect playback and music streaming
with zero external dependencies (only OS-level audio APIs).

## Features

- **Pure C11** - No external dependencies (no SDL_mixer, OpenAL, etc.)
- **Software mixing** - 32 simultaneous voices with panning and volume control
- **Cross-platform** - Native backends for macOS (AudioQueue), Linux (ALSA), Windows (WASAPI)
- **Sound effects** - Load-and-play with automatic voice management
- **Music streaming** - Memory-efficient streaming for long audio files
- **WAV support** - 8/16/24/32-bit PCM and 32-bit float, mono or stereo, with strict header/data validation and checked
  resampling to the mixer rate
- **Thread-safe** - Audio runs on dedicated thread, all API calls are thread-safe

## Platform Support

| Platform | Status   | Backend                                  |
|----------|----------|------------------------------------------|
| macOS    | Complete | AudioQueue (`src/vaud_platform_macos.m`) |
| Linux    | Complete | ALSA (`src/vaud_platform_linux.c`)       |
| Windows  | Complete | WASAPI (`src/vaud_platform_win32.c`)     |

## Building Standalone

From the **Zanna repository root**, configure and build as a standalone project:

```bash
# Configure from repository root
cmake -S src/lib/audio -B build-aud

# Build the library
cmake --build build-aud

# Run examples (if available)
./build-aud/examples/audio_test
```

### Build Options

```bash
# Disable tests
cmake -S src/lib/audio -B build-aud -DVAUD_BUILD_TESTS=OFF

# Disable examples
cmake -S src/lib/audio -B build-aud -DVAUD_BUILD_EXAMPLES=OFF

# Build in release mode
cmake -S src/lib/audio -B build-aud -DCMAKE_BUILD_TYPE=Release
```

### Platform Dependencies

- **macOS**: None (uses AudioToolbox framework, included in macOS)
- **Linux**: ALSA development libraries (`libasound2-dev` on Debian/Ubuntu)
- **Windows**: None (uses WASAPI, included in Windows Vista+)

## Building as Part of Zanna

When included via `add_subdirectory()` from a parent CMake project (like Zanna), ZannaAUD automatically integrates:

```cmake
# In parent CMakeLists.txt
add_subdirectory(src/lib/audio)

# Link against it
target_link_libraries(your_target PRIVATE zannaaud)
```

The library will use the parent project's test and example build settings.

## Library Output

- **Static library**: `libzannaaud.a` (Unix) or `zannaaud.lib` (Windows)
- **Location**: `build-aud/lib/` (standalone) or `build/lib/` (integrated)

## Quick Start

### Minimal Example

```c
#include "vaud.h"

int main(void) {
    // Create audio context
    vaud_context_t ctx = vaud_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create audio context: %s\n", vaud_get_last_error());
        return 1;
    }

    // Load a sound effect
    vaud_sound_t explosion = vaud_load_sound(ctx, "explosion.wav");
    if (!explosion) {
        fprintf(stderr, "Failed to load sound: %s\n", vaud_get_last_error());
        vaud_destroy(ctx);
        return 1;
    }

    // Play the sound
    vaud_voice_id voice = vaud_play(explosion);

    // Wait for it to finish
    while (vaud_voice_is_playing(ctx, voice)) {
        // In a real app, you'd be doing other things here
        usleep(10000);
    }

    // Cleanup
    vaud_free_sound(explosion);
    vaud_destroy(ctx);
    return 0;
}
```

Compile and link:

```bash
# macOS
gcc main.c -o myapp -Iinclude -Llib -lzannaaud -framework AudioToolbox

# Linux
gcc main.c -o myapp -Iinclude -Llib -lzannaaud -lasound -lpthread
```

### Music Streaming Example

```c
#include "vaud.h"

int main(void) {
    vaud_context_t ctx = vaud_create();

    // Load music for streaming
    vaud_music_t music = vaud_load_music(ctx, "background_music.wav");

    // Start playing with loop enabled
    vaud_music_play(music, 1);  // 1 = loop

    // Adjust volume
    vaud_music_set_volume(music, 0.8f);  // 80% volume

    // Main loop - music plays in background
    while (running) {
        // Your game/app logic here

        // Check position
        float pos = vaud_music_get_position(music);
        float duration = vaud_music_get_duration(music);
        printf("Playing: %.1f / %.1f seconds\n", pos, duration);
    }

    // Cleanup
    vaud_free_music(music);
    vaud_destroy(ctx);
    return 0;
}
```

## API Overview

### Context Management

```c
vaud_context_t vaud_create(void);
void vaud_destroy(vaud_context_t ctx);
void vaud_set_master_volume(vaud_context_t ctx, float volume);
float vaud_get_master_volume(vaud_context_t ctx);
void vaud_pause_all(vaud_context_t ctx);
void vaud_resume_all(vaud_context_t ctx);
```

`vaud_destroy` stops playback, shuts down the platform backend, and detaches
caller-owned sound/music handles. Existing handles can still be passed to
`vaud_free_sound` or `vaud_free_music` after the context has been destroyed.
Volume parameters are sanitized before reaching the mixer: non-finite values
become 0.0 and finite values outside 0.0..1.0 are clamped.

### Sound Effects

```c
vaud_sound_t vaud_load_sound(vaud_context_t ctx, const char* path);
vaud_sound_t vaud_load_sound_mem(vaud_context_t ctx, const void* data, size_t size);
void vaud_free_sound(vaud_sound_t sound);

vaud_voice_id vaud_play(vaud_sound_t sound);
vaud_voice_id vaud_play_ex(vaud_sound_t sound, float volume, float pan);
vaud_voice_id vaud_play_loop(vaud_sound_t sound, float volume, float pan);

void vaud_stop_voice(vaud_context_t ctx, vaud_voice_id voice);
void vaud_set_voice_volume(vaud_context_t ctx, vaud_voice_id voice, float volume);
void vaud_set_voice_pan(vaud_context_t ctx, vaud_voice_id voice, float pan);
int vaud_voice_is_playing(vaud_context_t ctx, vaud_voice_id voice);
```

### Music Streaming

```c
vaud_music_t vaud_load_music(vaud_context_t ctx, const char* path);
void vaud_free_music(vaud_music_t music);

void vaud_music_play(vaud_music_t music, int loop);
void vaud_music_stop(vaud_music_t music);
void vaud_music_pause(vaud_music_t music);
void vaud_music_resume(vaud_music_t music);

void vaud_music_set_volume(vaud_music_t music, float volume);
float vaud_music_get_volume(vaud_music_t music);
int vaud_music_is_playing(vaud_music_t music);
void vaud_music_seek(vaud_music_t music, float seconds);
float vaud_music_get_position(vaud_music_t music);
float vaud_music_get_duration(vaud_music_t music);
```

Streaming music resampling carries fractional source position and decoded
leftovers across buffer boundaries, so non-44.1 kHz music preserves duration
instead of dropping frames at refill boundaries. `vaud_music_seek` ignores NaN
or infinity and clamps finite offsets to the known stream duration. Calling
`vaud_music_play` on a stopped stream restarts from the beginning, including
after the stream has reached EOF.

Applications that use streaming music should call `vaud_update(ctx)` regularly
from the control/update thread, typically once per frame. `vaud_update` refills
empty stream buffers and processes loop rewinds. It claims empty buffer slots
under the context mutex, releases the mutex while doing file I/O or codec work,
then commits the decoded frames. The realtime mixer consumes already-decoded
buffers only and can continue mixing the current buffer while another slot is
being refilled; if the current slot itself is being rebuilt, that stream is
silent for that callback. Free/detach waits for any in-flight refill token before
releasing decoder and buffer storage.

### Error Handling

```c
const char* vaud_get_last_error(void);
void vaud_clear_error(void);
```

## Audio Format

### Internal Format

- **Sample rate**: 44100 Hz
- **Bit depth**: 16-bit signed
- **Channels**: Stereo (2 channels)

### Supported Input Formats

- **WAV files** (RIFF format)
    - 8-bit unsigned PCM
    - 16-bit signed PCM
    - 24-bit signed PCM
    - 32-bit signed PCM
    - 32-bit IEEE float
    - Mono or stereo
    - Any sample rate (automatically resampled)
    - `fmt` `blockAlign`/`byteRate` and `data` frame alignment must be valid

## Threading Model

ZannaAUD is designed for thread-safe playback control and uses a dedicated
audio thread:

- Playback, pause/resume, volume, pan, and query functions can be called from
  any non-destroying context thread
- Audio mixing runs on a dedicated background thread
- Voice allocations and music state changes are protected by mutex
- Streaming decode, seek, and loop-refill work runs on the control/update thread
  while holding the context mutex and a per-stream refill token
- The realtime mixer never waits for control-thread work. If the state mutex is
  already held, the current callback is filled with silence rather than blocking
  behind decode, seek, free, or destroy activity.
- Do not call `vaud_destroy`, `vaud_free_sound`, or `vaud_free_music`
  concurrently with other operations on the same context or handle. Stop worker
  activity first, then destroy/free handles.

**Correct usage:**

```c
// GOOD: Can call from any thread
void on_player_shoot(void) {
    vaud_play(laser_sound);  // Thread-safe
}

void on_music_button(void) {
    if (vaud_music_is_playing(music)) {
        vaud_music_pause(music);  // Thread-safe
    } else {
        vaud_music_resume(music);  // Thread-safe
    }
}
```

## Architecture

### Software Mixer

The mixer combines up to 32 simultaneous voices plus active music streams:

1. Each voice has independent volume and stereo panning
2. 32-bit accumulator prevents clipping during mixing
3. Soft limiting algorithm prevents harsh distortion
4. Constant-power pan law for natural stereo imaging
5. Non-finite voice/music/master volumes and pans are normalized before mixing
6. Backend periods larger than the fixed accumulator are rendered in bounded
   chunks instead of being replaced with silence

### Voice Management

When all voices are in use:

1. First attempts to steal oldest non-looping voice
2. If all voices are looping, steals absolute oldest voice
3. New sound starts immediately on stolen voice

### Music Streaming

Music uses triple-buffering for gapless playback:

1. Only a small portion is in memory at any time (~50ms buffer)
2. `vaud_update` refills buffers outside the realtime mixer callback
3. Refills are serialized against seek/stop/play/free so published music buffers
   stay owned by a live stream
4. Seamless looping with no audible gap

On Windows, the WASAPI backend negotiates the shared-mode render format and
converts the internal 44.1 kHz stereo S16 mixer output to common PCM or float
endpoint formats before releasing the render buffer.

## Directory Structure

```
src/lib/audio/
├── include/
│   ├── vaud.h              # Public API
│   └── vaud_config.h       # Configuration macros
├── src/
│   ├── vaud.c              # Core implementation
│   ├── vaud_wav.c          # WAV file parser
│   ├── vaud_mixer.c        # Software mixer
│   ├── vaud_internal.h     # Internal structures
│   ├── vaud_platform_macos.m    # macOS backend
│   ├── vaud_platform_linux.c    # Linux backend
│   └── vaud_platform_win32.c    # Windows backend
├── CMakeLists.txt          # Build configuration
└── README.md               # This file
```

## Zanna.Sound API (Zannalang)

ZannaAUD is exposed to Zannalang through the `Zanna.Sound` namespace:

### Sound Effects

```zanna
// Load a sound effect
let explosion = Zanna.Sound.Sound.Load("explosion.wav")

// Play with default settings
let voice = explosion.Play()

// Play with volume and pan
let voice = explosion.PlayEx(80, 0)  // 80% volume, center pan

// Play looped
let voice = explosion.PlayLoop(100, -50)  // Full volume, pan left

// Control playing voice
Zanna.Sound.Voice.Stop(voice)
Zanna.Sound.Voice.SetVolume(voice, 50)
Zanna.Sound.Voice.SetPan(voice, 100)  // Pan right
```

### Music Streaming

```zanna
// Load music
let bgm = Zanna.Sound.Music.Load("background.wav")

// Play with loop
bgm.Play(1)

// Control playback
bgm.Pause()
bgm.Resume()
bgm.Stop()

// Properties
let vol = bgm.Volume
bgm.Volume = 70
let pos = bgm.Position
let dur = bgm.Duration
```

### Audio System

```zanna
// Master volume (0-100)
Zanna.Sound.Audio.SetMasterVolume(80)
let vol = Zanna.Sound.Audio.GetMasterVolume()

// Pause/resume all audio
Zanna.Sound.Audio.PauseAll()
Zanna.Sound.Audio.ResumeAll()

// Stop all sound effects (but not music)
Zanna.Sound.Audio.StopAllSounds()
```

## Requirements

### Build Requirements

- **C11-compliant compiler** (GCC, Clang, or MSVC)
- **CMake 3.10+**
- **Platform SDK**:
    - macOS: Xcode Command Line Tools (AudioToolbox framework)
    - Linux: ALSA development libraries (`libasound2-dev`)
    - Windows: Windows SDK (WASAPI, Windows Vista+)

### Runtime Requirements

- **macOS 10.10+** (AudioToolbox)
- **Linux**: ALSA audio system
- **Windows Vista+** (WASAPI)

## Performance

- **Latency**: ~50ms (configurable buffer size)
- **Voices**: 32 simultaneous sounds
- **Memory**: ~8KB per loaded sound per second of audio
- **CPU**: Minimal (software mixing is very efficient)

## License

ZannaAUD is part of the Zanna project and distributed under the GNU GPL v3.
See [LICENSE](../../../LICENSE) for details.

## Contributing

ZannaAUD is part of the larger Zanna project. For questions or contributions, see the
main [Zanna documentation](../../../docs/README.md).
