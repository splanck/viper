# Plan 08 — Canvas3D Overlay Primitives + Game3D UI Widget Toolkit

> **Status (2026-07-03): steps 1–4 IMPLEMENTED; step 5 (WorldPanel3D) deferred;
> step 6 (ridgebound HUD rewrite) folded into Plan 11's demo pass.**
> - **Overlay primitives (§3.1):** Canvas3D gained `DrawLine2D`, `DrawFrame2D`,
>   `DrawRoundRect2D`/`DrawRoundFrame2D`, `DrawText2DScaled`, `DrawImage2DRegion`,
>   `SetClipRect2D`/`ClearClipRect2D`, and `MeasureText2D` (DrawRect2DAlpha had
>   landed earlier). Clipping is enqueue-time CPU clipping inside the overlay queue
>   (rect intersection, Liang-Barsky for lines, UV remap for images, per-dot for
>   text) so all four backends inherit it with zero backend changes. The image
>   queue was generalized to UV sub-rects (`canvas3d_queue_screen_image_uv`);
>   rounded rects tessellate as a center fan (6 segments/corner).
> - **Canvas-agnostic widgets (§3.2, ADR 0065):** `rt_gameui*` now draws through
>   `rt_gameui_draw_ops_t` (`rt_gameui_draw.h/.c`). The 2D binding assigns the
>   original primitives verbatim (all 63 existing UI tests unchanged = the
>   no-regression gate); the Canvas3D binding
>   (`render/rt_canvas3d_gameui.c`) registers via
>   `rt_gameui_register_canvas3d_ops` at `Canvas3D.New`, keeping `runtime/game`
>   free of `graphics/3d` includes exactly as §5's registration-hook default
>   anticipated. Widget `Draw(canvas)` accepts a Canvas3D handle unchanged.
>   Deviations: Canvas3D text advance-matches the 2D 8px metric (built-in font at
>   scale 8/12; heights differ by ~1px), and custom `Font` objects fall back to
>   the built-in font on Canvas3D (v1, documented).
> - **Deferred:** `WorldPanel3D` (§3.3) — billboarded widget surface; nothing in
>   the shipped demos blocks on it and the Sprite3D + `RenderTarget3D.AsPixels`
>   path already covers world-anchored imagery manually.
> - Verified: 247/247 canvas3d (clip/primitive test + widget-on-Canvas3D smoke),
>   63/63 UI tests, 22/22 surface/health gates, docs updated
>   (rendering3d.md overlay + HUD-widgets sections).

## 1. Objective & scope

Every 3D game hand-lays its HUD with raw `DrawRect2D`/`DrawText2D` (169 calls across 3dbowling + ridgebound) and hand-rolls menus (350–470 lines each). The 2D layer ships a full widget set (`Viper.Game.UI.*`: HudLabel, Bar, Panel, NineSlice, MenuList, Modal, HudSlider, HudDropdown, HudTooltip, GameButton, Table, HudTextInput) that cannot draw on `Canvas3D`. `misc/plans/Game3D-Showcase-polish/04-3d-menu-system-plan.md` already acknowledges the gap.

**In scope:** (1) close the Canvas3D overlay primitive gaps; (2) make the existing widget implementations canvas-agnostic via a draw-adapter, exposed as `Viper.Game3D.UI.*`; (3) a world-space billboarded panel for 3D-anchored labels/bars. **Out of scope:** new widget types, theming system overhaul, IME/text-shaping, gamepad UI navigation (v2).

**Zero external dependencies:** no UI/layout/text libraries; everything builds on the existing from-scratch canvas primitives and the built-in bitmap fonts.

## 2. Current state (verified anchors)

- 2D widgets: `src/runtime/game/rt_gameui.c` + `rt_gameui_widgets.c` + `rt_gameui_textinput.c` (+ `rt_gameui.h`, `rt_gameui_internal.h`); def blocks runtime.def:6174-6205 (flat) + 12122-12313 (classes). Each widget is an id-checked heap object with per-widget colors/fonts (no shared theme object); custom font falls back to the built-in 8×8.
- **Widget draw dependency set** (what the adapter must cover — usage measured across rt_gameui*): `rt_canvas_box` (20 uses), `rt_canvas_frame` (10), `rt_canvas_blit_region` (9), `rt_canvas_box_alpha` (5), `rt_canvas_text` (4), `rt_canvas_text_scaled` (3), `rt_canvas_text_font` (3), `rt_canvas_round_box` (3), `rt_canvas_line` (3), `rt_canvas_width/height`, `rt_canvas_text_width/_height`, `rt_canvas_text_font_scaled`, `rt_canvas_text_scaled_width`, `rt_canvas_round_frame`, `rt_canvas_is_handle`.
- Widget input is **pushed by the caller** (no polling): `HandleKey(i64,i1)`, `HandleMouseClick(i64,i64,i1)`, `HandleMouseDrag(i64,i64)`, `HandleMouseDown`, tooltip `Update(mx,my,down,dt)`.
- Canvas3D overlay today (`render/rt_canvas3d_overlay.c` + queue fns in `rt_canvas3d_draw.inc`): public `rt_canvas3d_draw_rect2d` (`overlay.c:296`; **alpha hardcoded 1.0** at `draw.inc:597-599`), `draw_text2d` (`overlay.c:371`; built-in 8×8 only), `draw_image2d` (`overlay.c:318`; full blit only). **The internal queue already supports RGBA**: `canvas3d_queue_screen_rect` / `canvas3d_queue_screen_line` take per-call alpha (used by `draw_line3d:238`, `draw_crosshair:337`). Overlay auto-opens a frame if `!c->in_frame` (`canvas3d_begin_overlay_frame` pattern in every fn).
- **Missing public overlay primitives:** alpha rect, 2D line, frame/outline, round box/round frame, scaled + custom-font text, image sub-region blit, clip/scissor (2D Canvas has `SetClipRect`/`ClearClipRect`, def:1513-1514 — Canvas3D has none; MenuList/Table/Dropdown need clip for scrolling content).
- 2D font machinery: `rt_canvas_text_font*` draws a `Viper.Graphics.Font` object; the font object itself is canvas-independent (glyph bitmaps) — reusable.

