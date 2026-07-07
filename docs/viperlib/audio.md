---
status: active
audience: public
last-verified: 2026-06-20
---

# Audio

> Sound effects and music playback for games and applications.

**Part of the [Viper Runtime Library](README.md)**

All audio classes live in the `Viper.Sound` namespace.

## Contents

- [Viper.Sound.Sound](#vipersoundsound)
- [Viper.Sound.Music](#vipersoundmusic)
- [Viper.Sound.Voice](#vipersoundvoice)
- [Viper.Sound.Audio (Static)](#vipersoundaudio-static)
- [Viper.Sound.SoundBank](#vipersoundsoundbank)
- [Viper.Sound.Synth](#vipersoundsynth)
- [Viper.Sound.MusicGen](#vipersoundmusicgen)
- [Mix Groups](#mix-groups)
- [Mix Group Effects](#mix-group-effects)
- [Spatial Audio](#spatial-audio)
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
| `Load(path)` | `Sound(String)`  | Load a sound from WAV, OGG Vorbis, or MP3. Returns `null` on failure |
| `LoadAsset(path)` | `Sound(String)` | Load WAV, OGG Vorbis, or MP3 bytes through `Viper.IO.Assets`; accepts plain asset paths and `asset://` URIs |

### Methods

| Method                   | Signature                          | Description                                              |
|--------------------------|------------------------------------|----------------------------------------------------------|
| `Play()`                 | `Integer()`                        | Play once through the SFX mix group. Returns voice ID for control |
| `PlayEx(volume, pan)`    | `Integer(Integer, Integer)`        | Play through the SFX mix group with volume (0–100) and pan (−100 to +100) |
| `PlayEx2(volume, pan, pitch)` | `Integer(Integer, Integer, Float)` | `PlayEx` plus a playback-rate multiplier (0.25–4.0; 1.0 = native) |
| `PlayLoop(volume, pan)`  | `Integer(Integer, Integer)`        | Play looped through the SFX mix group with volume and pan |

> **Voice limit:** Up to 32 sounds may play simultaneously. A 33rd `Play()` call stops
> the **oldest** playing (non-looping) sound to make room — called LRU eviction. The
> evicted sound stops with no error or notification. Looping sounds are evicted only
> when all 32 voices are looping.

### Voice Control

After playing a sound, you receive a voice ID that can be used with `Viper.Sound.Voice`:

| Method                                 | Description                                       |
|----------------------------------------|---------------------------------------------------|
| `Viper.Sound.Voice.IsPlaying(id)`      | Returns `true` if voice is still playing          |
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
bind Viper.Text.Fmt as Fmt;

func start() {
    Audio.Init();

    var snd = Sound.LoadAsset("assets/audio/laser.wav");
    if snd != null {
        // Play with default settings
        var id = snd.Play();

        // Play with volume and pan
        var id2 = snd.PlayEx(80, -50);  // 80% volume, panned left

        // Control the playing voice
        Voice.SetVolume(id2, 50);
        Say("Playing: " + Fmt.Bool(Voice.IsPlaying(id2)));

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

Buffered music class for longer audio tracks. Playback uses incremental decode and fixed-size buffers for memory efficiency.

**Type:** Instance (obj)
**Constructor:** `Viper.Sound.Music.Load(path)`

> **Concurrent limit:** Up to **4** music streams may be loaded at the same time.
> `Music.Load()` returns `null` if this limit is exceeded. Stop and free unused
> streams before loading new ones.

> **Formats and sample rates:** Music accepts WAV, OGG Vorbis, and MP3. Any supported
> sample rate is resampled to the engine mix rate during playback.

### Static Methods

| Method       | Signature        | Description                                       |
|--------------|------------------|---------------------------------------------------|
| `Load(path)` | `Music(String)`  | Load music from WAV, OGG Vorbis, or MP3. Returns `null` on failure or when the 4-stream limit is reached |

### Properties

| Property   | Type    | Access | Description                        |
|------------|---------|--------|------------------------------------|
| `Duration` | Integer | Read   | Total duration in milliseconds     |
| `Position` | Integer | Read   | Current position in milliseconds   |
| `Volume`   | Integer | R/W    | Playback volume (0–100)            |

### Methods

| Method         | Signature           | Description                              |
|----------------|---------------------|------------------------------------------|
| `IsPlaying()`  | `Boolean()`         | Returns `true` if currently playing |
| `Pause()`      | `Void()`            | Pause playback; also freezes an active crossfade involving this track |
| `Play(loop)`   | `Void(Integer)`     | Start playback (1 = loop, 0 = one-shot)  |
| `Resume()`     | `Void()`            | Resume paused playback and reclaim foreground ownership |
| `Seek(ms)`     | `Void(Integer)`     | Seek to position in milliseconds without stopping unrelated music |
| `Stop()`       | `Void()`            | Stop playback                            |

> **Seek behavior:** `Music.Seek(ms)` only repositions that stream. It does not cancel
> unrelated music or active playlist playback.

### Zia Example

```rust
module MusicDemo;

bind Viper.Terminal;
bind Viper.Sound.Audio as Audio;
bind Viper.Sound.Music as Music;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Text.Fmt as Fmt;

func start() {
    Audio.Init();
    var c = Canvas.New("Music Player", 400, 200);

    var mus = Music.Load("background.ogg");
    if mus != null {
        mus.Volume = 70;
        mus.Play(1);  // Looped

        Say("Duration: " + Fmt.Int(mus.get_Duration()) + " ms");

        while !c.get_ShouldClose() {
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
    DO WHILE NOT canvas.ShouldClose
        canvas.Poll()

        ' Display current position
        PRINT "Position: "; bgMusic.Position; " / "; bgMusic.Duration

        ' Pause/resume with space bar
        IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KeySpace) THEN
            IF bgMusic.IsPlaying() THEN
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
| `GetPitch(id)`             | `Float(Integer)`               | Current playback-rate multiplier (1.0 default)           |
| `IsPlaying(id)`            | `Boolean(Integer)`             | Check if voice is playing                                |
| `SetLowpass(id, hz)`       | `Void(Integer, Float)`         | Direct lowpass cutoff in Hz (≤ 0 disables)               |
| `SetOcclusion(id, amt)`    | `Void(Integer, Float)`         | Occlusion 0 (open) … 1 (occluded); smoothed ~80 ms       |
| `SetPan(id, pan)`          | `Void(Integer, Integer)`       | Pan: −100 = hard left, 0 = center, +100 = hard right     |
| `SetPitch(id, pitch)`      | `Void(Integer, Float)`         | Playback-rate multiplier, clamped 0.25–4.0               |
| `SetVolume(id, vol)`       | `Void(Integer, Integer)`       | Set volume for a voice (0–100)                           |
| `Stop(id)`                 | `Void(Integer)`                | Stop a playing voice                                     |

> **Pan law:** Mono sounds use equal-power panning, so `pan=0` plays evenly in both
> channels without the center-volume drop of a linear pan law. Stereo sounds keep
> their original channel balance at `pan=0`; panning attenuates the far side.

> **Pitch:** Rates other than 1.0 resample with a fractional cursor (linear
> interpolation), so pitch and duration scale together — pitch 2.0 plays one
> octave up in half the time. Typical gunshot variation: `PlayEx2(vol, pan,
> 0.92 + 0.16 * rng)`.

> **Occlusion:** The amount maps to a perceptual lowpass sweep (~22 kHz open
> down to ~800 Hz fully occluded) plus up to −6 dB of attenuation. Drive it
> from your own line-of-sight checks each frame; the mixer smooths changes so
> cover flips never click. `SetLowpass` composes with occlusion — the lower
> cutoff wins (useful for scoped-focus or underwater effects).

> **Invalid IDs:** All voice functions are safe to call with any integer ID. If the
> voice has already stopped or the ID was never valid, the call is a no-op.
> Voice IDs never use 0 or -1 and are not reused while the older voice is still active.

### Zia Example

```rust
module VoiceDemo;

bind Viper.Terminal;
bind Viper.Sound;

func start() {
    // Initialize audio system
    Audio.Init();

    // Voice control (safe with invalid IDs)
    SayBool(Voice.IsPlaying(0));  // false
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
| `Update()`                      | `Void()`                       | Advance music crossfades and service streaming buffers |

`Audio.Init()` may be called again after a failed initialization or after
`Audio.Shutdown()`. `Audio.Shutdown()` detaches existing `Sound` and `Music` handles:
destroying those handles remains safe, but playback calls on them fail until the asset is
loaded again.
Call `Audio.Update()` once per frame when using streaming `Music` or direct
`Music.CrossfadeTo`; `Playlist.Update()` already forwards through the same update path.
The mixer also attempts a locked buffer prefill if playback reaches an empty music buffer before end-of-stream has been reported, so transient stream-buffer gaps do not force the rest of the render block to silence.

### Zia Example

```rust
module AudioDemo;

bind Viper.Terminal;
bind Viper.Sound.Audio as Audio;
bind Viper.Text.Fmt as Fmt;

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
| `Register(name, path)`    | `Integer(String, String)`          | Load WAV, OGG Vorbis, or MP3 and register under name. Returns 1 on success |
| `RegisterSound(name, sound)` | `Integer(String, Sound)`        | Register an existing Sound object (e.g., from Synth). Returns 1 on success |
| `Play(name)`              | `Integer(String)`                  | Play named sound. Returns voice ID, or -1 if not found              |
| `PlayEx(name, vol, pan)`  | `Integer(String, Integer, Integer)` | Play with volume (0-100) and pan (-100 to 100)                     |
| `Has(name)`               | `Boolean(String)`                  | Check if name is registered                                         |
| `Get(name)`               | `Sound(String)`                    | Get the Sound object for a name, or null                            |
| `Remove(name)`            | `Void(String)`                     | Remove a named entry                                                |
| `Clear()`                 | `Void()`                           | Remove all entries                                                  |

Max 64 entries per bank. Names are matched exactly and are not truncated.
`RegisterSound` accepts only real `Sound` objects returned by `Sound.Load`,
`Synth`, or `MusicGen.Build`; generic objects are rejected and return 0.

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

In audio-disabled builds, Synth methods return `null` instead of trapping. This lets
asset setup code degrade cleanly while direct `Sound.Load`, `Sound.Play`, and `Music`
playback APIs still report that audio support is unavailable.

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

`Tone` and `Sweep` use their waveform argument for oscillator shape, not volume.
Use `Sound.PlayEx`, `Sound.PlayLoop`, or `Voice.SetVolume` to control playback volume.

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
| `SetLength(length)`        | `void(Integer)`         | Set song length in centbeats; equivalent to assigning `Length`    |
| `SetSwing(amount)`         | `void(Integer)`         | Swing feel (0-100). Shifts off-beat notes forward. 0 = straight   |
| `SetLoopable(flag)`        | `void(Boolean)`         | `true` = apply crossfade at loop boundary for click-free looping  |
| `Build()`                  | `Sound()`               | Pre-render all channels to a Sound object. Returns null on failure |

`Length`, note positions, and note durations are clamped to the maximum renderable
song span for the selected BPM. Notes whose start position is at or after that
maximum span are rejected. `Build()` returns `null` in audio-disabled builds.
Loopable output keeps the requested length and blends only the loop boundary; it
does not shorten the generated sound.

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
    song.SetLoopable(true);    // seamless loop
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
| named groups | 100+ | Runtime-registered groups for custom categories |

### Audio Methods (Mix Groups)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Audio.SetGroupVolume(group, vol)` | `void(Integer, Integer)` | Set group volume (0-100, clamped) |
| `Audio.GetGroupVolume(group)` | `Integer(Integer)` | Get group volume (100 if invalid group) |
| `Audio.RegisterGroup(name)` | `Integer(String)` | Register or find a named group. Built-ins `music` and `sfx` return 0 and 1 |
| `Audio.FindGroup(name)` | `Integer(String)` | Return a named group id, or -1 |
| `Audio.FindGroupOption(name)` | `Option[Integer](String)` | Return a named group id as `Some(id)`, or `None` |
| `Audio.SetGroupVolumeNamed(name, vol)` | `Void(String, Integer)` | Register if needed, then set volume |
| `Audio.GetGroupVolumeNamed(name)` | `Integer(String)` | Get a named group volume, or 100 if missing |
| `Audio.GroupName(group)` | `String(Integer)` | Return a registered group name, or empty string |
| `Audio.GroupAddLowpass(group, cutoffHz, q)` | `Integer(Integer, Float, Float)` | Add a low-pass filter insert and return an effect ID |
| `Audio.GroupAddHighpass(group, cutoffHz, q)` | `Integer(Integer, Float, Float)` | Add a high-pass filter insert and return an effect ID |
| `Audio.GroupAddPeaking(group, freqHz, q, gainDb)` | `Integer(Integer, Float, Float, Float)` | Add a peaking EQ insert and return an effect ID |
| `Audio.GroupAddDelay(group, ms, feedback, wet)` | `Integer(Integer, Float, Float, Float)` | Add a delay insert and return an effect ID |
| `Audio.GroupAddReverb(group, roomSize, damping, wet)` | `Integer(Integer, Float, Float, Float)` | Add a reverb insert and return an effect ID |
| `Audio.GroupSetFxBypass(group, fxId, bypass)` | `Void(Integer, Integer, Boolean)` | Enable or bypass one group effect |
| `Audio.GroupRemoveFx(group, fxId)` | `Void(Integer, Integer)` | Remove one group effect |
| `Audio.GroupClearFx(group)` | `Void(Integer)` | Remove every effect from the group |
| `Audio.SetGroupDucking(trigger, target, amount, attackSec, releaseSec)` | `Void(String, String, Float, Float, Float)` | Sidechain duck: while `trigger` is audible, `target`'s gain eases toward `1 − amount` |

> **Ducking:** Groups are resolved (and registered on first use) by name. While
> anything in the trigger group is audible, the target group's gain follows an
> exponential envelope toward `1 − amount` over `attackSec` and recovers to
> unity over `releaseSec`. Re-registering the same (trigger, target) pair
> replaces the rule; `amount <= 0` removes it. Up to 8 rules may be active.
> Classic use: `Audio.SetGroupDucking("weapons", "music", 0.35, 0.03, 0.4)`.

### Sound Methods (Group-Aware)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sound.PlayGroup(group)` | `Integer(Integer)` | Play in mix group (applies group volume) |
| `Sound.PlayExGroup(vol, pan, group)` | `Integer(Int, Int, Int)` | Play with volume/pan in group |
| `Sound.PlayLoopGroup(vol, pan, group)` | `Integer(Int, Int, Int)` | Loop with volume/pan in group |

Plain `Sound.Play`, `Sound.PlayEx`, and `Sound.PlayLoop` use the SFX group by default.
Group-specific play methods apply the requested group volume exactly once. Effective
volume = `voice_volume × group_volume / 100`; master volume is applied on top by the
audio system. Group volume setters/getters are safe to call concurrently with playback,
including audio-disabled builds where they remain pure settings state.

Named groups are useful for settings such as UI, ambience, dialogue, and cutscene sound
without overloading the two built-ins. Registered named groups receive stable ids within
the current process and can be passed to `PlayGroup`, `PlayExGroup`, and
`PlayLoopGroup`.

Group names are stored as fixed-size C backend names. Embedded `NUL` bytes are sanitized
to `_` before registration and lookup so `"amb\0x"` cannot alias `"amb"`.

## Mix Group Effects

Mix-group effects are insert chains on the group bus. Every playing voice and
music stream first contributes to its group, the chain processes that group once,
then the processed result is mixed into the master output.

The built-in effects are:

| Effect | Parameters | Typical use |
|--------|------------|-------------|
| Low-pass | `cutoffHz`, `q` | Occlusion, underwater/muffled SFX |
| High-pass | `cutoffHz`, `q` | Radio/phone voices, thin UI cues |
| Peaking EQ | `freqHz`, `q`, `gainDb` | Boost or cut one band |
| Delay | `ms`, `feedback`, `wet` | Echoes and slapback |
| Reverb | `roomSize`, `damping`, `wet` | Room or ambience tails |

Effect creation allocates any needed delay/reverb buffers up front. The mixer
process path performs no per-block allocations, and bypassed effects remain in
the chain without changing the signal.

### Zia Example

```rust
module AudioFxDemo;

bind Viper.Sound.Audio as Audio;
bind Viper.Sound.Sound as Sound;

func start() {
    if Audio.Init() == 0 {
        return;
    }

    var ambience = Audio.RegisterGroup("ambience");
    var sfx = Audio.RegisterGroup("sfx");

    var reverb = Audio.GroupAddReverb(ambience, 0.72, 0.35, 0.45);
    var occlusion = Audio.GroupAddLowpass(sfx, 1600.0, 0.707);

    var hit = Sound.LoadAsset("assets/audio/hit.wav");
    if hit != null {
        hit.PlayExGroup(90, 0, sfx);
    }

    Audio.GroupSetFxBypass(sfx, occlusion, true);
    Audio.GroupRemoveFx(ambience, reverb);
    Audio.GroupClearFx(sfx);

    Audio.Shutdown();
}
```

Effects require compiled audio support. The plain mix-group volume/name APIs
remain available as settings state in audio-disabled builds.

## Spatial Audio

`Viper.Sound.SpatialAudio3D` is the low-level spatial audio surface. Its
implementation lives under `src/runtime/audio/` with the rest of the audio
runtime. It computes distance attenuation, stereo pan, per-voice falloff state,
and Doppler metadata before delegating to normal `Sound` voice playback.

| Method | Signature | Description |
|--------|-----------|-------------|
| `SpatialAudio3D.SetListener(position, forward)` | `Void(Object, Object)` | Set the fallback listener position and forward direction |
| `SpatialAudio3D.PlayAt(sound, position, maxDist, volume)` | `Integer(Object, Object, Float, Integer)` | Play a sound from a world position |
| `SpatialAudio3D.UpdateVoice(voice, position, maxDist)` | `Void(Integer, Object, Float)` | Recompute attenuation and pan for a moving voice |
| `SpatialAudio3D.SyncBindings(dt)` | `Void(Float)` | Sync object-backed listeners/sources bound to scene nodes or cameras |

`SoundListener3D` and `SoundSource3D` remain in the
`Viper.Graphics3D` namespace because those object wrappers bind to
`SceneNode` and `Camera3D`. They call into the audio-owned spatial math; the
graphics side owns only scene/camera binding and lifetime integration.

| Graphics3D type | Description |
|-----------------|-------------|
| `SoundListener3D` | Active listener pose, velocity, node/camera binding, and `IsActive` selection |
| `SoundSource3D` | Positional `Sound` instance with ref/max distance, velocity, looping, and optional node binding |

Scene-driven games usually update transforms, call `SceneGraph.SyncBindings(dt)`,
then trigger `SoundSource3D.Play()` or `SpatialAudio3D.PlayAt(...)` for the
frame. Direct spatial callers can skip the object wrappers and call
`SpatialAudio3D.SetListener`, `PlayAt`, and `UpdateVoice` with `Vec3` handles.

---

## Music Crossfading

Smooth transitions between music tracks — the old track fades out while the new one fades in simultaneously.

Unrelated playlist or direct-music crossfades can run independently; starting one transition no longer cancels another unrelated fade.

Pausing a track or playlist that owns a crossfade now pauses the fade clock too; `Audio.Update()`
and `Playlist.Update()` do not advance a paused transition.

### Music Methods (Crossfade)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Music.CrossfadeTo(newMusic, duration)` | `void(Music, Integer)` | Crossfade to new track over duration ms, preserving the destination track's loop flag |

### Audio Properties (Crossfade)

| Property | Type | Description |
|----------|------|-------------|
| `Audio.IsCrossfading` | Boolean (read-only) | True during an active crossfade; reading it does not advance or complete the fade |

### Audio Methods (Crossfade)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Audio.Update()` | `void()` | Advance active crossfades and service streaming music buffers; call once per frame when using `Music.CrossfadeTo` or direct `Music` playback (paused crossfades stay frozen) |

### Playlist Properties (Crossfade)

| Property | Type | Description |
|----------|------|-------------|
| `Playlist.Crossfade` | Integer (read/write) | Auto-crossfade duration for track changes (0 = disabled) |

### Example

```rust
bind Viper.Sound;

// Set up volume sliders
Audio.SetGroupVolume(0, 80);  // Music at 80%
Audio.SetGroupVolume(1, 100); // SFX at 100%

// Play SFX in the SFX group
explosionSound.PlayGroup(1);

// Crossfade between music tracks
currentTrack.CrossfadeTo(newTrack, 2000); // 2-second crossfade
Audio.Update(); // call each frame while direct music/crossfades run

// Enable auto-crossfade on playlist
playlist.Crossfade = 1000; // 1-second crossfade between tracks
playlist.Update();         // advances playback and pending playlist crossfades
```

---

## Audio File Format

Viper Audio supports **WAV (PCM)**, **OGG Vorbis**, and **MP3** files. The format is
auto-detected from file magic bytes — no extension matching required.

| Property    | WAV                                      | OGG Vorbis                          | MP3                                 |
|-------------|------------------------------------------|-------------------------------------|-------------------------------------|
| Format      | PCM or 32-bit IEEE float                 | Vorbis I (baseline, Huffman-coded) | MPEG-1/2/2.5 Layer III              |
| Bit depth   | 8/16/24/32-bit PCM, or 32-bit float     | N/A (lossy compressed)             | N/A (lossy compressed)              |
| Channels    | Mono or Stereo                           | Mono or Stereo                     | Mono, Stereo, or Joint Stereo       |
| Sample rate | Any supported rate (resampled to 44100) | Any supported rate (resampled to 44100) | Any supported rate (resampled to 44100) |

### Tips

1. **Sound effects:** Any sample rate works — the engine resamples to 44100 Hz at load time.
2. **Music playback:** Any supported sample rate works — the engine resamples during buffered playback.
3. **Memory:** Sounds are loaded entirely into memory; keep individual clips short.
4. **Buffered decode:** WAV and OGG music read incrementally; MP3 music keeps the compressed file in memory and decodes frame-by-frame during playback.
5. **Float WAV endpoints:** 32-bit float WAV samples map full-scale `-1.0` to `-32768` and `+1.0` to `32767` when converted to the engine's 16-bit mix format.
6. **MP3 decoder scope:** Unsupported MP3 Huffman codebooks now fail at load time instead of producing corrupted audio. Re-encode the file if a specific MP3 is rejected.
7. **Encoding:** Use a tool such as ffmpeg to convert source audio between formats:
   ```
   ffmpeg -i input.wav output.ogg                      # OGG Vorbis
   ffmpeg -i input.wav -codec:a libmp3lame output.mp3 # MP3
   ffmpeg -i input.mp3 -ac 2 -f wav output.wav      # stereo music
   ffmpeg -i input.mp3 -ac 1 -f wav sfx.wav         # mono sound effect
   ```

---

## Limits and Behaviors

| Limit | Value | Notes |
|-------|-------|-------|
| Max simultaneous Sound voices | **32** | Oldest non-looping voice is evicted (LRU) when full |
| Max simultaneous Music streams | **4** | `Music.Load()` returns `null` when exceeded |
| Supported audio formats | **WAV, OGG Vorbis, MP3** | Sounds load fully into memory; music uses buffered incremental playback |
| Music sample rate | **1-384000 Hz** | Automatically resampled to 44100 Hz |
| Sound sample rate | **1-384000 Hz** | Resampled to 44100 Hz at load time |
| Pan range | −100 to +100 | −100 = hard left, 0 = center, +100 = hard right |
| Volume range | 0 to 100 | Applies to Sound, Music, and Voice |
| Max SoundBank entries | **64** | Keys are exact strings; long names remain distinct |
| Max MusicGen channels | **8** | Per song builder instance |
| Max MusicGen notes/channel | **4,096** | `AddNote()` returns 0 when full |
| Max MusicGen duration | **5 min** | `Build()` caps at 5 minutes |
| Max decoded Sound data | **100 MB** | Compressed sounds that decode beyond this return `null` |

Behavior notes:
`Sound.Load(path)` and `Music.Load(path)` reject paths containing embedded `NUL` bytes and return `null` instead of passing a truncated path to the backend.
`Playlist.Add(path)` and `Playlist.Insert(index, path)` treat a null path as an empty string entry.
`Playlist.Insert(index, path)` clamps out-of-range indices to the valid `[0, Count]` insertion range.
`Music.CrossfadeTo` keeps the destination track's current loop setting instead of forcing one-shot playback.
`Music.CrossfadeTo` rejects detached handles after `Audio.Shutdown()`; load the asset again after reinitializing audio.
`Playlist.Shuffle` uses the runtime RNG, so `Viper.Math.Random.Seed()` can make shuffle order reproducible.
Zero-length music streams fail to load rather than entering loop-rewind playback.

---

## See Also

- [Graphics](graphics/README.md) - Canvas and visual rendering
- [Input](input.md) - Keyboard and mouse input
- [Time](time.md) - Timing for audio synchronization
