---
status: active
audience: public
last-verified: 2026-05-11
---

# Basic Widgets
> Label, Button, TextInput, Checkbox, Slider, RadioButton, and more

**Part of [Viper Runtime Library](../README.md) › [GUI Widgets](README.md)**

---

## Basic Widgets

### Label

Text display widget.
Labels now initialize from the current GUI theme or app font, so text participates in measurement and painting as soon as a default font has been installed. `SetFont()` still overrides that font for one label.

**Constructor:** `NEW Viper.GUI.Label(parent, text)`

| Method                    | Signature                  | Description              |
|---------------------------|----------------------------|--------------------------|
| `SetText(text)`           | `Void(String)`             | Set label text           |
| `SetFont(font, size)`     | `Void(Font, Double)`       | Set font and size        |
| `SetColor(color)`         | `Void(Integer)`            | Set text color (ARGB)    |

```basic
DIM label AS Viper.GUI.Label
label = NEW Viper.GUI.Label(root, "Hello!")
label.SetColor(4294901760)  ' Red text
```

```rust
// Zia
var label = Label.New(root, "Hello!");
label.SetColor(0xFFFF0000);  // Red text
```

---

### Button

Clickable button widget.

**Constructor:** `NEW Viper.GUI.Button(parent, text)`

| Method                    | Signature                  | Description                              |
|---------------------------|----------------------------|------------------------------------------|
| `SetText(text)`           | `Void(String)`             | Set button text                          |
| `SetFont(font, size)`     | `Void(Font, Double)`       | Set font and size                        |
| `SetStyle(style)`         | `Void(Integer)`            | Set style (0=default, 1=primary, 2=secondary, 3=danger, 4=text) |

```basic
DIM saveBtn AS Viper.GUI.Button
saveBtn = NEW Viper.GUI.Button(root, "Save")
saveBtn.SetStyle(1)  ' Primary style
saveBtn.SetSize(100, 32)

IF saveBtn.WasClicked = 1 THEN
    SaveDocument()
END IF
```

```rust
// Zia
var saveBtn = Button.New(root, "Save");
saveBtn.SetStyle(1);  // Primary style
saveBtn.SetSize(100, 32);

if saveBtn.WasClicked() == 1 { SaveDocument(); }
```

---

### TextInput

Single-line or multiline text input field.

Long single-line values stay clipped to the field bounds, the caret auto-scrolls horizontally as the cursor moves, and the caret blink is advanced automatically while the field is focused.
When the widget is configured for multiline mode it now paints real per-line text, selection, and caret state, supports `Enter` for newline insertion, `Up` / `Down` for line navigation, line-local `Home` / `End`, drag selection, and mouse-wheel vertical scrolling.
`Ctrl+Left` / `Ctrl+Right` now stop on punctuation and whitespace boundaries instead of treating only ASCII spaces as word breaks. `Ctrl/Cmd+Shift+Z` redoes the last undone edit, `Shift+Click` extends the current selection, and double-click selects the word under the pointer.
Text insertion rejects invalid Unicode scalar values such as surrogate codepoints or values above `U+10FFFF`; public text replacement and insertion also skip malformed UTF-8 byte sequences before storing text. Valid input remains UTF-8 encoded in the widget buffer, and `max_length` is enforced by codepoint count rather than raw bytes.
Replacing a selection validates and allocates the full edit before mutating the
buffer, so a rejected insertion does not delete the previous selection.

**Constructor:** `NEW Viper.GUI.TextInput(parent)`

| Property | Type   | Access | Description            |
|----------|--------|--------|------------------------|
| `Text`   | String | Read   | Current input text     |

| Method                       | Signature          | Description              |
|------------------------------|--------------------|--------------------------|
| `SetText(text)`              | `Void(String)`     | Replace the text, reset the undo/redo baseline, and fire the change callback when the content actually changes |
| `SetPlaceholder(text)`       | `Void(String)`     | Set placeholder text     |
| `SetFont(font, size)`        | `Void(Font, Double)`| Set font and size       |

