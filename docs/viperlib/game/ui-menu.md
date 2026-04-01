# Viper.Game.UI — GameButton + MenuList Input

## MenuList.HandleInput

Added to the existing MenuList widget: `HandleInput(up, down, confirm)` handles navigation and returns the selected index when confirm is pressed (-1 otherwise). Wraps selection at list boundaries.

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
- `SetBorder(width, color)`
- `Draw(canvas, isSelected)` — Render with state-dependent colors
- `X`, `Y` — Position (get/set)

### Example
```zia
var btn = GameButton.New(100, 200, 200, 40, "Start Game")
btn.SetColors(0x333333, 0x4444AA)
btn.Draw(canvas, isHighlighted)
```
