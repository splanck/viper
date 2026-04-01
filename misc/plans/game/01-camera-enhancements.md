# Plan 01: Camera SmoothFollow + Deadzone

## Context

Camera.Follow() in rt_camera.c (line 230) is instant centering + bounds clamp.
The XENOSCAPE demo wraps it with SmoothValue for interpolation (camera.zia:120-127,
1090 lines total). No deadzone support — camera moves on every pixel of player movement.

NOTE: Camera.Shake is NOT needed — ScreenFX.Shake() already exists and is used
correctly by the demo. Camera3D has SmoothFollow but Camera (2D) does not.

## Changes

### rt_camera.c — 2 new methods (~80 LOC)

**SmoothFollow(targetX, targetY, lerpFactor)**
```c
void rt_camera_smooth_follow(rt_camera cam, int64_t tx, int64_t ty, int64_t lerp_pct) {
    // lerp_pct: 0-1000 (1000 = instant, 100 = slow smooth)
    int64_t cx = tx - cam->width / 2;
    int64_t cy = ty - cam->height / 2;
    // Deadzone check: skip if target within deadzone
    if (cam->deadzone_w > 0 || cam->deadzone_h > 0) {
        int64_t dx = cx - cam->x;
        int64_t dy = cy - cam->y;
        if (dx > -cam->deadzone_w/2 && dx < cam->deadzone_w/2 &&
            dy > -cam->deadzone_h/2 && dy < cam->deadzone_h/2)
            return;
    }
    cam->x += (cx - cam->x) * lerp_pct / 1000;
    cam->y += (cy - cam->y) * lerp_pct / 1000;
    camera_clamp_bounds(cam);
    cam->dirty = 1;
}
```

**SetDeadzone(width, height)**
```c
// New struct fields: deadzone_w, deadzone_h (default 0 = disabled)
void rt_camera_set_deadzone(rt_camera cam, int64_t w, int64_t h);
```

NOTE: Camera.Shake is NOT included — use ScreenFX.Shake() instead (already exists).

### rt_camera.h — add declarations

### runtime.def — 2 new entries
```
RT_FUNC(CameraSmoothFollow, rt_camera_smooth_follow, "Viper.Game.Camera.SmoothFollow", "void(obj,i64,i64,i64)")
RT_FUNC(CameraSetDeadzone, rt_camera_set_deadzone, "Viper.Game.Camera.SetDeadzone", "void(obj,i64,i64)")
```

### Files to modify
- `src/runtime/graphics/rt_camera.c` — add 2 methods + deadzone struct fields
- `src/runtime/graphics/rt_camera.h` — add declarations
- `src/il/runtime/runtime.def` — register 2 new functions

### Tests

**File:** `src/tests/unit/runtime/TestCameraEnhance.cpp`
```
TEST(Camera, SmoothFollowConverges)
  — Create camera, call SmoothFollow 60 times toward target, verify position converges

TEST(Camera, SmoothFollowInstantAt1000)
  — SmoothFollow with lerp=1000, verify instant centering (matches Follow behavior)

TEST(Camera, DeadzoneNoMovement)
  — Set deadzone 100x100, move target within zone, verify camera doesn't move

TEST(Camera, DeadzoneTriggersOutside)
  — Move target outside deadzone, verify camera follows

```

### Doc update
- `docs/viperlib/graphics/camera.md` — add SmoothFollow, SetDeadzone, Shake sections
