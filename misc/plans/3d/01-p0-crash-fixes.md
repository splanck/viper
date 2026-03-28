# Plan: P0 Crash Fixes

## Overview
After verification against actual code, 2 of 3 originally reported P0 bugs are already fixed. One confirmed issue remains.

## ~~1. Canvas3D Division by Zero~~ — ALREADY FIXED
Code at `rt_canvas3d.c:1017-1019` already clamps: `if (cs < 0.001f) cs = 1.0f;`

## ~~2. Camera3D asin NaN~~ — ALREADY FIXED
Code at `rt_camera3d.c:370` already clamps: `asin(fmax(-1.0, fmin(1.0, fy)))`

## 3. Sprite3D Use-After-Free — NEEDS VERIFICATION
**File:** `src/runtime/graphics/rt_sprite3d.c:159-189`
**Issue:** Per-frame mesh/material creation without temp buffer registration. `rt_canvas3d_add_temp_buffer` exists (line 1077 of canvas3d.c) but may not be called by sprite draw code.
**Status:** Needs closer inspection — the original bug report flagged this but the caching pattern needs verification against actual draw path.
**Fix if confirmed:** Cache mesh/material in sprite struct, or call `rt_canvas3d_add_temp_buffer()`.

## Verdict
This plan has minimal remaining work. The P0 crash surface is smaller than originally reported.
