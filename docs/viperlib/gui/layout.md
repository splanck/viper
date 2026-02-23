# Layout Widgets
> VBox, HBox, Grid, and other layout containers

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

### Grid

Fixed grid layout — arranges children in explicit column/row cells with optional spanning.

**Constructor:** `NEW Viper.GUI.Grid(columns, rows)`

| Method                                       | Signature                                         | Description                                     |
|----------------------------------------------|---------------------------------------------------|-------------------------------------------------|
| `SetColumns(n)`                              | `Void(Integer)`                                   | Change number of columns                        |
| `SetRows(n)`                                 | `Void(Integer)`                                   | Change number of rows                           |
| `SetGap(colGap, rowGap)`                     | `Void(Double, Double)`                            | Set gap between columns and rows                |
| `SetColumnWidth(col, width)`                 | `Void(Integer, Double)`                           | Override a specific column's width              |
| `SetRowHeight(row, height)`                  | `Void(Integer, Double)`                           | Override a specific row's height                |
| `Place(child, col, row, colSpan, rowSpan)`   | `Void(Widget, Integer, Integer, Integer, Integer)` | Place a child in a specific cell (supports spanning) |

```basic
' 3-column, 2-row grid
DIM grid AS Viper.GUI.Grid
grid = NEW Viper.GUI.Grid(3, 2)
grid.SetGap(8, 8)

' Fixed first column width, others auto
grid.SetColumnWidth(0, 120)

' Place widgets in cells (col, row, colSpan, rowSpan)
DIM lbl AS Viper.GUI.Label
lbl = NEW Viper.GUI.Label(grid, "Name:")
grid.Place(lbl, 0, 0, 1, 1)

DIM input AS Viper.GUI.TextInput
input = NEW Viper.GUI.TextInput(grid)
grid.Place(input, 1, 0, 2, 1)  ' spans 2 columns

DIM btn AS Viper.GUI.Button
btn = NEW Viper.GUI.Button(grid, "Submit")
grid.Place(btn, 2, 1, 1, 1)
```

### Notes

- Unset column widths and row heights are distributed equally from the remaining space.
- `SetColumnWidth` and `SetRowHeight` set fixed pixel sizes; use sparingly to keep layouts responsive.
- Children must be added to the Grid as children before calling `Place`.

---


## See Also

- [Core & Application](core.md)
- [Basic Widgets](widgets.md)
- [Containers & Advanced](containers.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
