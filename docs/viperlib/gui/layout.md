---
status: active
audience: public
last-verified: 2026-07-14
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

HBox and VBox spacing accounts for child margins. Native alignment settings also account for those
margins when centering or end-aligning content.
The public runtime currently constructs `VBox` and `HBox` layouts. The native GUI layer also has
Flex, Grid, and Dock layouts for C/C++ integrations; those native layouts apply preferred, minimum,
and maximum size constraints during measurement. Arrange passes clamp content and child dimensions
at zero, so excessive padding or margins cannot produce negative child sizes. Spacing and padding
also sanitize non-finite or negative values to zero.
Children positioned with `Widget.SetPosition(x, y)` are treated as manual children
and are skipped by parent layout passes. Use manual positioning for overlays or
absolute placement, and use preferred sizes, flex, margins, spacing, and padding
for children that should be arranged by a layout container.

Native Grid layouts keep declared row and column counts within a bounded runtime range and grow an
effective implicit row count for auto-flow or explicit placements beyond the declared rows. Extra
children are placed into real rows with nonzero cell bounds instead of falling back to the top-left
cell. `Viper.GUI.Grid`, documented with the application components, is instead a tabular data widget;
it is not this native layout manager.

### Layout Methods

| Method                 | Signature          | Description                       |
|------------------------|--------------------|-----------------------------------|
| `SetSpacing(spacing)`  | `Void(Double)`     | Set space between children        |
| `SetPadding(padding)`  | `Void(Double)`     | Set internal padding              |

These methods are registered on `Viper.GUI.Container`, but the public objects that implement
spacing are `VBox` and `HBox` (plus native Flex objects supplied by an integration). Calling
`SetSpacing()` on another widget handle is safely ignored. `SetPadding()` uses the common widget
padding field and works on a valid widget handle.

### Example

```basic
' Create a vertical layout
DIM vbox AS Viper.GUI.VBox
vbox = NEW Viper.GUI.VBox()
vbox.SetSpacing(10)
vbox.SetPadding(20)

' Add to the root variable obtained from app.Root
root.AddChild(vbox)

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

### GroupBox

A titled container for grouping related controls. The constructor attaches the group box to its
parent immediately; `AddChild()` attaches a child to its content area.

**Constructor:** `NEW Viper.GUI.GroupBox(parent, title)`

| Method            | Signature          | Description                              |
|-------------------|--------------------|------------------------------------------|
| `AddChild(child)` | `Void(Object)`     | Add a widget to the group box            |
| `SetTitle(title)` | `Void(String)`     | Replace the displayed title              |
| `Destroy()`       | `Void()`           | Destroy the group box and its descendants |

```basic
DIM group AS Viper.GUI.GroupBox
group = NEW Viper.GUI.GroupBox(root, "Connection")

DIM host AS Viper.GUI.TextInput
host = NEW Viper.GUI.TextInput(group)
host.SetPlaceholder("Host name")
```

```rust
var group = GroupBox.New(root, "Connection");
var host = TextInput.New(group);
host.SetPlaceholder("Host name");
```

---

> **Note:** For grid-like public layouts, nest HBox and VBox containers. The native GUI layer also
> supports wrapped Flex layouts and dynamically resized Grid layouts when an integration needs
> lower-level control.


## See Also

- [Core & Application](core.md)
- [Basic Widgets](widgets.md)
- [Containers & Advanced](containers.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
