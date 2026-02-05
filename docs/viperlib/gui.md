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
| `ShouldClose` | Integer | Read   | 1 if window close requested, 0 otherwise |
| `Root`        | Object  | Read   | Root widget container                 |

### Methods

| Method                 | Signature                    | Description                              |
|------------------------|------------------------------|------------------------------------------|
| `Poll()`               | `Void()`                     | Poll events and update widget states     |
| `Render()`             | `Void()`                     | Render all widgets to the window         |
| `SetFont(font, size)`  | `Void(Font, Double)`         | Set default font for all widgets         |

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

| Method                    | Signature                         | Description                              |
|---------------------------|-----------------------------------|------------------------------------------|
| `SetVisible(visible)`     | `Void(Integer)`                   | Set visibility (1=visible, 0=hidden)     |
| `SetEnabled(enabled)`     | `Void(Integer)`                   | Set enabled state (1=enabled, 0=disabled)|
| `SetSize(width, height)`  | `Void(Integer, Integer)`          | Set fixed size in pixels                 |
| `SetPosition(x, y)`       | `Void(Integer, Integer)`          | Set position in pixels                   |
| `IsHovered`               | `Integer`                         | 1 if mouse is over widget                |
| `IsPressed`               | `Integer`                         | 1 if widget is pressed                   |
| `IsFocused`               | `Integer`                         | 1 if widget has keyboard focus           |
| `WasClicked`              | `Integer`                         | 1 if widget was clicked this frame       |

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
| `Text`   | String | R/W    | Current input text     |

| Method                       | Signature          | Description              |
|------------------------------|--------------------|--------------------------|
| `SetText(text)`              | `Void(String)`     | Set input text           |
| `GetText()`                  | `String()`         | Get input text           |
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

| Property  | Type    | Access | Description               |
|-----------|---------|--------|---------------------------|
| `Checked` | Integer | R/W    | 1 if checked, 0 if not    |

| Method                   | Signature          | Description              |
|--------------------------|--------------------|--------------------------|
| `SetChecked(checked)`    | `Void(Integer)`    | Set checked state        |
| `IsChecked()`            | `Integer()`        | Get checked state        |
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
| `Value`  | Double | R/W    | Current slider value   |

| Method                     | Signature                  | Description                    |
|----------------------------|----------------------------|--------------------------------|
| `SetValue(value)`          | `Void(Double)`             | Set current value              |
| `GetValue()`               | `Double()`                 | Get current value              |
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
| `Value`  | Double | R/W    | Current spinner value  |

| Method                     | Signature                  | Description                    |
|----------------------------|----------------------------|--------------------------------|
| `SetValue(value)`          | `Void(Double)`             | Set current value              |
| `GetValue()`               | `Double()`                 | Get current value              |
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
| `Value`  | Double | R/W    | Progress value (0.0 to 1.0)  |

| Method                     | Signature          | Description              |
|----------------------------|--------------------|--------------------------|
| `SetValue(value)`          | `Void(Double)`     | Set progress (0.0-1.0)   |
| `GetValue()`               | `Double()`         | Get progress value       |

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

| Property   | Type    | Access | Description                |
|------------|---------|--------|----------------------------|
| `Selected` | Integer | R/W    | Selected item index (-1=none) |

| Method                     | Signature          | Description                     |
|----------------------------|--------------------|---------------------------------|
| `AddItem(text)`            | `Integer(String)`  | Add item, returns index         |
| `RemoveItem(index)`        | `Void(Integer)`    | Remove item by index            |
| `Clear()`                  | `Void()`           | Remove all items                |
| `SetSelected(index)`       | `Void(Integer)`    | Set selected index              |
| `GetSelected()`            | `Integer()`        | Get selected index              |
| `GetSelectedText()`        | `String()`         | Get selected item text          |
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
| `SelectedIndex`      | Integer | R/W    | Index of selected item (-1 if none)      |
| `WasSelectionChanged`| Integer | Read   | 1 if selection changed this frame        |

### Methods

| Method                     | Signature               | Description                     |
|----------------------------|-------------------------|---------------------------------|
| `AddItem(text)`            | `Object(String)`        | Add item, returns item handle   |
| `RemoveItem(item)`         | `Void(Object)`          | Remove item                     |
| `Clear()`                  | `Void()`                | Remove all items                |
| `Select(item)`             | `Void(Object)`          | Select item (NULL to deselect)  |
| `GetSelected()`            | `Object()`              | Get selected item handle        |
| `SelectIndex(index)`       | `Void(Integer)`         | Select item by index            |
| `GetCount()`               | `Integer()`             | Get number of items             |
| `GetSelectedIndex()`       | `Integer()`             | Get selected item index         |
| `SetFont(font, size)`      | `Void(Font, Double)`    | Set font for list items         |

