# Fix 105: Terrain Heightmap 16-Bit Precision

## Severity: P1 — High

## Problem

Terrain heightmaps are sampled from only the red channel of a Pixels object
(`rt_terrain3d.c:125-127`), giving just 256 discrete height levels. This causes
visible stairstepping on terrain surfaces and is inadequate for real-world terrain.

## Prerequisites

**All height-sampling sites must be updated together.** There are 3 places that
read from the heightmap:

1. **SetHeightmap** (`rt_terrain3d.c:125-127`) — initial height extraction from pixels
2. **GetHeightAt** (`rt_terrain3d.c:~204`) — bilinear height query. This reads from
   `t->heights[]` (already float), NOT from pixels directly. **No change needed.**
3. **GetNormalAt** (`rt_terrain3d.c:~230`) — normal via finite differences. Uses
   `t->heights[]`. **No change needed.**

Only site 1 reads from the pixel data. Sites 2 and 3 read from the float `heights[]`
array which is already in [0, 1] range. The fix only touches site 1.

## Fix

Use both the red and green channels for 16-bit precision (65,536 levels):

```c
uint32_t pixel = pv->data[sz * sw + sx];
uint32_t r = (pixel >> 24) & 0xFF;
uint32_t g = (pixel >> 16) & 0xFF;
float height = (float)((r << 8) | g) / 65535.0f;
t->heights[z * t->width + x] = height;
```

Backward-compatible: existing heightmaps using only red channel will still work
(green defaults to 0 in most image editors, so `R << 8 | 0` gives the same coarse
height scaled into 16-bit range).

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_terrain3d.c` | Change height sampling to R+G (~3 LOC) |

## Documentation Update

Update `docs/graphics3d-guide.md` in the Terrain section:
- Document that heightmaps now use R+G channels (R=high byte, G=low byte) for 16-bit height
- Note backward compatibility with 8-bit red-only heightmaps
- Example: creating a 16-bit heightmap in code by encoding `height * 65535` into R and G channels

## Test

Add to existing terrain test or create `test_terrain_height_precision`:
- Create a 4×4 heightmap with values 0.0, 0.25, 0.5, 0.75, 1.0 encoded as R+G
- Load as terrain, verify `GetHeightAt` returns values within ±0.001 of expected
- Create 8-bit red-only heightmap (G=0), verify backward compatibility
- Existing terrain tests pass (regression)
