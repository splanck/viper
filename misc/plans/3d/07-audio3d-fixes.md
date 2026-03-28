# Plan: Audio3D Fixes

## Overview
After code verification: stereo panning is correct. One confirmed issue remains.

## ~~1. Reversed Stereo Panning~~ — ALREADY CORRECT
**Verified:** Lines 55-58 compute `right = cross(forward, world_up)` as `(-fz, 0, fx)` which is the standard right-hand cross product for forward×(0,1,0).

## 2. Global max_distance (CONFIRMED)
**File:** `src/runtime/graphics/rt_audio3d.c:39,70-94`
**Verified:** `saved_max_dist` is a static global (line 39, default 50.0). This value is shared across all 3D voice positions, meaning different sounds with different falloff ranges will use whichever max_distance was set last.
**Fix:** Store max_distance per active 3D voice:
```c
#define MAX_3D_VOICES 32
static struct {
    int64_t voice_id;
    double max_distance;
    double position[3];
    int8_t active;
} s_3d_voices[MAX_3D_VOICES];
```
In `play_at()`: find free slot, store voice_id + max_distance.
In `update_voice()`: look up per-voice max_distance by voice_id.
In voice completion: mark slot inactive.

## Files Modified
- `src/runtime/graphics/rt_audio3d.c` — Per-voice distance tracking

## Verification
- Play two sounds: gunshot (max_distance=20) and ambient (max_distance=200)
- Move listener away — gunshot should fade faster than ambient