```basic
DIM nameInput AS Viper.GUI.TextInput
nameInput = NEW Viper.GUI.TextInput(root)
nameInput.SetPlaceholder("Enter your name...")
nameInput.SetSize(200, 28)

' Get value
DIM name AS STRING
name = nameInput.GetText()
```

```rust
// Zia
var nameInput = TextInput.New(root);
nameInput.SetPlaceholder("Enter your name...");
nameInput.SetSize(200, 28);

var name = nameInput.get_Text();
```

---

### Checkbox

Toggle checkbox with label.
Checkbox labels also use the current GUI theme or app font by default, matching buttons, text inputs, dropdowns, and list boxes.

**Constructor:** `NEW Viper.GUI.Checkbox(parent, text)`

| Method                        | Signature       | Description                                      |
|-------------------------------|-----------------|--------------------------------------------------|
| `IsChecked()`                 | `Boolean()`     | Get checked state                                |
| `IsIndeterminate()`           | `Boolean()`     | Get indeterminate tri-state state                |
| `SetChecked(checked)`         | `Void(Boolean)` | Set checked state                                |
| `SetIndeterminate(value)`     | `Void(Boolean)` | Show or clear indeterminate state; setting it clears checked state |
| `SetText(text)`               | `Void(String)`  | Set label text                                   |

```basic
DIM rememberMe AS Viper.GUI.Checkbox
rememberMe = NEW Viper.GUI.Checkbox(root, "Remember me")

IF rememberMe.IsChecked() = 1 THEN
    SaveCredentials()
END IF
```

```rust
// Zia
var rememberMe = Checkbox.New(root, "Remember me");

if rememberMe.IsChecked() == 1 { SaveCredentials(); }
```

---

### RadioButton

Mutually exclusive radio button (use with RadioGroup).
Radio buttons unregister from their group when destroyed, and destroying a group clears the group reference on surviving radio buttons. This prevents stale selection handles after dynamic UI teardown.
Runtime-created radio groups are opaque handles. After `Destroy()`, that handle is invalid and cannot be reused to create more radio buttons; existing buttons become ungrouped and remain valid widgets.

**Constructor:** `NEW Viper.GUI.RadioButton(parent, text, group)`

| Method                     | Signature          | Description              |
|----------------------------|--------------------|--------------------------|
| `IsSelected()`             | `Integer()`        | 1 if selected            |
| `SetSelected(selected)`    | `Void(Boolean)`    | Set selection state      |

```basic
' Create a group
DIM group AS Viper.GUI.RadioGroup
group = NEW Viper.GUI.RadioGroup()

' Create radio buttons
DIM optA AS Viper.GUI.RadioButton
DIM optB AS Viper.GUI.RadioButton
DIM optC AS Viper.GUI.RadioButton

optA = NEW Viper.GUI.RadioButton(root, "Option A", group)
optB = NEW Viper.GUI.RadioButton(root, "Option B", group)
optC = NEW Viper.GUI.RadioButton(root, "Option C", group)

' Select default
optA.SetSelected(true)
```

```rust
// Zia
var group = RadioGroup.New();
var optA = RadioButton.New(root, "Option A", group);
var optB = RadioButton.New(root, "Option B", group);
var optC = RadioButton.New(root, "Option C", group);
optA.SetSelected(true);
```

---

### Slider

Draggable value slider.
Non-finite values are rejected or replaced with bounded defaults; ranges are normalized so the minimum is never greater than the maximum. Step snapping is clamped after rounding, so a stepped value cannot overshoot the range. Changing `SetStep()` immediately re-snaps the current value to the new grid, so a slider cannot remain between valid step positions. Runtime-created sliders inherit the app default font and size like other text-bearing controls, so value labels stay consistent after `App.SetFont()` or `App.SetFontSize()`.

