# Plan 06 — Typography Premium

Date: 2026-07-17 · Track: R (C rendering) · Loop: C · Size: L

## 1. Objective

Text rendering worthy of a flagship IDE: gamma-correct antialiasing, working
JetBrains Mono coding ligatures, and per-glyph font fallback — all within the
zero-dependency, hand-written TTF stack.

## 2. Scope

1. **Gamma-correct glyph blending.** The blit in `vg_raster.c`/`vg_draw`
   composites 8-bit coverage in sRGB space, over-darkening light-on-dark
   text. Add integer sRGB↔linear LUTs (256-entry u8→u16 and inverse) applied
   around the blend — per-pixel loops stay float-free per the determinism
   contract. Expect visibly crisper text on the dark theme.
2. **GSUB ligatures.** Extend `src/lib/gui/src/font/vg_ttf.c` to parse GSUB:
   ScriptList/FeatureList/LookupList; LookupTypes 1 (single), 4 (ligature),
   6 (chaining contextual), 7 (extension) — the set JetBrains Mono's
   `calt`/`liga` features require. Add a shaping pass in the glyph-run
   builder. **Caret contract (the acceptance bar):** ligatures render as one
   glyph but column mapping, selection, and cursor drawing keep
   per-character positions — JetBrains Mono ligatures are monospaced-width,
   so split the ligature advance evenly across source characters.
3. **Per-glyph fallback.** Ordered fallback face chain consulted on cmap
   miss: user-configured face → platform faces found by direct OS font-dir
   scans (macOS `/System/Library/Fonts`, Windows `C:\Windows\Fonts` +
   registry face names, Linux `/usr/share/fonts` + `~/.fonts` — no
   fontconfig) → the embedded face as guarantee. Add `.ttc` container header
   support (many system fonts are collections; the header is small). Cache
   the scan result in the config dir with mtime validation.
4. **Explicitly out of scope:** color emoji (COLR/CBDT/sbix), CFF/OTF
   outlines, subpixel/LCD AA, hinting — documented as future work.

## 3. Runtime surface

`Zanna.GUI.CodeEditor.SetLigaturesEnabled(enabled: Boolean)` (+ getter),
default on; IDE setting in the Appearance card. Folded into the Plan 04
consolidated rendering ADR; full runtime checklist.

## 4. Tests / verification (exit gate)

- GSUB parse/substitution tests against the embedded JetBrains Mono bytes:
  `->`, `=>`, `!=`, `===`, `<=` produce the expected substituted glyph ids;
  chaining-context cases from `calt` covered.
- Caret/column mapping tests in the CodeEditor C tests: cursor/selection
  through ligated runs land on per-character boundaries.
- Fallback resolution unit tests (synthetic face table; missing-glyph chain).
- `test_vg_font_bounds.c` re-checked (metrics unaffected by gamma).
- Incremental build + targeted ctest; before/after screenshots on the dark
  theme for the gamma change; editor visual pass with ligatures on and off.

## 5. Risks

- GSUB chaining-context (LookupType 6) is the largest single C item in the
  program — build it table-driven with exhaustive unit fixtures.
- Gamma changes every AA pixel — no pixel-hash tests pin text (verified), but
  the manual pass is mandatory; keep a `ZANNA_GUI_TEXT_GAMMA=off` escape
  hatch env during rollout, removed at phase close.
- Font-dir scan performance — cached with mtime validation; first-run cost
  budgeted (<50 ms target on typical dirs, measured).
