# Application Components
> MenuBar, Toolbar, StatusBar, Dialogs, Notifications, Utilities, Themes

**Part of [Viper Runtime Library](../README.md) › [GUI Widgets](README.md)**

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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

> **Modal constraint:** Only one dialog may be active at a time. Calling a second dialog while the first is still open is a no-op — the first dialog remains active. Dismiss the active dialog before showing another.

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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
// Zia
Clipboard.SetText("Hello, World!");
if Clipboard.HasText() == 1 {
    var text = Clipboard.GetText();
    Say(text);
}
Clipboard.Clear();
```

---

### Container

Static utility for setting layout properties on any container widget (VBox, HBox, ScrollView, etc.).

**Type:** Static utility class

| Method                       | Signature              | Description                                      |
|------------------------------|------------------------|--------------------------------------------------|
| `Viper.GUI.Container.SetSpacing(widget, value)` | `Void(Object, Double)` | Set spacing between children of the container    |
| `Viper.GUI.Container.SetPadding(widget, value)` | `Void(Object, Double)` | Set internal padding around the container's edge |

### Notes

- These methods accept any layout container widget (VBox, HBox, or others) as the first argument.
- They are equivalent to calling `widget.SetSpacing(value)` / `widget.SetPadding(value)` directly on VBox or HBox instances, but are useful when the widget type is held as a generic `Object` reference.

### Zia Example

```rust
module ContainerDemo;

bind Viper.Terminal;
bind Viper.GUI.App as App;
bind Viper.GUI.VBox as VBox;
bind Viper.GUI.Container as Container;

func start() {
    var app = App.New("Container Demo", 400, 300);
    var root = app.get_Root();

    // Create a VBox and configure via the static utility
    var vbox = VBox.New();
    Container.SetSpacing(vbox, 12.0);
    Container.SetPadding(vbox, 16.0);
    root.AddChild(vbox);
}
```

### BASIC Example

```basic
DIM vbox AS OBJECT = NEW Viper.GUI.VBox()
app.Root.AddChild(vbox)

' Configure via static utility (useful when widget is an Object reference)
Viper.GUI.Container.SetSpacing(vbox, 10.0)
Viper.GUI.Container.SetPadding(vbox, 20.0)

' Equivalent direct call on a known VBox:
' vbox.SetSpacing(10)
' vbox.SetPadding(20)
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

```rust
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

```rust
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

```rust
// Zia
bind Viper.GUI.Theme as Theme;

Theme.SetDark();   // Dark theme (default)
Theme.SetLight();  // Light theme
```

---


## See Also

- [Core & Application](core.md)
- [Layout Widgets](layout.md)
- [Basic Widgets](widgets.md)
- [Containers & Advanced](containers.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
