# Plan 08: OGG/MP3 Music Streaming

## Context

OGG Vorbis and MP3 files loaded as music are fully decoded to a WAV buffer in memory
(`ogg_decode_to_wav` / `mp3_file_to_wav` in `rt_audio.c`). A 5-min stereo 44.1kHz file
consumes ~50 MB of RAM. WAV music streams from disk using only ~96 KB (3 × 32 KB buffers).

The goal: add streaming decode for OGG and MP3 so they use the same triple-buffer
architecture as WAV, decoding on-the-fly in the mixer's fill callback.

## Architecture Overview

### Current Flow (WAV)
```
vaud_load_music()
  → vaud_wav_open_stream() → stores FILE*, data_offset, metadata
  → pre-fill buffer[0] via vaud_music_fill_buffer()

mix_music() [audio thread, under mutex]
  → when buffer exhausted: vaud_music_fill_buffer()
    → vaud_wav_read_frames() → raw PCM from disk
    → vaud_resample() if needed
  → mix into output
```

### New Flow (OGG/MP3)
```
vaud_load_music_ogg() / vaud_load_music_mp3()
  → open file, create decoder, parse headers, store in vaud_music
  → pre-fill buffer[0] via vaud_music_fill_buffer()

mix_music() [audio thread, under mutex — unchanged]
  → when buffer exhausted: vaud_music_fill_buffer()
    → dispatch by format:
      WAV:  vaud_wav_read_frames() [existing]
      OGG:  ogg_stream_read_frames() [new]
      MP3:  mp3_stream_read_frames() [new]
    → vaud_resample() if needed [existing]
  → mix into output
```

## Detailed Design

### Step 1: Extend `vaud_music` struct (`vaud_internal.h`)

Add format tag and decoder state fields:

```c
struct vaud_music {
    // ... existing fields unchanged ...

    // Format-specific decoder state
    int format;              // 0=WAV, 1=OGG, 2=MP3
    void *ogg_reader;        // ogg_reader_t* (for format==1)
    void *vorbis_decoder;    // vorbis_decoder_t* (for format==1)
    void *mp3_state;         // mp3_stream_state_t* (for format==2)

    // Leftover PCM from last decode (decoders produce variable frame counts)
    int16_t *leftover_buf;
    int leftover_frames;
    int leftover_cap;
};
```

**Why leftover buffer:** Vorbis packets produce a variable number of PCM frames (depends
on block size). The mixer requests exactly `VAUD_MUSIC_BUFFER_FRAMES` (8192) frames, but
a Vorbis packet might produce 256 or 2048 frames. Excess frames must be saved for the
next fill call.

### Step 2: Add MP3 per-frame decode API (`rt_mp3.h` / `rt_mp3.c`)

```c
// New opaque streaming state
typedef struct mp3_stream mp3_stream_t;

// Open an MP3 file for streaming. Reads entire file into memory but decodes
// frame-by-frame. Returns NULL on failure.
mp3_stream_t *mp3_stream_open(const char *filepath);

// Decode the next frame, producing up to 1152 stereo samples.
// Returns number of samples per channel, 0 on EOF, -1 on error.
int mp3_stream_decode_frame(mp3_stream_t *stream, int16_t **out_pcm);

// Get metadata (available after open).
int mp3_stream_sample_rate(const mp3_stream_t *stream);
int mp3_stream_channels(const mp3_stream_t *stream);

// Free streaming state.
void mp3_stream_free(mp3_stream_t *stream);
```

**Implementation:** The stream holds the entire file in memory (MP3 files are small —
a 5-min 128kbps MP3 is only ~5 MB) and maintains a decode position. Each
`mp3_stream_decode_frame` call finds the next sync word, decodes one 1152-sample frame,
and advances the position. This reuses the existing `mp3_decode_file` pipeline but
processes one frame per call instead of all frames at once.

### Step 3: Add streaming read functions (`vaud.c`)

