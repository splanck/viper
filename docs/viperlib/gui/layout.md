---
status: active
audience: public
last-verified: 2026-04-29
---

# Layout Widgets
> VBox, HBox, and other layout containers

**Part of [Viper Runtime Library](../README.md) › [GUI Widgets](README.md)**

---

## Layout Widgets

### VBox

Vertical box layout - arranges children top to bottom.

**Constructor:** `NEW Viper.GUI.VBox()`

### HBox

Horizontal box layout - arranges children left to right.

**Constructor:** `NEW Viper.GUI.HBox()`

HBox and VBox spacing/alignment account for child margins when centering or end-aligning content.
VBox, HBox, Flex, Grid, and Dock containers apply preferred, minimum, and maximum size constraints during measurement. Arrange passes clamp content and child dimensions at zero, so excessive padding or margins cannot produce negative child sizes. Spacing, gaps, and explicit Grid track sizes also sanitize non-finite or negative values to zero.
Children positioned with `Widget.SetPosition(x, y)` are treated as manual children
and are skipped by parent layout passes. Use manual positioning for overlays or
absolute placement, and use preferred sizes, flex, margins, spacing, and padding
for children that should be arranged by a layout container.

Grid layouts keep declared row and column counts within a bounded runtime range and grow an effective implicit row count for auto-flow or explicit placements beyond the declared rows. Extra children are placed into real rows with nonzero cell bounds instead of falling back to the top-left cell.

### Layout Methods

| Method                 | Signature          | Description                       |
|------------------------|--------------------|-----------------------------------|
| `SetSpacing(spacing)`  | `Void(Double)`     | Set space between children        |
| `SetPadding(padding)`  | `Void(Double)`     | Set internal padding              |

### Example

```basic
' Create a vertical layout
DIM vbox AS Viper.GUI.VBox
vbox = NEW Viper.GUI.VBox()
vbox.SetSpacing(10)
vbox.SetPadding(20)

' Add to root
app.Root.AddChild(vbox)

' Add widgets to vbox
DIM label1 AS Viper.GUI.Label
label1 = NEW Viper.GUI.Label(vbox, "First")

DIM label2 AS Viper.GUI.Label
label2 = NEW Viper.GUI.Label(vbox, "Second")

DIM label3 AS Viper.GUI.Label
label3 = NEW Viper.GUI.Label(vbox, "Third")
```

```rust
// Zia
var vbox = VBox.New();
vbox.SetSpacing(10.0);
vbox.SetPadding(20.0);
root.AddChild(vbox);

var l1 = Label.New(vbox, "First");
var l2 = Label.New(vbox, "Second");
var l3 = Label.New(vbox, "Third");
```

---

> **Note:** For grid-like layouts, nest HBox and VBox containers. In the native GUI layer, wrapped flex layouts and dynamically resized grids are also supported when you need lower-level control.


## See Also

- [Core & Application](core.md)
- [Basic Widgets](widgets.md)
- [Containers & Advanced](containers.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