**Constructor:** `NEW Viper.GUI.Slider(parent, horizontal)`

| Property | Type   | Access | Description            |
|----------|--------|--------|------------------------|
| `Value`  | Double | Read   | Current slider value   |

| Method                     | Signature                  | Description                    |
|----------------------------|----------------------------|--------------------------------|
| `SetValue(value)`          | `Void(Double)`             | Set current value              |
| `SetRange(min, max)`       | `Void(Double, Double)`     | Set value range                |
| `SetStep(step)`            | `Void(Double)`             | Set step (0 for continuous)    |

```basic
DIM volume AS Viper.GUI.Slider
volume = NEW Viper.GUI.Slider(root, 1)  ' Horizontal
volume.SetRange(0, 100)
volume.SetValue(50)
volume.SetSize(200, 20)

' Use value
Viper.Sound.Audio.SetMasterVolume(INT(volume.GetValue()))
```

```rust
// Zia
var volume = Slider.New(root, 1);  // Horizontal
volume.SetRange(0.0, 100.0);
volume.SetValue(50.0);
volume.SetSize(200, 20);
```

---

### Spinner

Numeric input with increment/decrement buttons.

The spinner now behaves as a real interactive widget: the up/down buttons repaint correctly, `Up` / `Down` keys adjust the value while focused, the mouse wheel steps the value when hovered, and the value field accepts direct keyboard entry.
Typing while the spinner is focused starts inline editing, `Enter` commits the typed number, and `Escape` restores the previous formatted value.
Programmatic values, ranges, steps, and decimal counts are clamped to finite supported ranges.
Reversed ranges are normalized, non-finite input is rejected or clamped, and long formatted decimal values resize the internal display buffer instead of truncating at a fixed length.
Spinners created after `App.SetFont()` inherit the app font and size for their value text.

**Constructor:** `NEW Viper.GUI.Spinner(parent)`

| Property | Type   | Access | Description            |
|----------|--------|--------|------------------------|
| `Value`  | Double | Read   | Current spinner value  |

| Method                     | Signature                  | Description                    |
|----------------------------|----------------------------|--------------------------------|
| `SetValue(value)`          | `Void(Double)`             | Set current value              |
| `SetRange(min, max)`       | `Void(Double, Double)`     | Set value range                |
| `SetStep(step)`            | `Void(Double)`             | Set step increment             |
| `SetDecimals(decimals)`    | `Void(Integer)`            | Set decimal places             |

```basic
DIM quantity AS Viper.GUI.Spinner
quantity = NEW Viper.GUI.Spinner(root)
quantity.SetRange(1, 100)
quantity.SetStep(1)
quantity.SetDecimals(0)
quantity.SetValue(1)
```

```rust
// Zia
var quantity = Spinner.New(root);
quantity.SetRange(1.0, 100.0);
quantity.SetStep(1.0);
quantity.SetDecimals(0);
quantity.SetValue(1.0);
```

---

### ProgressBar

Progress indicator bar.

Styles `0`, `1`, and `2` render as a horizontal bar, a circular ring indicator, and an indeterminate sliding bar respectively. Indeterminate progress bars created through the underlying C GUI layer now advance their animation during the normal app render loop.
Programmatic values are clamped to the `0.0` through `1.0` range, with non-finite values treated as `0.0`.
Progress bars created after `App.SetFont()` inherit the app font and size for optional percentage text.

**Constructor:** `NEW Viper.GUI.ProgressBar(parent)`

| Property | Type   | Access | Description                  |
|----------|--------|--------|------------------------------|
| `Value`  | Double | Read   | Progress value (0.0 to 1.0)  |

| Method                     | Signature          | Description                                              |
|----------------------------|--------------------|----------------------------------------------------------|
| `SetValue(value)`          | `Void(Double)`     | Set progress (0.0-1.0)                                   |
| `SetStyle(style)`          | `Void(Integer)`    | Set style: `0` bar, `1` circular, `2` indeterminate      |
| `ShowPercentage(show)`     | `Void(Integer)`    | Show or hide percentage text                             |

