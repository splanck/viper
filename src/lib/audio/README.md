# ViperAUD - Cross-Platform Audio Library

**Version:** 1.0.0
**Status:** Complete (All Platforms)

ViperAUD is a cross-platform audio library written in C99 that provides sound effect playback and music streaming
with zero external dependencies (only OS-level audio APIs).

## Features

- **Pure C99** - No external dependencies (no SDL_mixer, OpenAL, etc.)
- **Software mixing** - 32 simultaneous voices with panning and volume control
- **Cross-platform** - Native backends for macOS (AudioQueue), Linux (ALSA), Windows (WASAPI)
- **Sound effects** - Load-and-play with automatic voice management
- **Music streaming** - Memory-efficient streaming for long audio files
- **WAV support** - 8-bit and 16-bit PCM, mono or stereo, any sample rate
- **Thread-safe** - Audio runs on dedicated thread, all API calls are thread-safe

## Platform Support

| Platform | Status          | Backend                                      |
|----------|-----------------|----------------------------------------------|
| macOS    | Complete        | AudioQueue (`src/vaud_platform_macos.m`)     |
| Linux    | Complete        | ALSA (`src/vaud_platform_linux.c`)           |
| Windows  | Complete        | WASAPI (`src/vaud_platform_win32.c`)         |

## Building Standalone

From the **Viper repository root**, configure and build as a standalone project:

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

## Building as Part of Viper

When included via `add_subdirectory()` from a parent CMake project (like Viper), ViperAUD automatically integrates:

```cmake
# In parent CMakeLists.txt
add_subdirectory(src/lib/audio)

# Link against it
target_link_libraries(your_target PRIVATE viperaud)
```

The library will use the parent project's test and example build settings.

## Library Output

- **Static library**: `libviperaud.a` (Unix) or `viperaud.lib` (Windows)
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
gcc main.c -o myapp -Iinclude -Llib -lviperaud -framework AudioToolbox

# Linux
gcc main.c -o myapp -Iinclude -Llib -lviperaud -lasound -lpthread
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
  - Mono or stereo
  - Any sample rate (automatically resampled)

## Threading Model

ViperAUD is **thread-safe** and uses a dedicated audio thread:

- All API functions can be called from any thread
- Audio mixing runs on a dedicated background thread
- Voice allocations and music state changes are protected by mutex

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

### Voice Management

When all voices are in use:
1. First attempts to steal oldest non-looping voice
2. If all voices are looping, steals absolute oldest voice
3. New sound starts immediately on stolen voice

### Music Streaming

Music uses triple-buffering for gapless playback:
1. Only a small portion is in memory at any time (~50ms buffer)
2. Background refills as buffers are consumed
3. Seamless looping with no audible gap

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

## Viper.Sound API (Viperlang)

ViperAUD is exposed to Viperlang through the `Viper.Sound` namespace:

### Sound Effects

```viper
// Load a sound effect
let explosion = Viper.Sound.Sound.Load("explosion.wav")

// Play with default settings
let voice = explosion.Play()

// Play with volume and pan
let voice = explosion.PlayEx(80, 0)  // 80% volume, center pan

// Play looped
let voice = explosion.PlayLoop(100, -50)  // Full volume, pan left

// Control playing voice
Viper.Sound.Voice.Stop(voice)
Viper.Sound.Voice.SetVolume(voice, 50)
Viper.Sound.Voice.SetPan(voice, 100)  // Pan right
```

### Music Streaming

```viper
// Load music
let bgm = Viper.Sound.Music.Load("background.wav")

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

```viper
// Master volume (0-100)
Viper.Sound.Audio.SetMasterVolume(80)
let vol = Viper.Sound.Audio.GetMasterVolume()

// Pause/resume all audio
Viper.Sound.Audio.PauseAll()
Viper.Sound.Audio.ResumeAll()

// Stop all sound effects (but not music)
Viper.Sound.Audio.StopAllSounds()
```

## Requirements

### Build Requirements

- **C99-compliant compiler** (GCC, Clang, or MSVC)
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

ViperAUD is part of the Viper project and distributed under the GNU GPL v3.
See [LICENSE](../../../LICENSE) for details.

## Contributing

ViperAUD is part of the larger Viper project. For questions or contributions, see the
main [Viper documentation](../../../docs/README.md).
