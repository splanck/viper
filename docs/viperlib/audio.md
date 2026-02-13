# Audio

> Sound effects and music playback for games and applications.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Sound.Sound](#vipersoundsound)
- [Viper.Sound.Music](#vipersoundmusic)
- [Viper.Sound.Voice](#vipersoundvoice)
- [Viper.Sound.Audio (Static)](#vipersoundaudio-static)
- [Viper.Sound.Playlist](#vipersoundplaylist)

---

## Viper.Sound.Sound

Sound effect class for short audio clips. Sounds are loaded entirely into memory for low-latency playback.

**Type:** Instance (obj)
**Constructor:** `Viper.Sound.Sound.Load(path)`

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

After playing a sound, you receive a voice ID that can be used with `Viper.Sound.Voice`:

| Method                              | Description                                    |
|-------------------------------------|------------------------------------------------|
| `Viper.Sound.Voice.Stop(id)`        | Stop a playing voice                           |
| `Viper.Sound.Voice.SetVolume(id, vol)` | Set voice volume (0-100)                    |
| `Viper.Sound.Voice.SetPan(id, pan)` | Set voice pan (-100 left, 0 center, 100 right) |
| `Viper.Sound.Voice.IsPlaying(id)`   | Returns 1 if voice is still playing            |

### Zia Example

```zia
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
        var id2 = snd.PlayEx(80, -50);

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

### Static Methods

| Method       | Signature        | Description                                       |
|--------------|------------------|---------------------------------------------------|
| `Load(path)` | `Music(String)`  | Load music from a WAV file. Returns NULL on failure |

### Properties

| Property   | Type    | Access | Description                        |
|------------|---------|--------|------------------------------------|
| `Volume`   | Integer | R/W    | Playback volume (0-100)            |
| `Position` | Integer | Read   | Current position in milliseconds   |
| `Duration` | Integer | Read   | Total duration in milliseconds     |

### Methods

| Method         | Signature           | Description                              |
|----------------|---------------------|------------------------------------------|
| `Play(loop)`   | `Void(Integer)`     | Start playback (1 = loop, 0 = one-shot)  |
| `Stop()`       | `Void()`            | Stop playback                            |
| `Pause()`      | `Void()`            | Pause playback                           |
| `Resume()`     | `Void()`            | Resume paused playback                   |
| `IsPlaying()`  | `Integer()`         | Returns 1 if currently playing, 0 if not |
| `Seek(ms)`     | `Void(Integer)`     | Seek to position in milliseconds         |

### Zia Example

```zia
module MusicDemo;

bind Viper.Terminal;
bind Viper.Sound.Audio as Audio;
bind Viper.Sound.Music as Music;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Fmt as Fmt;

func start() {
    Audio.Init();
    var c = Canvas.New("Music Player", 400, 200);

    var mus = Music.Load("background.wav");
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
' Load background music
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

| Method                     | Signature                      | Description                                    |
|----------------------------|--------------------------------|------------------------------------------------|
| `Stop(id)`                 | `Void(Integer)`                | Stop a playing voice                           |
| `SetVolume(id, vol)`       | `Void(Integer, Integer)`       | Set volume for a voice (0-100)                 |
| `SetPan(id, pan)`          | `Void(Integer, Integer)`       | Set pan for a voice (-100 to 100)              |
| `IsPlaying(id)`            | `Integer(Integer)`             | Check if voice is playing (returns 1 or 0)     |

### Zia Example

```zia
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
    Voice.SetPan(0, -50);        // safe no-op

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

| Method                          | Signature                      | Description                                    |
|---------------------------------|--------------------------------|------------------------------------------------|
| `Init()`                        | `Integer()`                    | Initialize the audio system. Returns 1 on success |
| `Shutdown()`                    | `Void()`                       | Shut down the audio system                     |
| `SetMasterVolume(vol)`          | `Void(Integer)`                | Set master volume for all audio (0-100)        |
| `GetMasterVolume()`             | `Integer()`                    | Get current master volume                      |
| `PauseAll()`                    | `Void()`                       | Pause all audio playback                       |
| `ResumeAll()`                   | `Void()`                       | Resume all audio playback                      |
| `StopAllSounds()`               | `Void()`                       | Stop all playing sounds (not music)            |

### Zia Example

```zia
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
| `Insert(index, path)` | `Void(Integer, String)` | Insert a music file at a specific position      |
| `Remove(index)`     | `Void(Integer)`        | Remove a track by index                           |
| `Clear()`           | `Void()`               | Remove all tracks from the playlist               |
| `Get(index)`        | `String(Integer)`      | Get the file path of a track at the given index   |

### Playback Control Methods

| Method       | Signature       | Description                                        |
|--------------|-----------------|----------------------------------------------------|
| `Play()`     | `Void()`        | Start playing from the beginning or resume         |
| `Pause()`    | `Void()`        | Pause playback                                     |
| `Stop()`     | `Void()`        | Stop playback and reset to beginning               |
| `Next()`     | `Void()`        | Skip to the next track                             |
| `Prev()`     | `Void()`        | Go back to the previous track                      |
| `Jump(index)`| `Void(Integer)` | Jump to a specific track by index                  |
| `Update()`   | `Void()`        | Update playlist state (call each frame for auto-advance) |

### Properties

| Property    | Type    | Access | Description                                         |
|-------------|---------|--------|-----------------------------------------------------|
| `Len`       | Integer | Read   | Number of tracks in the playlist                    |
| `Current`   | Integer | Read   | Current track index (-1 if empty)                   |
| `IsPlaying` | Boolean | Read   | True if the playlist is currently playing           |
| `IsPaused`  | Boolean | Read   | True if the playlist is paused                      |
| `Volume`    | Integer | R/W    | Playback volume (0-100)                             |
| `Shuffle`   | Boolean | R/W    | Enable/disable shuffle mode                         |
| `Repeat`    | Integer | R/W    | Repeat mode: 0 = none, 1 = repeat all, 2 = repeat one |

### Zia Example

```zia
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
        pl.Update();

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
