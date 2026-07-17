# Zanna Paint → First-Class Drawing Application

> Approved plan. Progress tracked in `paint-progress.md` (same dir).

## Context

`examples/apps/paint/` is a Zia drawing app (~5,300 lines, 25 core modules) built on `Zanna.Graphics.Canvas`. It is well-architected (fat-controller + composition-over-inheritance, cached layer compositing, zoomable viewport, snapshot undo) but its own module headers describe it as an **MVP**: several behaviors are deliberately stubbed at "good enough for a demo" quality, and large swaths of the `Zanna.Graphics` runtime go unused. The goal is a **maximal push** to a genuinely first-class paint program across four pillars — **pro color, more tools, layer power, visual/UX polish** — while fixing poor API usage and correctness gaps along the way. As a flagship demo it must both *look* polished and be *correct*.

Three findings from the deep read drive the plan:
1. **The layer model already carries true alpha** (`layers.zia:319-336` blends `alpha = raw % 256`), but tools don't use it — the eraser writes background color opaquely (`eraser.zia:97`) and the eyedropper guesses transparency with `if color != 0` (`eyedropper.zia:44`). These are correctness debts fixable with the *existing* runtime.
2. **`Zanna.Graphics.Canvas` and `Zanna.GUI.App` are different window systems.** Paint correctly stays on `Canvas` and uses `Zanna.Game.UI` widgets; only the *global* `Zanna.GUI` helpers (`FileDialog`, `MessageBox`, `Theme`, and — to add — `Clipboard`, `Cursor`) compose with a Canvas window. "Use Zanna.GUI" means leveraging those globals + the rich `Zanna.Graphics` drawing surface, **not** a chrome rewrite onto the widget tree.
3. **The richest unused surface is `Zanna.Graphics`:** `Color.FromHSL/FromHex/ToHex/GetH/S/L/Brighten/Darken/Complement`, `Canvas.Bezier/Arc/Polygon/GradientH/V`, `Pixels.DrawBezier/DrawTriangle/Rotate/RotateCW/Resize/Tint/Set(raw)`, and `Gradient2D` — all confirmed present in the runtime.

### Architecture decision (keep, don't rewrite)
Stay on `Zanna.Graphics.Canvas` with immediate-mode UI. Build new chrome (menus, color picker, toasts, context menus) as in-canvas widgets consistent with the existing `ui/` modules. This guarantees compatibility, honors the existing design, and is the only way a custom zoomable paint document can work. Maintain the **dark theme** throughout (accessibility requirement — user is visually impaired).

### Delivery
"Maximal push" = all four pillars, but **sequenced to keep the app building + running green at every step** (CLAUDE.md "always green locally"). Workstream A (structure + correctness) lands first because everything else builds on it.

---

## Workstream A — Structure & correctness (foundation; do first)

**A1. Tool interface + registry (kills the dispatch debt).**
Introduce `tools/tool.zia` defining a Zia `interface Tool` with a uniform signature:
`init`, `getName/getIcon/getShortcut/getToolId`, `onMouseDown/onMouseMove/onMouseUp(x, y, ctx)`, `isDrawing`, `drawPreview(gfx, view, ctx)`, `isInstant`.
Bundle tool dependencies into a `ToolContext` (canvas, colors, brushSettings, selection, clipboard) so every tool takes the same args. Each existing tool `implements Tool`; standardize their signatures (ignore unused fields). Add `tools/registry.zia` holding `List[Tool]` keyed by tool ID, replacing the three 8-way ladders in `app.zia:920-975`, `selectTool` (`app.zia:978`), and `isFreehandTool/activeToolIsDrawing` with one dynamic-dispatch path.
- *Primary approach:* interface + `List[Tool]` (Zia interfaces + optional method chaining are confirmed working). *Fallback if heterogeneous `List[Tool]` storage misbehaves:* keep concrete tools but move the (now uniform) dispatch into a single `tool_controller.zia` module so `app.zia` shrinks and repetition is owned in one place. Verify with a 10-line probe before committing.

