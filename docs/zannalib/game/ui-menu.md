---
status: active
audience: public
last-verified: 2026-07-15
---

# Zanna.Game.UI menu input and GameButton

## MenuList.HandleInput

`menu.HandleInput(up, down, confirm)` returns the selected zero-based index when `confirm` is true,
or `-1` otherwise. Up/down wrap at the ends. When both directions are true, selection does not
move but confirm still returns it. Hidden and empty menus return `-1` without changing selection.

MenuList holds at most 64 items; the 65th `AddItem` traps. Each label is copied into a 128-byte
buffer (127 content bytes) and truncation preserves a UTF-8 boundary. The selected index starts at
zero, `Clear` resets it to zero, and explicit selection clamps to the current item range.

```rust
module MenuListInputExample;

func start() {
    var menu = Zanna.Game.UI.HudMenuList.New(20, 20, 24);
    menu.AddItem("Start");
    menu.AddItem("Options");
    var choice = menu.HandleInput(false, true, true);
    Zanna.Terminal.SayInt(choice);
}
```

## GameButton

`Zanna.Game.UI.HudButton` is a styled drawing primitive, not an input/hit-test control. Its main
surface is:

- `New(x, y, width, height, text)`, `SetText`, `SetColors`, and `SetTextColors`;
- `SetBorder(width, color)`: non-positive width disables it, and color zero also suppresses draw;
- `SetSize`, mutable `X`/`Y`, read-only `Width`/`Height`, and mutable `Visible`/`TextScale`;
- `Draw(canvas, isSelected)`, accepting a 2D Canvas or Canvas3D and selecting the configured state
  colors. Hidden buttons are a no-op.

Dimensions clamp to 1–16,384 pixels and text scale to 1–16. The label is copied into 63 content
bytes on a UTF-8 boundary, then drawing clips it by the available built-in 8-pixel character cells.

```rust
module GameButtonExample;

func start() {
    var button = Zanna.Game.UI.HudButton.New(100, 200, 200, 40, "Start Game");
    button.SetColors(0x333333, 0x4444AA);
    button.SetTextColors(0xCCCCCC, 0xFFFFFF);
    button.set_TextScale(2);
    Zanna.Terminal.SayInt(button.get_Width());
}
```
