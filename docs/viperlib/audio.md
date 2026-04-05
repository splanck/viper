---
status: active
audience: public
last-verified: 2026-04-05
---

# Audio

> Sound effects and music playback for games and applications.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Sound.Sound](#vipersoundsound)
- [Viper.Sound.Music](#vipersoundmusic)
- [Viper.Sound.Voice](#vipersoundvoice)
- [Viper.Sound.Audio (Static)](#vipersoundaudio-static)
- [Viper.Sound.SoundBank](#vipersoundsoundbank)
- [Viper.Sound.Synth](#vipersoundsynth)
- [Viper.Sound.MusicGen](#vipersoundmusicgen)
- [Mix Groups](#mix-groups)
- [Music Crossfading](#music-crossfading)
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
' Load background music (WAV, any sample rate)
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

> **See also:** `Viper.Sound.Playlist` provides queue-based music playlist management with shuffle, repeat modes, and auto-advance.

---

## Viper.Sound.SoundBank

Named sound registry that maps string names to loaded Sound objects. Games use SoundBank to manage sounds by name instead of managing Sound handles directly.

**Type:** Instance (obj)
**Constructor:** `SoundBank.New()`

### Properties

| Property | Type    | Access | Description                  |
|----------|---------|--------|------------------------------|
| `Count`  | Integer | Read   | Number of registered sounds  |

### Methods

| Method                     | Signature                          | Description                                                          |
|----------------------------|------------------------------------|----------------------------------------------------------------------|
| `Register(name, path)`    | `Integer(String, String)`          | Load WAV file and register under name. Returns 1 on success         |
| `RegisterSound(name, sound)` | `Integer(String, Sound)`        | Register an existing Sound object (e.g., from Synth). Returns 1 on success |
| `Play(name)`              | `Integer(String)`                  | Play named sound. Returns voice ID, or -1 if not found              |
| `PlayEx(name, vol, pan)`  | `Integer(String, Integer, Integer)` | Play with volume (0-100) and pan (-100 to 100)                     |
| `Has(name)`               | `Boolean(String)`                  | Check if name is registered                                         |
| `Get(name)`               | `Sound(String)`                    | Get the Sound object for a name, or null                            |
| `Remove(name)`            | `Void(String)`                     | Remove a named entry                                                |
| `Clear()`                 | `Void()`                           | Remove all entries                                                  |

Max 64 entries per bank. Names are truncated at 31 characters.

### Zia Example

```rust
module BankDemo;

bind Viper.Sound;

func start() {
    Audio.Init();
    var bank = SoundBank.New();

    // Load from files
    bank.Register("laser", "sfx/laser.wav");
    bank.Register("explode", "sfx/boom.wav");

    // Register synthesized sounds
    var coinSfx = Synth.Sfx(1);  // Coin preset
    bank.RegisterSound("coin", coinSfx);

    // Play by name
    bank.Play("laser");
    bank.PlayEx("explode", 80, -50);  // 80% vol, panned left

    Audio.Shutdown();
}
```

---

## Viper.Sound.Synth

Procedural sound synthesis — generates Sound objects from parameters without WAV files. All generated sounds are 16-bit PCM mono at 44100 Hz.

**Type:** Static utility class

### Waveform Types

| Constant  | Value | Description          |
|-----------|-------|----------------------|
| Sine      | 0     | Smooth sine wave     |
| Square    | 1     | Digital square wave  |
| Sawtooth  | 2     | Ramp/sawtooth wave   |
| Triangle  | 3     | Triangle wave        |

### SFX Preset Types

| Constant  | Value | Description                                       |
|-----------|-------|---------------------------------------------------|
| Jump      | 0     | Quick ascending frequency sweep (square wave)     |
| Coin      | 1     | Two-tone high-pitched pickup sound                |
| Hit       | 2     | Short noise burst with fast decay                 |
| Explosion | 3     | Longer noise burst with slow decay                |
| Powerup   | 4     | Ascending sweep with triangle wave                |
| Laser     | 5     | Quick descending sweep with sawtooth              |

### Methods

| Method                              | Signature                                  | Description                                                                                       |
|-------------------------------------|--------------------------------------------|---------------------------------------------------------------------------------------------------|
| `Tone(freq, duration, wave)`        | `Sound(Integer, Integer, Integer)`         | Generate a fixed-frequency tone. freq: Hz (20-20000), duration: ms (1-10000), wave: waveform type |
| `Sweep(startHz, endHz, duration, wave)` | `Sound(Integer, Integer, Integer, Integer)` | Generate a frequency sweep between two frequencies                                              |
| `Noise(duration, volume)`           | `Sound(Integer, Integer)`                  | Generate white noise with exponential decay. volume: 0-100                                        |
| `Sfx(type)`                         | `Sound(Integer)`                           | Generate a preset game sound effect                                                               |

### Zia Example

```rust
module SynthDemo;

bind Viper.Sound;

func start() {
    Audio.Init();

    // Generate tones
    var beep = Synth.Tone(440, 200, 0);       // A4, 200ms, sine
    var buzz = Synth.Tone(220, 100, 1);       // A3, 100ms, square

    // Frequency sweeps
    var rising = Synth.Sweep(200, 800, 300, 0);  // Rising sine
    var laser = Synth.Sweep(1500, 200, 120, 2);  // Falling sawtooth

    // White noise
    var static_noise = Synth.Noise(500, 80);  // 500ms, 80% volume

    // Preset SFX (no WAV files needed!)
    var jumpSnd = Synth.Sfx(0);   // Jump
    var coinSnd = Synth.Sfx(1);   // Coin
    var hitSnd  = Synth.Sfx(2);   // Hit
    var boomSnd = Synth.Sfx(3);   // Explosion

    // Play them
    beep.Play();
    jumpSnd.Play();

    // Register in a SoundBank for easy access
    var bank = SoundBank.New();
    bank.RegisterSound("jump", jumpSnd);
    bank.RegisterSound("coin", coinSnd);
    bank.Play("jump");

    Audio.Shutdown();
}
```

---

## Viper.Sound.MusicGen

Procedural music composition — a tracker-style sequencer that builds multi-channel songs with ADSR envelopes and chiptune effects. Generates a standard Sound object via pre-rendering, requiring zero external audio assets. Think NES/SNES-era music but at 44.1kHz with full ADSR envelopes.

**Type:** Mutable builder class

### Concepts

- **Centbeats:** Beat positions and durations use centbeats where 100 = 1 beat. At 120 BPM, 1 centbeat = 5ms. This provides sub-beat precision (16th notes = 25 centbeats, triplets = 33).
- **Centi-Hz:** Effect speeds (vibrato, tremolo, arpeggio) use centi-Hz where 100 = 1 Hz.
- **Cents:** Pitch offsets (detune, vibrato depth) use cents where 100 = 1 semitone.
- **MIDI Notes:** Standard MIDI numbering (60 = C4, 69 = A4 = 440 Hz, range 0-127).

### Waveform Types

| Constant  | Value | Description                              |
|-----------|-------|------------------------------------------|
| Sine      | 0     | Smooth sine wave (pads, soft leads)      |
| Square    | 1     | Pulse wave with variable duty cycle (classic chiptune lead) |
| Sawtooth  | 2     | Bright sawtooth (basses, strings)        |
| Triangle  | 3     | Soft triangle (NES bass channel)         |
| Noise     | 4     | Filtered noise (drums, percussion)       |

### Song Builder Methods

| Method                                        | Signature                                          | Description                                                                |
|-----------------------------------------------|----------------------------------------------------|----------------------------------------------------------------------------|
| `New(bpm)`                                    | `MusicGen(Integer)`                                | Create a new song at the given tempo (20-300 BPM)                          |
| `AddChannel(waveform)`                        | `Integer(Integer)`                                 | Add a channel with waveform type (0-4). Returns channel index, or -1 if full |
| `SetEnvelope(ch, attack, decay, sustain, release)` | `void(Integer, Integer, Integer, Integer, Integer)` | Set ADSR envelope: attack/decay/release in ms (0-5000), sustain in % (0-100) |
| `SetChannelVol(ch, volume)`                   | `void(Integer, Integer)`                           | Set channel volume (0-100, default 80)                                     |
| `SetDuty(ch, duty)`                           | `void(Integer, Integer)`                           | Set square wave duty cycle (1-99, default 50). NES values: 12, 25, 50, 75 |
| `SetPan(ch, pan)`                             | `void(Integer, Integer)`                           | Set stereo pan (-100=left, 0=center, 100=right)                            |
| `SetDetune(ch, cents)`                        | `void(Integer, Integer)`                           | Constant pitch offset in cents (-1200 to 1200) for chorusing               |

### Effect Methods

| Method                                    | Signature                                   | Description                                                                              |
|-------------------------------------------|---------------------------------------------|------------------------------------------------------------------------------------------|
| `SetVibrato(ch, depth, speed)`            | `void(Integer, Integer, Integer)`           | Pitch modulation. depth: cents (0-200), speed: centi-Hz (500 = 5 Hz)                    |
| `SetTremolo(ch, depth, speed)`            | `void(Integer, Integer, Integer)`           | Volume modulation. depth: % (0-100), speed: centi-Hz (400 = 4 Hz)                       |
| `SetArpeggio(ch, semi1, semi2, speed)`    | `void(Integer, Integer, Integer, Integer)`  | Rapid pitch cycling through [root, +semi1, +semi2] at speed centi-Hz. 1500 = 15 Hz (classic) |
| `SetPortamento(ch, speed)`                | `void(Integer, Integer)`                    | Pitch glide between consecutive notes. speed: ms (0=off, 20-500 typical)                 |

### Note Methods

| Method                                        | Signature                                              | Description                                                  |
|-----------------------------------------------|--------------------------------------------------------|--------------------------------------------------------------|
| `AddNote(ch, beatPos, midiNote, duration)`    | `Integer(Integer, Integer, Integer, Integer)`          | Add a note. Returns 1 on success, 0 if channel is full      |
| `AddNoteVel(ch, beatPos, midiNote, dur, vel)` | `Integer(Integer, Integer, Integer, Integer, Integer)` | Add a note with explicit velocity (0-100). Default is 100    |

### Song Properties

| Property / Method           | Signature               | Description                                                       |
|-----------------------------|-------------------------|-------------------------------------------------------------------|
| `Bpm` (read-only)          | `Integer`               | Tempo in beats per minute                                         |
| `Length` (read/write)       | `Integer`               | Song length in centbeats (100 = 1 beat)                           |
| `ChannelCount` (read-only) | `Integer`               | Number of channels added                                          |
| `SetSwing(amount)`         | `void(Integer)`         | Swing feel (0-100). Shifts off-beat notes forward. 0 = straight   |
| `SetLoopable(flag)`        | `void(Integer)`         | 1 = apply crossfade at loop boundary for click-free looping       |
| `Build()`                  | `Sound()`               | Pre-render all channels to a Sound object. Returns null on failure |

### Noise Channel — Percussion

The noise channel (type 4) uses the MIDI note number to control timbre rather than pitch:
- **Low notes (0-30):** Dark, rumbly noise — kicks, toms
- **Mid notes (40-70):** Medium noise — snares, claps
- **High notes (80-127):** Bright, hissy noise — hi-hats, cymbals

### Limits

| Limit                    | Value      |
|--------------------------|------------|
| Max channels per song    | **8**      |
| Max notes per channel    | **4,096**  |
| Max song duration        | **5 min**  |
| Max BPM                  | **300**    |
| Min BPM                  | **20**     |
| Loop crossfade duration  | **10 ms**  |

### Zia Example

```rust
module MusicDemo;

bind Viper.Sound;

func start() {
    Audio.Init();

    // Create a 4-bar chiptune loop at 140 BPM
    var song = MusicGen.New(140);

    // Channels
    var lead = song.AddChannel(1);    // square
    var bass = song.AddChannel(2);    // sawtooth
    var drums = song.AddChannel(4);   // noise

    // Lead: plucky square with vibrato
    song.SetEnvelope(lead, 10, 60, 50, 80);
    song.SetDuty(lead, 25);
    song.SetVibrato(lead, 12, 500);
    song.SetPan(lead, -20);

    // Bass: punchy saw
    song.SetEnvelope(bass, 5, 40, 80, 50);

    // Drums: percussive noise
    song.SetEnvelope(drums, 1, 20, 0, 30);

    // Melody: C5 E5 G5 C6 (quarter notes)
    song.AddNote(lead, 0, 72, 100);
    song.AddNote(lead, 100, 76, 100);
    song.AddNote(lead, 200, 79, 100);
    song.AddNote(lead, 300, 84, 100);

    // Bass: C3 half notes
    song.AddNote(bass, 0, 48, 200);
    song.AddNote(bass, 200, 48, 200);

    // Drums: kick on 1 & 3, snare on 2 & 4
    song.AddNote(drums, 0, 2, 25);      // kick
    song.AddNote(drums, 100, 8, 30);    // snare
    song.AddNote(drums, 200, 2, 25);    // kick
    song.AddNote(drums, 300, 8, 30);    // snare

    // Build and play
    song.SetLength(400);       // 4 beats
    song.SetLoopable(1);       // seamless loop
    var sound = song.Build();

    if sound != null {
        var voice = sound.PlayLoop(70, 0);
        // voice can be stopped with Voice.Stop(voice)
    }

    Audio.Shutdown();
}
```

### BASIC Example

```basic
REM Procedural music demo
AUDIO.INIT
LET S = MUSICGEN.NEW(120)
LET CH = MUSICGEN.ADDCHANNEL(S, 1)
MUSICGEN.SETENVELOPE S, CH, 10, 50, 70, 100
MUSICGEN.ADDNOTE S, CH, 0, 60, 100
MUSICGEN.ADDNOTE S, CH, 100, 64, 100
MUSICGEN.ADDNOTE S, CH, 200, 67, 100
MUSICGEN.ADDNOTE S, CH, 300, 72, 100
MUSICGEN.SETLENGTH S, 400
MUSICGEN.SETLOOPABLE S, 1
LET SND = MUSICGEN.BUILD(S)
IF SND <> 0 THEN SOUND.PLAYLOOP SND, 70, 0
AUDIO.SHUTDOWN
```

---

## Mix Groups

Independent volume control for music vs sound effects. Players expect to adjust these separately in game settings menus.

### Group Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `MUSIC` | 0 | Music tracks (Music, Playlist) |
| `SFX` | 1 | Sound effects (Sound) |

### Audio Methods (Mix Groups)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Audio.SetGroupVolume(group, vol)` | `void(Integer, Integer)` | Set group volume (0-100, clamped) |
| `Audio.GetGroupVolume(group)` | `Integer(Integer)` | Get group volume (100 if invalid group) |

### Sound Methods (Group-Aware)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sound.PlayGroup(group)` | `Integer(Integer)` | Play in mix group (applies group volume) |
| `Sound.PlayExGroup(vol, pan, group)` | `Integer(Int, Int, Int)` | Play with volume/pan in group |
| `Sound.PlayLoopGroup(vol, pan, group)` | `Integer(Int, Int, Int)` | Loop with volume/pan in group |

Effective volume = `voice_volume × group_volume / 100`. Master volume is applied on top by the audio system.

---

## Music Crossfading

Smooth transitions between music tracks — the old track fades out while the new one fades in simultaneously.

### Music Methods (Crossfade)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Music.CrossfadeTo(newMusic, duration)` | `void(Music, Integer)` | Crossfade to new track over duration ms |

### Audio Properties (Crossfade)

| Property | Type | Description |
|----------|------|-------------|
| `Audio.IsCrossfading` | Boolean (read-only) | True during an active crossfade |

### Playlist Properties (Crossfade)

| Property | Type | Description |
|----------|------|-------------|
| `Playlist.Crossfade` | Integer (read/write) | Auto-crossfade duration for track changes (0 = disabled) |

### Example

```zia
bind Viper.Sound;

// Set up volume sliders
Audio.SetGroupVolume(0, 80);  // Music at 80%
Audio.SetGroupVolume(1, 100); // SFX at 100%

// Play SFX in the SFX group
explosionSound.PlayGroup(1);

// Crossfade between music tracks
Music.CrossfadeTo(currentTrack, newTrack, 2000); // 2-second crossfade

// Enable auto-crossfade on playlist
playlist.Crossfade = 1000; // 1-second crossfade between tracks
```

---

## Audio File Format

Viper Audio supports **WAV (PCM)** and **OGG Vorbis** files. The format is auto-detected
from file magic bytes — no extension matching required.

| Property    | WAV                              | OGG Vorbis                          | MP3                                 |
|-------------|----------------------------------|-------------------------------------|-------------------------------------|
| Format      | PCM (uncompressed)              | Vorbis I (baseline, Huffman-coded) | MPEG-1/2/2.5 Layer III              |
| Bit depth   | 8-bit or 16-bit                 | N/A (lossy compressed)             | N/A (lossy compressed)              |
| Channels    | Mono or Stereo                  | Mono or Stereo                     | Mono, Stereo, or Joint Stereo       |
| Sample rate | Any (resampled to 44100 Hz)     | Any (resampled to 44100 Hz)        | Any (resampled to 44100 Hz)         |

### Tips

1. **Sound effects:** Any sample rate works — the engine resamples to 44100 Hz at load time.
2. **Music streams:** Any sample rate works — the engine resamples on-the-fly during streaming.
3. **Memory:** Sounds are loaded entirely into memory; keep individual clips short.
4. **Streaming:** Music is streamed from disk for all formats (WAV, OGG, MP3), using ~100 KB of buffer memory regardless of track length.
5. **Encoding:** Use a tool such as ffmpeg to convert audio to WAV:
   ```
   ffmpeg -i input.mp3 -ac 2 -f wav output.wav      # stereo music
   ffmpeg -i input.mp3 -ac 1 -f wav sfx.wav         # mono sound effect
   ```

---

## Limits and Behaviors

| Limit | Value | Notes |
|-------|-------|-------|
| Max simultaneous Sound voices | **32** | Oldest non-looping voice is evicted (LRU) when full |
| Max simultaneous Music streams | **4** | `Music.Load()` returns `null` when exceeded |
| Supported audio formats | **WAV PCM, OGG Vorbis** | 8/16-bit PCM or Vorbis compressed |
| Music sample rate | **Any** | Automatically resampled to 44100 Hz |
| Sound sample rate | Any | Resampled to 44100 Hz at load time |
| Pan range | −100 to +100 | −100 = hard left, 0 = center, +100 = hard right |
| Volume range | 0 to 100 | Applies to Sound, Music, and Voice |
| Max SoundBank entries | **64** | Names truncated at 31 characters |
| Max MusicGen channels | **8** | Per song builder instance |
| Max MusicGen notes/channel | **4,096** | `AddNote()` returns 0 when full |
| Max MusicGen duration | **5 min** | `Build()` caps at 5 minutes |

---

## See Also

- [Graphics](graphics/README.md) - Canvas and visual rendering
- [Input](input.md) - Keyboard and mouse input
- [Time](time.md) - Timing for audio synchronization