**A2. Split the 1,255-line `app.zia`.** Extract:
- `ui/chrome.zia` — all `draw*` panel rendering (`drawToolbar/drawToolPanel/drawColorPanel/drawStatusBar` etc.).
- `input_router.zia` — `handleInput/handleKeyboard/handleCanvasInput` + mouse-edge state.
- `app.zia` keeps orchestration + lifecycle only. Target < 500 lines.

**A3. Correctness / API fixes (the "poor use of API" pass):**
- **True-alpha eraser** (`eraser.zia` + `canvas.zia`): add `canvas.erasePixel(x,y)` / `eraseStamp` that writes raw transparent via `Pixels.Set(x, y, 0)`. Eraser erases to transparency on normal layers; on the opaque base "Background" layer (index 0) it keeps erase-to-bg-color (correct Photoshop-style behavior). Requires the display path to show transparency — see A4.
- **Alpha-aware eyedropper** (`eyedropper.zia`): sample the **composite** (new `canvas.getCompositePixel(x,y)`), read raw RGBA via `Pixels.Get`, skip only when `Color.GetA(...) == 0`. Removes the `!= 0` heuristic.
- **Blur radius** (`app.zia:789`): stop hardcoding `2`; wire to a value (brush size or a small prompt). Minor.
- Audit remaining manual color math; prefer `Color.GetR/G/B/A` and `Color.RGB/RGBA` (colors.zia already mostly correct).

**A4. Display vs export composite (enables transparency visibly).**
`compositePixels()` stays alpha-preserving (used for save/export). Add a display path so erased/transparent regions reveal the checkerboard: either composite over a checker base in a display buffer, or switch `viewport.draw` to an alpha blit. Keep export free of checker.

---

## Workstream B — Pillar 1: Pro color system

**B1. `ui/color_picker.zia`** — a real picker drawn in-canvas: hue bar + saturation/value square (or H/S/L sliders), live preview, and a hex field. Backed entirely by `Color.FromHSL`, `Color.GetH/GetS/GetL`, `Color.FromHex`, `Color.ToHex`, `Color.RGB`, `Color.GetR/G/B`. Clicking the foreground/background swatch (`app.zia:1084-1095`) opens it.
**B2. RGB/hex readout** next to swatches; type-in hex via `Zanna.GUI.Clipboard` paste support.
**B3. Editable + savable palette** (`colors.zia`): replace the hardcoded 64 colors with an editable set; add custom-swatch assignment (right-drag a color in), and load/save a palette file (simple text via `Zanna.IO`). Add "recent colors" strip (model already exists, `colors.zia:107-124` — just needs UI).
**B4. Color harmony helpers** — buttons for `Color.Complement/Brighten/Darken/Saturate` to generate related swatches.

---

## Workstream C — Pillar 2: More drawing tools

New tool IDs in `config.zia` (extend past `TOOL_SELECT=8`): `TOOL_GRADIENT`, `TOOL_CURVE`, `TOOL_POLYGON`, `TOOL_TEXT`, `TOOL_SPRAY`. Register each in `tools/registry.zia` + `tool_palette.zia` (`ButtonGroup.Add`) + actions (`actions.zia`). Each is a new module under `tools/` following the `Tool` interface (and the line-tool drag/preview/commit pattern in `line.zia`).

