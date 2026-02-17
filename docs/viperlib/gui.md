# GUI

> Widget-based graphical user interface library for desktop applications.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Overview](#overview)
- [Viper.GUI.App](#vipergui-app)
- [Viper.GUI.Font](#vipergui-font)
- [Common Widget Functions](#common-widget-functions)
- [Layout Widgets](#layout-widgets)
- [Basic Widgets](#basic-widgets)
  - [Label](#label)
  - [Button](#button)
  - [TextInput](#textinput)
  - [Checkbox](#checkbox)
  - [RadioButton](#radiobutton)
  - [Slider](#slider)
  - [Spinner](#spinner)
  - [ProgressBar](#progressbar)
  - [Dropdown](#dropdown)
  - [ListBox](#listbox)
  - [Image](#image)
- [Container Widgets](#container-widgets)
  - [ScrollView](#scrollview)
  - [SplitPane](#splitpane)
  - [TabBar](#tabbar)
  - [TreeView](#treeview)
- [Advanced Widgets](#advanced-widgets)
  - [CodeEditor](#codeeditor)
- [Application Components](#application-components)
  - [MenuBar / Menu / MenuItem](#menubar)
  - [Toolbar / ToolbarItem](#toolbar)
  - [StatusBar / StatusBarItem](#statusbar)
  - [ContextMenu](#contextmenu)
  - [FindBar](#findbar)
  - [CommandPalette](#commandpalette)
  - [Breadcrumb](#breadcrumb)
  - [Minimap](#minimap)
- [Dialogs](#dialogs)
  - [MessageBox](#messagebox)
  - [FileDialog](#filedialog)
- [Notifications](#notifications)
  - [Toast](#toast)
  - [Tooltip](#tooltip)
- [Utilities](#utilities)
  - [Clipboard](#clipboard)
  - [Shortcuts](#shortcuts)
  - [Cursor](#cursor)
- [Themes](#themes)

---

## Overview

The Viper GUI library provides a comprehensive set of widgets for building desktop applications. All widgets are rendered using the ViperGFX graphics library and support both dark and light themes.

### Basic Structure (Zia)

```zia
module GuiDemo;

bind Viper.Terminal;
bind Viper.GUI.App as App;
bind Viper.GUI.Label as Label;
bind Viper.GUI.Button as Button;

func start() {
    var app = App.New("My Application", 800, 600);
    var root = app.get_Root();

    var label = Label.New(root, "Hello, World!");
    var btn = Button.New(root, "Click Me");
    btn.SetSize(100, 30);
    btn.SetPosition(10, 50);

    while app.get_ShouldClose() == 0 {
        app.Poll();

        if btn.WasClicked() == 1 {
            label.SetText("Button clicked!");
        }

        app.Render();
    }
}
```

### Basic Structure

```basic
' Create a GUI application
DIM app AS Viper.GUI.App
app = NEW Viper.GUI.App("My Application", 800, 600)

' Load a font
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("arial.ttf")
app.SetFont(font, 14)

' Get root widget and add children
DIM root AS Object
root = app.Root

' Create widgets
DIM label AS Viper.GUI.Label
label = NEW Viper.GUI.Label(root, "Hello, World!")

DIM button AS Viper.GUI.Button
button = NEW Viper.GUI.Button(root, "Click Me")
button.SetSize(100, 30)
button.SetPosition(10, 50)

' Main loop
DO WHILE app.ShouldClose = 0
    app.Poll()

    ' Handle events
    IF button.WasClicked = 1 THEN
        label.SetText("Button clicked!")
    END IF

    app.Render()
LOOP
```

---

## Viper.GUI.App

Main application class that manages the window and widget tree.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.GUI.App(title, width, height)`

### Properties

| Property      | Type    | Access | Description                           |
|---------------|---------|--------|---------------------------------------|
| `ShouldClose` | Boolean | Read   | True if window close requested        |
| `Root`        | Object  | Read   | Root widget container                 |

### Methods

| Method                  | Signature            | Description                              |
|-------------------------|----------------------|------------------------------------------|
| `GetDroppedFile(index)` | `String(Integer)`    | Get path of dropped file by index        |
| `GetDroppedFileCount()` | `Integer()`          | Number of files dropped                  |
| `Poll()`                | `Void()`             | Poll events and update widget states     |
| `Render()`              | `Void()`             | Render all widgets to the window         |
| `SetFont(font, size)`   | `Void(Font, Double)` | Set default font for all widgets         |
| `WasFileDropped()`      | `Integer()`          | 1 if a file was dropped on the window    |

### Example

```basic
DIM app AS Viper.GUI.App
app = NEW Viper.GUI.App("Calculator", 300, 400)

' Set up font
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("consola.ttf")
app.SetFont(font, 12)

' Main loop
DO WHILE app.ShouldClose = 0
    app.Poll()
    ' ... handle widget events ...
    app.Render()
LOOP
```

```zia
// Zia
var app = App.New("Calculator", 300, 400);
var root = app.get_Root();

while app.get_ShouldClose() == 0 {
    app.Poll();
    // ... handle widget events ...
    app.Render();
}
```

---

## Viper.GUI.Font

Font loading for GUI widgets. Supports TrueType (.ttf) fonts.

**Type:** Static/Instance
**Constructor:** `Viper.GUI.Font.Load(path)`

### Static Methods

| Method       | Signature        | Description                                 |
|--------------|------------------|---------------------------------------------|
| `Load(path)` | `Font(String)`   | Load font from TTF file. Returns NULL on failure |

### Example

```basic
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("fonts/roboto.ttf")

IF font <> NULL THEN
    app.SetFont(font, 14)
END IF
```

```zia
// Zia
var font = Font.Load("fonts/roboto.ttf");
app.SetFont(font, 14);
```

---

## Common Widget Functions

All widgets share these common functions:

| Method                        | Signature                | Description                              |
|-------------------------------|--------------------------|------------------------------------------|
| `AddChild(child)`             | `Void(Object)`           | Add a child widget                       |
| `ClearTooltip()`              | `Void()`                 | Remove tooltip from widget               |
| `GetDropData()`               | `String()`               | Get the dropped data payload             |
| `GetDropType()`               | `String()`               | Get the type of the dropped data         |
| `IsBeingDragged()`            | `Integer()`              | 1 if widget is being dragged             |
| `IsDragOver()`                | `Integer()`              | 1 if a drag is hovering over widget      |
| `IsFocused()`                 | `Integer()`              | 1 if widget has keyboard focus           |
| `IsHovered()`                 | `Integer()`              | 1 if mouse is over widget                |
| `IsPressed()`                 | `Integer()`              | 1 if widget is pressed                   |
| `SetAcceptedDropTypes(types)` | `Void(String)`           | Set accepted drop type(s)                |
| `SetDragData(type, data)`     | `Void(String, String)`   | Set drag data type and payload           |
| `SetDraggable(enabled)`       | `Void(Integer)`          | Enable/disable drag source               |
| `SetDropTarget(enabled)`      | `Void(Integer)`          | Enable/disable drop target               |
| `SetEnabled(enabled)`         | `Void(Integer)`          | Set enabled state (1=enabled, 0=disabled)|
| `SetFlex(flex)`               | `Void(Double)`           | Set flex factor for layout               |
| `SetPosition(x, y)`          | `Void(Integer, Integer)` | Set position in pixels                   |
| `SetSize(width, height)`      | `Void(Integer, Integer)` | Set fixed size in pixels                 |
| `SetTooltip(text)`            | `Void(String)`           | Set tooltip text for this widget         |
| `SetTooltipRich(title, body)` | `Void(String, String)`   | Set rich tooltip with title and body     |
| `SetVisible(visible)`         | `Void(Integer)`          | Set visibility (1=visible, 0=hidden)     |
| `WasClicked()`                | `Integer()`              | 1 if widget was clicked this frame       |
| `WasDropped()`                | `Integer()`              | 1 if something was dropped this frame    |

### Example

```basic
DIM button AS Viper.GUI.Button
button = NEW Viper.GUI.Button(root, "Submit")

' Configure
button.SetSize(120, 32)
button.SetPosition(50, 100)

' State checks in game loop
IF button.IsHovered = 1 THEN
    ' Show hover effect
END IF

IF button.WasClicked = 1 THEN
    ' Handle click
END IF
```

```zia
// Zia
var btn = Button.New(root, "Submit");
btn.SetSize(120, 32);
btn.SetPosition(50, 100);

if btn.IsHovered() == 1 { /* hover effect */ }
if btn.WasClicked() == 1 { /* handle click */ }
```

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

```zia
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

```zia
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

```zia
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

```basic
DIM nameInput AS Viper.GUI.TextInput
nameInput = NEW Viper.GUI.TextInput(root)
nameInput.SetPlaceholder("Enter your name...")
nameInput.SetSize(200, 28)

' Get value
DIM name AS STRING
name = nameInput.GetText()
```

```zia
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

| Method                | Signature       | Description              |
|-----------------------|-----------------|--------------------------|
| `IsChecked()`         | `Boolean()`     | Get checked state        |
| `SetChecked(checked)` | `Void(Integer)` | Set checked state        |
| `SetText(text)`       | `Void(String)`  | Set label text           |

```basic
DIM rememberMe AS Viper.GUI.Checkbox
rememberMe = NEW Viper.GUI.Checkbox(root, "Remember me")

IF rememberMe.IsChecked() = 1 THEN
    SaveCredentials()
END IF
```

```zia
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

```zia
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

```zia
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

```zia
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

| Method                     | Signature          | Description              |
|----------------------------|--------------------|--------------------------|
| `SetValue(value)`          | `Void(Double)`     | Set progress (0.0-1.0)   |

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

```zia
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

```zia
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

```zia
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

```zia
// Zia
var preview = Image.New(root);
preview.SetSize(200, 200);
preview.SetScaleMode(1);  // Fit

var pixels = Pixels.LoadBmp("photo.bmp");
preview.SetPixels(pixels, pixels.get_Width(), pixels.get_Height());
```

---

## Container Widgets

### ScrollView

Scrollable container for content larger than the viewport.

**Constructor:** `NEW Viper.GUI.ScrollView(parent)`

| Method                         | Signature                  | Description                    |
|--------------------------------|----------------------------|--------------------------------|
| `SetScroll(x, y)`              | `Void(Double, Double)`     | Set scroll position            |
| `SetContentSize(w, h)`         | `Void(Double, Double)`     | Set content size (0=auto)      |

### Example

```zia
// Zia
var scroll = ScrollView.New(root);
scroll.SetSize(400, 300);
scroll.SetContentSize(800.0, 600.0);
scroll.SetScroll(0.0, 0.0);
```

---

### SplitPane

Two-pane split view with draggable divider.

**Constructor:** `NEW Viper.GUI.SplitPane(parent, horizontal)`

| Property | Type   | Access | Description            |
|----------|--------|--------|------------------------|
| `First`  | Object | Read   | First pane widget      |
| `Second` | Object | Read   | Second pane widget     |

| Method                     | Signature          | Description                              |
|----------------------------|--------------------|------------------------------------------|
| `SetPosition(pos)`         | `Void(Double)`     | Set split position (0.0-1.0)             |

```basic
DIM split AS Viper.GUI.SplitPane
split = NEW Viper.GUI.SplitPane(root, 1)  ' Horizontal
split.SetPosition(0.3)  ' 30% / 70%

' Add content to panes
DIM leftPane AS Object = split.GetFirst()
DIM rightPane AS Object = split.GetSecond()
```

```zia
// Zia
var split = SplitPane.New(root, 1);  // Horizontal
split.SetPosition(0.3);  // 30% / 70%

var leftPane = split.get_First();
var rightPane = split.get_Second();
```

---

### TabBar

Tab strip for switching between views.

**Constructor:** `NEW Viper.GUI.TabBar(parent)`

### Properties

| Property   | Type    | Access | Description                          |
|------------|---------|--------|--------------------------------------|
| `TabCount` | Integer | Read   | Number of tabs                       |

### Methods

| Method                   | Signature                 | Description                              |
|--------------------------|---------------------------|------------------------------------------|
| `AddTab(title, closable)` | `Object(String, Integer)` | Add tab, returns tab handle             |
| `GetActive()`            | `Object()`                | Get active tab handle                    |
| `GetActiveIndex()`       | `Integer()`               | Get active tab index                     |
| `GetCloseClickedIndex()` | `Integer()`               | Index of tab whose close was clicked     |
| `GetTabAt(index)`        | `Object(Integer)`         | Get tab handle by index                  |
| `RemoveTab(tab)`         | `Void(Object)`            | Remove a tab                             |
| `SetActive(tab)`         | `Void(Object)`            | Set active tab                           |
| `SetAutoClose(enabled)`  | `Void(Integer)`           | Enable/disable auto-close on close click |
| `WasChanged()`           | `Integer()`               | 1 if active tab changed this frame       |
| `WasCloseClicked()`      | `Integer()`               | 1 if a close button was clicked          |

### Tab Methods

Tab handles returned by `AddTab()` support these methods:

| Method                    | Signature          | Description                    |
|---------------------------|--------------------|--------------------------------|
| `tab.SetTitle(title)`     | `Void(String)`     | Set tab title                  |
| `tab.SetModified(flag)`   | `Void(Integer)`    | Set modified indicator         |

```basic
DIM tabs AS Viper.GUI.TabBar
tabs = NEW Viper.GUI.TabBar(root)

DIM tab1 AS Object = tabs.AddTab("File.txt", 1)
DIM tab2 AS Object = tabs.AddTab("Config.ini", 1)

tab1.SetModified(1)  ' Show unsaved indicator

IF tabs.WasChanged() THEN
    DIM active AS Object = tabs.GetActive()
    ' Handle tab switch
END IF

IF tabs.WasCloseClicked() THEN
    DIM idx AS INTEGER = tabs.GetCloseClickedIndex()
    DIM tab AS Object = tabs.GetTabAt(idx)
    tabs.RemoveTab(tab)
END IF
```

```zia
// Zia
var tabs = TabBar.New(root);
var tab1 = tabs.AddTab("File.txt", 1);
var tab2 = tabs.AddTab("Config.ini", 1);
tab1.SetModified(1);  // Unsaved indicator

if tabs.WasChanged() == 1 {
    var active = tabs.GetActive();
}
if tabs.WasCloseClicked() == 1 {
    var idx = tabs.GetCloseClickedIndex();
    tabs.RemoveTab(tabs.GetTabAt(idx));
}
```

---

### TreeView

Hierarchical tree view with expandable nodes.

**Constructor:** `NEW Viper.GUI.TreeView(parent)`

### Methods

| Method                  | Signature                | Description                       |
|-------------------------|--------------------------|-----------------------------------|
| `AddNode(parent, text)` | `Object(Object, String)` | Add node, returns handle          |
| `Clear()`               | `Void()`                 | Remove all nodes                  |
| `Collapse(node)`        | `Void(Object)`           | Collapse a node                   |
| `Expand(node)`          | `Void(Object)`           | Expand a node                     |
| `GetSelected()`         | `Object()`               | Get selected node handle          |
| `RemoveNode(node)`      | `Void(Object)`           | Remove node and children          |
| `Select(node)`          | `Void(Object)`           | Select a node                     |
| `SetFont(font, size)`   | `Void(Font, Double)`     | Set font                          |
| `WasSelectionChanged()` | `Integer()`              | 1 if selection changed this frame |

### Node Methods

Node handles returned by `AddNode()` support these methods:

| Method                    | Signature          | Description                    |
|---------------------------|--------------------|--------------------------------|
| `node.GetText()`          | `String()`         | Get node display text          |
| `node.SetData(data)`      | `Void(String)`     | Set node user data             |
| `node.GetData()`          | `String()`         | Get node user data             |
| `node.IsExpanded()`       | `Integer()`        | 1 if node is expanded          |

```zia
// Zia example
var tree = TreeView.New(root);
tree.SetSize(250, 400);

var rootNode = tree.AddNode(null, "Project");
var srcNode = tree.AddNode(rootNode, "src");
tree.AddNode(srcNode, "main.zia");
tree.AddNode(srcNode, "utils.zia");
var docsNode = tree.AddNode(rootNode, "docs");
tree.AddNode(docsNode, "README.md");

tree.Expand(rootNode);
```

```basic
DIM tree AS Viper.GUI.TreeView
tree = NEW Viper.GUI.TreeView(root)
tree.SetSize(250, 400)

' Build tree structure
DIM rootNode AS Object = tree.AddNode(NULL, "Project")
DIM srcNode AS Object = tree.AddNode(rootNode, "src")
tree.AddNode(srcNode, "main.bas")
tree.AddNode(srcNode, "utils.bas")
DIM docsNode AS Object = tree.AddNode(rootNode, "docs")
tree.AddNode(docsNode, "README.md")

' Expand root by default
tree.Expand(rootNode)
```

---

## Advanced Widgets

### CodeEditor

Full-featured code editor with syntax highlighting.

**Constructor:** `NEW Viper.GUI.CodeEditor(parent)`

| Property     | Type    | Access | Description                    |
|--------------|---------|--------|--------------------------------|
| `Text`       | String  | Read   | Editor content                 |
| `LineCount`  | Integer | Read   | Number of lines                |
| `CursorLine` | Integer | Read   | Current cursor line            |
| `CursorCol`  | Integer | Read   | Current cursor column          |

| Method                                     | Signature                        | Description                              |
|--------------------------------------------|----------------------------------|------------------------------------------|
| `AddCursor(line, col)`                     | `Void(Integer, Integer)`         | Add an additional cursor                 |
| `AddFoldRegion(startLine, endLine)`        | `Void(Integer, Integer)`         | Add a foldable region                    |
| `AddHighlight(line, col, endLine, endCol)` | `Void(Int, Int, Int, Int)`       | Add text highlight region                |
| `ClearCursors()`                           | `Void()`                         | Remove extra cursors (keep primary)      |
| `ClearGutterIcons(type)`                   | `Void(Integer)`                  | Clear gutter icons of given type         |
| `ClearHighlights()`                        | `Void()`                         | Remove all highlights                    |
| `ClearModified()`                          | `Void()`                         | Clear modified flag                      |
| `Copy()`                                   | `Integer()`                      | Copy selection to clipboard              |
| `CursorHasSelection(cursorIdx)`            | `Integer(Integer)`               | Check if cursor has selection            |
| `Cut()`                                    | `Integer()`                      | Cut selection to clipboard               |
| `Fold(line)`                               | `Void(Integer)`                  | Fold region at line                      |
| `GetCursorCount()`                         | `Integer()`                      | Get number of cursors (multi-cursor)     |
| `GetGutterClickLine()`                     | `Integer()`                      | Get line of gutter click                 |
| `IsFolded(line)`                           | `Integer(Integer)`               | Check if line is folded                  |
| `IsModified()`                             | `Boolean()`                      | Check if modified                        |
| `Paste()`                                  | `Integer()`                      | Paste from clipboard                     |
| `Redo()`                                   | `Void()`                         | Redo last undone edit                    |
| `ScrollToLine(line)`                       | `Void(Integer)`                  | Scroll to line                           |
| `SelectAll()`                              | `Void()`                         | Select all text                          |
| `SetCursor(line, col)`                     | `Void(Integer, Integer)`         | Set cursor position                      |
| `SetFont(font, size)`                      | `Void(Font, Double)`             | Set monospace font                       |
| `SetGutterIcon(line, icon, type)`          | `Void(Integer, Object, Integer)` | Set gutter icon on a line                |
| `SetLanguage(name)`                        | `Void(String)`                   | Set syntax highlighting language         |
| `SetShowLineNumbers(show)`                 | `Void(Integer)`                  | Show/hide line numbers                   |
| `SetText(text)`                            | `Void(String)`                   | Set editor content                       |
| `SetTokenColor(tokenType, color)`          | `Void(Integer, Integer)`         | Set color for a token type               |
| `Unfold(line)`                             | `Void(Integer)`                  | Unfold region at line                    |
| `Undo()`                                   | `Void()`                         | Undo last edit                           |
| `WasGutterClicked()`                       | `Integer()`                      | 1 if gutter was clicked                  |

```basic
DIM editor AS Viper.GUI.CodeEditor
editor = NEW Viper.GUI.CodeEditor(root)
editor.SetSize(600, 400)

' Load file
DIM content AS STRING
content = Viper.IO.File.ReadAllText("main.bas")
editor.SetText(content)

' Save on Ctrl+S
IF Viper.Input.Keyboard.Held(341) AND Viper.Input.Keyboard.Pressed(83) THEN
    IF editor.IsModified() = 1 THEN
        Viper.IO.File.WriteAllText("main.bas", editor.GetText())
        editor.ClearModified()
    END IF
END IF
```

```zia
// Zia
var editor = CodeEditor.New(root);
editor.SetSize(600, 400);
editor.SetLanguage("zia");
editor.SetShowLineNumbers(1);
editor.SetText("func main() {\n    Say(\"hello\");\n}");

if editor.IsModified() == 1 {
    editor.ClearModified();
}
```

---

## Application Components

### MenuBar

Application menu bar widget.

**Constructor:** `NEW Viper.GUI.MenuBar(parent)`

| Method                | Signature          | Description                    |
|-----------------------|--------------------|--------------------------------|
| `AddMenu(title)`      | `Object(String)`   | Add menu, returns menu handle  |

### Menu

Dropdown menu (returned by `MenuBar.AddMenu()`).

| Method                  | Signature          | Description                    |
|-------------------------|--------------------|--------------------------------|
| `AddItem(text)`         | `Object(String)`   | Add item, returns item handle  |
| `AddSeparator()`        | `Object()`         | Add separator                  |
| `AddSubmenu(title)`     | `Object(String)`   | Add submenu, returns menu      |

### MenuItem

Menu item (returned by `Menu.AddItem()`).

| Method                  | Signature       | Description                    |
|-------------------------|-----------------|--------------------------------|
| `IsChecked()`           | `Boolean()`     | Check if checked               |
| `IsEnabled()`           | `Integer()`     | Check if enabled               |
| `SetChecked(checked)`   | `Void(Integer)` | Set check mark                 |
| `SetEnabled(enabled)`   | `Void(Integer)` | Enable/disable item            |
| `SetShortcut(shortcut)` | `Void(String)`  | Set keyboard shortcut display  |
| `SetText(text)`         | `Void(String)`  | Set item text                  |
| `WasClicked()`          | `Integer()`     | 1 if item was clicked          |

### Example

```zia
// Zia
var menubar = MenuBar.New(root);
var fileMenu = menubar.AddMenu("File");
var newItem = fileMenu.AddItem("New");
newItem.SetShortcut("Ctrl+N");
var openItem = fileMenu.AddItem("Open");
openItem.SetShortcut("Ctrl+O");
fileMenu.AddSeparator();
var exitItem = fileMenu.AddItem("Exit");

if newItem.WasClicked() == 1 { /* new file */ }
```

---

### Toolbar

Application toolbar widget.

**Constructor:** `NEW Viper.GUI.Toolbar(parent)`

| Method                              | Signature                      | Description                              |
|-------------------------------------|--------------------------------|------------------------------------------|
| `AddButton(id, icon)`               | `Object(String, String)`       | Add icon button, returns item handle     |
| `AddButtonWithText(id, icon, text)` | `Object(String,String,String)` | Add button with text and icon            |
| `AddSeparator()`                    | `Object()`                     | Add separator                            |
| `SetIconSize(size)`                 | `Void(Integer)`                | Set icon size in pixels                  |
| `SetStyle(style)`                   | `Void(Integer)`                | Set toolbar style                        |
| `SetVisible(visible)`               | `Void(Integer)`                | Show/hide toolbar                        |

### ToolbarItem

Toolbar button (returned by `Toolbar.AddButton()`).

| Method                | Signature       | Description                    |
|-----------------------|-----------------|--------------------------------|
| `IsEnabled()`         | `Integer()`     | Check if enabled               |
| `SetEnabled(enabled)` | `Void(Integer)` | Enable/disable button          |
| `SetIcon(icon)`       | `Void(String)`  | Change icon                    |
| `SetTooltip(text)`    | `Void(String)`  | Set tooltip text               |
| `WasClicked()`        | `Integer()`     | 1 if button was clicked        |

### Example

```zia
// Zia
var toolbar = Toolbar.New(root);
toolbar.SetIconSize(24);
var newBtn = toolbar.AddButton("new", "📄");
var saveBtn = toolbar.AddButtonWithText("save", "💾", "Save");
toolbar.AddSeparator();

newBtn.SetTooltip("New File");
if saveBtn.WasClicked() == 1 { /* save */ }
```

---

### StatusBar

Application status bar widget.

**Constructor:** `NEW Viper.GUI.StatusBar(parent)`

| Method                     | Signature                 | Description                              |
|----------------------------|---------------------------|------------------------------------------|
| `AddText(text, alignment)` | `Object(String, Integer)` | Add text item, returns item handle       |
| `SetCenterText(text)`      | `Void(String)`            | Set center-aligned text                  |
| `SetLeftText(text)`        | `Void(String)`            | Set left-aligned text                    |
| `SetRightText(text)`       | `Void(String)`            | Set right-aligned text                   |
| `SetVisible(visible)`      | `Void(Integer)`           | Show/hide status bar                     |

### StatusBarItem

Status bar item (returned by `StatusBar.AddText()`).

| Method                | Signature       | Description                    |
|-----------------------|-----------------|--------------------------------|
| `SetText(text)`       | `Void(String)`  | Set item text                  |
| `SetTooltip(text)`    | `Void(String)`  | Set tooltip text               |
| `SetVisible(visible)` | `Void(Integer)` | Show/hide item                 |
| `WasClicked()`        | `Integer()`     | 1 if item was clicked          |

### Example

```zia
// Zia
var statusbar = StatusBar.New(root);
statusbar.SetLeftText("Ready");
statusbar.SetRightText("Ln 1, Col 1");

var item = statusbar.AddText("UTF-8", 2);  // Right-aligned
item.SetTooltip("File encoding");
```

---

### ContextMenu

Right-click context menu.

**Constructor:** `NEW Viper.GUI.ContextMenu()`

| Method                                | Signature                | Description                              |
|---------------------------------------|--------------------------|------------------------------------------|
| `AddItem(text)`                       | `Object(String)`         | Add menu item                            |
| `AddItemWithShortcut(text, shortcut)` | `Object(String, String)` | Add item with shortcut display           |
| `AddSeparator()`                      | `Object()`               | Add separator                            |
| `Clear()`                             | `Void()`                 | Remove all items                         |
| `Hide()`                              | `Void()`                 | Hide the menu                            |
| `IsVisible()`                         | `Integer()`              | 1 if menu is visible                     |
| `Show(x, y)`                          | `Void(Integer, Integer)` | Show context menu at position            |

### Example

```zia
// Zia
var ctx = ContextMenu.New();
var cutItem = ctx.AddItem("Cut");
var copyItem = ctx.AddItemWithShortcut("Copy", "Ctrl+C");
ctx.AddSeparator();
var pasteItem = ctx.AddItem("Paste");

// Show on right-click
ctx.Show(mouseX, mouseY);
if copyItem.WasClicked() == 1 { /* copy */ }
```

---

### FindBar

Find and replace bar for text searching.

**Constructor:** `NEW Viper.GUI.FindBar(parent)`

| Method                      | Signature       | Description                              |
|-----------------------------|-----------------|------------------------------------------|
| `FindNext()`                | `Integer()`     | Find next match; returns 1 if found      |
| `FindPrev()`                | `Integer()`     | Find previous match; returns 1 if found  |
| `Focus()`                   | `Void()`        | Focus the find input                     |
| `GetCurrentMatch()`         | `Integer()`     | Get current match index                  |
| `GetFindText()`             | `String()`      | Get search text                          |
| `GetMatchCount()`           | `Integer()`     | Get total match count                    |
| `GetReplaceText()`          | `String()`      | Get replacement text                     |
| `IsCaseSensitive()`         | `Integer()`     | Check if case sensitive                  |
| `IsRegex()`                 | `Integer()`     | Check if regex mode                      |
| `IsReplaceMode()`           | `Integer()`     | Check if replace mode                    |
| `IsVisible()`               | `Integer()`     | Check if visible                         |
| `IsWholeWord()`             | `Integer()`     | Check if whole word mode                 |
| `Replace()`                 | `Integer()`     | Replace current match                    |
| `ReplaceAll()`              | `Integer()`     | Replace all matches; returns count       |
| `SetCaseSensitive(enabled)` | `Void(Integer)` | Enable/disable case sensitivity          |
| `SetFindText(text)`         | `Void(String)`  | Set search text                          |
| `SetRegex(enabled)`         | `Void(Integer)` | Enable/disable regex matching            |
| `SetReplaceMode(enabled)`   | `Void(Integer)` | Enable/disable replace mode              |
| `SetReplaceText(text)`      | `Void(String)`  | Set replacement text                     |
| `SetVisible(visible)`       | `Void(Integer)` | Show/hide find bar                       |
| `SetWholeWord(enabled)`     | `Void(Integer)` | Enable/disable whole word matching       |

### Example

```zia
// Zia
var findbar = FindBar.New(root);
findbar.SetCaseSensitive(0);
findbar.SetWholeWord(0);
findbar.SetReplaceMode(1);
findbar.SetFindText("search");
findbar.SetReplaceText("replace");

var count = findbar.ReplaceAll();
SayInt(findbar.GetMatchCount());
```

---

### CommandPalette

Command palette for searchable command execution.

**Constructor:** `NEW Viper.GUI.CommandPalette(parent)`

| Method                                          | Signature                    | Description                              |
|-------------------------------------------------|------------------------------|------------------------------------------|
| `AddCommand(id, title, category)`               | `Void(String,String,String)` | Register a command                       |
| `AddCommandWithShortcut(id,title,cat,shortcut)` | `Void(Str,Str,Str,Str)`      | Register command with shortcut           |
| `Clear()`                                       | `Void()`                     | Remove all commands                      |
| `GetSelected()`                                 | `String()`                   | Get selected command ID                  |
| `Hide()`                                        | `Void()`                     | Hide the palette                         |
| `IsVisible()`                                   | `Integer()`                  | 1 if palette is visible                  |
| `RemoveCommand(id)`                             | `Void(String)`               | Remove a command                         |
| `SetPlaceholder(text)`                          | `Void(String)`               | Set search placeholder text              |
| `Show()`                                        | `Void()`                     | Show the palette                         |
| `WasSelected()`                                 | `Integer()`                  | 1 if a command was selected              |

### Example

```zia
// Zia
var palette = CommandPalette.New(root);
palette.SetPlaceholder("Type a command...");
palette.AddCommand("new", "New File", "File");
palette.AddCommandWithShortcut("save", "Save", "File", "Ctrl+S");
palette.AddCommand("theme", "Toggle Theme", "View");

if palette.WasSelected() == 1 {
    var cmd = palette.GetSelected();
    // Handle command by ID
}
```

---

### Breadcrumb

Breadcrumb navigation widget.

**Constructor:** `NEW Viper.GUI.Breadcrumb(parent)`

| Method                     | Signature              | Description                              |
|----------------------------|------------------------|------------------------------------------|
| `AddItem(text, data)`      | `Void(String, String)` | Add breadcrumb item with data            |
| `Clear()`                  | `Void()`               | Remove all items                         |
| `GetClickedData()`         | `String()`             | Data of clicked item                     |
| `GetClickedIndex()`        | `Integer()`            | Index of clicked item                    |
| `SetItems(items)`          | `Void(String)`         | Set items from delimited string          |
| `SetMaxItems(max)`         | `Void(Integer)`        | Set max visible items                    |
| `SetPath(path, separator)` | `Void(String, String)` | Set path with separator (e.g. "/")       |
| `SetSeparator(sep)`        | `Void(String)`         | Set separator character                  |
| `WasItemClicked()`         | `Integer()`            | 1 if an item was clicked                 |

### Example

```zia
// Zia
var bc = Breadcrumb.New(root);
bc.SetPath("src/gui/widgets", "/");
bc.SetMaxItems(5);

if bc.WasItemClicked() == 1 {
    var idx = bc.GetClickedIndex();
    var data = bc.GetClickedData();
}
```

---

### Minimap

Code minimap widget (pairs with CodeEditor).

**Constructor:** `NEW Viper.GUI.Minimap(parent)`

| Method                         | Signature           | Description                              |
|--------------------------------|---------------------|------------------------------------------|
| `AddMarker(line, color, type)` | `Void(Int,Int,Int)` | Add line marker                          |
| `BindEditor(editor)`           | `Void(Object)`      | Bind to a CodeEditor                     |
| `ClearMarkers()`               | `Void()`            | Remove all markers                       |
| `GetWidth()`                   | `Integer()`         | Get minimap width                        |
| `RemoveMarkers(type)`          | `Void(Integer)`     | Remove markers by type                   |
| `SetScale(scale)`              | `Void(Double)`      | Set minimap scale                        |
| `SetShowSlider(show)`          | `Void(Integer)`     | Show/hide scroll slider                  |
| `SetWidth(width)`              | `Void(Integer)`     | Set minimap width                        |
| `UnbindEditor()`               | `Void()`            | Unbind from editor                       |

### Example

```zia
// Zia
var minimap = Minimap.New(root);
minimap.BindEditor(editor);
minimap.SetWidth(80);
minimap.SetScale(0.5);
minimap.SetShowSlider(1);
minimap.AddMarker(10, 0xFFFF0000, 1);  // Error marker
```

---

## Dialogs

### MessageBox

System message dialog boxes (static methods).

| Method                                       | Signature                 | Description                   |
|----------------------------------------------|---------------------------|-------------------------------|
| `Viper.GUI.MessageBox.Confirm(title, text)`  | `Integer(String, String)` | Show confirmation dialog      |
| `Viper.GUI.MessageBox.Error(title, text)`    | `Void(String, String)`    | Show error dialog             |
| `Viper.GUI.MessageBox.Info(title, text)`     | `Void(String, String)`    | Show info dialog              |
| `Viper.GUI.MessageBox.Question(title, text)` | `Integer(String, String)` | Show yes/no question dialog   |
| `Viper.GUI.MessageBox.Warning(title, text)`  | `Void(String, String)`    | Show warning dialog           |

### Example

```zia
// Zia
MessageBox.Info("Info", "Operation completed");
MessageBox.Warning("Warning", "File not saved");
MessageBox.Error("Error", "Failed to open file");

var answer = MessageBox.Question("Confirm", "Delete this file?");
if answer == 1 { /* user said yes */ }
```

---

### FileDialog

Native file dialog boxes (static methods).

| Method                                          | Signature                        | Description                              |
|-------------------------------------------------|----------------------------------|------------------------------------------|
| `Viper.GUI.FileDialog.Open(title, filter, dir)` | `String(String,String,String)`  | Open file dialog; returns path           |
| `Viper.GUI.FileDialog.OpenMultiple(title, filter, dir)` | `Object(String,String,String)` | Open multi-file dialog; returns seq |
| `Viper.GUI.FileDialog.Save(title, filter, dir, defaultName)` | `String(Str,Str,Str,Str)` | Save file dialog; returns path     |
| `Viper.GUI.FileDialog.SelectFolder(title, dir)` | `String(String, String)`       | Folder selection dialog                  |

### Example

```zia
// Zia
var path = FileDialog.Open("Open File", "*.txt;*.zia", "/home");
if path != "" { /* open file at path */ }

var savePath = FileDialog.Save("Save As", "*.txt", "/home", "untitled.txt");
var folder = FileDialog.SelectFolder("Choose Folder", "/home");
```

---

## Notifications

### Toast

Toast notification system.

| Method                                  | Signature                  | Description                              |
|-----------------------------------------|----------------------------|------------------------------------------|
| `Viper.GUI.Toast.DismissAll()`          | `Void()`                   | Dismiss all toasts                       |
| `Viper.GUI.Toast.Error(text)`           | `Void(String)`             | Show error toast                         |
| `Viper.GUI.Toast.Info(text)`            | `Void(String)`             | Show info toast                          |
| `Viper.GUI.Toast.New(text, type, dur)`  | `Object(String, Int, Int)` | Create custom toast; returns handle      |
| `Viper.GUI.Toast.SetMaxVisible(count)`  | `Void(Integer)`            | Set max visible toasts                   |
| `Viper.GUI.Toast.SetPosition(position)` | `Void(Integer)`            | Set toast position                       |
| `Viper.GUI.Toast.Success(text)`         | `Void(String)`             | Show success toast                       |
| `Viper.GUI.Toast.Warning(text)`         | `Void(String)`             | Show warning toast                       |

Toast handle methods:

| Method                     | Signature       | Description                    |
|----------------------------|-----------------|--------------------------------|
| `toast.Dismiss()`          | `Void()`        | Dismiss this toast             |
| `toast.SetAction(text)`    | `Void(String)`  | Add action button              |
| `toast.WasActionClicked()` | `Integer()`     | 1 if action was clicked        |
| `toast.WasDismissed()`     | `Integer()`     | 1 if toast was dismissed       |

### Example

```zia
// Zia
Toast.Info("File saved successfully");
Toast.Success("Build completed");
Toast.Warning("Disk space low");
Toast.Error("Connection failed");

Toast.SetPosition(2);    // Bottom-right
Toast.SetMaxVisible(3);

var t = Toast.New("Custom notification", 0, 5000);
t.SetAction("Undo");
if t.WasActionClicked() == 1 { /* undo action */ }
```

---

### Tooltip

Tooltip display system (static methods).

| Method                                          | Signature                | Description                    |
|-------------------------------------------------|--------------------------|--------------------------------|
| `Viper.GUI.Tooltip.Hide()`                      | `Void()`                 | Hide tooltip                   |
| `Viper.GUI.Tooltip.SetDelay(ms)`               | `Void(Integer)`          | Set show delay in ms           |
| `Viper.GUI.Tooltip.Show(text, x, y)`           | `Void(String, Int, Int)` | Show tooltip at position       |
| `Viper.GUI.Tooltip.ShowRich(title, body, x, y)` | `Void(Str,Str,Int,Int)` | Show rich tooltip              |

### Example

```zia
// Zia
Tooltip.SetDelay(500);
Tooltip.Show("Click to save", 100, 50);
Tooltip.ShowRich("Save File", "Saves the current document to disk", 100, 50);
Tooltip.Hide();
```

---

## Utilities

### Clipboard

System clipboard access (static methods).

| Method                              | Signature      | Description                    |
|-------------------------------------|----------------|--------------------------------|
| `Viper.GUI.Clipboard.Clear()`       | `Void()`       | Clear clipboard                |
| `Viper.GUI.Clipboard.GetText()`     | `String()`     | Get text from clipboard        |
| `Viper.GUI.Clipboard.HasText()`     | `Integer()`    | 1 if clipboard has text        |
| `Viper.GUI.Clipboard.SetText(text)` | `Void(String)` | Copy text to clipboard         |

### Example

```zia
// Zia
Clipboard.SetText("Hello, World!");
if Clipboard.HasText() == 1 {
    var text = Clipboard.GetText();
    Say(text);
}
Clipboard.Clear();
```

---

### Shortcuts

Keyboard shortcut registration system (static methods).

| Method                                          | Signature               | Description                              |
|-------------------------------------------------|-------------------------|------------------------------------------|
| `Viper.GUI.Shortcuts.Clear()`                   | `Void()`                | Remove all shortcuts                     |
| `Viper.GUI.Shortcuts.GetGlobalEnabled()`        | `Integer()`             | Check if shortcuts globally enabled      |
| `Viper.GUI.Shortcuts.GetTriggered()`            | `String()`              | Get ID of triggered shortcut             |
| `Viper.GUI.Shortcuts.IsEnabled(id)`             | `Integer(String)`       | Check if shortcut is enabled             |
| `Viper.GUI.Shortcuts.Register(id, keys, desc)`  | `Void(Str, Str, Str)`  | Register a shortcut                      |
| `Viper.GUI.Shortcuts.SetEnabled(id, enabled)`   | `Void(String, Integer)` | Enable/disable shortcut                  |
| `Viper.GUI.Shortcuts.SetGlobalEnabled(enabled)` | `Void(Integer)`         | Enable/disable all shortcuts             |
| `Viper.GUI.Shortcuts.Unregister(id)`            | `Void(String)`          | Unregister a shortcut                    |
| `Viper.GUI.Shortcuts.WasTriggered(id)`          | `Integer(String)`       | 1 if shortcut was triggered this frame   |

### Example

```zia
// Zia
Shortcuts.Register("save", "Ctrl+S", "Save file");
Shortcuts.Register("quit", "Ctrl+Q", "Quit application");

if Shortcuts.WasTriggered("save") == 1 {
    // Handle save
}
var triggered = Shortcuts.GetTriggered();
```

---

### Cursor

Mouse cursor control (static methods).

| Method                                | Signature        | Description                    |
|---------------------------------------|------------------|--------------------------------|
| `Viper.GUI.Cursor.Set(cursorType)`    | `Void(Integer)`  | Set cursor type                |
| `Viper.GUI.Cursor.Reset()`            | `Void()`         | Reset to default cursor        |
| `Viper.GUI.Cursor.SetVisible(visible)` | `Void(Integer)` | Show/hide cursor               |

### Example

```zia
// Zia
Cursor.Set(2);       // Hand cursor
Cursor.SetVisible(1);
Cursor.Reset();       // Default cursor
```

---

## Themes

### Setting Theme

```basic
' Use dark theme (default)
Viper.GUI.Theme.SetDark()

' Use light theme
Viper.GUI.Theme.SetLight()
```

```zia
// Zia
bind Viper.GUI.Theme as Theme;

Theme.SetDark();   // Dark theme (default)
Theme.SetLight();  // Light theme
```

---

## Complete Example (Zia)

```zia
module NoteEditor;

bind Viper.GUI.App as App;
bind Viper.GUI.VBox as VBox;
bind Viper.GUI.HBox as HBox;
bind Viper.GUI.Label as Label;
bind Viper.GUI.Button as Button;
bind Viper.GUI.CodeEditor as CodeEditor;
bind Viper.Fmt as Fmt;

func start() {
    var app = App.New("Note Editor", 800, 600);
    var root = app.get_Root();

    // Layout
    var vbox = VBox.New();
    vbox.SetSpacing(5.0);
    vbox.SetPadding(10.0);

    // Toolbar
    var toolbar = HBox.New();
    toolbar.SetSpacing(5.0);
    var newBtn = Button.New(toolbar, "New");
    newBtn.SetSize(60, 28);
    var saveBtn = Button.New(toolbar, "Save");
    saveBtn.SetStyle(1);
    saveBtn.SetSize(60, 28);

    // Editor
    var editor = CodeEditor.New(root);
    editor.SetSize(780, 500);

    // Status bar
    var status = Label.New(root, "Ready");

    while app.get_ShouldClose() == 0 {
        app.Poll();

        if newBtn.WasClicked() == 1 {
            editor.SetText("");
            status.SetText("New file");
        }
        if saveBtn.WasClicked() == 1 {
            editor.ClearModified();
            status.SetText("Saved!");
        }
        if editor.IsModified() == 1 {
            status.SetText("Modified - " + Fmt.Int(editor.get_LineCount()) + " lines");
        }

        app.Render();
    }
}
```

## Complete Example

```basic
' Complete GUI Application Example
DIM app AS Viper.GUI.App
app = NEW Viper.GUI.App("Note Editor", 800, 600)

' Load font
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("consola.ttf")
app.SetFont(font, 12)

' Create layout
DIM vbox AS Viper.GUI.VBox
vbox = NEW Viper.GUI.VBox()
vbox.SetSpacing(5)
vbox.SetPadding(10)
app.Root.AddChild(vbox)

' Toolbar
DIM toolbar AS Viper.GUI.HBox
toolbar = NEW Viper.GUI.HBox()
toolbar.SetSpacing(5)
vbox.AddChild(toolbar)

DIM newBtn AS Viper.GUI.Button
newBtn = NEW Viper.GUI.Button(toolbar, "New")
newBtn.SetSize(60, 28)

DIM openBtn AS Viper.GUI.Button
openBtn = NEW Viper.GUI.Button(toolbar, "Open")
openBtn.SetSize(60, 28)

DIM saveBtn AS Viper.GUI.Button
saveBtn = NEW Viper.GUI.Button(toolbar, "Save")
saveBtn.SetStyle(1)  ' Primary
saveBtn.SetSize(60, 28)

' Editor
DIM editor AS Viper.GUI.CodeEditor
editor = NEW Viper.GUI.CodeEditor(vbox)
editor.SetSize(780, 500)

' Status bar
DIM status AS Viper.GUI.Label
status = NEW Viper.GUI.Label(vbox, "Ready")

' Main loop
DO WHILE app.ShouldClose = 0
    app.Poll()

    ' Handle buttons
    IF newBtn.WasClicked = 1 THEN
        editor.SetText("")
        status.SetText("New file")
    END IF

    IF saveBtn.WasClicked = 1 THEN
        ' Save logic here
        editor.ClearModified()
        status.SetText("Saved!")
    END IF

    ' Update status
    IF editor.IsModified() = 1 THEN
        status.SetText("Modified - " + STR$(editor.GetLineCount()) + " lines")
    END IF

    app.Render()
LOOP
```

---

## See Also

- [Graphics](graphics.md) - Low-level graphics with Canvas
- [Input](input.md) - Keyboard and mouse input
- [Audio](audio.md) - Sound effects and music
