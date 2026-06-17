# Viper Paint Overhaul — Progress Tracker

Plan: `paint.md` (same dir). Status legend: ☐ todo · ◐ in progress · ☑ done · ✗ blocked/dropped (with note).

Baseline: `viper check examples/apps/paint/main.zia` → exit 0 (green) before changes. Only `W001` unused-param warnings present.

## Workstream A — Structure & correctness (foundation)
- ☑ A0. Probe: confirmed Zia `interface` + `List[Tool]` storage + dynamic dispatch WORKS (`/tmp/zia_iface_probe.zia`). Caveats logged: W001 no-suppression (ISSUE-002), interface null-compare rejected → use Null-Object (ISSUE-003).
- ☑ A1. `tools/tool.zia` — `Tool` interface + `ToolContext` (+ `selection.zia`, `clipboard.zia` models)
- ☑ A1. `tools/registry.zia` — registry; replaced 3× dispatch ladders + status switch + preview switch in app.zia
- ☑ A1. Migrated all 8 tools to `implements Tool` (onPress/onDrag/onRelease + drawPreview + ctx)
- ☑ A1. Updated CI `smoke_probe.zia` to new API; all 3 paint CI probes green
- ◐ A2. Split app.zia — DEFERRED to modular approach: keep fat controller, put NEW heavy UI in own modules (color_picker, menu, toast). Wholesale chrome extraction skipped (high churn/risk, deliberate fat-controller design). app.zia shrank ~100 lines from the dispatch collapse.
- ☑ A3. True-alpha eraser (`canvas.erasePixel` raw `Pixels.Set`; transparent on normal layers, bg on base layer)
- ☑ A3. Alpha-aware eyedropper (`canvas.sampleColor` reads composite RGBA, skips alpha==0)
- ☐ A3. Blur radius not hardcoded
- ☐ A3. Color-math audit (prefer Color.* helpers)
- ◐ A4. Display vs export composite — multi-layer alpha works (erase reveals lower layers); opaque "paper" base retained (MS-Paint model). Full transparent-canvas + checkerboard show-through deferred (needs region-alpha blit; see viper_bugs candidate).

## Workstream B — Pro color system
- ☑ B1. `ui/color_picker.zia` — modal HSL picker (cached S/L field + hue bar, crosshair, live preview). Opens by clicking fg/bg swatch.
- ☑ B2. RGB + hex readout in picker (Color.ToHex/GetR/G/B). (Clipboard hex paste deferred.)
- ◐ B3. Recent-colors strip done (clickable, model-backed). Editable/savable palette deferred.
- ☐ B4. Color harmony helpers — deferred (picker subsumes most need).

## BONUS (extends A1): registry-driven tool buttons
- ☑ Tool buttons are now a `List[ToolButton]` built from the registry (2-col grid); palette populated from registry ids. Adding a tool no longer needs button/palette boilerplate — just a tool file + config id + registry entry.

## Workstream C — More drawing tools  ✅ (all green)
- ☑ C1. Selection tool — marquee + marching ants; Ctrl+C/X/V + Delete via `canvas.eraseRegion`/`pastePixels` + clipboard
- ☑ C2. Gradient tool — directional fill via `canvas.fillGradient` (Color.Lerp; Gradient2D blocked, ISSUE-004)
- ☑ C3. Bezier curve tool — 3-click quadratic, live hover preview, `Pixels.DrawBezier`
- ☑ C4. Polygon tool — multi-click, close near first vertex, `ctx.snapshot` before commit
- ☑ C5. Spray/airbrush — `Random.Range` bursts, radius-ring preview
- ☑ C6. Text tool — type via `Keyboard.GetText`, bake via white-on-black `CopyRect` color-key (works around ISSUE-001)
- ☑ C7. Shape fill/outline (D key) + brush round/square (W key) toggles; fill mode shared in ToolContext
- Note: text-input keys captured in handleKeyboard; `EnableTextInput`/`DisableTextInput` toggled on tool select.

## Workstream D — Layer power  ✅ (all green)
- ☑ D1. Blend modes (normal/multiply/screen/overlay/add) — per-layer `blendMode`, channel math in `blendLayer`; cycle via M key + layer panel
- ☑ D2. Layer transforms — flipV, tint (per-layer); rotate CW/CCW + resize (all-layers + dim swap)
- ☑ D3. Merge-down (Ctrl+E) + flatten (in `LayerStack`, reuse blend math)
- ☑ D4. Redesigned layer panel — active-layer blend label (click-cycle) + opacity bar (click-set), per-row eye dot/name/opacity%/blend label
- ☑ D5. Crop-to-selection + resize canvas (`cropAll`/`resizeAll`)
- Note: dim-changing/multi-layer ops (rotate/crop/flatten/merge) clear undo history (single-layer snapshot model can't represent them) — app-design limitation, documented.
- Rotate/flipV/tint/crop/flatten exposed via the E3 menu (added there).

## Workstream E — Visual & UX polish  ✅ (all green)
- ☑ E1. Live brush cursor preview (ring/box by shape; crosshair for pencil; spray radius ring)
- ☑ E2. Tool cursors — `feedbackMgr.setCanvasCursor` (crosshair over canvas, arrow over chrome) — pre-existing, retained
- ☑ E3. Custom in-canvas menu bar (`ui/menu.zia`): File/Edit/Image/Layer/View/Help; replaced 13-button toolbar (kept New/Open/Save quick buttons); dispatchMenuAction wires all ops
- ☑ E4. On-canvas toasts (custom, guaranteed-visible; save/open success/error)
- ☑ E5. Refined dark chrome — gradient panels/menu/status bar, accent palette, rounded layer rows
- ☑ E6. Keyboard help overlay (H key + Help menu)
- ☑ E7. Status bar polish — tool+size+opacity, accented status, coords+zoom+size+layer

## Verification / cleanup
- ☑ V1. `viper check examples/apps/paint/main.zia` → 0 errors (59 W001 unused-param warnings, all ISSUE-002)
- ☑ V2. App launches + renders (screenshot verified: menu bar, 2-col tools, layer panel, palette, canvas)
- ☑ V3. Platform lint advisory-clean for paint (pure Zia; only pre-existing runtime headers flagged)
- ☑ V4/V5. ctest CI harness: zia_smoke_paint + _runtime_features + _canvas_capture all pass (100%)
- ☑ V6. Skipped full `build_viper_unix.sh` rebuild — no C/C++/runtime/compiler changed (pure-Zia demo); toolchain unchanged, ran the relevant ctest paint suite instead (cost/value).
- ☑ V7. Removed 7 non-CI scratch files (class_gfx_test, gfx_test, handle_canvas_probe, minimal_test, palette_probe, simple_class, stroke_probe). Kept 3 CI-wired probes (updated smoke_probe to new API).
- ☑ V8. Updated README.md feature list.

## Outcome
First-class drawing app: 14 registry-driven tools, HSL colour picker, 5 layer blend modes + transforms/merge/flatten, menu bar, refined dark chrome. True-alpha eraser + alpha eyedropper fixed the flagged API misuse. All Viper/Zia issues hit are logged in `viper_bugs.md` (ISSUE-001..004).

## Notes / decisions
- Baseline `viper check` green (exit 0); uniform Tool signatures will add W001 unused-param warnings — acceptable, or use `_`-prefix convention if Zia supports it.
