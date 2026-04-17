---
status: active
audience: public
last-verified: 2026-04-17
---

# Game UI Widgets

> Lightweight in-game UI widgets for HUDs, menus, and overlays.

**Part of [Viper Runtime Library — Game](../game.md)**

## Contents

- [Viper.Game.UI.Label](#vipergameuilabel)
- [Viper.Game.UI.Bar](#vipergameuibar)
- [Viper.Game.UI.Panel](#vipergameuipanel)
- [Viper.Game.UI.NineSlice](#vipergameuinineslice)
- [Viper.Game.UI.MenuList](#vipergameuimenulist)
- [Viper.Game.UI.Dialogue](#vipergameuidialogue)
- [Usage Example](#usage-example)

---

## Viper.Game.UI.Label

Positioned text widget with optional BitmapFont support and integer scaling.

**Type:** Instance (obj)
**Constructor:** `Label.New(x, y, text, color)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `X` | Integer | Read | X position |
| `Y` | Integer | Read | Y position |
| `Color` | Integer | Write | Text color (0xRRGGBB) |
| `Font` | BitmapFont | Write | Custom font (null = built-in 8x8) |
| `Scale` | Integer | Write | Integer scale (1 = normal, 2 = double, etc.) |
| `Visible` | Boolean | Write | Visibility toggle |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetText(text)` | `void(String)` | Update label text (max 511 chars) |
| `SetPos(x, y)` | `void(Integer, Integer)` | Move to new position |
| `Draw(canvas)` | `void(Canvas)` | Render to canvas |

---

## Viper.Game.UI.Bar

Progress/health/XP bar with configurable fill direction, colors, and optional border.

**Type:** Instance (obj)
**Constructor:** `Bar.New(x, y, width, height, fgColor, bgColor)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Value` | Integer | Read | Current value |
| `Max` | Integer | Read | Maximum value |
| `Border` | Integer | Write | Border color (0 = no border) |
| `Direction` | Integer | Write | Fill direction: 0=left-to-right, 1=right-to-left, 2=bottom-to-top, 3=top-to-bottom |
| `Visible` | Boolean | Write | Visibility toggle |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetValue(value, max)` | `void(Integer, Integer)` | Set current/max values. Value clamped to [0, max], max clamped to >= 1 |
| `SetPos(x, y)` | `void(Integer, Integer)` | Move to new position |
| `SetSize(w, h)` | `void(Integer, Integer)` | Resize the bar |
| `SetColors(fg, bg)` | `void(Integer, Integer)` | Update fill and background colors |
| `Draw(canvas)` | `void(Canvas)` | Render to canvas |

---

## Viper.Game.UI.Panel

Semi-transparent rectangular panel with optional border and rounded corners. Ideal for HUD backgrounds, dialogue boxes, and overlays.

**Type:** Instance (obj)
**Constructor:** `Panel.New(x, y, width, height, bgColor, alpha)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `CornerRadius` | Integer | Write | Rounded corner radius (0 = sharp) |
| `Visible` | Boolean | Write | Visibility toggle |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPos(x, y)` | `void(Integer, Integer)` | Move to new position |
| `SetSize(w, h)` | `void(Integer, Integer)` | Resize the panel |
| `SetColor(color, alpha)` | `void(Integer, Integer)` | Update background color and alpha (0-255) |
| `SetBorder(color, thickness)` | `void(Integer, Integer)` | Set border color and thickness |
| `Draw(canvas)` | `void(Canvas)` | Render to canvas |

---

## Viper.Game.UI.NineSlice

Scalable bordered UI element that stretches a source image using the 9-slice technique. Corners stay fixed, edges tile along one axis, center tiles to fill. Perfect for dialogue boxes, buttons, and inventory slots.

**Type:** Instance (obj)
**Constructor:** `NineSlice.New(pixels, left, top, right, bottom)`

The margins define the sizes of the corner/edge regions in the source Pixels image.

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Tint` | Integer | Write | Color tint (0 = no tint) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Draw(canvas, x, y, w, h)` | `void(Canvas, Integer, Integer, Integer, Integer)` | Draw at position with target size |

---

## Viper.Game.UI.MenuList

Vertical menu with selection highlight and keyboard-friendly wrap-around navigation.

**Type:** Instance (obj)
**Constructor:** `MenuList.New(x, y, itemHeight)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Selected` | Integer | Read/Write | Currently selected item index |
| `Count` | Integer | Read | Number of items |
| `Font` | BitmapFont | Write | Custom font (null = built-in 8x8) |
| `Visible` | Boolean | Write | Visibility toggle |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddItem(text)` | `void(String)` | Append a menu item (max 64 items) |
| `Clear()` | `void()` | Remove all items and reset selection |
| `MoveUp()` | `void()` | Move selection up (wraps to bottom at index 0) |
| `MoveDown()` | `void()` | Move selection down (wraps to top at last item) |
| `SetColors(text, selected, highlight)` | `void(Integer, Integer, Integer)` | Set text, selected text, and highlight background colors |
| `Draw(canvas)` | `void(Canvas)` | Render to canvas |

### Navigation Behavior

- `MoveUp()` at index 0 wraps to `Count - 1`
- `MoveDown()` at `Count - 1` wraps to 0
- Navigation on an empty list is a no-op

---

## Viper.Game.UI.Dialogue

Queued typewriter dialogue box with speaker labels, UTF-8-safe reveal timing, word wrapping,
and optional BitmapFont rendering.

**Type:** Instance (obj)
**Constructor:** `Dialogue.New(x, y, width, height)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Font` | BitmapFont | Write | Custom font for speaker/body/indicator text (null = built-in 8x8) |
| `TextColor` | Integer | Write | Dialogue body color (0xRRGGBB) |
| `SpeakerColor` | Integer | Write | Speaker label color (0xRRGGBB) |
| `BorderColor` | Integer | Write | Border color (`-1` disables the frame) |
| `Padding` | Integer | Write | Inner padding in pixels |
| `TextScale` | Integer | Write | Integer text scale (minimum 1) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetSpeed(charsPerSecond)` | `void(Integer)` | Reveal speed; values `<= 0` reveal the line instantly |
| `SetFont(font)` | `void(BitmapFont)` | Use a BitmapFont for text measurement and drawing |
| `SetTextColor(color)` | `void(Integer)` | Set body-text color |
| `SetSpeakerColor(color)` | `void(Integer)` | Set speaker-label color |
| `SetBgColor(color, alpha)` | `void(Integer, Integer)` | Set background fill color and alpha |
| `SetBorderColor(color)` | `void(Integer)` | Set border color (`-1` disables the border) |
| `SetPadding(pixels)` | `void(Integer)` | Set inner padding |
| `SetTextScale(scale)` | `void(Integer)` | Set integer text scale |
| `SetPos(x, y)` | `void(Integer, Integer)` | Move the dialogue box |
| `SetSize(width, height)` | `void(Integer, Integer)` | Resize the dialogue box |
| `Say(speaker, text)` | `void(String, String)` | Queue a spoken line |
| `SayText(text)` | `void(String)` | Queue narration with no speaker label |
| `Clear()` | `void()` | Remove all queued lines and reset playback state |
| `Update(dtMs)` | `void(Integer)` | Advance the typewriter by milliseconds |
| `Advance()` | `void()` | Skip the current line or move to the next one |
| `Skip()` | `void()` | Reveal the current line immediately |
| `IsActive()` | `Boolean()` | True while a line is being shown |
| `IsLineComplete()` | `Boolean()` | True when the current line is fully revealed |
| `IsFinished()` | `Boolean()` | True once the last line has been acknowledged |
| `IsWaiting()` | `Boolean()` | True when waiting for the caller to advance |
| `GetLineCount()` | `Integer()` | Number of queued lines |
| `GetCurrentLine()` | `Integer()` | Zero-based active line index |
| `GetSpeaker()` | `String()` | Speaker name of the active line, or empty string |
| `Draw(canvas)` | `void(Canvas)` | Render the panel, speaker label, wrapped text, and wait indicator |

### Notes

- Reveal timing counts UTF-8 codepoints, not bytes, so multi-byte characters are revealed as one character.
- The dialogue box stores UTF-8 safely; oversized speaker/text inputs are truncated on codepoint boundaries.
- One-way use case: `Advance()` behaves like a confirm button. If the line is mid-reveal it completes the line first; only the next call advances the queue.

---

## Usage Example

```zia
bind Viper.Graphics;
bind Viper.Game.UI;

// Create a HUD panel + health bar + score label
var hudPanel = Panel.New(0, 0, 320, 48, 0x000000, 180);
var healthBar = Bar.New(12, 12, 150, 20, 0xFF0000, 0x333333);
healthBar.Border = 0xFFFFFF;
healthBar.SetValue(75, 100);

var scoreLabel = Label.New(180, 16, "SCORE: 0", 0xFFFFFF);
scoreLabel.Scale = 2;

// Create a pause menu
var menu = MenuList.New(120, 100, 28);
menu.AddItem("Resume");
menu.AddItem("Options");
menu.AddItem("Quit");

// In the draw loop:
hudPanel.Draw(canvas);
healthBar.Draw(canvas);
scoreLabel.Draw(canvas);

// When paused:
menu.Draw(canvas);

// Handle input:
if upPressed { menu.MoveUp(); }
if downPressed { menu.MoveDown(); }
```

---

## Limits

| Limit | Value |
|-------|-------|
| Label text length | 511 characters |
| MenuList max items | 64 |
| MenuList item text length | 127 characters |
| Dialogue queued lines | 64 |
| Dialogue text payload | 511 bytes (UTF-8-safe truncation) |
| Dialogue speaker label | 63 bytes (UTF-8-safe truncation) |
| Panel alpha range | 0-255 (clamped) |
| Bar value | Clamped to [0, max] |
| Bar max | Clamped to >= 1 |

---

## See Also

- [Canvas & Color](../graphics/canvas.md) — Low-level drawing primitives
- [Fonts](../graphics/fonts.md) — BitmapFont for custom font rendering
- [ScreenFX](effects.md) — Screen shake, flash, and fade effects
