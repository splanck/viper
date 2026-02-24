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

> **Note:** For grid-like layouts, nest HBox and VBox containers.


## See Also

- [Core & Application](core.md)
- [Basic Widgets](widgets.md)
- [Containers & Advanced](containers.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
