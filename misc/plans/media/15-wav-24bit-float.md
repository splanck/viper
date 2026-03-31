# Plan 15: WAV 24-bit and Float32 PCM Support

## Context

The WAV loader (`vaud_wav.c:144-169`) only accepts `audio_format == 1` (PCM) with 8-bit or
16-bit samples. DAW exports and professional audio tools commonly produce 24-bit PCM or
32-bit float WAV files, which Viper currently rejects.

## Problem

Two format gaps:

1. **24-bit PCM** (`audio_format=1, bits_per_sample=24`): 3 bytes per sample, signed,
   little-endian. Common in music production. Must be downscaled to 16-bit.

2. **32-bit float** (`audio_format=3, bits_per_sample=32`): IEEE 754 float, range [-1, 1].
   Common in DAW bounce exports and audio editing software. Must be converted to 16-bit.

## Approach

### Step 1: Accept 24-bit PCM (~15 LOC)

In `vaud_wav.c`, extend the bits-per-sample validation:

```c
// Current (line 166-169):
if (bits != 8 && bits != 16) return error;

// New:
if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return error;
```

Add a conversion function:

```c
static inline int16_t s24_to_s16(const uint8_t *p) {
    // Little-endian 24-bit signed → 16-bit (discard LSB, keep upper 16 bits)
    int32_t val = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
    if (val & 0x800000) val |= 0xFF000000; // sign extend
    return (int16_t)(val >> 8);
}
```

Update `decode_pcm_frame()` to handle 24-bit:

```c
case 24:
    left = s24_to_s16(src);
    src += 3;
    if (channels == 2) { right = s24_to_s16(src); src += 3; }
    else right = left;
    break;
```

### Step 2: Accept float32 PCM (`audio_format=3`) (~20 LOC)

The WAV `fmt` chunk uses `audio_format = 3` for IEEE float. Extend the format check:

```c
// Current (line 144-149):
if (audio_format != 1) return error;

// New:
if (audio_format != 1 && audio_format != 3) return error;
```

Add a conversion function:

```c
static inline int16_t f32_to_s16(const uint8_t *p) {
    float val;
    memcpy(&val, p, sizeof(float));
    // Clamp to [-1, 1] and scale to 16-bit range
    if (val > 1.0f) val = 1.0f;
    if (val < -1.0f) val = -1.0f;
    return (int16_t)(val * 32767.0f);
}
```

Update `decode_pcm_frame()`:

```c
case 32:
    if (audio_format == 3) {
        left = f32_to_s16(src); src += 4;
        if (channels == 2) { right = f32_to_s16(src); src += 4; }
        else right = left;
    } else {
        // 32-bit integer PCM → take upper 16 bits
        int32_t val; memcpy(&val, src, 4); src += 4;
        left = (int16_t)(val >> 16);
        if (channels == 2) { memcpy(&val, src, 4); src += 4; right = (int16_t)(val >> 16); }
        else right = left;
    }
    break;
```

### Step 3: Pass `audio_format` through decode path

The `decode_pcm_frame` function currently doesn't know the audio format (PCM vs float).
Either:
- Add `audio_format` parameter to `decode_pcm_frame`
- Or check `bits == 32` combined with a format flag stored alongside

Simplest: add the `audio_format` to the function's parameters or to the decode state.

## Files to Modify

| File | Change |
|------|--------|
| `src/lib/audio/src/vaud_wav.c` | Extend validation, add s24/f32 converters, update decode |

## Estimated LOC

~40 lines (two converters ~15, validation changes ~5, decode_pcm_frame updates ~20).

## Verification

- Load 24-bit PCM WAV, verify it plays correctly (no clipping, no noise)
- Load 32-bit float WAV, verify correct playback
- Existing 8-bit and 16-bit WAV files unchanged (regression)
- WAV with unsupported format (e.g., ADPCM) still rejected
- Add test: create 24-bit and float32 WAV in test code, load and verify sample values