```basic
DIM progress AS Viper.GUI.ProgressBar
progress = NEW Viper.GUI.ProgressBar(root)
progress.SetSize(300, 20)

' Update during operation
FOR i = 1 TO 100
    DoWork(i)
    progress.SetValue(i / 100.0)
    app.Render()
NEXT i
```

```rust
// Zia
var progress = ProgressBar.New(root);
progress.SetSize(300, 20);
progress.SetValue(0.75);  // 75%
```

---

### Dropdown

Dropdown selection list.

Item additions, removals, selection changes, and font changes invalidate the widget immediately. Removing or clearing the selected item also raises the selection-change callback with the new effective selection. The closed control and popup width now size to the widest visible item instead of using a fixed narrow default. Popup hit-testing is screen-correct even when the control is nested or repositioned, long headers clip cleanly, and the popup panel tracks fractional scroll instead of stepping a whole row at a time. Keyboard typeahead selects matching items while closed and moves the hovered row while open; it decodes the first UTF-8 codepoint of each item instead of comparing raw bytes. `PageUp` / `PageDown` navigate long menus.

**Constructor:** `NEW Viper.GUI.Dropdown(parent)`

| Property       | Type    | Access | Description                |
|----------------|---------|--------|----------------------------|
| `Selected`     | Integer | Read   | Selected item index (-1=none) |
| `SelectedText` | String  | Read   | Selected item text            |

| Method                 | Signature          | Description                     |
|------------------------|--------------------|---------------------------------|
| `AddItem(text)`        | `Integer(String)`  | Add item, returns index         |
| `Clear()`              | `Void()`           | Remove all items                |
| `RemoveItem(index)`    | `Void(Integer)`    | Remove item by index            |
| `SetPlaceholder(text)` | `Void(String)`     | Set placeholder text            |
| `SetSelected(index)`   | `Void(Integer)`    | Set selected index              |

```rust
// Zia example
var dropdown = Dropdown.New(root);
dropdown.SetPlaceholder("Pick one...");
dropdown.AddItem("Red");
dropdown.AddItem("Green");
dropdown.AddItem("Blue");
dropdown.SetSelected(1);
Say("Selected: " + dropdown.get_SelectedText());
```

```basic
DIM colorPicker AS Viper.GUI.Dropdown
colorPicker = NEW Viper.GUI.Dropdown(root)
colorPicker.SetPlaceholder("Select color...")
colorPicker.AddItem("Red")
colorPicker.AddItem("Green")
colorPicker.AddItem("Blue")
colorPicker.SetSize(150, 28)

IF colorPicker.GetSelected() >= 0 THEN
    PRINT "Selected: "; colorPicker.GetSelectedText()
END IF
```

---

### ListBox

Scrollable list of selectable items with enhanced item management.

Hit-testing uses widget-local coordinates, so nested list boxes select the correct row. Measured height also follows the actual item count for short lists instead of reserving five rows unconditionally. In multi-select mode, `Ctrl/Cmd+Click` toggles rows, `Shift+Click` extends the active range, and keyboard navigation with `Shift` extends the current selection. Toggling the current row off now clears or moves the current selection instead of leaving `Selected` / `SelectedIndex` pointed at an unselected row.
Item text is clipped to the viewport and item add/remove/clear/select operations invalidate the list immediately so the visual state updates on the same frame.
Virtual list cache invalidation refreshes visible rows on the next paint even when the visible range has not changed.
`SelectIndex(index)` leaves the current selection unchanged when `index` is negative or outside the current item range, including virtual-list totals, and `ItemSetText()` preserves the old text if allocation fails.
`ItemSetData()` stores runtime strings with their explicit length, so embedded NUL bytes round-trip through `ItemGetData()`. Runtime-owned item data is freed automatically by `RemoveItem()` and `Clear()`.
Item handles are runtime-managed and become inert after `RemoveItem()`, `Clear()`, or list-box destruction; later item method calls return empty/0 values or no-op safely.
Virtual list selection and cache growth now fail closed if allocation or size
checks fail; the widget does not publish a larger virtual item count until its
selection bitmap can represent that range.
In virtual mode, `Count` reports the logical virtual item total rather than the number of materialized cache rows. `WasSelectionChanged()` reports real selection transitions, including selected-item removal and `Clear()` calls that remove a selection.

