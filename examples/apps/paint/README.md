# Zanna Paint

Zanna Paint is a Zia drawing app built on the Zanna graphics runtime. The drawing
surface stays `Pixels`-backed for direct paint operations, while the app leans on
higher-level runtime helpers for input actions, game UI widgets, file dialogs,
viewport management, history, layers, and diagnostics. Tools are dispatched
through a `Tool` interface + registry, so the toolbox is data-driven.

## Tools

- **Pencil / Brush / Eraser** — freehand drawing. The brush has size + opacity +
  round/square shape; the eraser is true-alpha (transparent on normal layers,
  background colour on the locked base layer).
- **Line / Rectangle / Ellipse** — drag with a live rubber-band; rectangles and
  ellipses toggle filled/outline (`D`).
- **Fill** — flood fill. **Eyedropper** — alpha-aware colour sampling from the
  composite.
- **Gradient** — drag to fill the layer with a foreground→background gradient.
- **Curve** — three-click quadratic Bezier with live preview.
- **Polygon** — click vertices, click the first vertex to close.
- **Spray** — airbrush that builds density while held.
- **Text** — click, type, Enter to bake into the layer.
- **Select** — rectangular marquee (marching ants) with cut/copy/paste/delete.

## Colour

- Click the foreground/background swatch to open the **HSL colour picker**
  (saturation/lightness field + hue bar, hex + RGB readout).
- A fixed palette plus a most-recent-colours strip; left/right-click sets
  foreground/background.

## Layers

- The right panel lists layers with visibility, name, opacity, and blend mode.
- The active-layer control strip cycles the **blend mode** (Normal / Multiply /
  Screen / Overlay / Add) and sets opacity via a click bar.
- Add / duplicate / delete / reorder / **merge down** / **flatten** (Layer menu).

## Image

- Grayscale, Invert, Blur, Flip H/V, Rotate 90° CW/CCW, Tint (foreground),
  Crop to Selection (Image menu).

## Menus & chrome

- A top **menu bar** (File / Edit / Image / Layer / View / Help) exposes every
  command; quick New/Open/Save buttons sit alongside.
- Live brush-size cursor, transient save/open toasts, and an `H` shortcut overlay.

## Shortcuts

- Tools: `1`-`8` or `P/B/E/L/R/O/F/I`; `S` select, `G` gradient, `U` curve,
  `Y` polygon, `A` spray, `T` text. `Tab`/`Backspace` cycle.
- Brush: `[` / `]` size, `W` shape, `D` fill/outline.
- Colour: `X` swap; click a swatch for the picker.
- File: `Ctrl+N/O/S`, `Ctrl+Shift+S` save as.
- Edit: `Ctrl+Z/Y` undo/redo, `Ctrl+C/X/V`, `Delete`.
- Layers: `Ctrl+E` merge down, `M` cycle blend mode.
- View: `PageUp/PageDown` zoom, `Home` reset, `Ctrl+wheel` zoom, wheel pan.
- `F11` fullscreen, `F3` debug overlay, `H` shortcuts.

## File formats

- Open: PNG, BMP, JPEG, GIF (via `Pixels.Load`). Save: PNG and BMP (the runtime
  has no JPEG/GIF encoders).

## Runtime features used

- `Zanna.Graphics.Canvas` (`BeginFrame`, `SetFps`, `DeltaTime`, `GradientV`,
  `CopyRect`, `Text`/`TextWidth`) drives the loop and chrome.
- `Zanna.Graphics.Pixels` (`BlendPixel`, `Set`, `DrawThickLine`, `DrawBezier`,
  `DrawTriangle`, `RotateCW/CCW`, `Resize`, `Tint`, `Copy`, `Scale`, image ops).
- `Zanna.Graphics.Color` HSL/hex helpers back the colour picker.
- `Zanna.Input.Action` for named keybindings; `Zanna.Input.Keyboard.GetText` for
  the text tool; `Zanna.Math.Random` for spray.
- `Zanna.Game.ButtonGroup` / `Zanna.Game.UI.HudButton` for the toolbox;
  `Zanna.GUI.FileDialog` / `MessageBox` / `Cursor` for dialogs + cursors.