## 3. Design

### 3.1 Step 1 — Overlay primitive extension (`rt_canvas3d_overlay.c` + `rt_canvas3d_draw.inc`)

New public functions (all following the existing begin-overlay-frame + queue pattern; colors `0xRRGGBB` + explicit alpha param to match the 2D `box_alpha` convention):

| New Canvas3D API | Backed by |
|---|---|
| `DrawRect2DAlpha(x,y,w,h,color,alpha)` | existing `canvas3d_queue_screen_rect` (alpha already supported) |
| `DrawLine2D(x0,y0,x1,y1,color,alpha)` | existing `canvas3d_queue_screen_line` |
| `DrawFrame2D(x,y,w,h,color,alpha)` | 4× queue_screen_rect (1px) — one queue helper `canvas3d_queue_screen_frame` |
| `DrawRoundRect2D/DrawRoundFrame2D(x,y,w,h,radius,color,alpha)` | new queue prim: rect + corner fans (triangulated quarter-discs, same tessellation the 2D `round_box` uses) |
| `DrawText2DScaled(x,y,text,color,scale)` | scale factor through the existing 8×8 glyph queue |
| `DrawText2DFont(x,y,text,font,scale)` | glyph-blit loop over the Font object via the screen-image queue (sub-rect — below) |
| `DrawImage2DRegion(x,y,w,h,pixels,sx,sy,sw,sh)` | extend `canvas3d_queue_screen_image` with source-rect UVs |
| `SetClipRect2D(x,y,w,h)` / `ClearClipRect2D()` | scissor state on the overlay queue: CPU-clip queued prims at enqueue time (rect/line/image clipping in canvas code — **not** backend scissor state, so all four backends get it for free and SW parity is exact) |
| `MeasureText2D(text,scale) -> i64` (+ font variant) | pure computation, mirrors `rt_canvas_text_width` |

Enqueue-time CPU clipping is the key simplifier: the overlay queue is already canvas-side; clipping there means zero backend changes.

### 3.2 Step 2 — Canvas-agnostic widgets via draw adapter

Refactor `rt_gameui*` to draw through a function-table adapter instead of direct `rt_canvas_*` calls:

```c
/* src/runtime/game/rt_gameui_draw.h */
typedef struct rt_gameui_draw_ops {
    void *canvas;
    void (*box)(void*, i64 x, i64 y, i64 w, i64 h, i64 color);
    void (*box_alpha)(void*, ..., double alpha);
    void (*frame)(void*, ...);  void (*round_box)(void*, ...); void (*round_frame)(void*, ...);
    void (*line)(void*, ...);
    void (*text)(void*, ...);   void (*text_scaled)(void*, ...); void (*text_font)(void*, ...);
    void (*blit_region)(void*, ...);
    i64  (*width)(void*); i64 (*height)(void*);
    i64  (*text_width)(void*, rt_string, double scale); i64 (*text_height)(void*);
    void (*set_clip)(void*, ...); void (*clear_clip)(void*);
} rt_gameui_draw_ops;
```

