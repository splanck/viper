# Viper.Game.UI — GameButton + MenuList Input

## MenuList.HandleInput

Added to the existing MenuList widget: `HandleInput(up, down, confirm)` handles navigation and returns the selected index when confirm is pressed (-1 otherwise). Wraps selection at list boundaries. Hidden or empty menus return `-1` without changing selection. If both `up` and `down` are true, selection does not move but `confirm` still returns the current selected index. Lists are capped at 64 items; adding beyond that cap traps.

```zia
var choice = menuList.HandleInput(upPressed, downPressed, confirmPressed)
if choice == 0 { startGame() }
if choice == 1 { showOptions() }
```

## GameButton

Standalone styled button with normal/selected visual states. For custom menu layouts beyond MenuList.

### API
- `GameButton.New(x, y, w, h, text)` — Create button
- `SetText(text)`, `SetColors(normal, selected)`, `SetTextColors(normal, selected)`
- `SetBorder(width, color)` — width <= 0 disables the border
- `SetSize(width, height)` — resize the button
- `Draw(canvas, isSelected)` — Render with state-dependent colors
- `X`, `Y` — Position (get/set)
- `Width`, `Height` — Size (get)
- `Visible`, `TextScale` — Visibility and built-in text scale (get/set)

Text is copied into the button, clipped to the inner button width when drawn, and truncated on UTF-8 codepoint boundaries. Invalid constructor or `SetSize` dimensions are clamped to 1 pixel, and very large dimensions are capped at 16384 pixels.

### Example
```zia
var btn = GameButton.New(100, 200, 200, 40, "Start Game")
btn.SetColors(0x333333, 0x4444AA)
btn.Draw(canvas, isHighlighted)
```
