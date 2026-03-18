# Plan: Compressed Audio — Viper Audio Format (ADPCM)

## 1. Summary & Objective

Add a custom compressed audio format (.vaf — Viper Audio Format) using IMA ADPCM encoding. Provides ~4:1 compression ratio over raw PCM WAV, dramatically reducing game distribution sizes. The existing `Sound.Load()` and `Music.Load()` auto-detect format by file header magic bytes.

**Why:** WAV-only means massive distribution sizes. One minute of 44100 Hz stereo WAV = ~10 MB. A game with 5 music tracks is 50+ MB of audio alone. ADPCM achieves ~4:1 compression with negligible quality loss for game audio, bringing that to ~12 MB.

## 2. Scope

**In scope:**
- IMA ADPCM encoder (WAV → .vaf conversion)
- IMA ADPCM decoder (streaming block decode)
- .vaf file format: custom header + ADPCM data blocks
- Auto-detection in `Sound.Load()` / `Music.Load()` by header magic
- Block-based streaming for Music (4KB decode blocks)
- `Viper.Sound.Encode(inputPath, outputPath)` — conversion utility
- Mono and stereo support
- 44100 Hz (matching existing audio pipeline)

**Out of scope:**
- General-purpose audio compression (OGG, MP3, FLAC, AAC)
- Variable bitrate encoding
- Lossless compression
- Sample rate conversion during encode
- Metadata/tags in .vaf files
- Multi-channel (>2 channel) audio

## 3. Zero-Dependency Implementation Strategy

**IMA ADPCM** (Interactive Multimedia Association Adaptive Differential Pulse Code Modulation) is a public-domain algorithm standardized by the IMA in 1992. It encodes 16-bit PCM samples as 4-bit differences with an adaptive step size. The algorithm is:

- **Encoder (~150 LOC):** For each sample, compute difference from predicted value, quantize to 4-bit nibble, update step size from a 89-entry lookup table.
- **Decoder (~100 LOC):** For each 4-bit nibble, reconstruct sample using step table and accumulator. Exact inverse of encoder.
- **Step table:** 89 constant integers (globally shared, no dynamic allocation).

This is the same codec used by Microsoft WAV ADPCM, Xbox audio, and many game engines. Completely documented, patent-free, no external code needed.

### .vaf File Format

```
Offset  Size  Field
0       4     Magic: "VAF1" (0x56414631)
4       2     Channels (1=mono, 2=stereo)
6       4     Sample rate (44100)
10      4     Total samples (per channel)
14      4     Block size (bytes per ADPCM block, default 1024)
18      2     Bits per sample (always 4 for IMA ADPCM)
20      ...   ADPCM data blocks
```

Each ADPCM block (for mono):
```
Offset  Size  Field
0       2     Predictor (initial sample, int16)
2       1     Step index (0-88)
3       1     Reserved (0)
4       ...   Packed 4-bit nibbles (2 samples per byte)
```

For stereo: interleaved left/right blocks.

## 4. Technical Requirements

### New Files
- `src/runtime/audio/rt_audio_codec.h` — encoder/decoder API
- `src/runtime/audio/rt_audio_codec.c` — implementation (~400 LOC)

### C API (rt_audio_codec.h)

```c
// === ADPCM Step Table (public for tests) ===
extern const int16_t rt_adpcm_step_table[89];
extern const int8_t  rt_adpcm_index_table[16];

// === Encoding ===
// Encode a WAV file to .vaf format
// Returns 1 on success, 0 on failure
int8_t rt_audio_encode_vaf(rt_string input_wav_path, rt_string output_vaf_path);

// Encode raw PCM buffer to ADPCM block
// Returns bytes written to output buffer
int64_t rt_adpcm_encode_block(const int16_t *pcm, int64_t sample_count,
                               uint8_t *output, int64_t output_capacity);

// === Decoding ===
// Decode a .vaf file to raw PCM (for Sound.Load)
// Returns allocated PCM buffer and sets out parameters
int16_t *rt_audio_decode_vaf(const char *path, int32_t *out_channels,
                              int32_t *out_sample_rate, int64_t *out_sample_count);

// Decode a single ADPCM block to PCM
// Returns samples decoded
int64_t rt_adpcm_decode_block(const uint8_t *adpcm, int64_t block_bytes,
                               int16_t *output, int64_t output_capacity);

// === Streaming Decoder (for Music) ===
typedef struct rt_vaf_stream rt_vaf_stream;

rt_vaf_stream *rt_vaf_stream_open(const char *path);
void           rt_vaf_stream_close(rt_vaf_stream *stream);
int64_t        rt_vaf_stream_read(rt_vaf_stream *stream, int16_t *buffer,
                                   int64_t sample_count);           // Returns samples read
void           rt_vaf_stream_seek(rt_vaf_stream *stream, int64_t sample_pos);
int64_t        rt_vaf_stream_tell(rt_vaf_stream *stream);
int64_t        rt_vaf_stream_total_samples(rt_vaf_stream *stream);
int32_t        rt_vaf_stream_channels(rt_vaf_stream *stream);
int32_t        rt_vaf_stream_sample_rate(rt_vaf_stream *stream);
int8_t         rt_vaf_stream_is_eof(rt_vaf_stream *stream);

// === Format Detection ===
// Check if file is a .vaf by reading first 4 bytes
int8_t rt_audio_is_vaf(const char *path);
```