**Constructor:** `NEW Viper.GUI.ListBox(parent)`

### Properties

| Property             | Type    | Access | Description                              |
|----------------------|---------|--------|------------------------------------------|
| `Count`              | Integer | Read   | Number of items in the list              |
| `Selected`           | Object  | Read   | Currently selected item handle           |
| `SelectedIndex`      | Integer | Read   | Index of selected item (-1 if none)      |

### Methods

| Method                    | Signature              | Description                       |
|---------------------------|------------------------|-----------------------------------|
| `AddItem(text)`           | `Object(String)`       | Add item, returns item handle     |
| `Clear()`                 | `Void()`               | Remove all items                  |
| `ItemGetData(item)`       | `String(Object)`       | Get item user data                |
| `ItemGetText(item)`       | `String(Object)`       | Get item display text             |
| `ItemSetData(item, data)` | `Void(Object, String)` | Set item user data                |
| `ItemSetText(item, text)` | `Void(Object, String)` | Set item display text             |
| `RemoveItem(item)`        | `Void(Object)`         | Remove item                       |
| `Select(item)`            | `Void(Object)`         | Select item (NULL to deselect)    |
| `SelectIndex(index)`      | `Void(Integer)`        | Select item by index              |
| `SetFont(font, size)`     | `Void(Font, Double)`   | Set font for list items           |
| `WasSelectionChanged()`   | `Integer()`            | 1 if selection changed this frame |

### Example

```basic
DIM fileList AS Viper.GUI.ListBox
fileList = NEW Viper.GUI.ListBox(root)
fileList.SetSize(200, 300)

' Add items with display text
DIM item1 AS Object = fileList.AddItem("document.txt")
DIM item2 AS Object = fileList.AddItem("image.png")
DIM item3 AS Object = fileList.AddItem("data.csv")

' Attach user data to items (e.g., file paths)
fileList.ItemSetData(item1, "/home/user/documents/document.txt")
fileList.ItemSetData(item2, "/home/user/pictures/image.png")
fileList.ItemSetData(item3, "/home/user/data/data.csv")

' Get item count via property
PRINT "Total items: "; fileList.Count

' Select by index
fileList.SelectIndex(0)  ' Select first item

' Check for selection changes in main loop
DO WHILE NOT app.ShouldClose
    app.Poll()

    IF fileList.WasSelectionChanged() = 1 THEN
        DIM selected AS Object = fileList.Selected
        IF selected <> NULL THEN
            PRINT "Selected: "; fileList.ItemGetText(selected)
            PRINT "Path: "; fileList.ItemGetData(selected)
        END IF
    END IF

    app.Render()
LOOP
```

### File Browser Example

```basic
' Build a simple file browser
DIM fileList AS Viper.GUI.ListBox
fileList = NEW Viper.GUI.ListBox(root)
fileList.SetSize(250, 400)

' Set custom font
DIM monoFont AS Viper.GUI.Font
monoFont = Viper.GUI.Font.Load("consola.ttf")
fileList.SetFont(monoFont, 11)

' Populate with files
SUB LoadFiles(path AS STRING)
    fileList.Clear()

    DIM files() AS STRING
    files = Viper.IO.Directory.GetFiles(path)

    FOR i = 0 TO UBOUND(files)
        DIM item AS Object = fileList.AddItem(files(i))
        fileList.ItemSetData(item, path + "/" + files(i))  ' Store full path
    NEXT i
END SUB

' Handle double-click to open file
IF fileList.WasSelectionChanged() = 1 THEN
    DIM idx AS INTEGER = fileList.SelectedIndex
    IF idx >= 0 THEN
        DIM item AS Object = fileList.Selected
        OpenFile(fileList.ItemGetData(item))
    END IF
END IF
```