- Two bindings: `rt_gameui_draw_ops_canvas2d(void *canvas)` (thin wrappers over today's `rt_canvas_*` calls — behavior-identical, existing 2D tests are the regression net) and `rt_gameui_draw_ops_canvas3d(void *canvas3d)` (the step-1 primitives).
- Widget `Draw(canvas)` methods detect the handle class (`rt_canvas_is_handle` vs Canvas3D class id) and pick the binding — **the same widget object works on both canvases**; no widget-logic duplication, no divergence risk.
- Public exposure: register the *same* widget classes under `Viper.Game3D.UI.*` aliases? **No** — leaf-name uniqueness forbids duplicate class leaves and aliasing creates surface debt. Instead: keep the classes as `Viper.Game.UI.*` and simply make their `Draw` accept a Canvas3D handle (sig stays `void(obj)`). Game3D docs gain a "HUD widgets" section pointing at `Viper.Game.UI` — one widget set, two canvases. (`Viper.Game` binds fine from 3D games; ridgebound already binds multiple namespaces.)
- Input plumbing helper: `Input3D` exposes mouse pos/buttons already; add a tiny doc recipe + `gamebase3d.zia` (plan 02) forwards `HandleMouseClick`/`HandleKey` from the Input3D snapshot — no new C.

### 3.3 Step 3 — World-space panel

`Viper.Graphics3D.WorldPanel3D` (new class, `"obj"`): an offscreen-composited billboard — internally a `Pixels` surface the user draws widgets onto via a 2D-canvas-compatible target, rendered as a camera-facing quad (reuse the `Sprite3D` billboard path, `render/rt_sprite3d.c`). v1: fixed-size pixel surface + `SetWorldPosition/SetScale/SetOpacity` + `Refresh()` (re-uploads when dirty). Health bars/name plates become: draw Bar widget onto panel surface → panel billboards in world. Defer interaction (raycast-to-panel picking) to v2.

## 4. Implementation steps

1. Overlay primitives batch 1: `DrawRect2DAlpha/DrawLine2D/DrawFrame2D/DrawText2DScaled/MeasureText2D` + queue helpers + unit tests (SW readback asserting pixels) — **this commit also unblocks plan 02's fade transitions**.
2. Overlay primitives batch 2: round rects, font text, image region, clip (enqueue-time clipping + clip unit tests: half-clipped rect/text/image readbacks).
3. `rt_gameui_draw_ops` adapter refactor + 2D binding + full 2D UI test suite green (pure refactor; zero visual change — existing gameui tests are the gate).
4. Canvas3D binding + widget-on-3D smoke tests (Panel+Bar+MenuList render on SW Canvas3D readback; input handlers drive MenuList selection).
5. `WorldPanel3D` + billboard rendering + unit test (panel follows world point under camera orbit; SW readback).
6. Rewrite `ridgebound/hud.zia` onto widgets (the proof; ~148 lines → target < 60) + docs (`game3d.md` HUD section, `canvas.md` overlay additions).

## 5. Public API changes (runtime.def)

- `Viper.Graphics3D.Canvas3D`: new methods from §3.1 (RT_FUNC handlers `rt_canvas3d_draw_rect2d_alpha`, `_draw_line2d`, `_draw_frame2d`, `_draw_round_rect2d`, `_draw_round_frame2d`, `_draw_text2d_scaled`, `_draw_text2d_font`, `_draw_image2d_region`, `_set_clip_rect2d`, `_clear_clip_rect2d`, `_measure_text2d`).
- `Viper.Game.UI.*`: no signature changes (Draw's `obj` param now accepts Canvas3D — document).
- New class `Viper.Graphics3D.WorldPanel3D` (leaf `WorldPanel3D` unique) + class ID in `rt_graphics3d_ids.h`; new file `render/rt_worldpanel3d.c` → baseline bump.
- ADR: one lightweight ADR covering "Game.UI widgets are canvas-polymorphic" (cross-layer dependency note: `runtime/game` now calls into `graphics/3d` overlay — verify layering; if `game → graphics/3d` is a forbidden edge, invert with a registration hook where Canvas3D registers its ops table with rt_gameui at init, keeping `game` free of 3d includes — **decide at implementation start; the registration-hook form is the safe default**).

## 6. Tests

- Primitive readbacks (SW): alpha blend value asserted, line endpoints, clip correctness, font glyph placement, region blit.
- Adapter refactor: entire existing gameui unit suite unchanged (fail-before impossible — this is the no-regression gate).
- Widget-on-3D: Given a MenuList drawn on Canvas3D SW, When HandleKey(down)+HandleKey(enter), Then selection index advances and the highlight row moves (readback).
- WorldPanel: billboard center projects to expected screen position across 3 camera angles.
- Golden: ridgebound HUD before/after visual parity capture.

## 7. Verification gates

Full build + ctest (`-L graphics3d`, gameui labels, `-L slow`); surface audits + completeness after def changes; overlay primitives verified on Metal + SW (they're canvas-side queue code — backend-neutral by construction, but verify the image-region UV path on Metal too).

## 8. Risks & constraints

- **Layering:** the `game ↔ graphics/3d` edge (see §5 ADR note) is the one architectural decision to settle first.
- **Text quality:** 8×8 bitmap font at 3D-game resolutions is chunky — `DrawText2DFont` with the existing Font objects is the mitigation; a nicer default font asset is a separate future item.
- **Clip semantics:** enqueue-time clipping means clip state applies to calls made while set (immediate semantics) — identical to the 2D canvas; document.
- **Zero external dependencies:** no font/UI/vector libraries; round-corner tessellation and glyph blitting are in-tree code only.
