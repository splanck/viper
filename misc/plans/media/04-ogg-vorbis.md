# Plan 04: OGG Vorbis Audio Decoding

## Goal

Add OGG Vorbis decoding to Viper's audio system, enabling compressed music and sound
effects without requiring MP3 licensing or external libraries.  OGG Vorbis is the standard
open-source compressed audio format for games.

## Scope

**In scope:**
- OGG container parsing (page/packet extraction)
- Vorbis decoding (floor/residue/IMDCT pipeline)
- Sound loading: decode entire file to PCM (`rt_sound_load` path)
- Music streaming: decode on-the-fly from disk (`rt_music_load` path)
- Mono and stereo
- All standard sample rates (resampled to 44100 Hz via existing `vaud_resample`)

**Out of scope:**
- OGG Opus (separate codec, can layer on the OGG container parser later)
- OGG FLAC (FLAC in OGG container — extremely rare)
- Encoding/saving OGG files
- Seeking (can add later; initial implementation is forward-only)

## Background

Viper currently only supports WAV (uncompressed PCM).  For game music, WAV files are
enormous (~50 MB for a 5-minute stereo track).  OGG Vorbis achieves 10:1 compression
with near-transparent quality.  It's patent-free and the standard format for game audio
(used by Unity, Godot, SDL_mixer, FMOD, etc.).

A from-scratch Vorbis decoder is the largest item in this plan set (~3000 LOC), but it
aligns with Viper's zero-dependency philosophy.

## Technical Design

### 1. Architecture

```
.ogg file
  -> OGG page parser (sync + CRC)
  -> OGG packet assembler (spanning pages)
  -> Vorbis identification header
  -> Vorbis comment header (skip)
  -> Vorbis setup header (codebooks, floors, residues, mappings, modes)
  -> Audio packets:
       -> Mode/window select
       -> Floor decode (type 1)
       -> Residue decode (type 0/1/2)
       -> Inverse coupling (stereo)
       -> Floor curve application
       -> IMDCT (window overlap-add)
  -> Output: interleaved 16-bit PCM
```

### 2. Component Breakdown (~3000 LOC total)

| Component | Est. LOC | File |
|-----------|----------|------|
| OGG page parser | ~200 | `rt_ogg.c` |
| OGG packet assembler | ~100 | `rt_ogg.c` |
| Vorbis header decode | ~200 | `rt_vorbis.c` |
| Codebook decode | ~300 | `rt_vorbis.c` |
| Floor type 1 decode | ~250 | `rt_vorbis.c` |
| Residue type 0/1/2 decode | ~300 | `rt_vorbis.c` |
| Inverse MDCT | ~200 | `rt_vorbis.c` |
| Window functions | ~50 | `rt_vorbis.c` |
| Overlap-add + output | ~100 | `rt_vorbis.c` |
| Stereo channel coupling | ~50 | `rt_vorbis.c` |
| Bit reader (LSB-first) | ~100 | `rt_vorbis.c` |
| Integration (sound load) | ~100 | `rt_audio.c` changes |
| Integration (music stream) | ~150 | `vaud.c` changes |
| Headers | ~100 | `rt_ogg.h`, `rt_vorbis.h` |
| Memory management | ~100 | Codebook/floor/residue allocation |

### 3. IMDCT Strategy

Use a split-radix IMDCT with pre/post twiddle factors.  Vorbis uses block sizes of
256 and 2048 samples (or up to 8192).  The IMDCT can be implemented as:

1. Pre-twiddle multiply
2. N/4-point complex FFT (Cooley-Tukey radix-2)
3. Post-twiddle multiply

All fixed-point is possible but float32 is simpler and fast enough for audio.  Since
Viper already uses float for audio panning/volume, float32 IMDCT is acceptable.

### 4. File Format Detection

Extend the WAV loader to check file magic bytes:
- WAV: starts with `RIFF` (0x52494646)
- OGG: starts with `OggS` (0x4F676753)

Dispatch to the appropriate decoder based on magic.

### 5. Streaming Architecture

For music streaming, the OGG decoder needs to maintain state between buffer fills:

```c
typedef struct {
    FILE *file;
    ogg_sync_state sync;       // Page boundary tracking
    vorbis_decoder_state dec;  // Codec state (codebooks, floor curves, etc.)
    int16_t *overlap_buf;      // Previous block's right half for overlap-add
    int32_t overlap_frames;
} ogg_music_stream;
```

The `vaud_music` structure gets an opaque decoder pointer.  Buffer refills call
`ogg_stream_decode_frames(stream, output, frame_count)`.

## Files to Create

| File | Purpose |
|------|---------|
| `src/runtime/audio/rt_ogg.h` | OGG container parser API |
| `src/runtime/audio/rt_ogg.c` | OGG page/packet parser |
| `src/runtime/audio/rt_vorbis.h` | Vorbis decoder API |
| `src/runtime/audio/rt_vorbis.c` | Vorbis decoder implementation |

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/audio/rt_audio.c` | Format dispatch in `rt_sound_load` |
| `src/lib/audio/src/vaud.c` | Format dispatch in `vaud_load_music` |
| `src/lib/audio/src/vaud_wav.c` | Extract magic-byte detection to shared helper |
| `src/runtime/CMakeLists.txt` | Add new source files |
| `src/tests/CMakeLists.txt` | Add OGG decode tests |

## Test Plan

- Decode reference OGG files and compare PCM output against known checksums
- Mono OGG file
- Stereo OGG file at various bitrates (64k, 128k, 192k)
- Non-44100 Hz OGG (verify resampling)
- Truncated OGG file (graceful error)
- Stream a long OGG file as music (verify no glitches at buffer boundaries)

## Risks

- **Complexity:** Vorbis is the most complex codec to implement from scratch.  The
  codebook/floor/residue system has many edge cases.  Reference: the xiph.org Vorbis I
  spec is 108 pages.  Mitigate by implementing incrementally: OGG parser first, then
  headers, then audio packets.
- **Precision:** Float32 IMDCT may have minor rounding differences vs reference decoder.
  Acceptable for audio playback (inaudible).
- **Memory:** Codebooks can be large (up to 8192 entries with 16 dimensions).  Use lazy
  allocation and free codebooks after setup header is fully parsed.

## Implementation Order

1. OGG page parser + packet assembler (standalone testable)
2. Vorbis header parsing (identification + setup)
3. Bit reader
4. Codebook decode
5. Floor type 1
6. Residue decode
7. IMDCT + window + overlap-add
8. Integration with `rt_sound_load` (full-file decode)
9. Integration with `vaud_load_music` (streaming)