```rust
// Zia
var fileList = ListBox.New(root);
fileList.SetSize(200, 300);

var item1 = fileList.AddItem("document.txt");
var item2 = fileList.AddItem("image.png");
fileList.ItemSetData(item1, "/home/user/doc.txt");
fileList.SelectIndex(0);

if fileList.WasSelectionChanged() == 1 {
    var sel = fileList.get_Selected();
    Say(fileList.ItemGetText(sel));
}
```

---

### OutputPane

Scrollable text surface for build/run logs, search results, and the integrated
terminal (`SetTerminalMode`). It renders a monospace grid, so its font metrics
should drive any column/row layout built on top of it.

**Constructor:** `NEW Viper.GUI.OutputPane(parent)`

**Text & cell metrics** — use these instead of hardcoding a pixels-per-character
guess; they track the pane's actual font and DPI scale:

| Method               | Signature         | Description                                                       |
|----------------------|-------------------|-------------------------------------------------------------------|
| `GetCellWidth()`     | `Integer()`       | Pixel advance of one character cell (`"M"`); `0` if no font        |
| `GetCellHeight()`    | `Integer()`       | Pixel height of one line; `0` if no font                          |
| `MeasureText(text)`  | `Integer(String)` | Pixel width of `text` in the pane's font; `0` if no font/empty     |
| `ColumnsForWidth()`  | `Integer()`       | Whole columns that fit across the pane: `floor(width / cellWidth)` |
| `RowsForHeight()`    | `Integer()`       | Whole rows that fit down the pane: `floor(height / cellHeight)`    |

`ColumnsForWidth() == GetWidth() / GetCellWidth()` and `MeasureText("M") == GetCellWidth()`.
All five return `0` until a font is set with `SetFont`.

**Content & terminal:** `Append(text)`, `AppendLine(text)`,
`AppendStyled(text, fg, bg, bold)`, `Clear()`, `ScrollToTop()`, `ScrollToBottom()`,
`SetAutoScroll(on)`, `SetMaxLines(n)`, `get_LineCount`, `GetSelection()`,
`SelectAll()`, `SetFont(font, size)`, `SetTerminalMode(on)`, and `TakeInput()`
(drains captured keystrokes in terminal mode).

### Example

```rust
// Zia — size a terminal grid from the real font instead of guessing 8x18 px.
var pane = OutputPane.New(root);
pane.SetFont(monoFont, 14.0);
pane.SetTerminalMode(true);
var cols = pane.ColumnsForWidth();   // was: pane.GetWidth() / 8
var rows = pane.RowsForHeight();     // was: pane.GetHeight() / 18
```

---

### Image

Image display widget.

`SetScaleMode()` and `SetOpacity()` affect the rendered output directly. If you do not call `SetSize()`, the widget measures to the image's natural pixel dimensions once pixels have been assigned.
`SetPixels()` accepts a `Viper.Graphics.Pixels` object whose elements are packed as `0xRRGGBBAA`; the GUI layer converts them to byte RGBA. Width or height values less than or equal to zero use the source dimensions, requested sizes larger than the source are clamped, and `NULL` pixels clear the image.
The native C widget `LoadFile()` now decodes BMP directly and uses platform image decoders for PNG/JPEG where available; the Viper runtime binding continues to route through `Viper.Graphics.Pixels` for PNG, BMP, JPEG, and GIF.

**Constructor:** `NEW Viper.GUI.Image(parent)`