- **C1. Selection tool** (`tools/select.zia`, the reserved `TOOL_SELECT`): rectangular marquee with marching-ants overlay; copy (`Pixels.Copy` → clipboard `Pixels`), cut (copy + erase region), paste/move (stamp clipboard, drag to reposition), delete. Add a `selection.zia` model (bounds + floating buffer) and a `clipboard.zia` holding a `Pixels`. Highest-value tool.
- **C2. Gradient tool** (`tools/gradient.zia`): drag to define direction/length; fill selection-or-layer between foreground and background using `Gradient2D` (`Sample(t)` per pixel along the drag vector; `FillHorizontal/FillVertical` fast path for axis-aligned). Linear first; radial as stretch.
- **C3. Bezier curve tool** (`tools/curve.zia`): click anchors + drag control point(s); preview with `Canvas.Bezier`, commit with `Pixels.DrawBezier`. *Confirm arity* — runtime `Canvas.Bezier` is cubic (`rt_drawing_advanced.c:1086`); `Pixels.DrawBezier` is quadratic per docs. Match the tool to the available signatures.
- **C4. Polygon/polyline tool** (`tools/polygon.zia`): click to add vertices, double-click/Enter to close; outline via repeated `Pixels.DrawLine`, optional fill for convex via `Pixels.DrawTriangle` fan.
- **C5. Spray/airbrush** (`tools/spray.zia`): time-accumulated random dots within brush radius using `Zanna.Math.Random` + `setPixelBlend`. *Confirm Random API name* via `zanna --dump-runtime-api`.
- **C6. Text tool** (`tools/text.zia`): type a string, place on layer. No `Pixels` text primitive exists, so render glyphs with `Canvas.Text` onto an offscreen region and grab them via `Canvas.CopyRect → Pixels`, then `Pixels.Copy` into the active layer. *Mark as the riskiest tool*; if `CopyRect` proves unsuitable, scope text to an overlay annotation and note the runtime gap (no `Pixels.DrawText`).
- **C7. Shape fill/outline toggle**: surface the existing `filled` flag on rectangle/ellipse in the UI; add a round/square brush toggle (model exists: `brush.zia:97 toggleShape`).

---

## Workstream D — Pillar 3: Layer power

- **D1. Blend modes** (`layers.zia`): add `blendMode` to `PaintLayer`; extend `blendLayer` (`layers.zia:319`) with multiply / screen / overlay / add computed per channel from `Color.GetR/G/B`. The O(W×H) loop already exists — just richer math.
- **D2. Layer transforms** (`canvas.zia`): wire `flipActiveV` (exists at `canvas.zia:301` but unused by UI), and add rotate (`Pixels.RotateCW/RotateCCW/Rotate(angle)`), resize/scale (`Pixels.Resize` bilinear), and colorize (`Pixels.Tint`). All route through `replaceActivePixels` + `snapshotForUndo`.
- **D3. Merge down + flatten** (`layers.zia`): `mergeActiveDown()` composites active onto the layer below (respecting opacity/mode) and removes it; `flatten()` collapses to one layer.
- **D4. Redesigned layer panel** (`layer_panel.zia`): per-row thumbnail (downscale via `Pixels.Scale`), opacity slider (model: `setActiveLayerOpacity`), blend-mode cycle, inline add/dup/del/up/down (move these off the crammed toolbar), double-click to rename (model: `renameLayer`). Drag-to-reorder as stretch.
- **D5. Canvas-size ops**: resize canvas / crop-to-selection using `Pixels.Resize` + `Pixels.Copy`.

---

## Workstream E — Pillar 4: Visual & UX polish

- **E1. Live brush cursor preview** (`ui/chrome.zia` overlay): draw a ring/box at the cursor showing brush size+shape, scaled by `view.zoom` (use `gfx.Ring`/`gfx.Frame`, coords via `view.canvasToScreenX/Y` + `canvasSizeToScreen`). Crosshair for shape/fill tools.
- **E2. Tool-specific OS cursors** via `Zanna.GUI.Cursor.Set` (`rt_gui_system.c` confirmed); fallback to custom-drawn cursor if it no-ops on a Canvas window (verify early).
- **E3. Custom menu bar** (`ui/menu.zia`): in-canvas File / Edit / Image / Layer / View dropdowns. Declutters the 13-button toolbar (`app.zia:284-340`) and is expected of a first-class app. Toolbar becomes a small icon row of common actions.
- **E4. On-canvas toasts** (`feedback.zia`): transient, fading notifications (timed via `gfx.DeltaTime`) for save/error/import events, replacing blocking `MessageBox` for non-critical feedback (keep MessageBox for true errors). Avoids `Zanna.GUI.Toast` (needs the widget tree).
- **E5. Refined dark chrome**: vertical gradients on panels/toolbar (`gfx.GradientV`), consistent rounded buttons + accent color, section headers, an accent focus glow, improved spacing/typography. New palette constants in `config.zia`.
- **E6. Keyboard help overlay** (F1/`?`) listing shortcuts; "?" toolbar button.
- **E7. Status bar polish**: tool + brush + zoom + cursor coords + selection size, with separators.

