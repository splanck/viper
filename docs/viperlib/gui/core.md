---
status: active
audience: public
last-verified: 2026-04-29
---

# Core & Application
> App, Font, and common widget functions

**Part of [Viper Runtime Library](../README.md) › [GUI Widgets](README.md)**

---

## Overview

The Viper GUI library provides a comprehensive set of widgets for building desktop applications. All widgets are rendered using the ViperGFX graphics library and support both dark and light themes.

### Basic Structure (Zia)

```rust
module GuiDemo;

bind Viper.Terminal;
bind Viper.GUI.App as App;
bind Viper.GUI.Label as Label;
bind Viper.GUI.Button as Button;

func start() {
    var app = App.New("My Application", 800, 600);
    var root = app.get_Root();

    var label = Label.New(root, "Hello, World!");
    var btn = Button.New(root, "Click Me");
    btn.SetSize(100, 30);
    btn.SetPosition(10, 50);

    while !app.get_ShouldClose() {
        app.Poll();

        if btn.WasClicked() == 1 {
            label.SetText("Button clicked!");
        }

        app.Render();
    }
}
```

### Basic Structure

```basic
' Create a GUI application
DIM app AS Viper.GUI.App
app = NEW Viper.GUI.App("My Application", 800, 600)

' Load a font
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("arial.ttf")
app.SetFont(font, 14)

' Get root widget and add children
DIM root AS Object
root = app.Root

' Create widgets
DIM label AS Viper.GUI.Label
label = NEW Viper.GUI.Label(root, "Hello, World!")

DIM button AS Viper.GUI.Button
button = NEW Viper.GUI.Button(root, "Click Me")
button.SetSize(100, 30)
button.SetPosition(10, 50)

' Main loop
DO WHILE NOT app.ShouldClose
    app.Poll()

    ' Handle events
    IF button.WasClicked = 1 THEN
        label.SetText("Button clicked!")
    END IF

    app.Render()
LOOP
```

---

## Viper.GUI.App

Main application class that manages the window and widget tree.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.GUI.App(title, width, height)`

### Properties

| Property      | Type    | Access | Description                           |
|---------------|---------|--------|---------------------------------------|
| `ShouldClose` | Boolean | Read   | True if window close requested        |
| `Root`        | Object  | Read   | Root widget container                 |

### Methods

| Method                  | Signature            | Description                              |
|-------------------------|----------------------|------------------------------------------|
| `Destroy()`             | `Void()`             | Destroy the app and owned root widget tree |
| `GetDroppedFile(index)` | `String(Integer)`    | Get path of dropped file by index        |
| `GetDroppedFileCount()` | `Integer()`          | Number of files dropped                  |
| `Poll()`                | `Void()`             | Poll events and update widget states     |
| `Render()`              | `Void()`             | Render all widgets to the window         |
| `SetFont(font, size)`   | `Void(Font, Double)` | Set default font for all widgets         |
| `WasFileDropped()`      | `Integer()`          | 1 if a file was dropped on the window    |

### Window Management

| Method                    | Signature         | Description                                          |
|---------------------------|-------------------|------------------------------------------------------|
| `GetWidth()`              | `Integer()`       | Get window width in pixels                           |
| `GetHeight()`             | `Integer()`       | Get window height in pixels                          |
| `Minimize()`              | `Void()`          | Minimize the window                                  |
| `Maximize()`              | `Void()`          | Maximize the window                                  |
| `Restore()`               | `Void()`          | Restore from minimized or maximized state            |
| `IsMaximized()`           | `Boolean()`       | Check if the window is maximized                     |
| `IsMinimized()`           | `Boolean()`       | Check if the window is minimized                     |
| `IsFullscreen()`          | `Boolean()`       | Check if the window is in fullscreen mode            |
| `SetFullscreen(enabled)`  | `Void(Integer)`   | Enter or exit fullscreen mode                        |
| `Focus()`                 | `Void()`          | Bring the window to the front and focus it           |
| `IsFocused()`             | `Boolean()`       | Check if the window has focus                        |
| `SetPreventClose(prevent)` | `Void(Boolean)`  | Enable or disable close-button prevention            |
| `GetFps()`                | `Number()`        | Get current frames per second                        |
| `SetFps(fps)`             | `Void(Number)`    | Set target frames per second                         |
| `GetScale()`              | `Number()`        | Get display scale factor (e.g. 2.0 for HiDPI)        |
| `GetMonitorWidth()`       | `Integer()`       | Get monitor width in pixels                          |
| `GetMonitorHeight()`      | `Integer()`       | Get monitor height in pixels                         |
| `SetWindowSize(w, h)`     | `Void(Integer, Integer)` | Resize the window                           |
| `GetFontSize()`           | `Number()`        | Get current UI font size                             |
| `SetFontSize(size)`       | `Void(Number)`    | Set UI font size                                     |

### Lifecycle And Validation

`Destroy()` is idempotent. After an app is destroyed, app methods reject the stale handle without dereferencing it. The `Root` widget is owned by the app, so destroy the application with `app.Destroy()`; calling widget `Destroy()` on `app.Root` is ignored.

`SetSize()` and `SetWindowSize()` share the same window-size validation. Width and height are clamped to at least one pixel and to the platform `int32` range, and invalid font sizes, scale factors, and non-finite numeric values fall back to bounded defaults.

Low-level widget dispatch keeps focus, hover, modal, click, and capture state
separate for each recently dispatched root. When a subtree is hidden, detached,
or destroyed, cached event state for that subtree is cleared so later root
switches cannot restore stale widget handles.

Paint callbacks receive screen-space `x`/`y` for direct drawing compatibility;
use `vg_widget_get_bounds()` inside paint when a custom C widget needs its
parent-local layout rectangle. GUI key codes are ASCII-compatible for printable
keys, but special keys are not numerically identical to VGFX key codes; use
`vg_key_from_vgfx_key()` or `vg_event_from_platform()` for platform input.

### Example

```basic
DIM app AS Viper.GUI.App
app = NEW Viper.GUI.App("Calculator", 300, 400)

