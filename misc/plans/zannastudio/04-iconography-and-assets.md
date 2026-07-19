# Plan 04 — Iconography and Visual Assets

Date: 2026-07-17 · Track: R (C rendering) · Loop: C · Size: L

## 1. Objective

A general, scalable vector icon library replacing the 14 hand-coded toolbar
painters, giving Zanna Studio premium file-type, tool, and state iconography —
plus the formalized vector Z brand mark.

## 2. Design

- New `src/lib/gui/src/core/vg_icon.c` + `include/vg_icon.h`:
  - Compact path format: fixed-point coords, ops move/line/quad/close, per
    subpath fill color slots (theme-token-tinted at raster time).
  - Rasterized through the existing `vg_draw`/`vg_raster` AA fills —
    determinism contract holds (integer per-pixel).
  - LRU cache keyed (icon id, size, color), bounded; pattern mirrors
    `vg_cache.c`.
  - Icons authored as static path tables in a generated-style `.inc`
    (hand-maintained, reviewed visually).
- Author ~50 icons: file types (zia/basic/il/json/md/image/generic), folders
  (open/closed), git states (modified/added/deleted/renamed/untracked),
  debug (start/stop/pause/continue/step-over/in/out/restart/breakpoint),
  search/replace, terminal, diagnostics severities, chevrons, close, pin,
  split, settings gear, activity-bar (explorer/SCM/search/debug/extensions
  placeholder), the Z brand mark.
- Rewire `vg_toolbar.c` painters (currently `toolbar_draw_vector_icon`,
  ~line 1444) to icon-library refs; add file-type icons to `vg_treeview`
  rows, dirty/close glyphs to `vg_tabbar`, severity glyphs to `vg_statusbar`
  and problem lists.

## 3. Runtime surface (consolidated rendering ADR)

Write the **consolidated premium-rendering ADR** at phase start (number =
next free in `docs/adr/` at execution — the directory has live numbering
collisions; always re-verify). It covers P4-P6 additive surface:

- `Zanna.GUI.Button.SetIconName(name: String)` (+ Label, toolbar items,
  tree-item and tab-item icon setters as the chrome needs).
- P5's `Zanna.GUI.App.SetSmoothScroll(enabled)` and P6's
  `Zanna.GUI.CodeEditor.SetLigaturesEnabled(enabled)`.
- P7's `Zanna.GUI.CodeEditor.AttachSharedBuffer(other)` if that route is
  chosen.

Per-item checklist: modular def entries (`defs/classes/gui_sound.def`), decls
in `rt_gui.h`, `RuntimeSurfacePolicy.inc`, `source_health_baseline.tsv` bump
for new `rt_*` files, graphics-off stubs, `check_runtime_completeness.sh`
green. Any new class (e.g. `Zanna.GUI.Icon`) needs a hand-added `RTCLS_*` in
`RuntimeClasses.hpp` and a globally-unique leaf name (`Icon` — verify).

## 3a. As-built record (2026-07-18)

- ADR landed as **0137**. Engine: `src/lib/gui/src/core/vg_icon_vector.c` +
  `include/vg_icon_vector.h` — fully fixed-point (Q16 edges, integer
  coverage, fixed 8-segment quad flattening), even-odd fill, 4x vertical
  supersampling with analytic horizontal coverage, tint-independent
  per-role coverage masks in a 96-entry LRU. **49 icons** registered,
  including the multi-role `zanna-mark` (brand green/steel/teal).
- The canonical `vg_icon_t` gained `VG_ICON_VECTOR` (+
  `vg_icon_from_vector`); toolbar and context-menu render paths handle it.
  The 14 hand-painted toolbar glyph painters were deleted — the legacy
  codepoint keys now map onto vector icons inside
  `toolbar_draw_vector_icon`, and the per-codepoint semantic colors come
  from theme tokens instead of hardcoded hex.
- Runtime surface: `Button.SetIconName` + `Label.SetIconName` (real +
  graphics-off stubs, registry defs, rt_gui.h). **Toolbar items reuse the
  existing `SetNamedIcon`** — its resolver consults the vector library
  first. GUI ABI manifest pins reviewed and updated (+2 functions,
  +2 methods, new hash).
- IDE adoption: welcome hero (`zanna-mark` on the title, `new-file` /
  `folder-open` button icons), About dialog mark. New probe
  `vector_icons_probe.zia` (`zia_zannastudio_vector_icons`).
- C test `test_vg_icon_vector` (212 assertions): registry round-trip,
  per-icon in-box coverage, AA presence, cold/warm-cache bit-identical
  hashes, tint sensitivity, brand-mark role colors, invalid-input no-ops.
- **Staged follow-ups — CLOSED (2026-07-18):** the explorer tree now sets
  per-node vector icons (`zanna-mark` roots, `folder|folder-open` folders,
  extension-mapped file glyphs) through the existing `TreeView.Node.SetIcon`
  surface via the new `vector:<name>[|<expanded>]` value form (no new RT
  method; ADR 0137 amended), and the old text glyph prefixes were dropped;
  the tab close X is drawn with the AA `close` vector icon (raw-line
  fallback if the library is absent; the modified marker stays the `*`
  title suffix); the status bar diagnostics item shows `error`/`warning`/
  `check` severity glyphs via the new `Zanna.GUI.StatusBarItem.SetIconName`
  (full runtime checklist; ABI manifest pins updated to 1115/1006, hash
  0x2d61efa91809eb66). Remaining note: Label icons render on non-wrapped
  labels only.

## 4. Tests / verification (exit gate)

- C tests in `src/lib/gui/tests/`: path rasterization determinism (pixel-hash
  on the mock backend, like `test_vg_draw`), cache bounds/eviction, tinting.
- IDE probe exercising icon-name setters end-to-end.
- Incremental build + targeted ctest; completeness green; visual pass
  (toolbar, tree, tabs, status) on host platform.

## 5. Risks

- Icon authoring time dominates (art, not code) — batch in reviewable groups;
  the 14 existing painters are the quality bar to beat.
- Cache memory at HiDPI sizes — bounded LRU with size accounting.
- Tinting vs theme changes — cache key includes resolved color; theme switch
  flushes.