### Integration with Existing Audio

Modify `rt_audio.c`:
- `rt_sound_load()`: Check `rt_audio_is_vaf(path)` first. If true, use `rt_audio_decode_vaf()` instead of WAV parser. Resulting PCM buffer is identical to WAV load.
- `rt_music_load()`: Check for .vaf. If true, create `rt_vaf_stream` for block-based streaming decode.

### Runtime Encode Function

```c
// Exposed to Zia as static function
int8_t rt_audio_encode(rt_string input_path, rt_string output_path);
```

## 5. runtime.def Registration

```c
//=============================================================================
// SOUND - AUDIO ENCODING
//=============================================================================

RT_FUNC(AudioEncode, rt_audio_encode, "Viper.Sound.Encode", "i1(str,str)")

// No new classes — .vaf loading is transparent through existing Sound.Load / Music.Load
```

Minimal runtime.def changes since .vaf support is transparent: `Sound.Load("music.vaf")` just works.

## 6. CMakeLists.txt Changes

In `src/runtime/CMakeLists.txt`, add to `RT_AUDIO_SOURCES`:
```cmake
audio/rt_audio_codec.c
```

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| Input WAV not found | Encode returns 0 |
| Input not valid WAV | Encode returns 0 |
| Output path not writable | Encode returns 0 |
| .vaf with invalid magic | Sound.Load falls through to WAV parser |
| .vaf with wrong sample rate | Decode succeeds, resample in Sound.Load |
| Truncated .vaf file | Decoder returns what it could decode, fills rest with silence |
| Seek beyond end of stream | Clamp to end, return 0 samples |
| NULL path | Return NULL/0 |
| Non-44100 Hz WAV input | Encode includes rate in header; decoder reports it for resampling |

## 8. Tests

### C Unit Tests (`src/tests/test_rt_audio_codec.cpp`)

1. **Round-trip encode/decode**
   - Given: Known PCM buffer (sine wave, 1 second, 44100 Hz)
   - When: Encode to ADPCM block, then decode back
   - Then: Output within ±2 of original per sample (ADPCM 4-bit precision)

2. **File format round-trip**
   - Given: A test .wav file
   - When: `rt_audio_encode_vaf("test.wav", "test.vaf")` then load "test.vaf"
   - Then: Sample count matches, channels match, first 100 samples within tolerance

3. **Streaming decoder**
   - Given: A .vaf file (encoded from test WAV)
   - When: Open stream, read 4096 samples, seek to 0, read again
   - Then: Both reads produce identical output

4. **Format detection**
   - Given: A .vaf file and a .wav file
   - When: `rt_audio_is_vaf()` called on both
   - Then: Returns 1 for .vaf, 0 for .wav

5. **Compression ratio**
   - Given: 1-second stereo 44100 Hz WAV (176,400 bytes PCM)
   - When: Encoded to .vaf
   - Then: .vaf file is ~44,100 bytes (4:1 ratio, within 10%)

6. **Empty/short input**
   - When: Encode a 0-sample WAV
   - Then: Returns valid .vaf with 0 samples

### Zia Runtime Tests (`tests/runtime/test_audio_codec.zia`)

1. **Encode function accessible**
   - When: `Viper.Sound.Encode("nonexistent.wav", "out.vaf")`
   - Then: Returns false (no crash)

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| UPDATE | `docs/viperlib/audio.md` — add .vaf format section, Encode function, update Audio File Format table |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/audio/rt_audio.c` | **Modify** — add .vaf detection in Sound.Load / Music.Load |
| `src/runtime/audio/rt_synth.c` | Pattern: PCM buffer generation (16-bit, 44100 Hz) |
| `src/runtime/audio/rt_soundbank.c` | Pattern: file loading with format detection |
| `src/runtime/io/rt_compress.c` | Pattern: compression/decompression in pure C |
| `src/il/runtime/runtime.def` | Registration (add Encode as static function) |