' Set up font
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("consola.ttf")
app.SetFont(font, 12)

' Main loop
DO WHILE NOT app.ShouldClose
    app.Poll()
    ' ... handle widget events ...
    app.Render()
LOOP
```

```rust
// Zia
var app = App.New("Calculator", 300, 400);
var root = app.get_Root();

while !app.get_ShouldClose() {
    app.Poll();
    // ... handle widget events ...
    app.Render();
}
```

---

## Viper.GUI.Font

Font loading for GUI widgets. Supports TrueType (.ttf) fonts.
TrueType outline rasterization preserves separate contour boundaries, so glyphs with holes or multiple independent contours render with the intended even-odd fill instead of connecting unrelated contour endpoints.

**Type:** Static/Instance
**Constructor:** `Viper.GUI.Font.Load(path)`

### Static Methods

| Method       | Signature        | Description                                 |
|--------------|------------------|---------------------------------------------|
| `Load(path)` | `Font(String)`   | Load font from TTF file. Returns NULL on failure |
| `Destroy()`  | `Void()`         | Destroy the font, or defer it while live GUI objects still reference it |

`Font.Destroy()` is safe to call while a live app or widget still references the font. In that case the runtime retires the font and frees it during app teardown, preventing dangling GUI font pointers.

### Example

```basic
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("fonts/roboto.ttf")

IF font <> NULL THEN
    app.SetFont(font, 14)
