# Audio

> Sound effects and music playback for games and applications.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Sound.Sound](#vipersoundsound)
- [Viper.Sound.Music](#vipersoundmusic)
- [Viper.Sound.Voice](#vipersoundvoice)
- [Viper.Sound.Audio (Static)](#vipersoundaudio-static)
- [Viper.Sound.Playlist](#vipersoundplaylist)
- [Audio File Format](#audio-file-format)
- [Limits and Behaviors](#limits-and-behaviors)

---

## Viper.Sound.Sound

Sound effect class for short audio clips. Sounds are loaded entirely into memory for low-latency playback.

**Type:** Instance (obj)
**Constructor:** `Viper.Sound.Sound.Load(path)`

### Static Methods

| Method       | Signature        | Description                                       |
|--------------|------------------|---------------------------------------------------|
| `Load(path)` | `Sound(String)`  | Load a sound from a WAV file. Returns `null` on failure |

### Methods

| Method                   | Signature                          | Description                                              |
|--------------------------|------------------------------------|----------------------------------------------------------|
| `Play()`                 | `Integer()`                        | Play the sound once. Returns voice ID for control        |
| `PlayEx(volume, pan)`    | `Integer(Integer, Integer)`        | Play with volume (0–100) and pan (−100 to +100)          |
| `PlayLoop(volume, pan)`  | `Integer(Integer, Integer)`        | Play looped with volume and pan. Returns voice ID        |

> **Voice limit:** Up to 32 sounds may play simultaneously. A 33rd `Play()` call stops
> the **oldest** playing (non-looping) sound to make room — called LRU eviction. The
> evicted sound stops with no error or notification. Looping sounds are evicted only
> when all 32 voices are looping.

### Voice Control

After playing a sound, you receive a voice ID that can be used with `Viper.Sound.Voice`:

| Method                                 | Description                                       |
|----------------------------------------|---------------------------------------------------|
| `Viper.Sound.Voice.IsPlaying(id)`      | Returns 1 if voice is still playing               |
| `Viper.Sound.Voice.SetPan(id, pan)`    | Set pan: −100 = hard left, 0 = center, 100 = right |
| `Viper.Sound.Voice.SetVolume(id, vol)` | Set voice volume (0–100)                          |
| `Viper.Sound.Voice.Stop(id)`           | Stop a playing voice                              |

### Zia Example

```rust
module SoundDemo;

bind Viper.Terminal;
bind Viper.Sound.Audio as Audio;
bind Viper.Sound.Sound as Sound;
bind Viper.Sound.Voice as Voice;
bind Viper.Fmt as Fmt;

func start() {
    Audio.Init();

    var snd = Sound.Load("laser.wav");
    if snd != null {
        // Play with default settings
        var id = snd.Play();

        // Play with volume and pan
        var id2 = snd.PlayEx(80, -50);  // 80% volume, panned left

        // Control the playing voice
        Voice.SetVolume(id2, 50);
        Say("Playing: " + Fmt.Int(Voice.IsPlaying(id2)));

        // Play looping
        var loopId = snd.PlayLoop(60, 0);
        Voice.Stop(loopId);
    }

    Audio.Shutdown();
}
```

### Example

```basic
' Load sound effects
DIM laser AS Viper.Sound.Sound
DIM explosion AS Viper.Sound.Sound

laser = Viper.Sound.Sound.Load("laser.wav")
explosion = Viper.Sound.Sound.Load("explosion.wav")

IF laser <> NULL THEN
    ' Play with default settings
    laser.Play()

    ' Play with custom volume and pan
    DIM voiceId AS INTEGER
    voiceId = laser.PlayEx(80, -50)  ' 80% volume, panned left

    ' Control the playing sound
    Viper.Sound.Voice.SetVolume(voiceId, 50)
END IF

' Play looping background sound
DIM engineSound AS INTEGER
engineSound = laser.PlayLoop(60, 0)

' Later, stop the loop
Viper.Sound.Voice.Stop(engineSound)
```

---

## Viper.Sound.Music

Streaming music class for longer audio tracks. Music is streamed from disk for memory efficiency.

**Type:** Instance (obj)
**Constructor:** `Viper.Sound.Music.Load(path)`

> **Concurrent limit:** Up to **4** music streams may be loaded at the same time.
> `Music.Load()` returns `null` if this limit is exceeded. Stop and free unused
> streams before loading new ones.

> **Sample rate:** Music files must be **44100 Hz**. Files at other sample rates
> (e.g., 48000 Hz) will play at incorrect pitch and speed. Sound effects are
> automatically resampled at load time; music streams are not.

### Static Methods

| Method       | Signature        | Description                                       |
|--------------|------------------|---------------------------------------------------|
| `Load(path)` | `Music(String)`  | Load music from a WAV file. Returns `null` on failure or when the 4-stream limit is reached |

### Properties

| Property   | Type    | Access | Description                        |
|------------|---------|--------|------------------------------------|
| `Duration` | Integer | Read   | Total duration in milliseconds     |
| `Position` | Integer | Read   | Current position in milliseconds   |
| `Volume`   | Integer | R/W    | Playback volume (0–100)            |

### Methods

| Method         | Signature           | Description                              |
|----------------|---------------------|------------------------------------------|
| `IsPlaying()`  | `Integer()`         | Returns 1 if currently playing, 0 if not |
| `Pause()`      | `Void()`            | Pause playback                           |
| `Play(loop)`   | `Void(Integer)`     | Start playback (1 = loop, 0 = one-shot)  |
| `Resume()`     | `Void()`            | Resume paused playback                   |
| `Seek(ms)`     | `Void(Integer)`     | Seek to position in milliseconds         |
| `Stop()`       | `Void()`            | Stop playback                            |

### Zia Example

```rust
module MusicDemo;

bind Viper.Terminal;
bind Viper.Sound.Audio as Audio;
bind Viper.Sound.Music as Music;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Fmt as Fmt;

func start() {
    Audio.Init();
    var c = Canvas.New("Music Player", 400, 200);

    var mus = Music.Load("background.wav");  // Must be 44100 Hz WAV
    if mus != null {
        mus.set_Volume(70);
        mus.Play(1);  // Looped

        Say("Duration: " + Fmt.Int(mus.get_Duration()) + " ms");

        while c.get_ShouldClose() == 0 {
            c.Poll();
            Say("Pos: " + Fmt.Int(mus.get_Position()));
            c.Flip();
        }

        mus.Stop();
    }

    Audio.Shutdown();
}
```

### Example

```basic
' Load background music (must be 44100 Hz WAV)
DIM bgMusic AS Viper.Sound.Music
bgMusic = Viper.Sound.Music.Load("background.wav")

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
        IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_SPACE) THEN
            IF bgMusic.IsPlaying() = 1 THEN
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

## Viper.Sound.Voice

Static class for controlling individual playing voices (sound instances).

**Type:** Static utility class

### Methods

| Method                     | Signature                      | Description                                              |
|----------------------------|--------------------------------|----------------------------------------------------------|
| `IsPlaying(id)`            | `Integer(Integer)`             | Check if voice is playing (returns 1 or 0)               |
| `SetPan(id, pan)`          | `Void(Integer, Integer)`       | Pan: −100 = hard left, 0 = center, +100 = hard right     |
| `SetVolume(id, vol)`       | `Void(Integer, Integer)`       | Set volume for a voice (0–100)                           |
| `Stop(id)`                 | `Void(Integer)`                | Stop a playing voice                                     |

> **Pan law:** At `pan=0` (center) the signal is equal in both channels. `pan=100`
> routes the full signal to the right channel with zero output on the left. The
> gain is applied linearly: `left = (100 − pan) / 200`, `right = (100 + pan) / 200`.

> **Invalid IDs:** All voice functions are safe to call with any integer ID. If the
> voice has already stopped or the ID was never valid, the call is a no-op.

### Zia Example

```rust
module VoiceDemo;

bind Viper.Terminal;
bind Viper.Sound;

func start() {
    // Initialize audio system
    Audio.Init();

    // Voice control (safe with invalid IDs)
    SayInt(Voice.IsPlaying(0));  // 0
    Voice.Stop(0);               // safe no-op
    Voice.SetVolume(0, 50);      // safe no-op
    Voice.SetPan(0, -50);        // safe no-op (panned left)

    // Master volume
    Audio.SetMasterVolume(80);
    SayInt(Audio.GetMasterVolume());  // 80

    // Global controls
    Audio.PauseAll();
    Audio.ResumeAll();
    Audio.StopAllSounds();

    // Cleanup
    Audio.Shutdown();
}
```

---

## Viper.Sound.Audio (Static)

Global audio system control functions.

**Type:** Static utility class

### Methods

| Method                          | Signature                      | Description                                       |
|---------------------------------|--------------------------------|---------------------------------------------------|
| `GetMasterVolume()`             | `Integer()`                    | Get current master volume (0–100)                 |
| `Init()`                        | `Integer()`                    | Initialize the audio system. Returns 1 on success |
| `PauseAll()`                    | `Void()`                       | Pause all audio playback                          |
| `ResumeAll()`                   | `Void()`                       | Resume all audio playback                         |
| `SetMasterVolume(vol)`          | `Void(Integer)`                | Set master volume for all audio (0–100)           |
| `Shutdown()`                    | `Void()`                       | Shut down the audio system                        |
| `StopAllSounds()`               | `Void()`                       | Stop all playing sounds (does not affect music)   |

### Zia Example

```rust
module AudioDemo;

bind Viper.Terminal;
bind Viper.Sound.Audio as Audio;
bind Viper.Fmt as Fmt;

func start() {
    var ok = Audio.Init();
    Say("Init: " + Fmt.Int(ok));

    Audio.SetMasterVolume(80);
    Say("Volume: " + Fmt.Int(Audio.GetMasterVolume()));

    Audio.PauseAll();
    Audio.ResumeAll();
    Audio.StopAllSounds();
    Audio.Shutdown();
    Say("Done");
}
```

### Example

```basic
' Initialize audio
Viper.Sound.Audio.Init()

' Set master volume
Viper.Sound.Audio.SetMasterVolume(80)

' Pause all audio during pause menu
SUB ShowPauseMenu()
    Viper.Sound.Audio.PauseAll()

    ' ... show menu ...

    Viper.Sound.Audio.ResumeAll()
END SUB

' Stop all sound effects when changing levels
SUB ChangeLevel(level AS INTEGER)
    Viper.Sound.Audio.StopAllSounds()

    ' ... load new level ...
END SUB

' Cleanup before exit
Viper.Sound.Audio.Shutdown()
```

---

## Viper.Sound.Playlist

Music playlist with queue management for sequential track playback.

**Type:** Instance (obj)
**Constructor:** `Viper.Sound.Playlist.New()`

### Track Management Methods

| Method              | Signature              | Description                                       |
|---------------------|------------------------|---------------------------------------------------|
| `Add(path)`         | `Void(String)`         | Add a music file to the end of the playlist       |
| `Clear()`           | `Void()`               | Remove all tracks from the playlist               |
| `Get(index)`        | `String(Integer)`      | Get the file path of a track at the given index   |
| `Insert(index, path)` | `Void(Integer, String)` | Insert a music file at a specific position      |
| `Remove(index)`     | `Void(Integer)`        | Remove a track by index                           |

### Playback Control Methods

| Method        | Signature       | Description                                                   |
|---------------|-----------------|---------------------------------------------------------------|
| `Jump(index)` | `Void(Integer)` | Jump to a specific track by index                             |
| `Next()`      | `Void()`        | Skip to the next track                                        |
| `Pause()`     | `Void()`        | Pause playback                                                |
| `Play()`      | `Void()`        | Start playing from the beginning or resume                    |
| `Prev()`      | `Void()`        | Go back to the previous track                                 |
| `Stop()`      | `Void()`        | Stop playback and reset to beginning                          |
| `Update()`    | `Void()`        | **Required — call once per tick for automatic track advance** |

> ⚠️ **`Update()` is required for automatic track advancement.** Call it once per
> game or app tick (inside your main loop). If omitted, the playlist will stop
> after each track finishes and never advance to the next one.

### Properties

| Property    | Type    | Access | Description                                            |
|-------------|---------|--------|--------------------------------------------------------|
| `Current`   | Integer | Read   | Current track index (−1 if empty or not started)       |
| `IsPaused`  | Boolean | Read   | True if the playlist is paused                         |
| `IsPlaying` | Boolean | Read   | True if the playlist is currently playing              |
| `Len`       | Integer | Read   | Number of tracks in the playlist                       |
| `Repeat`    | Integer | R/W    | 0 = no repeat, 1 = repeat all, 2 = repeat one track   |
| `Shuffle`   | Boolean | R/W    | Enable/disable shuffle mode                            |
| `Volume`    | Integer | R/W    | Playback volume (0–100)                                |

### Zia Example

```rust
module PlaylistDemo;

bind Viper.Terminal;
bind Viper.Sound.Audio as Audio;
bind Viper.Sound.Playlist as Playlist;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Fmt as Fmt;

func start() {
    Audio.Init();
    var c = Canvas.New("Playlist", 400, 200);

    var pl = Playlist.New();
    pl.Add("track1.wav");
    pl.Add("track2.wav");
    pl.Add("track3.wav");
    pl.set_Volume(80);
    pl.set_Shuffle(0);
    pl.set_Repeat(1);  // Repeat all
    pl.Play();

    while c.get_ShouldClose() == 0 {
        c.Poll();
        pl.Update();  // Required — call every tick

        Say("Track " + Fmt.Int(pl.get_Current()) + " of " + Fmt.Int(pl.get_Len()));

        c.Flip();
    }

    pl.Stop();
    Audio.Shutdown();
}
```

### Example

```basic
' Create and populate a playlist
DIM pl AS OBJECT = Viper.Sound.Playlist.New()

pl.Add("track1.wav")
pl.Add("track2.wav")
pl.Add("track3.wav")

' Configure playback
pl.Volume = 80
pl.Shuffle = 0
pl.Repeat = 1  ' Repeat all

' Start playing
pl.Play()

' In game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    pl.Update()  ' Required for auto-advance

    PRINT "Track "; pl.Current; " of "; pl.Len

    ' Skip to next track
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_RIGHT) THEN
        pl.Next()
    END IF

    ' Previous track
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_LEFT) THEN
        pl.Prev()
    END IF

    canvas.Flip()
LOOP

pl.Stop()
```

---

## Audio File Format

Viper Audio supports **WAV (PCM) files only**. MP3, OGG, FLAC, and other
compressed formats are not supported.

| Property    | Supported values                           |
|-------------|---------------------------------------------|
| Format      | PCM (uncompressed) only                    |
| Bit depth   | 8-bit or 16-bit                            |
| Channels    | Mono or Stereo                             |
| Sample rate | **44100 Hz required for Music**; any rate accepted for Sound (resampled at load time) |

### Tips

1. **Sound effects:** Any sample rate works — the engine resamples to 44100 Hz at load time.
2. **Music streams:** Must be encoded at exactly **44100 Hz**. Files at 48000 Hz or other rates will play at the wrong pitch and speed.
3. **Memory:** Sounds are loaded entirely into memory; keep individual clips short.
4. **Streaming:** Music is streamed from disk, so long tracks use very little memory.
5. **Encoding:** Use a tool such as ffmpeg to convert audio to the correct format:
   ```
   ffmpeg -i input.mp3 -ar 44100 -ac 2 -f wav output.wav      # music
   ffmpeg -i input.mp3 -ar 44100 -ac 1 -f wav sfx.wav         # mono sound effect
   ```

---

## Limits and Behaviors

| Limit | Value | Notes |
|-------|-------|-------|
| Max simultaneous Sound voices | **32** | Oldest non-looping voice is evicted (LRU) when full |
| Max simultaneous Music streams | **4** | `Music.Load()` returns `null` when exceeded |
| Supported audio format | **WAV PCM only** | 8/16-bit, mono/stereo |
| Music sample rate | **44100 Hz** | Other rates play at incorrect pitch |
| Sound sample rate | Any | Resampled to 44100 Hz at load time |
| Pan range | −100 to +100 | −100 = hard left, 0 = center, +100 = hard right |
| Volume range | 0 to 100 | Applies to Sound, Music, Voice, and Playlist |

---

## See Also

- [Graphics](graphics/README.md) - Canvas and visual rendering
- [Input](input.md) - Keyboard and mouse input
- [Time](time.md) - Timing for audio synchronization