| Method                         | Signature                              | Description                    |
|--------------------------------|----------------------------------------|--------------------------------|
| `SetPixels(pixels, w, h)`      | `Void(Pixels, Integer, Integer)`       | Set image from a `Viper.Graphics.Pixels` buffer |
| `LoadFile(path)`               | `Integer(String)`                      | Load PNG, BMP, JPEG, or GIF file directly (1=ok, 0=fail) |
| `Clear()`                      | `Void()`                               | Clear image                    |
| `SetScaleMode(mode)`           | `Void(Integer)`                        | 0=none, 1=fit, 2=fill, 3=stretch |
| `SetOpacity(opacity)`          | `Void(Double)`                         | Set opacity (0.0-1.0)          |

```basic
DIM preview AS Viper.GUI.Image
preview = NEW Viper.GUI.Image(root)
preview.SetSize(200, 200)
preview.SetScaleMode(1)  ' Fit

' Load directly from file (PNG, BMP, JPEG, or GIF)
preview.LoadFile("photo.png")
```

```rust
// Zia
var preview = Image.New(root);
preview.SetSize(200, 200);
preview.SetScaleMode(1);  // Fit

// Load directly from file (PNG, BMP, JPEG, or GIF)
preview.LoadFile("photo.png");
```

---

### Color Widgets

The native GUI layer includes `ColorSwatch`, `ColorPalette`, and `ColorPicker` widgets for C applications and demos.
`ColorPalette` renders its swatch grid directly and selection callbacks fire once per completed click. `ColorPicker.SetColor()`, `SetRGB()`, and `SetAlpha()` synchronize their child sliders and preview swatch before emitting one external change callback, so observers no longer see transient intermediate colors.

---

### Grid

Tabular data grid whose columns **auto-size to their widest cell**, with optional column
headers. A non-interactive display widget (no selection or scrolling) for property and data
panels — use it instead of padding monospace strings to fake columns.

**Constructor:** `NEW Viper.GUI.Grid(parent)`

| Method                   | Signature                       | Description                                                  |
|--------------------------|---------------------------------|--------------------------------------------------------------|
| `SetColumns(count)`      | `Void(Integer)`                 | Set the column count (clears existing headers and cells)     |
| `SetHeader(col, text)`   | `Void(Integer, String)`         | Set a column header (drawn as a header row)                  |
| `SetCell(row, col, text)`| `Void(Integer, Integer, String)`| Set a cell's text, growing the row count as needed           |
| `GetCell(row, col)`      | `String(Integer, Integer)`      | A cell's text (empty when out of range)                      |
| `Clear()`                | `Void()`                        | Remove all rows (columns and headers are kept)               |
| `SetFont(font, size)`    | `Void(Object, Number)`          | Set the header/cell font                                     |
| `GetColumnWidth(col)`    | `Integer(Integer)`              | Auto-sized pixel width of a column (its widest content + padding); `0` if no font |
| `RowCount`               | `Integer` (property)            | Number of populated rows                                     |
| `ColumnCount`            | `Integer` (property)            | Number of columns                                            |

A column's width tracks its widest header or cell, so the table stays aligned no matter the
content or font size. `GetColumnWidth()` exposes the computed width if you need it for surrounding
layout. The grid also supports the common `Widget` methods (`SetSize`, `SetVisible`, `Destroy`, …).

### Example

```rust
// Zia — a property table that sizes its columns to the data.
var grid = Grid.New(panel);
grid.SetColumns(2);
grid.SetHeader(0, "Property");
grid.SetHeader(1, "Value");
grid.SetCell(0, 0, "Width");      grid.SetCell(0, 1, "1280");
grid.SetCell(1, 0, "Color Space"); grid.SetCell(1, 1, "sRGB");
// Columns 0 and 1 auto-size to "Color Space" and "Property"/"sRGB" respectively.
```

---

### PopupList

