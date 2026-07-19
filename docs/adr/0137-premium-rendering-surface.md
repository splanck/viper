---
status: active
audience: contributors
last-verified: 2026-07-18
---

# ADR 0137: Premium Rendering Surface for Zanna Studio

Date: 2026-07-18

## Status

Accepted.

## Context

The Zanna Studio program (`misc/plans/zannastudio/`) phases 4-6 modernize the
GUI toolkit's rendering: a scalable vector icon library replacing the
fourteen hand-painted toolbar glyphs, real-delta-time motion with smooth
scrolling and display-aligned pacing, and premium typography (gamma-correct
antialiasing, ligatures, per-glyph fallback). Some of that work needs small
additive `Zanna.GUI.*` surface so the Zia application layer can opt in. Per
ADR 0006, runtime C ABI surface changes are recorded here once for the whole
program rather than as per-method micro-ADRs.

## Decision

1. **Vector icon library (toolkit layer).** A new deterministic vector icon
   engine (`src/lib/gui/src/core/vg_icon_vector.c`) renders named icons from
   compact fixed-point path tables through an anti-aliased integer scanline
   fill with an LRU coverage-mask cache. The canonical `vg_icon_t` gains a
   `VG_ICON_VECTOR` variant so every widget that renders icons (toolbar,
   context menu, tree, dialogs, palette) can adopt vector icons through its
   existing icon slot. The legacy toolbar codepoint glyphs map onto vector
   icons, retiring the hand-painted set without breaking callers.

2. **Additive runtime surface.** The following methods are added, each with
   graphics-off stubs, `RuntimeSurfacePolicy.inc` coverage, and registry
   entries in the modular defs:
   - `Zanna.GUI.Button.SetIconName(name: str)` — draw a named vector icon
     before the button label (empty name clears it).
   - `Zanna.GUI.Label.SetIconName(name: str)` — draw a named vector icon
     before the label text (empty name clears it).
   - `Zanna.GUI.StatusBarItem.SetIconName(name: str)` — draw a named vector
     icon before a text/button status item (empty name clears it).
   - Toolbar items reuse the existing
     `Zanna.GUI.ToolbarItem.SetNamedIcon(name: str)`: its name resolver now
     consults the vector library first and falls back to the legacy builtin
     codepoint table, so no new toolbar surface was required.
   - Tree nodes reuse the existing `Zanna.GUI.TreeView.Node.SetIcon(str)`:
     the value form `vector:<name>` (optionally `vector:<name>|<expanded>`
     for open-folder variants) selects vector icons, so per-node file-type
     icons required no new tree surface.
   Unknown names are ignored (the widget keeps its previous icon); name
   lookup is exposed to C as `vg_icon_vector_find(name)`.
   - Reserved for phases 5-6 under this same ADR:
     `Zanna.GUI.App.SetSmoothScroll(enabled: i1)` and
     `Zanna.GUI.CodeEditor.SetLigaturesEnabled(enabled: i1)` (+ getters).

3. **Determinism.** The icon engine performs all per-pixel and edge math in
   fixed point (Q16 coordinates, integer coverage accumulation, fixed
   8-segment quadratic flattening), preserving the toolkit's bit-identical
   cross-platform output contract. Coverage masks are tint-independent;
   color is applied at blit time, and multi-role icons (the Zanna brand
   mark) rasterize one mask per color role.

## Consequences

- Icon identity is by stable string name (kebab-case, e.g. `"file-zia"`,
  `"git-modified"`, `"debug-step-over"`, `"zanna-mark"`); the set can grow
  without further ABI changes.
- Widgets currently passing single-codepoint glyph labels keep working; the
  toolbar maps those codepoints to vector icons internally.
- The runtime-surface checklist applies to every method above (registry
  defs, `rt_gui.h` declarations, stubs, surface policy, completeness gate).
- Phases 5-6 add their two toggles without a new ADR; anything beyond the
  listed surface requires a fresh decision record.