```c
// OGG: decode packets into a flat PCM buffer
static int32_t ogg_stream_read_frames(struct vaud_music *music,
                                       int16_t *output, int32_t max_frames) {
    int32_t frames_written = 0;

    // First, drain any leftover frames from previous decode
    if (music->leftover_frames > 0) {
        int32_t to_copy = (music->leftover_frames < max_frames)
                          ? music->leftover_frames : max_frames;
        memcpy(output, music->leftover_buf, to_copy * 2 * sizeof(int16_t));
        music->leftover_frames -= to_copy;
        if (music->leftover_frames > 0) {
            memmove(music->leftover_buf, music->leftover_buf + to_copy * 2,
                    music->leftover_frames * 2 * sizeof(int16_t));
        }
        frames_written += to_copy;
    }

    // Decode packets until we have enough frames
    while (frames_written < max_frames) {
        const uint8_t *pkt_data;
        size_t pkt_len;
        if (!ogg_reader_next_packet(music->ogg_reader, &pkt_data, &pkt_len))
            break; // EOF

        int16_t *pcm = NULL;
        int samples = 0;
        if (vorbis_decode_packet(music->vorbis_decoder, pkt_data, pkt_len,
                                  &pcm, &samples) != 0 || samples <= 0)
            continue;

        int32_t space = max_frames - frames_written;
        int32_t to_copy = (samples < space) ? samples : space;
        memcpy(output + frames_written * 2, pcm, to_copy * 2 * sizeof(int16_t));
        frames_written += to_copy;

        // Save excess to leftover buffer
        if (samples > to_copy) {
            int excess = samples - to_copy;
            // Ensure leftover buffer has capacity
            if (excess > music->leftover_cap) {
                free(music->leftover_buf);
                music->leftover_cap = excess + 256;
                music->leftover_buf = malloc(music->leftover_cap * 2 * sizeof(int16_t));
            }
            if (music->leftover_buf) {
                memcpy(music->leftover_buf, pcm + to_copy * 2,
                       excess * 2 * sizeof(int16_t));
                music->leftover_frames = excess;
            }
        }
    }
    return frames_written;
}

// MP3: decode frames into a flat PCM buffer (same pattern)
static int32_t mp3_stream_read_frames(struct vaud_music *music,
                                       int16_t *output, int32_t max_frames) {
    // Same leftover-drain + decode-loop pattern as OGG
    // Each mp3_stream_decode_frame() produces up to 1152 frames
    ...
}
```

### Step 4: Update `vaud_music_fill_buffer` (`vaud.c`)

Dispatch by format:

```c
int32_t vaud_music_fill_buffer(struct vaud_music *music, int32_t buf_idx) {
    int16_t *out = music->buffers[buf_idx];

    int32_t raw_frames;
    switch (music->format) {
        case 1: // OGG
            raw_frames = ogg_stream_read_frames(music, out, VAUD_MUSIC_BUFFER_FRAMES);
            break;
        case 2: // MP3
            raw_frames = mp3_stream_read_frames(music, out, VAUD_MUSIC_BUFFER_FRAMES);
            break;
        default: // WAV (existing path)
            if (music->sample_rate == VAUD_SAMPLE_RATE) {
                return vaud_wav_read_frames(...);
            }
            // ... existing resampling path ...
    }

    // Resampling (shared across all formats)
    if (music->sample_rate != VAUD_SAMPLE_RATE && raw_frames > 0) {
        // ... existing resample logic ...
    }
    return raw_frames;
}
```

### Step 5: Add format detection to `rt_music_load` (`rt_audio.c`)

```c
void *rt_music_load(rt_string path) {
    const char *path_str = rt_string_cstr(path);

    int fmt = detect_audio_format(path_str);
    vaud_music_t mus = NULL;

    switch (fmt) {
        case 1: mus = vaud_load_music(g_audio_ctx, path_str); break;     // WAV
        case 2: mus = vaud_load_music_ogg(g_audio_ctx, path_str); break; // OGG
        case 3: mus = vaud_load_music_mp3(g_audio_ctx, path_str); break; // MP3
        default: return NULL;
    }
    // ... wrap in rt_music object (existing code) ...
}
```

### Step 6: Add `vaud_load_music_ogg` / `vaud_load_music_mp3` (`vaud.c`)

```c
vaud_music_t vaud_load_music_ogg(vaud_context_t ctx, const char *path) {
    ogg_reader_t *reader = ogg_reader_open_file(path);
    vorbis_decoder_t *dec = vorbis_decoder_new();

    // Parse 3 header packets
    for (int i = 0; i < 3; i++) {
        const uint8_t *pkt; size_t pkt_len;
        ogg_reader_next_packet(reader, &pkt, &pkt_len);
        vorbis_decode_header(dec, pkt, pkt_len, i);
    }

    vaud_music_t music = calloc(1, sizeof(struct vaud_music));
    music->format = 1;
    music->ogg_reader = reader;
    music->vorbis_decoder = dec;
    music->sample_rate = vorbis_get_sample_rate(dec);
    music->channels = vorbis_get_channels(dec);
    music->frame_count = ...; // estimated from file size or granule position

    // Allocate buffers (same as WAV path)
    for (int i = 0; i < VAUD_MUSIC_BUFFER_COUNT; i++)
        music->buffers[i] = malloc(VAUD_MUSIC_BUFFER_FRAMES * 2 * sizeof(int16_t));

    // Pre-fill first buffer
    music->buffer_frames[0] = vaud_music_fill_buffer(music, 0);

    // Add to context...
    return music;
}
```

### Step 7: Handle seeking for compressed formats