### Item Methods

ListBox items support additional properties for flexible data management:

| Method                     | Signature               | Description                     |
|----------------------------|-------------------------|---------------------------------|
| `item.GetText()`           | `String()`              | Get item display text           |
| `item.SetText(text)`       | `Void(String)`          | Set item display text           |
| `item.GetData()`           | `String()`              | Get item user data              |
| `item.SetData(data)`       | `Void(String)`          | Set item user data              |

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
item1.SetData("/home/user/documents/document.txt")
item2.SetData("/home/user/pictures/image.png")
item3.SetData("/home/user/data/data.csv")

' Get item count
PRINT "Total items: "; fileList.GetCount()

' Select by index
fileList.SelectIndex(0)  ' Select first item

' Check for selection changes in main loop
DO WHILE app.ShouldClose = 0
    app.Poll()

    IF fileList.WasSelectionChanged = 1 THEN
        DIM selected AS Object = fileList.GetSelected()
        IF selected <> NULL THEN
            PRINT "Selected: "; selected.GetText()
            PRINT "Path: "; selected.GetData()
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
        item.SetData(path + "/" + files(i))  ' Store full path
    NEXT i
END SUB

' Handle double-click to open file
IF fileList.WasSelectionChanged = 1 THEN
    DIM idx AS INTEGER = fileList.GetSelectedIndex()
    IF idx >= 0 THEN
        DIM item AS Object = fileList.GetSelected()
        OpenFile(item.GetData())
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

| Method                     | Signature          | Description                              |
|----------------------------|--------------------|------------------------------------------|
| `SetPosition(pos)`         | `Void(Double)`     | Set split position (0.0-1.0)             |
| `GetFirst()`               | `Object()`         | Get first pane widget                    |
| `GetSecond()`              | `Object()`         | Get second pane widget                   |

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

| Method                         | Signature                      | Description                    |
|--------------------------------|--------------------------------|--------------------------------|
| `AddTab(title, closable)`      | `Object(String, Integer)`      | Add tab, returns tab handle    |
| `RemoveTab(tab)`               | `Void(Object)`                 | Remove a tab                   |
| `SetActive(tab)`               | `Void(Object)`                 | Set active tab                 |
| `SetTitle(tab, title)`         | `Void(Object, String)`         | Set tab title                  |
| `SetModified(tab, modified)`   | `Void(Object, Integer)`        | Set modified indicator         |

```basic
DIM tabs AS Viper.GUI.TabBar
tabs = NEW Viper.GUI.TabBar(root)

DIM tab1 AS Object = tabs.AddTab("File.txt", 1)
DIM tab2 AS Object = tabs.AddTab("Config.ini", 1)

tabs.SetModified(tab1, 1)  ' Show unsaved indicator
```

---

### TreeView

Hierarchical tree view with expandable nodes.

**Constructor:** `NEW Viper.GUI.TreeView(parent)`

| Method                         | Signature                          | Description                    |
|--------------------------------|------------------------------------|--------------------------------|
| `AddNode(parent, text)`        | `Object(Object, String)`           | Add node, returns handle       |
| `RemoveNode(node)`             | `Void(Object)`                     | Remove node and children       |
| `Clear()`                      | `Void()`                           | Remove all nodes               |
| `Expand(node)`                 | `Void(Object)`                     | Expand a node                  |
| `Collapse(node)`               | `Void(Object)`                     | Collapse a node                |
| `Select(node)`                 | `Void(Object)`                     | Select a node                  |
| `SetFont(font, size)`          | `Void(Font, Double)`               | Set font                       |

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
| `Text`       | String  | R/W    | Editor content                 |
| `LineCount`  | Integer | Read   | Number of lines                |
| `Modified`   | Integer | Read   | 1 if content modified          |

| Method                         | Signature                      | Description                    |
|--------------------------------|--------------------------------|--------------------------------|
| `SetText(text)`                | `Void(String)`                 | Set editor content             |
| `GetText()`                    | `String()`                     | Get editor content             |
| `SetCursor(line, col)`         | `Void(Integer, Integer)`       | Set cursor position            |
| `ScrollToLine(line)`           | `Void(Integer)`                | Scroll to line                 |
| `GetLineCount()`               | `Integer()`                    | Get number of lines            |
| `IsModified()`                 | `Integer()`                    | Check if modified              |
| `ClearModified()`              | `Void()`                       | Clear modified flag            |
| `SetFont(font, size)`          | `Void(Font, Double)`           | Set monospace font             |

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
