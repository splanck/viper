# Viper Paint

Viper Paint is a Zia drawing app built on the Viper graphics runtime. The drawing surface stays `Pixels`-backed for direct paint operations, while the app uses higher-level runtime helpers for input actions, game UI widgets, file dialogs, viewport management, history, layers, and diagnostics.

## Runtime Features

- `Viper.Graphics.Canvas.BeginFrame`, `SetFps`, and `DeltaTime` drive the main loop.
- `Viper.Input.Action` maps tool and app commands to named actions.
- `Viper.Game.ButtonGroup` manages mutually exclusive tool selection.
- `Viper.Game.UI.GameButton` renders the toolbar/tool buttons while Paint keeps custom hit testing for the Canvas window.
- `Viper.Graphics.Pixels.Load`, `SavePng`, `SaveBmp`, `BlendPixel`, and `DrawThickLine` back image IO and brush rendering.
- `Viper.Graphics.Pixels.Invert`, `Grayscale`, `FlipH`, `Blur`, and cached `Scale` output back layer/image operations.
- `Viper.GUI.FileDialog` and `MessageBox` provide open/save dialogs and error feedback.
- `Viper.Game.Config` loads runtime defaults from `paint.runtime.json`.
- `Viper.Game.DebugOverlay` is available with `F3`.

## Shortcuts

- `1`-`8` or `P/B/E/L/R/O/F/I`: select Pencil, Brush, Eraser, Line, Rectangle, Ellipse, Fill, or Eyedropper.
- `Tab` / `Backspace`: cycle tools forward or backward.
- `[` / `]`: decrease or increase brush size.
- `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`: new, open, save, save as.
- `Ctrl+Z`, `Ctrl+Y`: undo and redo.
- `PageUp`, `PageDown`, `Home`: zoom in, zoom out, reset zoom.
- `Ctrl+mouse wheel`: zoom under the viewport; mouse wheel pans.
- `F11`: fullscreen toggle.
- `F3`: debug overlay.

## Layers And Image Operations

- The right panel lists layers. Left-click a layer to select it; right-click toggles visibility.
- Toolbar layer actions: `+ Layer`, `Dup`, `Del`, `Up`, and `Down`.
- Toolbar image actions operate on the active layer: `Gray`, `Invert`, `FlipH`, and `Blur`.
- Multi-layer compositing is cached and invalidated on pixel/layer changes; zoomed viewport images are cached per document revision and zoom level.

## File Formats

- Open supports the formats handled by `Pixels.Load`: PNG, BMP, JPEG, and GIF.
- Save supports PNG and BMP. JPEG/GIF are intentionally not offered for save because the current graphics runtime does not expose JPEG/GIF encoders.

## Notes

Paint is still a `Canvas` application rather than a full `Viper.GUI.App` because the GUI runtime does not yet expose a first-class embeddable interactive drawing surface. A future `Viper.GUI.PaintSurface` or equivalent would let Paint move menus, docking panels, shortcuts, command palette, and drag/drop fully into the GUI shell model used by ViperIDE.
