# Video Playback — Implementation Overview

## Goal

Add video playback to Viper for two use cases:
1. **Games** — cutscenes, intro videos, cinematics (rendered to Canvas/Canvas3D)
2. **GUI applications** — media player widgets, embedded video in Viper.GUI apps

All implementations follow Viper's zero-dependency principle (from-scratch C).

## Tier Plan

| Tier | Plan | Container | Video Codec | Audio Codec | LOC | Status |
|------|------|-----------|-------------|-------------|-----|--------|
| 0 | 01 | — | — | — | ~100 | Pre-req: JPEG buffer decode refactor |
| 1 | 02 | AVI (RIFF) | MJPEG | PCM WAV | ~800 | First: validates architecture |
| 1 | 03 | — | — | — | ~200 | VideoPlayer runtime class + Zia API |
| 2 | 04 | OGG (reuse) | Theora | Vorbis (reuse) | ~3500 | Primary format (Godot precedent) |
| — | 05 | — | — | — | ~300 | GUI VideoWidget |
| — | 06 | — | — | — | ~200 | Documentation + demos |

Total: ~5100 LOC across 6 plans.

## Implementation Order

1. **Plan 01** — Refactor JPEG decoder for buffer-based decode (pre-req for MJPEG)
2. **Plan 02** — AVI RIFF container parser
3. **Plan 03** — VideoPlayer runtime class (ties container + codec + audio + API)
4. **Plan 04** — Theora video codec (decode-only, from spec)
5. **Plan 05** — GUI VideoWidget for Viper.GUI applications
6. **Plan 06** — Documentation, demos, and ctests

## Key Existing Infrastructure to Reuse

| Component | File | Reuse |
|-----------|------|-------|
| JPEG decoder | `rt_pixels_io.c:1165-1819` | MJPEG = sequence of JPEG frames |
| OGG container | `rt_ogg.c/h` (276 LOC) | Theora uses OGG containers |
| Vorbis decoder | `rt_vorbis.c/h` (1100+ LOC) | Audio track in OGG Theora files |
| WAV decoder | audio system | Audio track in AVI files |
| DEFLATE | `rt_compress.c/h` | Some containers use zlib |
| Pixels API | `rt_pixels.c/h` | Frame buffer for decoded video |
| Canvas/Canvas3D | rt_canvas.c / rt_canvas3d.c | Display surface |
| Audio streaming | `rt_mp3.c`, `rt_vorbis.c` | Triple-buffer pattern for A/V sync |
| GIF animation | `rt_gif.c` (420 LOC) | Multi-frame playback pattern |

## Codecs NOT Planned

| Codec | Reason |
|-------|--------|
| H.264/AVC | Patent-encumbered (MPEG-LA), ~50K LOC for basic decoder |
| H.265/HEVC | More patents than H.264, more complex |
| AV1 | Royalty-free but ~100K LOC reference decoder |
| VP9 | Patent concerns (Sisvel pool), very complex |
| VP8/WebM | Stretch goal for future release, not in scope here |
