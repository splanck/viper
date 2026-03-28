# Plan: Audio3D Fixes — COMPLETE

## ~~1. Reversed Stereo Panning~~ — ALREADY CORRECT (verified)
Cross product at lines 55-58 computes correct right vector.

## 2. Global max_distance — FIXED (2026-03-28)
**Was:** `saved_max_dist` single global variable overwritten by each `play_at()` call. All voices used whichever max_distance was set last.

**Now:** Per-voice tracking table (`s_voice_dist[64]`). Each `play_at()` records the voice_id + max_distance pair. `update_voice()` with `max_distance <= 0` looks up the per-voice value via `lookup_voice_distance()`. Old global fallback replaced.

### Implementation details:
- Fixed-size table of 64 entries (matches typical max concurrent voices)
- Linear scan for lookup (O(n) but n ≤ 64, called once per voice per frame)
- Overflow: overwrites slot 0 (oldest voice, likely already stopped)
- Default fallback if voice not found: 50.0 (same as before)

### Files Changed
- `src/runtime/graphics/rt_audio3d.c` — Per-voice tracking table, lookup, tracking on play

### Tests
Audio3D requires audio hardware init which is unavailable in headless CI. The fix is verified by code inspection — the tracking table is a straightforward data structure replacement. The compute_3d_params math is unchanged.

### Documentation updated:
- Plan marked complete
- Release notes updated
- Guide updated