---

## New / modified files

**New:** `tools/tool.zia` (interface + `ToolContext`), `tools/registry.zia`, `tools/select.zia`, `tools/gradient.zia`, `tools/curve.zia`, `tools/polygon.zia`, `tools/spray.zia`, `tools/text.zia`, `selection.zia`, `clipboard.zia`, `ui/color_picker.zia`, `ui/menu.zia`, `ui/chrome.zia`, `input_router.zia`.
**Heavily modified:** `app.zia` (slim to orchestration), `layers.zia` (blend modes, merge), `canvas.zia` (erase/transparent, transforms, composite-pixel read), `colors.zia` (editable palette, harmony), `layer_panel.zia` (redesign), `eraser.zia`, `eyedropper.zia`, `config.zia` (tool IDs, new colors), `actions.zia` (new bindings), `tool_palette.zia`, `file_service.zia` (export options), `viewport.zia` (alpha display), `main.zia` (new `bind`s), `README.md` (feature list).
**Constraint:** keep each existing tool `implements Tool`; every new/modified file gets the standard Zanna GPL header (this is the public repo).

## Reused runtime APIs (confirmed present)
- Color: `Color.FromHSL/FromHex/ToHex/GetH/GetS/GetL/GetA/RGB/RGBA/Brighten/Darken/Saturate/Complement/Lerp` (`rt_graphics.h`).
- Pixels: `Set`(raw)/`Get`/`DrawBezier`/`DrawTriangle`/`RotateCW/RotateCCW/Rotate`/`Resize`/`Tint`/`Copy`/`FlipV`/`Scale`/`Clone` (`rt_pixels_*.c`).
- Canvas: `Bezier`/`Arc`/`Polygon`/`Polyline`/`GradientH/V`/`CopyRect`/`Ring`/`Frame` (`rt_drawing_advanced.c`, `rt_graphics2d_*`).
- `Gradient2D` (`rt_graphics2d.h`); `Zanna.GUI.Cursor`/`Clipboard` (`rt_gui_system.c`); `Zanna.Math.Random`.
- *Confirm before use* (signatures/arity/names): `Canvas.Bezier` cubic vs `Pixels.DrawBezier` quadratic; `Color.FromHSL` ranges; `Math.Random` method name; `Cursor.Set` behavior on a Canvas window — via `docs/zannalib/` + `zanna --dump-runtime-api`.

## Verification
1. **Fast type-check after each module:** `zanna check examples/apps/paint/main.zia --diagnostic-format=json` (exit 0 clean).
2. **Build the demo:** `./scripts/build_demos.sh` (and `zanna run` from `examples/apps/paint/` per `zanna.project` `entry main.zia`) — compile clean, app launches.
3. **Cross-platform gates** (CLAUDE.md): `./scripts/lint_platform_policy.sh` + `./scripts/run_cross_platform_smoke.sh`. (Pure-Zia app, but run per policy.)
4. **Manual run-through per pillar** (no automated UI test harness for demos): each tool draws + previews + commits + undoes; eraser reveals transparency over checkerboard; color picker round-trips HSL/hex; blend modes visibly differ; layer panel opacity/merge/reorder work; save/open PNG+BMP round-trip; export preserves alpha (no checker baked in).
5. **Regression:** existing shortcuts (README list) still work; undo/redo across all new ops; zoom/pan unaffected.
6. **Full green:** finish with a no-skip `./scripts/build_zanna_unix.sh` + demo build before reporting done.
7. Remove the leftover `*_probe.zia` / `*_test.zia` scratch files in the paint dir (or fold into a smoke check) so the demo ships clean.
