---
status: active
audience: contributors
last-verified: 2026-07-03
---

# ADR 0065 — Game.UI widgets are canvas-polymorphic (2D Canvas + Canvas3D)

## Status

Accepted (2026-07-03).

## Context

Every 3D game hand-laid its HUD with raw `Canvas3D.DrawRect2D`/`DrawText2D`
calls because the `Viper.Game.UI.*` widget set (HudLabel, Bar, Panel,
NineSlice, MenuList, Modal, HudSlider, HudDropdown, HudTooltip, GameButton,
Table, HudTextInput) drew exclusively through the 2D `rt_canvas_*`
primitives. Duplicating the widget set for Canvas3D would fork ~4,600 lines
of widget logic, and class-leaf-name uniqueness forbids re-registering the
same classes under a `Viper.Game3D.UI.*` alias.

Layering constraint: `runtime/game` must not depend on `runtime/graphics/3d`
(the widgets are part of the 2D-facing game layer; the 3D stack sits beside
it, not beneath it).

## Decision

1. The widgets draw through a function-pointer table
   (`rt_gameui_draw_ops_t`, `src/runtime/game/rt_gameui_draw.h`) instead of
   calling `rt_canvas_*` directly. The 2D binding assigns the existing
   primitives verbatim, so 2D behavior is unchanged by construction.
2. The Canvas3D binding is **registered from the 3D layer**
   (`rt_gameui_register_canvas3d_ops`, called at `Canvas3D.New`): the 3D
   layer pushes its probe + ops-fill hooks down into `runtime/game`, keeping
   `game/` free of `graphics/3d` includes. Unknown handles trap exactly as
   the old `expected Canvas` validation did.
3. Widget `Draw(canvas)` signatures are unchanged; the `obj` parameter now
   also accepts a Canvas3D handle. No new classes, no aliases.
4. Canvas3D text ops draw the built-in Canvas3D 5×7 font advance-matched to
   the 2D 8px-per-character metric (scale 8/12) so layout computed from
   `rt_canvas_text_width` remains correct. Custom `Font` objects fall back
   to the built-in font on Canvas3D (v1 limitation).

## Consequences

- One widget implementation, two canvases; no divergence risk.
- 3D HUD/menu code can drop hand-rolled drawing for the widget set
  (Canvas3D overlay primitives added for parity: alpha rects, lines,
  frames, rounded rects, scaled text, region blits, clip rects).
- Widgets acquire a hidden dispatch cost of one table resolve per Draw —
  negligible next to the drawing itself.
- The registration hook is process-global; headless builds without
  graphics simply never register the 3D binding.