A caret-anchored, filterable, keyboard-navigable popup list rendered above content — the
reusable core of an autocomplete/quick-pick UI. The **host drives keyboard and visibility** and
supplies pre-ranked items (language-specific ranking stays in the host); the widget owns the
popup mechanics (filter, selection, anchor, accept). Created hidden.

**Constructor:** `NEW Viper.GUI.PopupList(root)`

| Method                   | Signature              | Description                                                  |
|--------------------------|------------------------|--------------------------------------------------------------|
| `AddItem(text)`          | `Void(String)`         | Append an item (host adds them in its preferred rank order)  |
| `Clear()`                | `Void()`               | Remove all items and reset the filter/selection              |
| `SetFilter(text)`        | `Void(String)`         | Keep only items containing `text` (case-insensitive)         |
| `VisibleCount`           | `Integer` (property)   | Number of items matching the filter                          |
| `NavigateUp()` / `NavigateDown()` | `Void()`      | Move the selection within the visible items (clamped)        |
| `SetSelectedIndex(i)`    | `Void(Integer)`        | Set the selection within the visible items                   |
| `GetSelectedIndex()`     | `Integer()`            | Selection index within the visible items, or `-1` if none    |
| `GetSelected()`          | `String()`             | Text of the selected visible item (empty if none)            |
| `AcceptSelected()`       | `Void()`               | Mark the current selection accepted                          |
| `WasAccepted()`          | `Boolean()`            | Whether `AcceptSelected` was called since the last query (consume-on-read) |
| `AnchorAt(x, y)`         | `Void(Number, Number)` | Position the popup top-left (e.g. at the caret pixel)        |
| `SetWidth(w)`            | `Void(Number)`         | Set the popup width                                          |
| `SetMaxRows(n)`          | `Void(Integer)`        | Maximum visible rows before clamping height                 |
| `SetFont(font, size)`    | `Void(Object, Number)` | Set the item font                                           |
| `SetVisible(on)` / `IsVisible()` | `Void(Boolean)` / `Boolean()` | Show/hide the popup                       |

The host typically: clears + adds candidates, `SetFilter(typed)`, `AnchorAt(caretX, caretY)`,
`SetVisible(true)`; then on arrow keys calls `NavigateUp/Down`, on Enter calls `AcceptSelected` and
reads `WasAccepted` to perform the insertion, and hides the popup on edit or escape.

### Example

```rust
// Zia — a caret-anchored completion popup.
var popup = PopupList.New(root);
popup.AddItem("Console");
popup.AddItem("Convert");
popup.AddItem("Canvas");
popup.SetFilter("con");                 // -> "Console", "Convert"
popup.AnchorAt(editor.GetCursorPixelX(), editor.GetCursorPixelY() + 20);
popup.SetVisible(true);
// On Enter:
if popup.WasAccepted() { editor.Insert(popup.GetSelected()); popup.SetVisible(false); }
```

---

### CodeEditor — InsertAndPlaceCursor

`Viper.GUI.CodeEditor` is the syntax-highlighting multi-line editor (insert, cursor, find, and
selection APIs). One method worth calling out for snippet-style insertion:

| Method                              | Signature                  | Description                                                              |
|-------------------------------------|----------------------------|--------------------------------------------------------------------------|
| `InsertAndPlaceCursor(text, caretOffset)` | `Void(String, Integer)` | Insert `text` at the cursor, then place the caret `caretOffset` characters into the inserted text (counting newlines) |

Use it to drop the caret *inside* a multi-line insertion instead of at its end — e.g. inserting
`"if  {\n\n}"` and placing the caret after `if ` — without walking the inserted text by hand.

```rust
// Zia — insert a snippet and land the caret between the braces.
editor.InsertAndPlaceCursor("if () {\n    \n}", 4); // caret after "if ("
```

---


## See Also

- [Core & Application](core.md)
- [Layout Widgets](layout.md)
- [Containers & Advanced](containers.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
