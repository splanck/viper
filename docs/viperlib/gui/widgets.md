# Basic Widgets
> Label, Button, TextInput, Checkbox, Slider, RadioButton, and more

**Part of [Viper Runtime Library](../README.md) › [GUI Widgets](README.md)**

---

## Basic Widgets

### Label

Text display widget.

**Constructor:** `NEW Viper.GUI.Label(parent, text)`

| Method                    | Signature                  | Description              |
|---------------------------|----------------------------|--------------------------|
| `SetText(text)`           | `Void(String)`             | Set label text           |
| `SetFont(font, size)`     | `Void(Font, Double)`       | Set font and size        |
| `SetColor(color)`         | `Void(Integer)`            | Set text color (ARGB)    |

```basic
DIM label AS Viper.GUI.Label
label = NEW Viper.GUI.Label(root, "Hello!")
label.SetColor(&HFFFF0000)  ' Red text
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
| `GetText()`               | `String()`                 | Get current button label text            |
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

Single-line text input field.

**Constructor:** `NEW Viper.GUI.TextInput(parent)`

| Property | Type   | Access | Description            |
|----------|--------|--------|------------------------|
| `Text`   | String | Read   | Current input text     |

| Method                       | Signature          | Description              |
|------------------------------|--------------------|--------------------------|
| `SetText(text)`              | `Void(String)`     | Set input text           |
| `SetPlaceholder(text)`       | `Void(String)`     | Set placeholder text     |
| `SetFont(font, size)`        | `Void(Font, Double)`| Set font and size       |
| `Tick(dt)`                   | `Void(Double)`     | Advance cursor blink timer (call each frame with delta-time in seconds) |

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

**Constructor:** `NEW Viper.GUI.Checkbox(parent, text)`

| Method                        | Signature       | Description                                      |
|-------------------------------|-----------------|--------------------------------------------------|
| `IsChecked()`                 | `Boolean()`     | Get checked state                                |
| `SetChecked(checked)`         | `Void(Integer)` | Set checked state                                |
| `SetIndeterminate(state)`     | `Void(Boolean)` | Set indeterminate (tri-state) display — shown as dash instead of check mark |
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

**Constructor:** `NEW Viper.GUI.RadioButton(parent, text, group)`

| Method                     | Signature          | Description              |
|----------------------------|--------------------|--------------------------|
| `IsSelected()`             | `Integer()`        | 1 if selected            |
| `SetSelected(selected)`    | `Void(Integer)`    | Set selection state      |

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
optA.SetSelected(1)
```

```rust
// Zia
var group = RadioGroup.New();
var optA = RadioButton.New(root, "Option A", group);
var optB = RadioButton.New(root, "Option B", group);
var optC = RadioButton.New(root, "Option C", group);
optA.SetSelected(1);
```

---

### Slider

Draggable value slider.

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
Viper.Audio.MasterVolume = INT(volume.GetValue())
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

**Constructor:** `NEW Viper.GUI.ProgressBar(parent)`

| Property | Type   | Access | Description                  |
|----------|--------|--------|------------------------------|
| `Value`  | Double | Read   | Progress value (0.0 to 1.0)  |

| Method                     | Signature          | Description                                              |
|----------------------------|--------------------|----------------------------------------------------------|
| `SetValue(value)`          | `Void(Double)`     | Set progress (0.0-1.0)                                   |
| `SetIndeterminate(state)`  | `Void(Boolean)`    | Enable/disable indeterminate (animated spinner) mode     |
| `Tick(dt)`                 | `Void(Double)`     | Advance indeterminate animation (call each frame with delta-time in seconds) |

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
DO WHILE app.ShouldClose = 0
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

### Image

Image display widget.

**Constructor:** `NEW Viper.GUI.Image(parent)`

| Method                         | Signature                              | Description                    |
|--------------------------------|----------------------------------------|--------------------------------|
| `SetPixels(pixels, w, h)`      | `Void(Pixels, Integer, Integer)`       | Set image from Pixels buffer   |
| `Clear()`                      | `Void()`                               | Clear image                    |
| `SetScaleMode(mode)`           | `Void(Integer)`                        | 0=none, 1=fit, 2=fill, 3=stretch |
| `SetOpacity(opacity)`          | `Void(Double)`                         | Set opacity (0.0-1.0)          |

```basic
DIM preview AS Viper.GUI.Image
preview = NEW Viper.GUI.Image(root)
preview.SetSize(200, 200)
preview.SetScaleMode(1)  ' Fit

DIM pixels AS Viper.Graphics.Pixels
pixels = Viper.Graphics.Pixels.LoadBmp("photo.bmp")
preview.SetPixels(pixels, pixels.Width, pixels.Height)
```

```rust
// Zia
var preview = Image.New(root);
preview.SetSize(200, 200);
preview.SetScaleMode(1);  // Fit

var pixels = Pixels.LoadBmp("photo.bmp");
preview.SetPixels(pixels, pixels.get_Width(), pixels.get_Height());
```

---


## See Also

- [Core & Application](core.md)
- [Layout Widgets](layout.md)
- [Containers & Advanced](containers.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
