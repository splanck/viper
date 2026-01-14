# Audio

> Sound effects and music playback for games and applications.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Audio.Sound](#viperaudiosoound)
- [Viper.Audio.Music](#viperaudiomusic)
- [Viper.Audio (Static)](#viperaudio-static)

---

## Viper.Audio.Sound

Sound effect class for short audio clips. Sounds are loaded entirely into memory for low-latency playback.

**Type:** Instance (obj)
**Constructor:** `Viper.Audio.Sound.Load(path)`

### Static Methods

| Method       | Signature        | Description                                       |
|--------------|------------------|---------------------------------------------------|
| `Load(path)` | `Sound(String)`  | Load a sound from a WAV file. Returns NULL on failure |

### Methods

| Method                   | Signature                          | Description                                              |
|--------------------------|------------------------------------|----------------------------------------------------------|
| `Play()`                 | `Integer()`                        | Play the sound once. Returns voice ID for control        |
| `PlayEx(volume, pan)`    | `Integer(Integer, Integer)`        | Play with volume (0-100) and pan (-100 to 100)           |
| `PlayLoop(volume, pan)`  | `Integer(Integer, Integer)`        | Play looped with volume and pan. Returns voice ID        |

### Voice Control

After playing a sound, you receive a voice ID that can be used to control playback:

| Function                           | Description                                    |
|------------------------------------|------------------------------------------------|
| `Viper.Audio.StopVoice(id)`        | Stop a playing voice                           |
| `Viper.Audio.SetVoiceVolume(id, vol)` | Set voice volume (0-100)                    |
| `Viper.Audio.SetVoicePan(id, pan)` | Set voice pan (-100 left, 0 center, 100 right) |
| `Viper.Audio.IsVoicePlaying(id)`   | Returns 1 if voice is still playing            |

### Example

```basic
' Load sound effects
DIM laser AS Viper.Audio.Sound
DIM explosion AS Viper.Audio.Sound

laser = Viper.Audio.Sound.Load("laser.wav")
explosion = Viper.Audio.Sound.Load("explosion.wav")

IF laser <> NULL THEN
    ' Play with default settings
    laser.Play()

    ' Play with custom volume and pan
    DIM voiceId AS INTEGER
    voiceId = laser.PlayEx(80, -50)  ' 80% volume, panned left

    ' Control the playing sound
    Viper.Audio.SetVoiceVolume(voiceId, 50)
END IF

' Play looping background sound
DIM engineSound AS INTEGER
engineSound = laser.PlayLoop(60, 0)

' Later, stop the loop
Viper.Audio.StopVoice(engineSound)
```

---

## Viper.Audio.Music

Streaming music class for longer audio tracks. Music is streamed from disk for memory efficiency.

**Type:** Instance (obj)
**Constructor:** `Viper.Audio.Music.Load(path)`

### Static Methods

| Method       | Signature        | Description                                       |
|--------------|------------------|---------------------------------------------------|
| `Load(path)` | `Music(String)`  | Load music from a WAV file. Returns NULL on failure |

### Properties

| Property   | Type    | Access | Description                        |
|------------|---------|--------|------------------------------------|
| `Volume`   | Integer | R/W    | Playback volume (0-100)            |
| `Playing`  | Integer | Read   | 1 if currently playing, 0 if not   |
| `Position` | Integer | Read   | Current position in milliseconds   |
| `Duration` | Integer | Read   | Total duration in milliseconds     |

### Methods

| Method         | Signature           | Description                              |
|----------------|---------------------|------------------------------------------|
| `Play(loop)`   | `Void(Integer)`     | Start playback (1 = loop, 0 = one-shot)  |
| `Stop()`       | `Void()`            | Stop playback                            |
| `Pause()`      | `Void()`            | Pause playback                           |
| `Resume()`     | `Void()`            | Resume paused playback                   |
| `Seek(ms)`     | `Void(Integer)`     | Seek to position in milliseconds         |

### Example

```basic
' Load background music
DIM bgMusic AS Viper.Audio.Music
bgMusic = Viper.Audio.Music.Load("background.wav")

IF bgMusic <> NULL THEN
    ' Set initial volume
    bgMusic.Volume = 70

    ' Start playing (looped)
    bgMusic.Play(1)

    ' Check duration
    PRINT "Duration: "; bgMusic.Duration; " ms"

    ' Game loop
    DO WHILE canvas.ShouldClose = 0
        canvas.Poll()

        ' Display current position
        PRINT "Position: "; bgMusic.Position; " / "; bgMusic.Duration

        ' Pause/resume with space bar
        IF Viper.Input.Keyboard.Pressed(32) THEN
            IF bgMusic.Playing = 1 THEN
                bgMusic.Pause()
            ELSE
                bgMusic.Resume()
            END IF
        END IF

        canvas.Flip()
    LOOP

    ' Cleanup
    bgMusic.Stop()
END IF
```

---

## Viper.Audio (Static)

Global audio system control functions.

**Type:** Static utility class

### Properties

| Property       | Type    | Access | Description                    |
|----------------|---------|--------|--------------------------------|
| `MasterVolume` | Integer | R/W    | Master volume for all audio (0-100) |

### Methods

| Method                          | Signature                      | Description                                    |
|---------------------------------|--------------------------------|------------------------------------------------|
| `PauseAll()`                    | `Void()`                       | Pause all audio playback                       |
| `ResumeAll()`                   | `Void()`                       | Resume all audio playback                      |
| `StopAllSounds()`               | `Void()`                       | Stop all playing sounds (not music)            |
| `StopVoice(id)`                 | `Void(Integer)`                | Stop a specific voice                          |
| `SetVoiceVolume(id, vol)`       | `Void(Integer, Integer)`       | Set volume for a voice (0-100)                 |
| `SetVoicePan(id, pan)`          | `Void(Integer, Integer)`       | Set pan for a voice (-100 to 100)              |
| `IsVoicePlaying(id)`            | `Integer(Integer)`             | Check if voice is playing (returns 1 or 0)     |

### Example

```basic
' Set master volume
Viper.Audio.MasterVolume = 80

' Pause all audio during pause menu
SUB ShowPauseMenu()
    Viper.Audio.PauseAll()

    ' ... show menu ...

    Viper.Audio.ResumeAll()
END SUB

' Stop all sound effects when changing levels
SUB ChangeLevel(level AS INTEGER)
    Viper.Audio.StopAllSounds()

    ' ... load new level ...
END SUB
```

---

## Audio File Format

Currently, Viper Audio supports **WAV files** only:

- **Format:** PCM (uncompressed)
- **Bit depth:** 8-bit or 16-bit
- **Channels:** Mono or Stereo
- **Sample rate:** Any (44100 Hz recommended)

### Tips

1. **Sound Effects**: Use short, mono WAV files for best performance
2. **Music**: Use stereo WAV files at 44100 Hz for best quality
3. **Memory**: Sounds are loaded entirely into memory; keep them short
4. **Streaming**: Music is streamed, so longer tracks are memory-efficient

---

## See Also

- [Graphics](graphics.md) - Canvas and visual rendering
- [Input](input.md) - Keyboard and mouse input
- [Time](time.md) - Timing for audio synchronization
