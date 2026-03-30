# Plan 05: Music Stream Resampling

## Goal

Add sample-rate conversion to the music streaming path so that music files at any sample
rate (22050, 44100, 48000, 96000, etc.) play at correct pitch and speed.  Currently, music
streams must be exactly 44100 Hz.

## Scope

**In scope:**
- Resample music stream buffers during the buffer-fill phase
- Reuse the existing `vaud_resample()` linear interpolation algorithm
- Support any input sample rate (common: 22050, 44100, 48000, 96000)
- Preserve stereo channel layout through resampling

**Out of scope:**
- Higher-quality resampling (sinc interpolation, polyphase filters)
- Sample rate changes during playback (dynamic resampling)
- Changing the system output sample rate from 44100 Hz

## Background

Sound effects already support resampling (`vaud.c:257-270`): when a sound is loaded,
`vaud_resample()` converts it to 44100 Hz before storing.  Music streams bypass this
because they decode on-the-fly from disk — the buffers are filled with raw PCM at the
source sample rate and mixed directly into the output.

When a 48000 Hz WAV is loaded as music, it plays 48000/44100 = 1.088x too fast and
slightly sharp.  This is documented as a limitation in `docs/viperlib/audio.md:146`.

## Technical Design

### 1. Where Resampling Happens

In `vaud.c`, the music buffer fill path reads raw frames from disk via
`vaud_wav_read_frames()` and stores them in `music->buffers[i]`.  Resampling should
happen immediately after reading, before the buffer is marked as ready for mixing.

The relevant code is in the buffer-fill function (called from the music refill logic).

### 2. Approach: Resample-on-Fill (~100 LOC)

```c
// In the music buffer refill path:
if (music->sample_rate != VAUD_SAMPLE_RATE) {
    // Read raw frames into a temporary buffer
    int16_t *raw = temp_buffer;
    int32_t raw_frames = vaud_wav_read_frames(
        music->file, raw, raw_request, music->channels, music->bits_per_sample);

    // Compute output frame count
    int64_t out_frames = vaud_resample_output_frames(
        raw_frames, music->sample_rate, VAUD_SAMPLE_RATE);

    // Resample into the music buffer
    vaud_resample(raw, raw_frames, music->sample_rate,
                  music->buffers[buf_idx], out_frames,
                  VAUD_SAMPLE_RATE, 2 /* stereo */);

    music->buffer_frames[buf_idx] = (int32_t)out_frames;
} else {
    // Direct read (existing path, no resampling needed)
    int32_t read = vaud_wav_read_frames(
        music->file, music->buffers[buf_idx],
        VAUD_MUSIC_BUFFER_FRAMES, music->channels, music->bits_per_sample);
    music->buffer_frames[buf_idx] = read;
}
```

### 3. Temporary Buffer

Need a temp buffer to hold raw frames before resampling.  Size it for the worst case:
reading `VAUD_MUSIC_BUFFER_FRAMES` frames at the highest ratio.

For downsampling (96000 -> 44100), we need to read more raw frames to produce
`VAUD_MUSIC_BUFFER_FRAMES` output frames:

```c
int64_t raw_request = (int64_t)VAUD_MUSIC_BUFFER_FRAMES *
                      music->sample_rate / VAUD_SAMPLE_RATE + 2;
```

The temp buffer can be allocated once during `vaud_load_music()` and stored in the
`vaud_music` struct, or stack-allocated if the max size is bounded.

### 4. Position Tracking

The `music->position` field tracks playback progress in source frames.  After resampling,
we need to advance `position` by the number of *source* frames consumed (not output frames):

```c
music->position += raw_frames;  // source frames consumed
```

This ensures looping (`position >= frame_count`) triggers at the correct point.

### 5. Loop Boundary

When the music loops, the last buffer fill may produce fewer output frames than requested
(because the source runs out).  The existing loop logic already handles partial buffers;
no changes needed there.

## Files to Modify

| File | Change |
|------|--------|
| `src/lib/audio/src/vaud.c` | Add resample logic to music buffer fill (~80 LOC) |
| `src/lib/audio/src/vaud_internal.h` | Add temp buffer field to `vaud_music` struct |
| `src/lib/audio/src/vaud.c` | Allocate/free temp buffer in load/free_music |

## Test Plan

- Load a 48000 Hz WAV as music, verify it plays at correct speed (compare duration)
- Load a 22050 Hz WAV as music, verify correct pitch
- Load a 44100 Hz WAV (no resampling — regression test)
- Test looping with a non-44100 Hz file (verify seamless loop)

## Risks

- **Linear interpolation quality:** The existing `vaud_resample()` uses linear interpolation,
  which introduces slight high-frequency attenuation.  For music, this is usually inaudible.
  If quality complaints arise, a sinc-based resampler (~200 LOC) can be added later.
- **Buffer sizing:** The temp buffer must be large enough for the max read.  A 96 kHz source
  needs ~2.18x the buffer size.  Allocate `VAUD_MUSIC_BUFFER_FRAMES * 3 * 2 * sizeof(int16_t)`
  to handle up to ~130 kHz sources with margin.
