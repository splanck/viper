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
