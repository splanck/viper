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

| Method                          | Signature                    | Description                              |
|---------------------------------|------------------------------|------------------------------------------|
| `Poll()`                        | `Void()`                     | Poll events and update widget states     |
| `Render()`                      | `Void()`                     | Render all widgets to the window         |
| `SetFont(font, size)`           | `Void(Font, Double)`         | Set default font for all widgets         |
| `WasFileDropped()`              | `Integer()`                  | 1 if a file was dropped on the window    |
| `GetDroppedFileCount()`         | `Integer()`                  | Number of files dropped                  |
| `GetDroppedFile(index)`         | `String(Integer)`            | Get path of dropped file by index        |

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

---

## Common Widget Functions

All widgets share these common functions:

| Method                              | Signature                         | Description                              |
|-------------------------------------|-----------------------------------|------------------------------------------|
| `SetVisible(visible)`               | `Void(Integer)`                   | Set visibility (1=visible, 0=hidden)     |
| `SetEnabled(enabled)`               | `Void(Integer)`                   | Set enabled state (1=enabled, 0=disabled)|
| `SetSize(width, height)`            | `Void(Integer, Integer)`          | Set fixed size in pixels                 |
| `SetPosition(x, y)`                 | `Void(Integer, Integer)`          | Set position in pixels                   |
| `SetFlex(flex)`                     | `Void(Double)`                    | Set flex factor for layout               |
| `AddChild(child)`                   | `Void(Object)`                    | Add a child widget                       |
| `IsHovered()`                       | `Integer()`                       | 1 if mouse is over widget                |
| `IsPressed()`                       | `Integer()`                       | 1 if widget is pressed                   |
| `IsFocused()`                       | `Integer()`                       | 1 if widget has keyboard focus           |
| `WasClicked()`                      | `Integer()`                       | 1 if widget was clicked this frame       |
| `SetTooltip(text)`                  | `Void(String)`                    | Set tooltip text for this widget         |
| `SetTooltipRich(title, body)`       | `Void(String, String)`            | Set rich tooltip with title and body     |
| `ClearTooltip()`                    | `Void()`                          | Remove tooltip from widget               |
| `SetDraggable(enabled)`             | `Void(Integer)`                   | Enable/disable drag source               |
| `SetDragData(type, data)`           | `Void(String, String)`            | Set drag data type and payload           |
| `IsBeingDragged()`                  | `Integer()`                       | 1 if widget is being dragged             |
| `SetDropTarget(enabled)`            | `Void(Integer)`                   | Enable/disable drop target               |
| `SetAcceptedDropTypes(types)`       | `Void(String)`                    | Set accepted drop type(s)                |
| `IsDragOver()`                      | `Integer()`                       | 1 if a drag is hovering over widget      |
| `WasDropped()`                      | `Integer()`                       | 1 if something was dropped this frame    |
| `GetDropType()`                     | `String()`                        | Get the type of the dropped data         |
| `GetDropData()`                     | `String()`                        | Get the dropped data payload             |

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

---

### Checkbox

Toggle checkbox with label.

**Constructor:** `NEW Viper.GUI.Checkbox(parent, text)`

| Method                   | Signature          | Description              |
|--------------------------|--------------------|--------------------------|
| `SetChecked(checked)`    | `Void(Integer)`    | Set checked state        |
| `IsChecked()`            | `Boolean()`        | Get checked state        |
| `SetText(text)`          | `Void(String)`     | Set label text           |

