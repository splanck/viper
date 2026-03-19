# Plan: Screen Transitions Library

## 1. Summary & Objective

Extend the existing `Viper.Game.ScreenFX` class with new transition effects: Wipe, CircleIn/Out, SlideIn/Out, Dissolve, and Pixelate. Each generates per-frame overlay data that the game loop applies to the canvas, enabling visually diverse scene transitions beyond the current fade-only approach.

**Why:** Every demo game uses the same black fade for scene changes. Professional games use wipes, iris transitions, slides, and dissolves to match their genre and mood (RPG circle-out, platformer wipes, horror dissolve).

## 2. Scope

**In scope:**
- 6 new transition effects added to existing ScreenFX
- Wipe: horizontal, vertical, diagonal coverage
- CircleIn / CircleOut: iris/circle closing/opening from center point
- SlideIn / SlideOut: push-scroll effect
- Dissolve: random pixel coverage
- Pixelate: resolution reduction over time
- `IsFinished()` method for chaining transitions with scene switches
- Direction enum for wipe/slide (LEFT, RIGHT, UP, DOWN)

**Out of scope:**
- Render-to-texture capture (transitions operate on overlay color/geometry)
- Custom transition shaders
- Transitions that require two scenes rendered simultaneously (crossfade between scenes)
- Per-pixel masking with arbitrary shapes

## 3. Zero-Dependency Implementation Strategy

Each transition computes per-frame state (a clipping rectangle, circle radius, pixel pattern, or block size) that the game loop reads and applies using existing Canvas drawing primitives. The dissolve effect uses a precomputed Bayer matrix for deterministic dithering rather than random pixels. Pure C, ~300 LOC added to existing `rt_screenfx.c`.

## 4. Technical Requirements

### Modified Files
- `src/runtime/collections/rt_screenfx.h` — add new function declarations
- `src/runtime/collections/rt_screenfx.c` — add new transition implementations

### New C API (additions to rt_screenfx.h)

```c
// Direction constants
#define RT_DIR_LEFT   0
#define RT_DIR_RIGHT  1
#define RT_DIR_UP     2
#define RT_DIR_DOWN   3

// New transition effects
void rt_screenfx_wipe(rt_screenfx fx, int64_t direction, int64_t color,
                       int64_t duration);
void rt_screenfx_circle_in(rt_screenfx fx, int64_t cx, int64_t cy,
                            int64_t color, int64_t duration);
void rt_screenfx_circle_out(rt_screenfx fx, int64_t cx, int64_t cy,
                             int64_t color, int64_t duration);
void rt_screenfx_slide_in(rt_screenfx fx, int64_t direction, int64_t duration);
void rt_screenfx_slide_out(rt_screenfx fx, int64_t direction, int64_t duration);
void rt_screenfx_dissolve(rt_screenfx fx, int64_t color, int64_t duration);
void rt_screenfx_pixelate(rt_screenfx fx, int64_t max_block_size, int64_t duration);

// State queries for rendering transitions
int64_t rt_screenfx_get_wipe_progress(rt_screenfx fx);      // 0-1000 (fixed-point)
int64_t rt_screenfx_get_circle_radius(rt_screenfx fx);      // Current radius in pixels
int64_t rt_screenfx_get_circle_cx(rt_screenfx fx);           // Circle center X
int64_t rt_screenfx_get_circle_cy(rt_screenfx fx);           // Circle center Y
int64_t rt_screenfx_get_slide_offset(rt_screenfx fx);        // Pixel offset for slide
int64_t rt_screenfx_get_dissolve_threshold(rt_screenfx fx);  // 0-255 threshold for Bayer
int64_t rt_screenfx_get_pixelate_size(rt_screenfx fx);       // Current block size

// Completion check (works for all effect types)
int8_t  rt_screenfx_is_finished(rt_screenfx fx);
int8_t  rt_screenfx_is_any_finished(rt_screenfx fx);

// High-level draw helper: applies current transition overlay to canvas
void    rt_screenfx_draw(rt_screenfx fx, void *canvas, int64_t screen_w, int64_t screen_h);
```

### Effect Type Enum Extension

```c
typedef enum {
    RT_SCREENFX_SHAKE    = 0,
    RT_SCREENFX_FLASH    = 1,
    RT_SCREENFX_FADE_IN  = 2,
    RT_SCREENFX_FADE_OUT = 3,
    // New types:
    RT_SCREENFX_WIPE        = 4,
    RT_SCREENFX_CIRCLE_IN   = 5,
    RT_SCREENFX_CIRCLE_OUT  = 6,
    RT_SCREENFX_SLIDE_IN    = 7,
    RT_SCREENFX_SLIDE_OUT   = 8,
    RT_SCREENFX_DISSOLVE    = 9,
    RT_SCREENFX_PIXELATE    = 10,
} rt_screenfx_type_t;
```

## 5. runtime.def Registration

