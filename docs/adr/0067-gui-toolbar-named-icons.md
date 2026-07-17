---
status: active
audience: contributors
last-verified: 2026-07-05
---

# ADR 0067: GUI Toolbar Named Icon Runtime API

## Status

Accepted

## Context

ZannaIDE toolbar buttons were using single-character labels as implicit icons.
The low-level GUI toolbar already renders known glyph codepoints as vector icons,
but the public runtime API only accepted image paths or pixel objects. That made
IDE code depend on ambiguous glyph literals and prevented other Zanna programs
from using the same dependency-free icon set semantically.

Runtime C ABI changes require an ADR.

## Decision

Expose built-in semantic toolbar icon APIs:

- `Zanna.GUI.Toolbar.AddNamedButton(name, tooltip)`
- `Zanna.GUI.Toolbar.AddNamedButtonWithText(name, text, tooltip)`
- `Zanna.GUI.Toolbar.AddNamedToggle(name, tooltip)`
- `Zanna.GUI.ToolbarItem.SetNamedIcon(name)`

The icon names map to existing glyph-backed vector icons. Unknown names clear or
omit the icon instead of trapping, preserving a usable toolbar on typo or older
name drift.

## Consequences

ZannaIDE can migrate away from toolbar text glyphs without adding external image
assets. The ABI remains additive. Future icons should extend the name-to-glyph
mapping and the GUI vector renderer together, with tests for each public name.
