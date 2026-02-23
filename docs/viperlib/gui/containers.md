# Containers & Advanced
> ScrollView, SplitPane, TabBar, TreeView, CodeEditor, Minimap

**Part of [Viper Runtime Library](../README.md) › [GUI Widgets](README.md)**

---

## Container Widgets

### ScrollView

Scrollable container for content larger than the viewport.

**Constructor:** `NEW Viper.GUI.ScrollView(parent)`

| Method                         | Signature                  | Description                    |
|--------------------------------|----------------------------|--------------------------------|
| `GetScrollX()`                 | `Double()`                 | Get current horizontal scroll offset |
| `GetScrollY()`                 | `Double()`                 | Get current vertical scroll offset   |
| `SetContentSize(w, h)`         | `Void(Double, Double)`     | Set content size (0=auto)      |
| `SetScroll(x, y)`              | `Void(Double, Double)`     | Set scroll position            |

### Example

```rust
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
| `GetPosition()`            | `Double()`         | Get current split position (0.0-1.0)     |
| `SetPosition(pos)`         | `Void(Double)`     | Set split position (0.0-1.0)             |

```basic
DIM split AS Viper.GUI.SplitPane
split = NEW Viper.GUI.SplitPane(root, 1)  ' Horizontal
split.SetPosition(0.3)  ' 30% / 70%

' Add content to panes
DIM leftPane AS Object = split.GetFirst()
DIM rightPane AS Object = split.GetSecond()
```

```rust
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

```rust
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

```rust
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

| Property     | Type    | Access | Description                                                    |
|--------------|---------|--------|----------------------------------------------------------------|
| `Text`       | String  | Read   | Editor content                                                 |
| `LineCount`  | Integer | Read   | Number of lines                                                |
| `CursorLine` | Integer | Read   | Current cursor line                                            |
| `CursorCol`  | Integer | Read   | Current cursor column                                          |
| `WordWrap`   | Boolean | R/W    | Enable visual word-wrap — long lines are split at the viewport edge (display-only; does not affect underlying text) |

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
| `GetSelectedText()`                        | `String()`                       | Get currently selected text (empty string if no selection) |
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

```rust
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


## See Also

- [Core & Application](core.md)
- [Layout Widgets](layout.md)
- [Basic Widgets](widgets.md)
- [Application Components](application.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