```basic
DIM rememberMe AS Viper.GUI.Checkbox
rememberMe = NEW Viper.GUI.Checkbox(root, "Remember me")

IF rememberMe.IsChecked() = 1 THEN
    SaveCredentials()
END IF
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

---

### Dropdown

Dropdown selection list.

**Constructor:** `NEW Viper.GUI.Dropdown(parent)`

| Property       | Type    | Access | Description                |
|----------------|---------|--------|----------------------------|
| `Selected`     | Integer | Read   | Selected item index (-1=none) |
| `SelectedText` | String  | Read   | Selected item text            |

| Method                     | Signature          | Description                     |
|----------------------------|--------------------|---------------------------------|
| `AddItem(text)`            | `Integer(String)`  | Add item, returns index         |
| `RemoveItem(index)`        | `Void(Integer)`    | Remove item by index            |
| `Clear()`                  | `Void()`           | Remove all items                |
| `SetSelected(index)`       | `Void(Integer)`    | Set selected index              |
| `SetPlaceholder(text)`     | `Void(String)`     | Set placeholder text            |

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

| Method                          | Signature               | Description                     |
|---------------------------------|-------------------------|---------------------------------|
| `AddItem(text)`                 | `Object(String)`        | Add item, returns item handle   |
| `RemoveItem(item)`              | `Void(Object)`          | Remove item                     |
| `Clear()`                       | `Void()`                | Remove all items                |
| `Select(item)`                  | `Void(Object)`          | Select item (NULL to deselect)  |
| `SelectIndex(index)`            | `Void(Integer)`         | Select item by index            |
| `WasSelectionChanged()`         | `Integer()`             | 1 if selection changed this frame |
| `ItemGetText(item)`             | `String(Object)`        | Get item display text           |
| `ItemSetText(item, text)`       | `Void(Object, String)`  | Set item display text           |
| `ItemGetData(item)`             | `String(Object)`        | Get item user data              |
| `ItemSetData(item, data)`       | `Void(Object, String)`  | Set item user data              |
| `SetFont(font, size)`           | `Void(Font, Double)`    | Set font for list items         |

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

---

## Container Widgets

### ScrollView

Scrollable container for content larger than the viewport.

**Constructor:** `NEW Viper.GUI.ScrollView(parent)`

| Method                         | Signature                  | Description                    |
|--------------------------------|----------------------------|--------------------------------|
| `SetScroll(x, y)`              | `Void(Double, Double)`     | Set scroll position            |
| `SetContentSize(w, h)`         | `Void(Double, Double)`     | Set content size (0=auto)      |

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

---

### TabBar

Tab strip for switching between views.

**Constructor:** `NEW Viper.GUI.TabBar(parent)`

### Properties

| Property   | Type    | Access | Description                          |
|------------|---------|--------|--------------------------------------|
| `TabCount` | Integer | Read   | Number of tabs                       |

### Methods

| Method                         | Signature                      | Description                              |
|--------------------------------|--------------------------------|------------------------------------------|
| `AddTab(title, closable)`      | `Object(String, Integer)`      | Add tab, returns tab handle              |
| `RemoveTab(tab)`               | `Void(Object)`                 | Remove a tab                             |
| `SetActive(tab)`               | `Void(Object)`                 | Set active tab                           |
| `GetActive()`                  | `Object()`                     | Get active tab handle                    |
| `GetActiveIndex()`             | `Integer()`                    | Get active tab index                     |
| `WasChanged()`                 | `Integer()`                    | 1 if active tab changed this frame       |
| `WasCloseClicked()`            | `Integer()`                    | 1 if a close button was clicked          |
| `GetCloseClickedIndex()`       | `Integer()`                    | Index of tab whose close was clicked     |
| `GetTabAt(index)`              | `Object(Integer)`              | Get tab handle by index                  |
| `SetAutoClose(enabled)`        | `Void(Integer)`                | Enable/disable auto-close on close click |

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

---

### TreeView

Hierarchical tree view with expandable nodes.

**Constructor:** `NEW Viper.GUI.TreeView(parent)`

### Methods

| Method                         | Signature                          | Description                    |
|--------------------------------|------------------------------------|--------------------------------|
| `AddNode(parent, text)`        | `Object(Object, String)`           | Add node, returns handle       |
| `RemoveNode(node)`             | `Void(Object)`                     | Remove node and children       |
| `Clear()`                      | `Void()`                           | Remove all nodes               |
| `Expand(node)`                 | `Void(Object)`                     | Expand a node                  |
| `Collapse(node)`               | `Void(Object)`                     | Collapse a node                |
| `Select(node)`                 | `Void(Object)`                     | Select a node                  |
| `GetSelected()`                | `Object()`                         | Get selected node handle       |
| `WasSelectionChanged()`        | `Integer()`                        | 1 if selection changed this frame |
| `SetFont(font, size)`          | `Void(Font, Double)`               | Set font                       |

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

| Method                                    | Signature                          | Description                              |
|-------------------------------------------|------------------------------------|------------------------------------------|
| `SetText(text)`                           | `Void(String)`                     | Set editor content                       |
| `SetCursor(line, col)`                    | `Void(Integer, Integer)`           | Set cursor position                      |
| `ScrollToLine(line)`                      | `Void(Integer)`                    | Scroll to line                           |
| `IsModified()`                            | `Boolean()`                        | Check if modified                        |
| `ClearModified()`                         | `Void()`                           | Clear modified flag                      |
| `SetFont(font, size)`                     | `Void(Font, Double)`               | Set monospace font                       |
| `SetLanguage(name)`                       | `Void(String)`                     | Set syntax highlighting language         |
| `SetTokenColor(tokenType, color)`         | `Void(Integer, Integer)`           | Set color for a token type               |
| `AddHighlight(line, col, endLine, endCol)`| `Void(Int, Int, Int, Int)`         | Add text highlight region                |
| `ClearHighlights()`                       | `Void()`                           | Remove all highlights                    |
| `SetShowLineNumbers(show)`                | `Void(Integer)`                    | Show/hide line numbers                   |
| `SetGutterIcon(line, icon, type)`         | `Void(Integer, Object, Integer)`   | Set gutter icon on a line                |
| `ClearGutterIcons(type)`                  | `Void(Integer)`                    | Clear gutter icons of given type         |
| `WasGutterClicked()`                      | `Integer()`                        | 1 if gutter was clicked                  |
| `GetGutterClickLine()`                    | `Integer()`                        | Get line of gutter click                 |
| `AddFoldRegion(startLine, endLine)`       | `Void(Integer, Integer)`           | Add a foldable region                    |
| `Fold(line)`                              | `Void(Integer)`                    | Fold region at line                      |
| `Unfold(line)`                            | `Void(Integer)`                    | Unfold region at line                    |
| `IsFolded(line)`                          | `Integer(Integer)`                 | Check if line is folded                  |
| `GetCursorCount()`                        | `Integer()`                        | Get number of cursors (multi-cursor)     |
| `AddCursor(line, col)`                    | `Void(Integer, Integer)`           | Add an additional cursor                 |
| `ClearCursors()`                          | `Void()`                           | Remove extra cursors (keep primary)      |
| `CursorHasSelection(cursorIdx)`           | `Integer(Integer)`                 | Check if cursor has selection            |
| `Undo()`                                  | `Void()`                           | Undo last edit                           |
| `Redo()`                                  | `Void()`                           | Redo last undone edit                    |
| `Copy()`                                  | `Integer()`                        | Copy selection to clipboard              |
| `Cut()`                                   | `Integer()`                        | Cut selection to clipboard               |
| `Paste()`                                 | `Integer()`                        | Paste from clipboard                     |
| `SelectAll()`                             | `Void()`                           | Select all text                          |

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

| Method                    | Signature          | Description                    |
|---------------------------|--------------------|--------------------------------|
| `SetText(text)`           | `Void(String)`     | Set item text                  |
| `SetEnabled(enabled)`     | `Void(Integer)`    | Enable/disable item            |
| `IsEnabled()`             | `Integer()`        | Check if enabled               |
| `SetChecked(checked)`     | `Void(Integer)`    | Set check mark                 |
| `IsChecked()`             | `Boolean()`        | Check if checked               |
| `SetShortcut(shortcut)`   | `Void(String)`     | Set keyboard shortcut display  |
| `WasClicked()`            | `Integer()`        | 1 if item was clicked          |

---

### Toolbar

Application toolbar widget.

**Constructor:** `NEW Viper.GUI.Toolbar(parent)`

| Method                              | Signature                    | Description                              |
|-------------------------------------|------------------------------|------------------------------------------|
| `AddButton(id, icon)`              | `Object(String, String)`     | Add icon button, returns item handle     |
| `AddButtonWithText(id, icon, text)` | `Object(String,String,String)` | Add button with text and icon          |
| `AddSeparator()`                    | `Object()`                   | Add separator                            |
| `SetIconSize(size)`                 | `Void(Integer)`              | Set icon size in pixels                  |
| `SetStyle(style)`                   | `Void(Integer)`              | Set toolbar style                        |
| `SetVisible(visible)`               | `Void(Integer)`              | Show/hide toolbar                        |

### ToolbarItem

Toolbar button (returned by `Toolbar.AddButton()`).

| Method                    | Signature          | Description                    |
|---------------------------|--------------------|--------------------------------|
| `SetEnabled(enabled)`     | `Void(Integer)`    | Enable/disable button          |
| `IsEnabled()`             | `Integer()`        | Check if enabled               |
| `SetTooltip(text)`        | `Void(String)`     | Set tooltip text               |
| `SetIcon(icon)`           | `Void(String)`     | Change icon                    |
| `WasClicked()`            | `Integer()`        | 1 if button was clicked        |

---

### StatusBar

Application status bar widget.

**Constructor:** `NEW Viper.GUI.StatusBar(parent)`

| Method                       | Signature                  | Description                              |
|------------------------------|----------------------------|------------------------------------------|
| `SetLeftText(text)`          | `Void(String)`             | Set left-aligned text                    |
| `SetCenterText(text)`        | `Void(String)`             | Set center-aligned text                  |
| `SetRightText(text)`         | `Void(String)`             | Set right-aligned text                   |
| `AddText(text, alignment)`   | `Object(String, Integer)`  | Add text item, returns item handle       |
| `SetVisible(visible)`        | `Void(Integer)`            | Show/hide status bar                     |

### StatusBarItem

Status bar item (returned by `StatusBar.AddText()`).

| Method                    | Signature          | Description                    |
|---------------------------|--------------------|--------------------------------|
| `SetText(text)`           | `Void(String)`     | Set item text                  |
| `SetTooltip(text)`        | `Void(String)`     | Set tooltip text               |
| `SetVisible(visible)`     | `Void(Integer)`    | Show/hide item                 |
| `WasClicked()`            | `Integer()`        | 1 if item was clicked          |

---

### ContextMenu

Right-click context menu.

**Constructor:** `NEW Viper.GUI.ContextMenu()`

| Method                              | Signature                  | Description                              |
|-------------------------------------|----------------------------|------------------------------------------|
| `AddItem(text)`                     | `Object(String)`           | Add menu item                            |
| `AddItemWithShortcut(text, shortcut)` | `Object(String, String)` | Add item with shortcut display           |
| `AddSeparator()`                    | `Object()`                 | Add separator                            |
| `Show(x, y)`                        | `Void(Integer, Integer)`   | Show context menu at position            |
| `Hide()`                            | `Void()`                   | Hide the menu                            |
| `IsVisible()`                       | `Integer()`                | 1 if menu is visible                     |
| `Clear()`                           | `Void()`                   | Remove all items                         |

---

### FindBar

Find and replace bar for text searching.

**Constructor:** `NEW Viper.GUI.FindBar(parent)`

| Method                        | Signature          | Description                              |
|-------------------------------|--------------------|------------------------------------------|
| `SetFindText(text)`           | `Void(String)`     | Set search text                          |
| `GetFindText()`               | `String()`         | Get search text                          |
| `SetReplaceText(text)`        | `Void(String)`     | Set replacement text                     |
| `GetReplaceText()`            | `String()`         | Get replacement text                     |
| `SetCaseSensitive(enabled)`   | `Void(Integer)`    | Enable/disable case sensitivity          |
| `IsCaseSensitive()`           | `Integer()`        | Check if case sensitive                  |
| `SetWholeWord(enabled)`       | `Void(Integer)`    | Enable/disable whole word matching       |
| `IsWholeWord()`               | `Integer()`        | Check if whole word mode                 |
| `SetRegex(enabled)`           | `Void(Integer)`    | Enable/disable regex matching            |
| `IsRegex()`                   | `Integer()`        | Check if regex mode                      |
| `SetReplaceMode(enabled)`     | `Void(Integer)`    | Enable/disable replace mode              |
| `IsReplaceMode()`             | `Integer()`        | Check if replace mode                    |
| `SetVisible(visible)`         | `Void(Integer)`    | Show/hide find bar                       |
| `IsVisible()`                 | `Integer()`        | Check if visible                         |
| `FindNext()`                  | `Integer()`        | Find next match; returns 1 if found      |
| `FindPrev()`                  | `Integer()`        | Find previous match; returns 1 if found  |
| `Replace()`                   | `Integer()`        | Replace current match                    |
| `ReplaceAll()`                | `Integer()`        | Replace all matches; returns count       |
| `GetMatchCount()`             | `Integer()`        | Get total match count                    |
| `GetCurrentMatch()`           | `Integer()`        | Get current match index                  |
| `Focus()`                     | `Void()`           | Focus the find input                     |

---

### CommandPalette

Command palette for searchable command execution.

**Constructor:** `NEW Viper.GUI.CommandPalette(parent)`

| Method                               | Signature                    | Description                              |
|--------------------------------------|------------------------------|------------------------------------------|
| `AddCommand(id, title, category)`    | `Void(String,String,String)` | Register a command                       |
| `AddCommandWithShortcut(id,title,cat,shortcut)` | `Void(Str,Str,Str,Str)` | Register command with shortcut       |
| `RemoveCommand(id)`                  | `Void(String)`               | Remove a command                         |
| `Clear()`                            | `Void()`                     | Remove all commands                      |
| `Show()`                             | `Void()`                     | Show the palette                         |
| `Hide()`                             | `Void()`                     | Hide the palette                         |
| `IsVisible()`                        | `Integer()`                  | 1 if palette is visible                  |
| `SetPlaceholder(text)`               | `Void(String)`               | Set search placeholder text              |
| `GetSelected()`                      | `String()`                   | Get selected command ID                  |
| `WasSelected()`                      | `Integer()`                  | 1 if a command was selected              |

---

### Breadcrumb

Breadcrumb navigation widget.

**Constructor:** `NEW Viper.GUI.Breadcrumb(parent)`

| Method                       | Signature                  | Description                              |
|------------------------------|----------------------------|------------------------------------------|
| `SetPath(path, separator)`   | `Void(String, String)`     | Set path with separator (e.g. "/")       |
| `SetItems(items)`            | `Void(String)`             | Set items from delimited string          |
| `AddItem(text, data)`        | `Void(String, String)`     | Add breadcrumb item with data            |
| `Clear()`                    | `Void()`                   | Remove all items                         |
| `WasItemClicked()`           | `Integer()`                | 1 if an item was clicked                 |
| `GetClickedIndex()`          | `Integer()`                | Index of clicked item                    |
| `GetClickedData()`           | `String()`                 | Data of clicked item                     |
| `SetSeparator(sep)`          | `Void(String)`             | Set separator character                  |
| `SetMaxItems(max)`           | `Void(Integer)`            | Set max visible items                    |

---

### Minimap

Code minimap widget (pairs with CodeEditor).

**Constructor:** `NEW Viper.GUI.Minimap(parent)`

| Method                         | Signature              | Description                              |
|--------------------------------|------------------------|------------------------------------------|
| `BindEditor(editor)`           | `Void(Object)`         | Bind to a CodeEditor                     |
| `UnbindEditor()`               | `Void()`               | Unbind from editor                       |
| `SetWidth(width)`              | `Void(Integer)`        | Set minimap width                        |
| `GetWidth()`                   | `Integer()`            | Get minimap width                        |
| `SetScale(scale)`              | `Void(Double)`         | Set minimap scale                        |
| `SetShowSlider(show)`          | `Void(Integer)`        | Show/hide scroll slider                  |
| `AddMarker(line, color, type)` | `Void(Int,Int,Int)`    | Add line marker                          |
| `RemoveMarkers(type)`          | `Void(Integer)`        | Remove markers by type                   |
| `ClearMarkers()`               | `Void()`               | Remove all markers                       |

---

## Dialogs

### MessageBox

System message dialog boxes (static methods).

| Method                            | Signature                  | Description                              |
|-----------------------------------|----------------------------|------------------------------------------|
| `Viper.GUI.MessageBox.Info(title, text)`     | `Void(String, String)`    | Show info dialog              |
| `Viper.GUI.MessageBox.Warning(title, text)`  | `Void(String, String)`    | Show warning dialog           |
| `Viper.GUI.MessageBox.Error(title, text)`    | `Void(String, String)`    | Show error dialog             |
| `Viper.GUI.MessageBox.Question(title, text)` | `Integer(String, String)` | Show yes/no question dialog   |
| `Viper.GUI.MessageBox.Confirm(title, text)`  | `Integer(String, String)` | Show confirmation dialog      |

---

### FileDialog

Native file dialog boxes (static methods).

| Method                                          | Signature                        | Description                              |
|-------------------------------------------------|----------------------------------|------------------------------------------|
| `Viper.GUI.FileDialog.Open(title, filter, dir)` | `String(String,String,String)`  | Open file dialog; returns path           |
| `Viper.GUI.FileDialog.OpenMultiple(title, filter, dir)` | `Object(String,String,String)` | Open multi-file dialog; returns seq |
| `Viper.GUI.FileDialog.Save(title, filter, dir, defaultName)` | `String(Str,Str,Str,Str)` | Save file dialog; returns path     |
| `Viper.GUI.FileDialog.SelectFolder(title, dir)` | `String(String, String)`       | Folder selection dialog                  |

---

## Notifications

### Toast

Toast notification system.

| Method                                    | Signature                    | Description                              |
|-------------------------------------------|------------------------------|------------------------------------------|
| `Viper.GUI.Toast.Info(text)`              | `Void(String)`               | Show info toast                          |
| `Viper.GUI.Toast.Success(text)`           | `Void(String)`               | Show success toast                       |
| `Viper.GUI.Toast.Warning(text)`           | `Void(String)`               | Show warning toast                       |
| `Viper.GUI.Toast.Error(text)`             | `Void(String)`               | Show error toast                         |
| `Viper.GUI.Toast.New(text, type, dur)`    | `Object(String, Int, Int)`   | Create custom toast; returns handle      |
| `Viper.GUI.Toast.SetPosition(position)`   | `Void(Integer)`              | Set toast position                       |
| `Viper.GUI.Toast.SetMaxVisible(count)`    | `Void(Integer)`              | Set max visible toasts                   |
| `Viper.GUI.Toast.DismissAll()`            | `Void()`                     | Dismiss all toasts                       |

Toast handle methods:

| Method                     | Signature          | Description                    |
|----------------------------|--------------------|--------------------------------|
| `toast.SetAction(text)`    | `Void(String)`     | Add action button              |
| `toast.WasActionClicked()` | `Integer()`        | 1 if action was clicked        |
| `toast.WasDismissed()`     | `Integer()`        | 1 if toast was dismissed       |
| `toast.Dismiss()`          | `Void()`           | Dismiss this toast             |

---

### Tooltip

Tooltip display system (static methods).

| Method                                      | Signature                    | Description                    |
|---------------------------------------------|------------------------------|--------------------------------|
| `Viper.GUI.Tooltip.Show(text, x, y)`       | `Void(String, Int, Int)`     | Show tooltip at position       |
| `Viper.GUI.Tooltip.ShowRich(title, body, x, y)` | `Void(Str,Str,Int,Int)` | Show rich tooltip              |
| `Viper.GUI.Tooltip.Hide()`                 | `Void()`                     | Hide tooltip                   |
| `Viper.GUI.Tooltip.SetDelay(ms)`           | `Void(Integer)`              | Set show delay in ms           |

---

## Utilities

### Clipboard

System clipboard access (static methods).

| Method                              | Signature        | Description                    |
|-------------------------------------|------------------|--------------------------------|
| `Viper.GUI.Clipboard.SetText(text)` | `Void(String)`   | Copy text to clipboard         |
| `Viper.GUI.Clipboard.GetText()`     | `String()`       | Get text from clipboard        |
| `Viper.GUI.Clipboard.HasText()`     | `Integer()`      | 1 if clipboard has text        |
| `Viper.GUI.Clipboard.Clear()`       | `Void()`         | Clear clipboard                |

---

### Shortcuts

Keyboard shortcut registration system (static methods).

| Method                                           | Signature                    | Description                              |
|--------------------------------------------------|------------------------------|------------------------------------------|
| `Viper.GUI.Shortcuts.Register(id, keys, desc)`  | `Void(Str, Str, Str)`       | Register a shortcut                      |
| `Viper.GUI.Shortcuts.Unregister(id)`             | `Void(String)`               | Unregister a shortcut                    |
| `Viper.GUI.Shortcuts.Clear()`                    | `Void()`                     | Remove all shortcuts                     |
| `Viper.GUI.Shortcuts.IsEnabled(id)`              | `Integer(String)`            | Check if shortcut is enabled             |
| `Viper.GUI.Shortcuts.SetEnabled(id, enabled)`    | `Void(String, Integer)`      | Enable/disable shortcut                  |
| `Viper.GUI.Shortcuts.GetGlobalEnabled()`         | `Integer()`                  | Check if shortcuts globally enabled      |
| `Viper.GUI.Shortcuts.SetGlobalEnabled(enabled)`  | `Void(Integer)`              | Enable/disable all shortcuts             |
| `Viper.GUI.Shortcuts.WasTriggered(id)`           | `Integer(String)`            | 1 if shortcut was triggered this frame   |
| `Viper.GUI.Shortcuts.GetTriggered()`             | `String()`                   | Get ID of triggered shortcut             |

---

### Cursor

Mouse cursor control (static methods).

| Method                                | Signature        | Description                    |
|---------------------------------------|------------------|--------------------------------|
| `Viper.GUI.Cursor.Set(cursorType)`    | `Void(Integer)`  | Set cursor type                |
| `Viper.GUI.Cursor.Reset()`            | `Void()`         | Reset to default cursor        |
| `Viper.GUI.Cursor.SetVisible(visible)` | `Void(Integer)` | Show/hide cursor               |

---

## Themes

### Setting Theme

```basic
' Use dark theme (default)
Viper.GUI.Theme.SetDark()

' Use light theme
Viper.GUI.Theme.SetLight()
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