Add new RT_FUNC entries and extend existing ScreenFX RT_CLASS_BEGIN block:

```c
// New RT_FUNC entries (add near existing ScreenFX functions)
RT_FUNC(ScreenFXWipe,          rt_screenfx_wipe,           "Viper.Game.ScreenFX.Wipe",          "void(obj,i64,i64,i64)")
RT_FUNC(ScreenFXCircleIn,     rt_screenfx_circle_in,      "Viper.Game.ScreenFX.CircleIn",      "void(obj,i64,i64,i64,i64)")
RT_FUNC(ScreenFXCircleOut,    rt_screenfx_circle_out,     "Viper.Game.ScreenFX.CircleOut",     "void(obj,i64,i64,i64,i64)")
RT_FUNC(ScreenFXSlideIn,      rt_screenfx_slide_in,       "Viper.Game.ScreenFX.SlideIn",       "void(obj,i64,i64)")
RT_FUNC(ScreenFXSlideOut,     rt_screenfx_slide_out,      "Viper.Game.ScreenFX.SlideOut",      "void(obj,i64,i64)")
RT_FUNC(ScreenFXDissolve,     rt_screenfx_dissolve,       "Viper.Game.ScreenFX.Dissolve",      "void(obj,i64,i64)")
RT_FUNC(ScreenFXPixelate,     rt_screenfx_pixelate,       "Viper.Game.ScreenFX.Pixelate",      "void(obj,i64,i64)")
RT_FUNC(ScreenFXIsFinished,   rt_screenfx_is_finished,    "Viper.Game.ScreenFX.get_IsFinished","i1(obj)")
RT_FUNC(ScreenFXDraw,         rt_screenfx_draw,           "Viper.Game.ScreenFX.Draw",          "void(obj,obj,i64,i64)")

// Add to existing RT_CLASS_BEGIN("Viper.Game.ScreenFX", ...) block:
    RT_METHOD("Wipe", "void(i64,i64,i64)", ScreenFXWipe)
    RT_METHOD("CircleIn", "void(i64,i64,i64,i64)", ScreenFXCircleIn)
    RT_METHOD("CircleOut", "void(i64,i64,i64,i64)", ScreenFXCircleOut)
    RT_METHOD("SlideIn", "void(i64,i64)", ScreenFXSlideIn)
    RT_METHOD("SlideOut", "void(i64,i64)", ScreenFXSlideOut)
    RT_METHOD("Dissolve", "void(i64,i64)", ScreenFXDissolve)
    RT_METHOD("Pixelate", "void(i64,i64)", ScreenFXPixelate)
    RT_PROP("IsFinished", "i1", ScreenFXIsFinished, none)
    RT_METHOD("Draw", "void(obj,i64,i64)", ScreenFXDraw)
```

## 6. CMakeLists.txt Changes

No new files — extends existing `rt_screenfx.c` already in `RT_COLLECTIONS_SOURCES`.

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| Invalid direction constant | Default to RT_DIR_LEFT |
| Duration ≤ 0 | Clamp to 1 ms |
| Max concurrent effects exceeded (8) | Oldest non-active effect replaced |
| Circle center off-screen | Works correctly (circle expands from off-screen) |
| Block size ≤ 0 for Pixelate | Clamp to 1 |
| NULL canvas passed to Draw | No-op |
| NULL fx handle | No-op (all functions) |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_screen_transitions.zia`)

1. **Wipe completion**
   - Given: `fx.Wipe(0, 0x000000, 500)` (left wipe, 500ms)
   - When: `fx.Update(500)` called
   - Then: `fx.IsFinished == true`

2. **Circle transition state**
   - Given: `fx.CircleIn(160, 120, 0x000000, 1000)`
   - When: `fx.Update(500)` (half-way)
   - Then: `fx.IsActive == true`, `fx.IsFinished == false`

3. **Multiple effects**
   - Given: `fx.Wipe(...)` then `fx.Flash(...)`
   - When: Both running
   - Then: `fx.IsActive == true` for both types

4. **Draw integration**
   - Given: Active dissolve effect on canvas
   - When: `fx.Draw(canvas, 320, 240)` called
   - Then: No crash, pixels affected

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| UPDATE | `docs/viperlib/game/effects.md` — add Wipe/Circle/Slide/Dissolve/Pixelate sections |
| UPDATE | `docs/viperlib/game.md` — mention new transition effects in ScreenFX summary |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/collections/rt_screenfx.c` | **Primary file to extend** |
| `src/runtime/collections/rt_screenfx.h` | **Primary header to extend** |
| `src/runtime/graphics/rt_drawing.c` | Canvas.Box/Disc for transition rendering |
| `src/runtime/graphics/rt_drawing_advanced.c` | Canvas.BoxAlpha for overlay |
| `examples/games/lib/gamebase.zia` | GameBase.transitionTo() — consumer of ScreenFX |
| `src/il/runtime/runtime.def:8152-8166` | Existing ScreenFX registration to extend |
