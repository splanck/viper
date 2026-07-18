# Plan 02 — Brand Reface: Palettes and Chrome

Date: 2026-07-17 · Track: serial root · Loop: C + Zia · Size: M

## 1. Objective

Zanna Studio looks like the Zanna brand everywhere: both built-in themes
retuned to the brand palette (Dark remains the default), the welcome surface
and About dialog redesigned around a vector Z brand mark, and every hardcoded
IDE color routed through theme tokens.

## 2. Palette

Brand ramps: green `#8CC63F → #5FA832`, steel `#B9C2C6 → #4F7A72`, teal
`#2BC8C4 → #189AA8`, charcoal-green field `#0D1A1C`. Colors in `vg_theme` are
`0x00RRGGBB`.

Retune `g_dark_theme` and `g_light_theme` in
`src/lib/gui/src/core/vg_theme.c` **keeping theme names ("Dark"/"Light") and
the `vg_theme_dark()`/`vg_theme_light()` accessors** — repo tests reference
palettes symbolically, so nothing symbolic changes. Mapping guide (dark):

- `bg_primary 0x0D1A1C`; secondary/tertiary as lightened charcoal-green steps
  with enough separation for `bg_selected`/`bg_active` visibility.
- `accent_primary` from the green ramp (`0x8CC63F` reads best on the field);
  `accent_info`/`fg_link` from the teal ramp; `fg_secondary` family from
  steel. Danger/warning stay semantically red/amber.
- All 9 `syntax_*` colors retuned to brand-adjacent hues preserving role
  separation (keywords/types/strings/numbers/comments visually distinct).
- Light theme: same hue family on light field; verify with the same contrast
  gate.

## 3. Scope and files

- C: `src/lib/gui/src/core/vg_theme.c` (both palettes).
- New C test `src/lib/gui/tests/test_vg_theme_contrast.c`: every built-in
  fg/bg pairing passes the repo's WCAG contrast checker (locate the checker
  API in `rt_gui_theme.c` / theme layer during implementation). This is
  palette-agnostic and protects all future retunes.
- Zia sweep of the 7 hardcoded-color files to theme-derived values:
  `ui/tool_panel_shell.zia`, `commands/debug_commands.zia`,
  `commands/build_commands.zia`, `build/breakpoints.zia`,
  `scm/scm_gutter_controller.zia`, `editor/diagnostics.zia`,
  `editor/inlay_hints.zia`.
- Welcome redesign (`ui/welcome_view.zia`): brand mark, product identity
  ("Zanna Studio" + tagline), quick actions (New Project once Plan 08 lands;
  New File / Open Folder now), recent files, shortcut hints. Update
  `welcome_probe.zia` in the same change.
- About redesign (`ui/ide_overlays.zia`): brand mark, product name, build
  summary, credits line.
- Status bar accents (`ui/status_shell.zia`).

## 4. Brand mark strategy

The brand PNGs (`misc/images/zanna-brand-assets/`, 365KB-1.4MB) are too heavy
to embed in-app. The in-app mark is a **hand-authored vector Z** drawn with
`vg_draw` primitives (three parallelogram strokes: green top bar, steel
diagonal, teal base — fixed-point, deterministic, HiDPI-crisp, zero-dep).
If Plan 04's icon library lands first, the mark becomes an icon entry; until
then a small local paint helper in the welcome/About code path is acceptable.

## 5. Runtime surface

None expected. If theme enumeration/inspection surface becomes necessary it
moves to the consolidated rendering ADR in Plan 04 and takes the full runtime
checklist.

## 6. Verification (exit gate)

Incremental build; `build_ide.sh`; targeted ctest (`zannastudio` label + GUI
lib tests incl. the new contrast test); visual pass on the host platform (all
widgets in both themes; the settings panel theme toggle); OS dark/light
following re-checked. No version bumps.

## 7. Risks

- Contrast regressions on the default dark path — mitigated by the new test
  and keeping Dark default (visually-impaired primary user).
- Selection/active-state separation on the new field — explicit mapping-guide
  item; verify in the visual pass.
- Any test constructing expectations from theme values indirectly — none
  found in the design check, but the targeted ctest run guards it.