END IF
```

```rust
// Zia
var font = Font.Load("fonts/roboto.ttf");
app.SetFont(font, 14);
```

---

## Common Widget Functions

All widgets share these common functions:

Focusable widgets now participate in standard desktop keyboard traversal: `Tab` moves to the next focusable widget and `Shift+Tab` moves to the previous one, respecting modal roots when a dialog or popup is active.
Passing `NULL` to `Focus()` is a no-op. Detached widgets no longer inherit the current app's font or theme until they are attached to an app-owned tree or explicitly configured.
Common numeric setters clamp invalid input: negative sizes, margins, padding, flex factors, and layout gaps become zero, very large layout values are bounded, and non-finite doubles use safe defaults. Widget identifiers are generated from a 64-bit counter for long-running GUI sessions.

| Method                        | Signature                | Description                              |
|-------------------------------|--------------------------|------------------------------------------|
| `AddChild(child)`             | `Void(Object)`           | Add a child widget                       |
| `ClearTooltip()`              | `Void()`                 | Remove tooltip from widget               |
| `GetDropData()`               | `String()`               | Get the dropped data payload             |
| `GetDropType()`               | `String()`               | Get the type of the dropped data         |
| `GetFlex()`                   | `Double()`               | Get flex factor                          |
| `GetHeight()`                 | `Integer()`              | Get widget height in pixels              |
| `GetWidth()`                  | `Integer()`              | Get widget width in pixels               |
| `GetX()`                      | `Integer()`              | Get widget X position in pixels          |
| `GetY()`                      | `Integer()`              | Get widget Y position in pixels          |
| `IsBeingDragged()`            | `Integer()`              | 1 if widget is being dragged             |
| `IsDragOver()`                | `Integer()`              | 1 if a drag is hovering over widget      |
| `IsEnabled()`                 | `Integer()`              | 1 if widget is enabled                   |
| `IsFocused()`                 | `Integer()`              | 1 if widget has keyboard focus           |
| `IsHovered()`                 | `Integer()`              | 1 if mouse is over widget                |
| `IsPressed()`                 | `Integer()`              | 1 if widget is pressed                   |
| `IsVisible()`                 | `Integer()`              | 1 if widget is visible                   |
| `SetAcceptedDropTypes(types)` | `Void(String)`           | Set accepted drop type(s)                |
| `SetDragData(type, data)`     | `Void(String, String)`   | Set drag data type and payload           |
| `SetDraggable(enabled)`       | `Void(Integer)`          | Enable/disable drag source               |
| `SetDropTarget(enabled)`      | `Void(Integer)`          | Enable/disable drop target               |
| `SetEnabled(enabled)`         | `Void(Integer)`          | Set enabled state (1=enabled, 0=disabled)|
| `SetFlex(flex)`               | `Void(Double)`           | Set flex factor for layout               |
| `SetMargin(margin)`           | `Void(Integer)`          | Set uniform outer margin in pixels       |
| `SetMaxSize(width, height)`   | `Void(Double, Double)`   | Set maximum layout size; use `0` to clear a maximum |
| `SetPosition(x, y)`          | `Void(Integer, Integer)` | Set position in pixels                   |
| `SetPreferredSize(width, height)` | `Void(Double, Double)` | Set preferred layout size while preserving min/max constraints |
| `SetSize(width, height)`      | `Void(Integer, Integer)` | Set fixed size in pixels                 |
| `SetTooltip(text)`            | `Void(String)`           | Set tooltip text for this widget         |
| `SetTooltipRich(title, body)` | `Void(String, String)`   | Set rich tooltip with title and body     |
| `SetVisible(visible)`         | `Void(Integer)`          | Set visibility (1=visible, 0=hidden)     |
| `WasClicked()`                | `Integer()`              | 1 if widget was clicked this frame       |
| `WasDropped()`                | `Integer()`              | 1 if something was dropped this frame    |

### Drag and Drop

| Method                        | Signature              | Description                                              |
|-------------------------------|------------------------|----------------------------------------------------------|
| `SetDraggable(enabled)`       | `Void(Integer)`        | Enable or disable dragging this widget                   |
| `SetDragData(type, data)`     | `Void(String, String)` | Set the type and data payload for drag operations        |
| `IsBeingDragged()`            | `Integer()`            | 1 if this widget is currently being dragged              |
| `SetDropTarget(enabled)`      | `Void(Integer)`        | Mark this widget as a drop target                        |
| `SetAcceptedDropTypes(types)` | `Void(String)`         | Specify which drag data types are accepted               |
| `IsDragOver()`                | `Integer()`            | 1 if a dragged item is hovering over this widget         |
| `WasDropped()`                | `Integer()`            | 1 if a drop occurred on this widget this frame           |
| `GetDropType()`               | `String()`             | Get the type of the dropped data                         |
| `GetDropData()`               | `String()`             | Get the data payload that was dropped                    |

### Example

```basic
DIM button AS Viper.GUI.Button
button = NEW Viper.GUI.Button(root, "Submit")

' Configure
button.SetSize(120, 32)
button.SetPosition(50, 100)

' State checks in game loop
IF button.IsHovered = 1 THEN
    ' Show hover effect
END IF

IF button.WasClicked = 1 THEN
    ' Handle click
END IF
```

```rust
// Zia
var btn = Button.New(root, "Submit");
btn.SetSize(120, 32);
btn.SetPosition(50, 100);

if btn.IsHovered() == 1 { /* hover effect */ }
if btn.WasClicked() == 1 { /* handle click */ }
```

---


## See Also

- [Layout Widgets](layout.md)
- [Basic Widgets](widgets.md)
- [Containers & Advanced](containers.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