**Approach:** For OGG/MP3, seeking is limited to "restart from beginning" (for looping).
Full arbitrary seeking requires either:
- OGG: page-level granule bisection search (moderate complexity)
- MP3: frame-counting scan from beginning (slow but correct)

**Initial implementation:** Support restart-to-beginning only. For the loop case in
`mix_music`, instead of `fseek(data_offset)`, reset the decoder state:

```c
// In mix_music loop handling (vaud_mixer.c):
if (read == 0 && music->loop) {
    if (music->format == 0) {
        fseek(file, data_offset, SEEK_SET); // WAV
    } else if (music->format == 1) {
        // OGG: reopen reader, re-parse headers
        ogg_reader_free(music->ogg_reader);
        music->ogg_reader = ogg_reader_open_file(music->filepath);
        // skip 3 headers...
    } else if (music->format == 2) {
        // MP3: reset decode position to start
        mp3_stream_seek_start(music->mp3_state);
    }
    music->position = 0;
    read = vaud_music_fill_buffer(music, current_buffer);
}
```

**For `vaud_music_seek`:** Log a warning and no-op for compressed formats. Add full
seeking as a follow-up.

### Step 8: Handle duration for compressed formats

**OGG:** Read the last page's granule_position from the end of the file during open.
This gives exact duration. Add a helper:
```c
int64_t ogg_get_total_samples(const char *filepath);
```

**MP3:** Estimate from file size and bitrate: `frames ≈ file_size / frame_size * 1152`.
For VBR files, check for a Xing/VBRI header in the first frame.

### Step 9: Update `vaud_free_music` cleanup

```c
// In vaud_free_music, add:
if (music->ogg_reader) ogg_reader_free(music->ogg_reader);
if (music->vorbis_decoder) vorbis_decoder_free(music->vorbis_decoder);
if (music->mp3_state) mp3_stream_free(music->mp3_state);
free(music->leftover_buf);
```

### Step 10: Store filepath for loop restart

OGG loop restart needs to reopen the file. Store a copy of the filepath:
```c
// Add to vaud_music:
char *filepath;  // strdup'd, freed in vaud_free_music
```

## Files to Create

| File | Change |
|------|--------|
| (none — all changes in existing files) | |

## Files to Modify

| File | Change | Est. LOC |
|------|--------|----------|
| `src/lib/audio/src/vaud_internal.h` | Add format/decoder/leftover fields to struct | ~15 |
| `src/lib/audio/src/vaud.c` | `ogg_stream_read_frames`, `mp3_stream_read_frames`, `vaud_load_music_ogg`, `vaud_load_music_mp3`, update `fill_buffer` dispatch, update `free_music` | ~250 |
| `src/lib/audio/src/vaud_mixer.c` | Update loop restart for compressed formats | ~20 |
| `src/runtime/audio/rt_mp3.h` | Add `mp3_stream_t` per-frame API | ~15 |
| `src/runtime/audio/rt_mp3.c` | Implement `mp3_stream_open/decode_frame/free` | ~120 |
| `src/runtime/audio/rt_audio.c` | Format detection in `rt_music_load` | ~20 |
| `src/runtime/audio/rt_ogg.h` | (possibly) Add `ogg_reader_reset` for loop restart | ~10 |
| `src/runtime/audio/rt_ogg.c` | Implement `ogg_reader_reset` | ~15 |
| `docs/viperlib/audio.md` | Update music streaming docs | ~10 |

**Total: ~475 LOC**

## Threading Safety

All decoder state is accessed only from `vaud_music_fill_buffer`, which runs inside the
mixer's mutex lock (`vaud_mixer_render` acquires `ctx->mutex` before calling `mix_music`).
No additional synchronization is needed.

## Seeking Limitations (Initial Implementation)

| Operation | WAV | OGG | MP3 |
|-----------|-----|-----|-----|
| Play | Yes | Yes | Yes |
| Pause/Resume | Yes | Yes | Yes |
| Stop (reset to 0) | Yes | Yes | Yes |
| Loop | Yes | Yes (reopen) | Yes (reset pos) |
| Seek to time | Yes | **No** (no-op) | **No** (no-op) |
| Duration query | Yes | Yes (granule) | Yes (estimate) |

Full seek support can be added later via OGG page bisection and MP3 frame scanning.

## Verification

- Load a 5-min WAV as music → unchanged behavior, ~96 KB buffer usage
- Load a 5-min OGG as music → plays correctly, ~96 KB + decoder state (~50 KB)
- Load a 5-min MP3 as music → plays correctly, ~96 KB + file buffer (~5 MB for 128kbps)
- Loop test: all 3 formats loop seamlessly
- Pause/resume: all 3 formats work
- Memory: verify OGG/MP3 music uses <10 MB (vs ~50 MB current)
- Existing `test_ogg_vorbis` and `test_mp3_decode` tests pass
