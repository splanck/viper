---
status: active
audience: public
last-verified: 2026-07-14
---

# Application Components
> MenuBar, Toolbar, StatusBar, navigation aids, dialogs, notifications, utilities, themes, and VideoWidget

**Part of [Viper Runtime Library](../README.md) › [GUI Widgets](README.md)**

---

## Application Components

### MenuBar

Application menu bar widget.

Desktop menu dropdowns size themselves to the longest visible item/shortcut row, and a click outside the open dropdown closes it without activating the highlighted item.

**Constructor:** `NEW Viper.GUI.MenuBar(parent)`

| Method                | Signature          | Description                    |
|-----------------------|--------------------|--------------------------------|
| `AddMenu(title)`      | `Object(String)`   | Add menu, returns menu handle  |

### Menu

Dropdown menu (returned by `MenuBar.AddMenu()`).

Disabled top-level menus render in the disabled state and ignore mouse and keyboard open requests.
Menu and menu-item handles are owner-checked; removing an item through the wrong menu, or a menu through the wrong menubar, is ignored rather than corrupting either list.
Removed menu-item handles become inert, open/highlighted menu state is cleared when its item disappears, and stale accelerator entries are ignored instead of firing removed actions.
Menu-item shortcuts registered through `SetShortcut()` or add-with-shortcut runtime paths are rebuilt after menu mutations and dispatch from menu-bar key events as well as native macOS menus.
Menu and menu-item handles are runtime-managed and become inert after removal, `Clear()`, or menubar destruction; later method calls return empty/0 values or no-op safely.

| Method                  | Signature          | Description                    |
|-------------------------|--------------------|--------------------------------|
| `AddItem(text)`         | `Object(String)`   | Add item, returns item handle  |
| `AddSeparator()`        | `Object()`         | Add separator                  |
| `AddSubmenu(title)`     | `Object(String)`   | Add submenu, returns menu      |
| `IsEnabled()`           | `Boolean()`        | Check if menu is enabled       |
| `SetEnabled(enabled)`   | `Void(Boolean)`    | Enable/disable the menu title  |

### MenuItem

Menu item (returned by `Menu.AddItem()`).
Checkable state is explicit: `IsCheckable()` is false until `SetCheckable(true)` or `SetChecked(...)` makes the item a toggle. Disabling checkable state clears the checked mark.

| Method                  | Signature       | Description                    |
|-------------------------|-----------------|--------------------------------|
| `IsChecked()`           | `Boolean()`     | Check if checked               |
| `IsCheckable()`         | `Boolean()`     | Check if item supports checked state |
| `IsEnabled()`           | `Boolean()`     | Check if enabled               |
| `SetCheckable(enabled)` | `Void(Boolean)` | Enable/disable checked-state support |
| `SetChecked(checked)`   | `Void(Boolean)` | Set check mark                 |
| `SetEnabled(enabled)`   | `Void(Boolean)` | Enable/disable item            |
| `SetIcon(pixels)`       | `Void(Object)`  | Set image icon from a `Pixels` handle |
| `SetShortcut(shortcut)` | `Void(String)`  | Set keyboard shortcut display  |
| `SetText(text)`         | `Void(String)`  | Set item text                  |
| `WasClicked()`          | `Boolean()`     | True if item was clicked       |

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

if newItem.WasClicked() { /* create a new file */ }
```

---

### Toolbar

Application toolbar widget.

When the toolbar is narrower than its contents, hidden items are exposed through an automatic overflow popup rather than becoming inaccessible.
Flexible spacer items now consume the remaining strip width. Native dropdown toolbar items open an
attached menu directly; the current runtime `AddDropdown()` surface creates a plain dropdown item
without exposing a menu-attachment setter.
Runtime text and icon changes invalidate layout and overflow measurement immediately, so the strip repacks without waiting for an unrelated refresh.
Toolbar items now render pixel icons directly, keyboard focus is visible, and `Left` / `Right` / `Home` / `End` navigate the strip while `Enter` / `Space` activate the focused item or overflow button.
Named toolbar icon helpers map stable semantic names such as `save`, `run`, `explorer`, and `source-control` to the runtime's built-in vector icons without requiring asset files.
Items returned by runtime add methods can be removed by handle even when they do not have an internal string id.
Removed toolbar item handles become inert until the toolbar is destroyed, and removing a clicked item clears the runtime click cache so `WasClicked()` cannot report a stale click.

**Constructor:** `NEW Viper.GUI.Toolbar(parent)`

**Vertical constructor:** `Viper.GUI.Toolbar.NewVertical(parent)`

| Method                              | Signature                      | Description                              |
|-------------------------------------|--------------------------------|------------------------------------------|
| `AddButton(icon, tooltip)`          | `Object(String, String)`       | Add icon button, returns item handle     |
| `AddButtonWithText(icon, text, tooltip)` | `Object(String,String,String)` | Add button with text and optional icon   |
| `AddNamedButton(name, tooltip)`     | `Object(String, String)`       | Add button using a built-in semantic icon |
| `AddNamedButtonWithText(name, text, tooltip)` | `Object(String,String,String)` | Add text button using a built-in semantic icon |
| `AddNamedToggle(name, tooltip)`     | `Object(String, String)`       | Add toggle using a built-in semantic icon |
| `AddToggle(icon, tooltip)`          | `Object(String, String)`       | Add toggle using an icon path             |
| `AddSeparator()`                    | `Object()`                     | Add separator                            |
| `AddSpacer()`                       | `Object()`                     | Add a flexible spacer                    |
| `NewVertical(parent)`               | `Object(Object)`               | Create a vertical toolbar                |
| `GetIconSize()`                     | `Integer()`                    | Get icon-size preset (`0`, `1`, or `2`)  |
| `SetIconSize(size)`                 | `Void(Integer)`                | Set preset: `0`=16 px, `1`=24 px, `2`=32 px; other values clamp |
| `SetStyle(style)`                   | `Void(Integer)`                | `0` hides labels; `1` or `2` shows labels (icons remain); unknown values are ignored |
| `SetVisible(visible)`               | `Void(Boolean)`                | Show/hide toolbar                        |

### ToolbarItem

Toolbar button (returned by `Toolbar.AddButton()`).
Toolbar item handles are runtime-managed and become inert after `RemoveItem()` or toolbar destruction; later item method calls return empty/false values or no-op safely.

| Method                | Signature       | Description                    |
|-----------------------|-----------------|--------------------------------|
| `IsEnabled()`         | `Boolean()`     | Check if enabled               |
| `SetEnabled(enabled)` | `Void(Boolean)` | Enable/disable button          |
| `SetIcon(icon)`       | `Void(String)`  | Change icon                    |
| `SetIconPixels(pixels)` | `Void(Object)` | Change icon from a `Pixels` handle |
| `SetNamedIcon(name)`  | `Void(String)`  | Change icon to a built-in semantic icon |
| `SetText(text)`       | `Void(String)`  | Change button label            |
| `SetTooltip(text)`    | `Void(String)`  | Set tooltip text               |
| `SetToggled(value)`   | `Void(Boolean)` | Set a toggle item's state      |
| `IsToggled()`         | `Boolean()`     | Read a toggle item's state     |
| `WasClicked()`        | `Boolean()`     | True if button was clicked     |

### Example

```rust
// Zia
var toolbar = Toolbar.New(root);
toolbar.SetIconSize(1);  // Medium preset: 24 px
var newBtn = toolbar.AddNamedButton("new", "New File");
var saveBtn = toolbar.AddNamedButtonWithText("save", "Save", "Save current file");
toolbar.AddSeparator();

if saveBtn.WasClicked() { /* save */ }
```

---

### StatusBar

Application status bar widget.

Text, visibility, progress, and tooltip updates invalidate the owning bar immediately, so layout and paint stay in sync with runtime changes.
Removed status-bar item handles become inert until the status bar is destroyed, and removing a clicked item clears the runtime click cache.
Buttons added with `AddButton()` are wired into `StatusBarItem.WasClicked()` for edge-triggered polling.

**Constructor:** `NEW Viper.GUI.StatusBar(parent)`

| Method                     | Signature                 | Description                              |
|----------------------------|---------------------------|------------------------------------------|
| `AddText(text, alignment)` | `Object(String, Integer)` | Add text item, returns item handle       |
| `AddButton(text, alignment)` | `Object(String, Integer)` | Add clickable button item, returns handle |
| `SetCenterText(text)`      | `Void(String)`            | Set center-aligned text                  |
| `SetLeftText(text)`        | `Void(String)`            | Set left-aligned text                    |
| `SetRightText(text)`       | `Void(String)`            | Set right-aligned text                   |
| `SetVisible(visible)`      | `Void(Boolean)`           | Show/hide status bar                     |

### StatusBarItem

Status bar item (returned by `StatusBar.AddText()`).
Status-bar item handles are runtime-managed and become inert after removal, `Clear()`, or status-bar destruction; later item method calls return empty/0 values or no-op safely.

| Method                | Signature       | Description                    |
|-----------------------|-----------------|--------------------------------|
| `SetText(text)`       | `Void(String)`  | Set item text                  |
| `SetTooltip(text)`    | `Void(String)`  | Set tooltip text               |
| `WasClicked()`        | `Boolean()`     | True once after a button item was clicked |
| `SetVisible(visible)` | `Void(Boolean)` | Show/hide item                 |

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

Context menus now anchor in screen space for nested widgets, capture input while open, clamp to the host window, and reliably dismiss on outside click instead of letting clicks fall through to the underlying UI. Dismissing a submenu restores capture to its parent menu, callback payloads are tracked independently for selection and dismiss handlers, and destroyed menus are removed from the right-click registry.
Nested context submenus are owned by their parent menu item. Destroying the parent destroys attached submenus, while explicitly destroying a child submenu detaches the parent item so later parent cleanup remains safe.
`Clear()` dismisses any active submenu, resets hover/click state, and retires removed item handles so later item method calls are ignored safely.
Context menu, submenu, and menu-item handles are runtime-managed and become inert after `Clear()`, `Destroy()`, or parent-menu destruction; later method calls return empty/0 values or no-op safely.

**Constructor:** `NEW Viper.GUI.ContextMenu()`

| Method                                | Signature                | Description                              |
|---------------------------------------|--------------------------|------------------------------------------|
| `AddItem(text)`                       | `Object(String)`         | Add menu item                            |
| `AddItemWithShortcut(text, shortcut)` | `Object(String, String)` | Add item with shortcut display           |
| `AddSeparator()`                      | `Object()`               | Add separator and return its item handle |
| `AddSubmenu(title)`                   | `Object(String)`         | Add submenu and return its menu handle   |
| `Clear()`                             | `Void()`                 | Remove all items                         |
| `Hide()`                              | `Void()`                 | Hide the menu                            |
| `IsVisible()`                         | `Boolean()`              | True if menu is visible                  |
| `Show(x, y)`                          | `Void(Integer, Integer)` | Show at screen-space coordinates         |

### Example

```rust
// Zia
var ctx = ContextMenu.New();
var cutItem = ctx.AddItem("Cut");
var copyItem = ctx.AddItemWithShortcut("Copy", "Ctrl+C");
var separator = ctx.AddSeparator();
var pasteItem = ctx.AddItem("Paste");

// Show on right-click
ctx.Show(mouseX, mouseY);
if copyItem.WasClicked() { /* copy */ }
```

---

### FindBar

Find and replace bar for text searching.

`GetFindText()` and `GetReplaceText()` read the live text currently shown in the inputs, `Replace()` returns `0` when there is no active editor or no current match to replace, and pointer input now routes through the standard widget event pipeline so focus, hover, and click behavior match the rest of the toolkit.
`SetRegex(true)` enables regular-expression search in the bound `CodeEditor`; matches may have variable length and still honor case-sensitive and whole-word options. POSIX-style regular expressions are used on platforms that provide them, and Windows builds use the built-in regex subset for literals, `.`, character classes, `^`/`$`, and `*`/`+`/`?`.
If the parent widget destroys the underlying bar, the runtime wrapper disconnects from it and all later `FindBar` calls become no-ops. If the bound `CodeEditor` is destroyed, the bar unbinds itself before any later search or replace call. Boolean option getters reflect the live widget state, including user checkbox clicks and `Ctrl+H` replace-mode toggles.

**Constructor:** `NEW Viper.GUI.FindBar(parent)`

| Method                      | Signature       | Description                              |
|-----------------------------|-----------------|------------------------------------------|
| `FindNextOption()`          | `Option[Integer]()` | Find next match; returns the current 1-based match index |
| `FindPrevOption()`          | `Option[Integer]()` | Find previous match; returns the current 1-based match index |
| `FindNext()`                | `Integer()`     | Compatibility API: returns 1 if found, 0 otherwise |
| `FindPrev()`                | `Integer()`     | Compatibility API: returns 1 if found, 0 otherwise |
| `BindEditor(editor)`        | `Void(Object)`  | Bind the CodeEditor to search             |
| `UnbindEditor()`            | `Void()`        | Disconnect the bound editor               |
| `Destroy()`                 | `Void()`        | Destroy the find-bar wrapper/widget       |
| `Focus()`                   | `Void()`        | Focus the find input                     |
| `GetCurrentMatch()`         | `Integer()`     | Get current match as a 1-based index, or 0 when there is no match |
| `GetFindText()`             | `String()`      | Get search text                          |
| `GetMatchCount()`           | `Integer()`     | Get total match count                    |
| `GetReplaceText()`          | `String()`      | Get replacement text                     |
| `IsCaseSensitive()`         | `Boolean()`     | Check if case sensitive                  |
| `IsRegex()`                 | `Boolean()`     | Check if regex mode                      |
| `IsReplaceMode()`           | `Boolean()`     | Check if replace mode                    |
| `IsVisible()`               | `Boolean()`     | Check if visible                         |
| `IsWholeWord()`             | `Boolean()`     | Check if whole word mode                 |
| `Replace()`                 | `Integer()`     | Replace current match; returns 0 when nothing was replaced |
| `ReplaceAll()`              | `Integer()`     | Replace all matches; returns count       |
| `SetCaseSensitive(enabled)` | `Void(Boolean)` | Enable/disable case sensitivity          |
| `SetFindText(text)`         | `Void(String)`  | Set search text                          |
| `SetRegex(enabled)`         | `Void(Boolean)` | Enable/disable regex matching            |
| `SetReplaceMode(enabled)`   | `Void(Boolean)` | Enable/disable replace mode              |
| `SetReplaceText(text)`      | `Void(String)`  | Set replacement text                     |
| `SetVisible(visible)`       | `Void(Boolean)` | Show/hide find bar                       |
| `SetWholeWord(enabled)`     | `Void(Boolean)` | Enable/disable whole word matching       |

### Example

```rust
// Zia
var findbar = FindBar.New(root);
findbar.SetCaseSensitive(false);
findbar.SetWholeWord(false);
findbar.SetReplaceMode(true);
findbar.SetFindText("search");
findbar.SetReplaceText("replace");

var count = findbar.ReplaceAll();
findbar.GetMatchCount();
```

---

### CommandPalette

Command palette for searchable command execution.

The palette renders a full panel background, row highlight, and right-aligned shortcuts. Keyboard navigation and mouse-wheel scrolling now maintain a visible selection window, so moving past the first page of results keeps the active command onscreen.
Palette wrapper handles are finalized by the runtime collector and are disconnected when the owning app is destroyed, so explicit `Destroy()` and app shutdown use the same idempotent cleanup path.
The constructor accepts an app handle, app root, or widget parent. A widget parent resolves ownership from its app tree, so command shortcuts route through the same app that owns the palette.

**Constructor:** `NEW Viper.GUI.CommandPalette(parent)`

| Method                                          | Signature                    | Description                              |
|-------------------------------------------------|------------------------------|------------------------------------------|
| `AddCommand(id, title, category)`               | `Void(String,String,String)` | Register a command                       |
| `AddCommandWithShortcut(id,title,cat,shortcut)` | `Void(Str,Str,Str,Str)`      | Register command with shortcut           |
| `Clear()`                                       | `Void()`                     | Remove all commands                      |
| `Destroy()`                                     | `Void()`                     | Destroy the palette wrapper/widget       |
| `GetSelected()`                                 | `String()`                   | Get selected command ID                  |
| `GetQuery()`                                    | `String()`                   | Get current query text                   |
| `GetQueryGeneration()`                          | `Integer()`                  | Read the generation incremented by query changes |
| `Hide()`                                        | `Void()`                     | Hide the palette                         |
| `IsVisible()`                                   | `Boolean()`                  | True if palette is visible               |
| `RemoveCommand(id)`                             | `Void(String)`               | Remove a command                         |
| `SetClientFiltered(enabled)`                    | `Void(Boolean)`              | Let the host supply already-filtered results |
| `SetPlaceholder(text)`                          | `Void(String)`               | Set search placeholder text              |
| `SetQuery(text)`                                | `Void(String)`               | Replace current query text               |
| `Show()`                                        | `Void()`                     | Show the palette                         |
| `WasSelected()`                                 | `Boolean()`                  | True if a command was selected           |

### Example

```rust
// Zia
var palette = CommandPalette.New(root);
palette.SetPlaceholder("Type a command...");
palette.AddCommand("new", "New File", "File");
palette.AddCommandWithShortcut("save", "Save", "File", "Ctrl+S");
palette.AddCommand("theme", "Toggle Theme", "View");

if palette.WasSelected() {
    var cmd = palette.GetSelected();
    // Handle command by ID
}
```

---

### Command

A single UI command (action) bound to the surfaces that invoke it — a menu item, a toolbar button, a keyboard shortcut, and a command-palette entry — so they are polled and kept in sync from one place instead of by hand.

`SetShortcut` also registers the chord with the global `Viper.GUI.Shortcuts` registry under the command id, so the shortcut and the command share one identifier (call it after the app/window exists, like `Shortcuts.Register`). A **disabled command never reports invoked**, even via shortcut or palette. A bound widget's click is **consumed on read**, so once a widget is bound to a command, the command owns its click polling — stop calling `WasClicked()` on that widget directly.

Bound `MenuItem` / `ToolbarItem` handles are held as weak references and accessed through the runtime's self-guarding accessors, so a destroyed widget simply stops firing rather than dangling.

**Constructor:** `NEW Viper.GUI.Command(id, title)`

| Method                     | Signature             | Description                                                        |
|----------------------------|-----------------------|-------------------------------------------------------------------|
| `GetId()`                  | `String()`            | The command's stable id                                           |
| `GetTitle()`               | `String()`            | The command's display title                                       |
| `SetShortcut(keys)`        | `Void(String)`        | Set chord (e.g. `"Ctrl+B"`) and register it with `Shortcuts` under the id |
| `GetShortcut()`            | `String()`            | The shortcut chord (`""` if none)                                 |
| `SetEnabled(on)`           | `Void(Boolean)`       | Enable/disable; pushed to bound widgets                           |
| `IsEnabled()`              | `Boolean()`           | True if enabled                                                   |
| `SetCheckable(on)`         | `Void(Boolean)`       | Mark as a toggle; pushed to a bound menu item                     |
| `IsCheckable()`            | `Boolean()`           | True if checkable                                                 |
| `SetChecked(on)`           | `Void(Boolean)`       | Set checked/toggled state; pushed to bound widgets                |
| `IsChecked()`              | `Boolean()`           | True if checked                                                   |
| `BindMenuItem(item)`       | `Void(MenuItem)`      | Bind a menu item to read (clicks) and drive (enabled/checked)     |
| `BindToolbarItem(item)`    | `Void(ToolbarItem)`   | Bind a toolbar item to read (clicks) and drive (enabled/toggled)  |
| `Poll()`                   | `Boolean()`           | Standalone poll: read bound widgets + shortcut, push state, return true if invoked this frame |
| `WasInvoked()`             | `Boolean()`           | The invoked flag from the most recent command- or registry-level poll |
| `Snapshot()`               | `Map()`               | `{id, title, shortcut, enabled, checkable, checked, invoked}`     |

### CommandRegistry

Owns a set of commands and routes the menu item, toolbar button, keyboard shortcut, and palette selection for all of them in a single per-frame `Poll()`. Added commands are retained (the registry co-owns them); `Find` returns the command or `null`; `Poll` returns the id of an invoked command, or `""`.

**Constructor:** `NEW Viper.GUI.CommandRegistry()`

| Method                  | Signature           | Description                                                       |
|-------------------------|---------------------|-------------------------------------------------------------------|
| `Add(command)`          | `Void(Command)`     | Register (retain) a command; duplicates are ignored               |
| `Count()`               | `Integer()`         | Number of registered commands                                     |
| `FindOption(id)`        | `Option[Command](String)` | The command with that id, or `None`                         |
| `Find(id)`              | `Command(String)`   | Compatibility API: the command with that id, or `null`            |
| `BindPalette(palette)`  | `Void(CommandPalette)` | The palette whose selection routes to registered commands      |
| `Poll()`                | `String()`          | Poll the palette and every command once; return an invoked command id (`""` if none). Each command's `WasInvoked()` is also updated |
| `Clear()`               | `Void()`            | Release all commands                                              |

### Example

```rust
// Zia — bind one action to its menu item, toolbar button, shortcut and palette,
// then dispatch them all from a single poll.
var registry = CommandRegistry.New();

var save = Command.New("file.save", "Save");
save.SetShortcut("Ctrl+S");          // also registers with Viper.GUI.Shortcuts under "file.save"
save.BindMenuItem(fileSaveItem);     // a Viper.GUI.MenuItem
save.BindToolbarItem(saveButton);    // a Viper.GUI.ToolbarItem
registry.Add(save);

var wrap = Command.New("view.wordwrap", "Word Wrap");
wrap.SetCheckable(true);
wrap.BindMenuItem(wordWrapItem);
registry.Add(wrap);

registry.BindPalette(palette);

// Once per frame — routes menu + toolbar + shortcut + palette to the right command.
var invoked = registry.Poll();
if invoked == "file.save" { /* save the document */ }
if wrap.WasInvoked() { /* apply the requested word-wrap state */ }
```

---

### Breadcrumb

Breadcrumb navigation widget.

When the path is truncated, the overflow affordance now opens a real dropdown menu that paints, tracks hover, captures input while open, and reports the selected breadcrumb normally.
Breadcrumb wrappers install runtime finalizers and ignore calls after `Destroy()`, which keeps repeated cleanup safe.
`SetPath(path, separator)` treats `separator` as a literal string. For example,
`"alpha::beta"` split with `"::"` yields `alpha`, `beta`; it does not split on
each `:` character independently.
Replacing or clearing items also clears stale click payloads. After
`WasItemClicked()` reports `false`, `GetClickedIndex()` returns `-1` and
`GetClickedData()` returns an empty string until the next click.

**Constructor:** `NEW Viper.GUI.Breadcrumb(parent)`

| Method                     | Signature              | Description                              |
|----------------------------|------------------------|------------------------------------------|
| `AddItem(text, data)`      | `Void(String, String)` | Add breadcrumb item with data            |
| `Clear()`                  | `Void()`               | Remove all items                         |
| `Destroy()`                | `Void()`               | Destroy the breadcrumb wrapper/widget    |
| `GetClickedData()`         | `String()`             | Data of clicked item                     |
| `GetClickedIndex()`        | `Integer()`            | Index of clicked item                    |
| `SetItems(items)`          | `Void(String)`         | Set items from a comma-separated string, trimming spaces |
| `SetMaxItems(max)`         | `Void(Integer)`        | Set max visible items                    |
| `SetPath(path, separator)` | `Void(String, String)` | Set path with separator (e.g. "/")       |
| `SetSeparator(sep)`        | `Void(String)`         | Set the displayed separator string       |
| `SetVisible(visible)`      | `Void(Boolean)`        | Show/hide breadcrumb                     |
| `IsVisible()`              | `Boolean()`            | Check whether the breadcrumb is visible  |
| `WasItemClicked()`         | `Boolean()`            | True if an item was clicked              |

### Example

```rust
// Zia
var bc = Breadcrumb.New(root);
bc.SetPath("src/gui/widgets", "/");
bc.SetMaxItems(5);

if bc.WasItemClicked() {
    var idx = bc.GetClickedIndex();
    var data = bc.GetClickedData();
}
```

---

### Minimap

Code minimap widget (pairs with CodeEditor).

Minimap wrappers install runtime finalizers and clamp width/scale changes through the live widget only; calls after `Destroy()` are ignored. If the bound `CodeEditor` is destroyed, the minimap unbinds itself before painting, scrolling, or handling input.

**Constructor:** `NEW Viper.GUI.Minimap(parent)`

| Method                         | Signature           | Description                              |
|--------------------------------|---------------------|------------------------------------------|
| `AddMarker(line, color, type)` | `Void(Int,Int,Int)` | Add line marker                          |
| `BindEditor(editor)`           | `Void(Object)`      | Bind to a CodeEditor                     |
| `ClearMarkers()`               | `Void()`            | Remove all markers                       |
| `Destroy()`                    | `Void()`            | Destroy the minimap wrapper/widget       |
| `GetWidth()`                   | `Integer()`         | Get minimap width                        |
| `IsVisible()`                  | `Boolean()`         | Check if visible                         |
| `RemoveMarkers(line)`          | `Void(Integer)`     | Remove all markers on a source line      |
| `SetScale(scale)`              | `Void(Double)`      | Set scale, clamped to 0.05–0.5; non-finite values become 0.1 |
| `SetShowSlider(show)`          | `Void(Boolean)`     | Show/hide scroll slider                  |
| `SetVisible(visible)`          | `Void(Boolean)`     | Show/hide minimap                        |
| `SetWidth(width)`              | `Void(Integer)`     | Set minimap width (minimum 64)           |
| `UnbindEditor()`               | `Void()`            | Unbind from editor                       |

### Example

```rust
// Zia
var minimap = Minimap.New(root);
minimap.BindEditor(editor);
minimap.SetWidth(80);
minimap.SetScale(0.5);
minimap.SetShowSlider(true);
minimap.AddMarker(10, 0xFFFF0000, 1);  // Diagnostic marker
```

---

## Dialogs

In-app dialogs are maintained in an app-owned modal stack. The topmost visible dialog is the modal
event root; when it closes or is destroyed, the previous live dialog becomes active again. Static
message/file-dialog helpers and object `Show()` calls run their modal loop synchronously until the
shown dialog closes.

Dialog hit-testing is local to the dialog surface, so button clicks, close clicks, drags, and centered placement continue to work after the dialog is moved or shown relative to nested widgets.

### MessageBox

GUI message dialog boxes. Static helpers show one dialog immediately; object-style dialogs let you configure buttons before `Show()`.
`Info`, `Warning`, and `Error` return `0` after the only OK-style action, matching
their runtime `Integer(String, String)` signatures. One-shot dialogs return their
documented fallback value when no active GUI window is available instead of trying
to run a modal loop without an owner.
Stateful message boxes are safe to destroy explicitly; calling `Show()` on a
destroyed wrapper returns `-1` instead of trying to reuse the freed dialog.
Object-style `Show()` also returns `-1` when the dialog closes without a button
result, such as a window close or unavailable owner app.
Custom button IDs preserve the exact `Integer` passed to `AddButton()`, including
`0` and values outside the C dialog result enum range. `AddButton()` does not make button ID `0` the default implicitly; call `SetDefaultButton(id)` to choose the Enter-key button.

| Method                                       | Signature                 | Description                   |
|----------------------------------------------|---------------------------|-------------------------------|
| `Viper.GUI.MessageBox.Confirm(title, text)`  | `Integer(String, String)` | Show OK/cancel dialog; OK=1, otherwise 0 |
| `Viper.GUI.MessageBox.Error(title, text)`    | `Integer(String, String)` | Show error dialog; returns 0  |
| `Viper.GUI.MessageBox.Info(title, text)`     | `Integer(String, String)` | Show info dialog; returns 0   |
| `Viper.GUI.MessageBox.Prompt(title, text)`   | `String(String, String)`  | Show a text prompt; entered nonempty text on OK, otherwise empty |
| `Viper.GUI.MessageBox.Question(title, text)` | `Integer(String, String)` | Show yes/no question; Yes=1, otherwise 0 |
| `Viper.GUI.MessageBox.Warning(title, text)`  | `Integer(String, String)` | Show warning dialog; returns 0 |

Object-style dialogs:

| Method | Signature | Description |
|--------|-----------|-------------|
| `Viper.GUI.MessageBox.New(title, text, type)` | `Object(String,String,Integer)` | Create a message dialog object |
| `Viper.GUI.MessageBox.NewInfo(title, text)` | `Object(String,String)` | Create an info dialog object |
| `Viper.GUI.MessageBox.NewWarning(title, text)` | `Object(String,String)` | Create a warning dialog object |
| `Viper.GUI.MessageBox.NewError(title, text)` | `Object(String,String)` | Create an error dialog object |
| `Viper.GUI.MessageBox.NewQuestion(title, text)` | `Object(String,String)` | Create a question dialog object |
| `box.AddButton(text, id)` | `Void(String,Integer)` | Add a custom button returning `id` |
| `box.SetDefaultButton(id)` | `Void(Integer)` | Mark the matching custom button as default |
| `box.Show()` | `Integer()` | Show the dialog and return the selected ID |
| `box.Destroy()` | `Void()` | Destroy the dialog object |

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

Native or in-app file dialog boxes (static methods).

Save dialogs honor the default filename field, append the configured default extension when needed, and keep buttons/bookmarks/file-list hit-testing correct after the window is repositioned.
Object-style dialogs snapshot their accepted path list after each `Show()`. Path accessors read that
snapshot until the next `Show()` or until `Destroy()` releases the dialog.
Destroying an object-style dialog removes it from the app's modal stack before freeing it, and destroyed handles are inert for setters, `Show()`, and path accessors.
Object-style dialogs remember the app that created them, include the default bookmarks used by static dialogs, and do not switch owners just because another app becomes active before a failed `Show()`.
`FileDialog.New(type)` uses `0` for open, `1` for save, and `2` for folder selection. It returns
`NULL` for any other value instead of silently creating an open-file dialog.
On macOS, one-shot static dialogs use native panels, including native multi-select for `OpenMultiple`. On other GUI builds, static and object-style dialogs use the same in-app modal dialog and therefore require an active `Viper.GUI.App` window; they return an empty string or `0` when no active GUI window is available. The lower-level C convenience API can install a modal runner so `Open`, `Save`, and `SelectFolder` wait for the in-app dialog instead of returning immediately.
`OpenMultiple()` returns selected paths joined with semicolons. Literal semicolons and backslashes inside paths are escaped as `\;` and `\\`; use `PathListCount()` and `PathListGet()` to decode the list without truncating paths that contain those characters.
Dialog paths and filter patterns reject embedded NUL bytes instead of truncating the value passed to native or in-app dialog code. `SetFilter(NULL, pattern)` and `AddFilter(NULL, pattern)` use the label `Files`, while a missing or invalid pattern is ignored.
The in-app dialog implementation now scrolls long file and bookmark lists, keeps the selected row visible during keyboard navigation, clips long path text, and supports caret-aware editing in the save-name field (`Left` / `Right`, `Home`, `End`, `Backspace`, `Delete`).

| Method                                          | Signature                        | Description                              |
|-------------------------------------------------|----------------------------------|------------------------------------------|
| `Viper.GUI.FileDialog.Open(title, defaultPath, filter)` | `String(String,String,String)`  | Open file dialog; returns path           |
| `Viper.GUI.FileDialog.OpenMultiple(title, defaultPath, filter)` | `String(String,String,String)` | Open multi-file dialog; returns escaped path list |
| `Viper.GUI.FileDialog.PathListCount(paths)` | `Integer(String)` | Count paths in an escaped `OpenMultiple` result |
| `Viper.GUI.FileDialog.PathListGet(paths, index)` | `String(String,Integer)` | Decode one path from an escaped `OpenMultiple` result |
| `Viper.GUI.FileDialog.Save(title, defaultPath, filter, defaultName)` | `String(Str,Str,Str,Str)` | Save file dialog; returns path     |
| `Viper.GUI.FileDialog.SelectFolder(title, dir)` | `String(String, String)`       | Folder selection dialog                  |

Object-style dialogs are also available when an app needs multiple named
filters, a default filename, or multiple selected paths:

| Method | Signature | Description |
|--------|-----------|-------------|
| `Viper.GUI.FileDialog.New(type)` | `Object(Integer)` | Create by mode (`0`=open, `1`=save, `2`=folder); invalid values return `NULL` |
| `Viper.GUI.FileDialog.NewOpen()` | `Object()` | Create an open-file dialog object |
| `Viper.GUI.FileDialog.NewSave()` | `Object()` | Create a save-file dialog object |
| `Viper.GUI.FileDialog.NewFolder()` | `Object()` | Create a select-folder dialog object |
| `dialog.SetTitle(title)` | `Void(String)` | Set dialog title |
| `dialog.SetPath(path)` | `Void(String)` | Set starting path |
| `dialog.SetFilter(label, pattern)` | `Void(String,String)` | Replace filters with one named filter |
| `dialog.AddFilter(label, pattern)` | `Void(String,String)` | Add another named filter |
| `dialog.SetDefaultName(name)` | `Void(String)` | Set default filename for save dialogs |
| `dialog.SetMultiple(enabled)` | `Void(Boolean)` | Enable multiple selection for open dialogs |
| `dialog.Show()` | `Integer()` | Show the dialog; returns 1 on accept, 0 on cancel or unavailable GUI window |
| `dialog.GetPath()` | `String()` | Return the first selected path |
| `dialog.PathCount` | `Integer` | Number of selected paths |
| `dialog.GetPathAt(index)` | `String(Integer)` | Return selected path by index |
| `dialog.Destroy()` | `Void()` | Destroy the dialog object |

### Example

```rust
// Zia
var path = FileDialog.Open("Open File", "/home", "*.txt;*.zia");
if path != "" { /* open file at path */ }

var savePath = FileDialog.Save("Save As", "/home", "*.txt", "untitled.txt");
var folder = FileDialog.SelectFolder("Choose Folder", "/home");

var paths = FileDialog.OpenMultiple("Open Files", "/home", "*.txt");
var count = FileDialog.PathListCount(paths);
var firstSelected = FileDialog.PathListGet(paths, 0);

var dlg = FileDialog.NewSave();
dlg.SetTitle("Export Image");
dlg.SetDefaultName("painting.png");
dlg.AddFilter("PNG image", "*.png");
dlg.AddFilter("BMP image", "*.bmp");
if dlg.Show() == 1 {
    var exportPath = dlg.GetPath();
}
dlg.Destroy();
```

---

## Notifications

### Toast

Toast notification system.

Toasts now render as wrapped card-style notifications with animated slide/fade entry and exit. Manual dismissals and auto-expiry both honor the exit animation before the toast is removed, and long message/action text is wrapped instead of overflowing off the card. Custom toast durations are clamped to the supported `uint32` millisecond range; negative durations become `0`, which means sticky/no auto-dismiss. `WasDismissed()` is edge-triggered and also reports stale toast handles once after their owning app notification manager has been cleaned up.

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
| `toast.WasActionClicked()` | `Boolean()`     | True if action was clicked     |
| `toast.WasDismissed()`     | `Boolean()`     | True once after toast dismissal |

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
if t.WasActionClicked() { /* undo action */ }
```

---

### Tooltip

Tooltip display system (static methods).

Widget tooltips now wrap long text, draw a rounded opaque panel/background, and automatically disappear when the hovered or anchored widget is hidden, disabled, or destroyed.
Leave events honor the configured hide delay, timed tooltips auto-hide after their display duration, and hovering the same widget again after auto-hide re-shows the tooltip without requiring the pointer to visit another widget first.

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

### ClipboardText

System clipboard access (static methods).

Desktop backends now provide text clipboard support on Linux as well as macOS and Windows, so `TextInput`, `CodeEditor`, and other editor widgets can use the same copy/cut/paste path across platforms.

| Method                              | Signature      | Description                    |
|-------------------------------------|----------------|--------------------------------|
| `Viper.GUI.ClipboardText.Clear()`       | `Void()`       | Clear clipboard                |
| `Viper.GUI.ClipboardText.GetText()`     | `String()`     | Get text from clipboard        |
| `Viper.GUI.ClipboardText.HasText()`     | `Boolean()`    | True if clipboard has text     |
| `Viper.GUI.ClipboardText.SetText(text)` | `Void(String)` | Copy text to clipboard         |

### Example

```rust
// Zia
bind Viper.GUI;
bind Viper.Terminal;

ClipboardText.SetText("Hello, World!");
if ClipboardText.HasText() {
    var text = ClipboardText.GetText();
    Say(text);
}
ClipboardText.Clear();
```

---

### Container

Receiver class for setting layout properties on widget handles.

**Type:** Instance receiver over a container handle

| Method                       | Signature              | Description                                      |
|------------------------------|------------------------|--------------------------------------------------|
| `container.SetSpacing(value)` | `Void(Double)` | Set spacing for VBox/HBox (and native Flex)      |
| `container.SetPadding(value)` | `Void(Double)` | Set internal padding around the container's edge |

### Notes

- `SetSpacing()` changes public `VBox` and `HBox` objects (and native Flex objects). Other valid
  widget types safely ignore it; in particular, it does not configure `ScrollView` spacing.
- `SetPadding()` changes the common padding field on a valid widget handle.
- `VBox` and `HBox` expose both methods directly. `Viper.GUI.Container` is useful when a handle is
  stored through a generic receiver.

### Zia Example

```rust
module ContainerDemo;

bind Viper.Terminal;
bind Viper.GUI.App as App;
bind Viper.GUI.VBox as VBox;

func start() {
    var app = App.New("Container Demo", 400, 300);
    var root = app.get_Root();

    // Create a VBox and configure it directly
    var vbox = VBox.New();
    vbox.SetSpacing(12.0);
    vbox.SetPadding(16.0);
    root.AddChild(vbox);
}
```

### BASIC Example

```basic
DIM vbox AS OBJECT = NEW Viper.GUI.VBox()
DIM root AS OBJECT = app.Root
root.AddChild(vbox)

' Configure through the container receiver
vbox.SetSpacing(10.0)
vbox.SetPadding(20.0)
```

---

### Shortcuts

Keyboard shortcut registration system (static methods).

Shortcut matching uses the translated `VG_KEY_*` key space, so function keys, arrows, `Home`/`End`, `PageUp`/`PageDown`, and other named keys match the same strings accepted by `Register()`. Function keys are case-insensitive (`F5` and `f5` are equivalent), and malformed tokens such as `F1x` are rejected. Focused widgets receive key-down events first; a global shortcut only fires when the widget tree leaves the key unhandled. While a modal dialog is open, global shortcuts are suppressed so accelerators cannot leak through the modal.
Invalid shortcut strings are rejected without registering a new shortcut or replacing an existing one. IDs and key specs containing embedded NUL bytes are rejected instead of being truncated. Shortcut triggers are retained for the whole current frame: `GetTriggered()` returns the first triggered id, and `WasTriggered(id)` can report any shortcut fired before the next poll clears the frame state. `SetGlobalEnabled(false)` stops new shortcut matches, but `WasTriggered(id)` still reports a shortcut that was already triggered earlier in the same frame.

| Method                                          | Signature               | Description                              |
|-------------------------------------------------|-------------------------|------------------------------------------|
| `Viper.GUI.Shortcuts.Clear()`                   | `Void()`                | Remove all shortcuts                     |
| `Viper.GUI.Shortcuts.GetGlobalEnabled()`        | `Boolean()`             | Check if shortcuts globally enabled      |
| `Viper.GUI.Shortcuts.GetTriggered()`            | `String()`              | Get ID of triggered shortcut             |
| `Viper.GUI.Shortcuts.IsEnabled(id)`             | `Boolean(String)`       | Check if shortcut is enabled             |
| `Viper.GUI.Shortcuts.Register(id, keys, desc)`  | `Void(Str, Str, Str)`  | Register a shortcut                      |
| `Viper.GUI.Shortcuts.SetEnabled(id, enabled)`   | `Void(String, Boolean)` | Enable/disable shortcut                  |
| `Viper.GUI.Shortcuts.SetGlobalEnabled(enabled)` | `Void(Boolean)`         | Enable/disable all shortcuts             |
| `Viper.GUI.Shortcuts.Unregister(id)`            | `Void(String)`          | Unregister a shortcut                    |
| `Viper.GUI.Shortcuts.WasTriggered(id)`          | `Boolean(String)`       | True if shortcut was triggered this frame |

### Example

```rust
// Zia
Shortcuts.Register("save", "Ctrl+S", "Save file");
Shortcuts.Register("quit", "Ctrl+Q", "Quit application");

if Shortcuts.WasTriggered("save") {
    // Handle save
}
var triggered = Shortcuts.GetTriggered();
```

---

### Cursor

Mouse cursor control (static methods).

`Set()` accepts the graphics-backend values `0`=default arrow, `1`=pointer/hand, `2`=text
I-beam, `3`=horizontal resize, `4`=vertical resize, and `5`=wait. Values outside this range reset to
the default arrow.

| Method                                | Signature        | Description                    |
|---------------------------------------|------------------|--------------------------------|
| `Viper.GUI.Cursor.Set(cursorType)`    | `Void(Integer)`  | Set cursor type                |
| `Viper.GUI.Cursor.Reset()`            | `Void()`         | Reset to default cursor        |
| `Viper.GUI.Cursor.SetVisible(visible)` | `Void(Boolean)` | Show/hide cursor               |

### Example

```rust
// Zia
Cursor.Set(1);       // Pointer cursor
Cursor.SetVisible(true);
Cursor.Reset();       // Default cursor
```

---

## Themes

`Viper.GUI.Theme` is a static class; call its methods directly or through a static bind, not through `NEW`.

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

### VideoWidget

Embedded video player widget that renders decoded frames inside a GUI layout. Wraps `Viper.Graphics.VideoPlayer` with GUI lifecycle integration.
Construction returns `NULL` when the video cannot be opened or reports non-positive dimensions.

**Type:** Instance (obj)
**Constructor:** `VideoWidget.New(parent, path)`

#### Properties

| Property       | Type    | Access | Description |
|----------------|---------|--------|-------------|
| `IsPlaying`    | Boolean | Read   | True while the video is playing |
| `Position`     | Double  | Read   | Current playback position in seconds |
| `Duration`     | Double  | Read   | Total duration in seconds |
| `Root`         | Widget  | Read   | Internal root widget for advanced composition |
| `ShowControls` | Boolean | Read/Write | Show or hide built-in play/pause controls |
| `Loop`         | Boolean | Read/Write | Loop the video when it ends |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Play()` | `Void()` | Start or resume playback |
| `Pause()` | `Void()` | Pause at the current frame |
| `Stop()` | `Void()` | Stop and reset to the start |
| `Update(deltaSeconds)` | `Void(Double)` | Process controls and refresh the frame; finite positive values also advance playback |
| `SetVolume(volume)` | `Void(Double)` | Set playback volume `[0.0–1.0]`; non-finite values become `0.0` |
| `SetFlex(flex)` / `SetMargin(margin)` | `Void(Double)` / `Void(Integer)` | Proxy layout configuration to the internal root widget |
| `SetPosition(x, y)` | `Void(Integer, Integer)` | Manually position the internal root widget |
| `SetSize(w, h)` | `Void(Integer, Integer)` | Set the internal root's fixed logical size |
| `SetPreferredSize(w, h)` / `SetMaxSize(w, h)` | `Void(Double, Double)` | Proxy preferred/maximum sizing to the internal root widget |
| `SetVisible(flag)` / `SetEnabled(flag)` | `Void(Boolean)` | Proxy visibility/enabled state to the internal root widget |
| `AddChild(widget)` | `Void(Object)` | Add extra content to the widget's internal root container |
| `Destroy()` | `Void()` | Release decoder resources |

```rust
module VideoWidgetDemo;

bind Viper.GUI.VideoWidget as VideoWidget;
bind Viper.GUI.App as App;
bind Viper.Time.Clock as Clock;

func start() {
    var app = App.New("Video Player", 800, 600);
    var root = app.get_Root();
    var vid = VideoWidget.New(root, "assets/intro.ogv");
    vid.set_ShowControls(true);
    vid.set_Loop(false);
    vid.Play();

    var lastMs = Clock.Ticks();
    while !app.get_ShouldClose() {
        app.Poll();
        var nowMs = Clock.Ticks();
        var deltaSeconds = (nowMs - lastMs) / 1000.0;
        lastMs = nowMs;
        vid.Update(deltaSeconds);
        app.Render();
    }

    vid.Destroy();
    app.Destroy();
}
```

```basic
DIM app AS Viper.GUI.App = NEW Viper.GUI.App("Video Player", 800, 600)
DIM root AS Object = app.Root
DIM vid AS Viper.GUI.VideoWidget = NEW Viper.GUI.VideoWidget(root, "assets/intro.ogv")
vid.ShowControls = TRUE
vid.Loop = FALSE
vid.Play()

DIM lastMs AS INTEGER = Viper.Time.Clock.Ticks()
DIM nowMs AS INTEGER
DIM deltaSeconds AS DOUBLE
DO WHILE NOT app.ShouldClose
    app.Poll()
    nowMs = Viper.Time.Clock.Ticks()
    deltaSeconds = (nowMs - lastMs) / 1000.0
    lastMs = nowMs
    vid.Update(deltaSeconds)
    app.Render()
LOOP

vid.Destroy()
app.Destroy()
```

`VideoWidget.Destroy` releases the decoder and its GUI subtree immediately. The runtime finalizer also destroys the subtree if the wrapper is collected while still attached, so controls are not left alive without their player. Call `Update` once per frame inside the application loop — do not drive it from a background thread.
All `VideoWidget` layout, visibility, child, and playback methods are GUI main-thread operations. The widget accepts decoded frame-size changes from the underlying player and resizes the internal image surface safely when the frame dimensions change.

---

### IDE Test, Virtualization, And Accessibility Helpers

These helpers are runtime-side primitives for editor applications that need deterministic tests, large result views, command state snapshots, and contrast checks.

#### TestHarness

`Viper.GUI.TestHarness.New()` creates a headless registry for named widget regions. It is not a replacement for a real `App`; it is a compact automation model for deterministic tests and Zia probes.

| Method | Signature | Description |
|--------|-----------|-------------|
| `RegisterWidget(id, type, name, x, y, w, h)` | `Void(String, String, String, Integer...)` | Register a focusable test region |
| `FindByIdOption(id)` / `FindByNameOption(name)` / `FindByTypeOption(type)` | `Option[Map](String)` | Return a structured lookup record, or `None` |
| `FindById(id)` / `FindByName(name)` / `FindByType(type)` | `Map(String)` | Compatibility API: return a structured lookup record with `found` |
| `SendKey(key, modifiers)` | `Void(String, Integer)` | Queue a key event and update focus traversal for `Tab` |
| `SendMouse(eventType, x, y, button)` | `Void(String, Integer, Integer, Integer)` | Queue a mouse event and focus the hit widget on `down` |
| `Tick(frames)` | `Integer(Integer)` | Advance the harness frame counter |
| `GetFocus()` | `String()` | Return the focused widget id |
| `FocusOrder()` | `Seq()` | Return registered widget ids in traversal order |
| `CaptureRegion(x, y, w, h)` | `Map(Integer...)` | Return a deterministic pixel snapshot map |

`Viper.GUI.TestHarness.AssertNonBlank(snapshot)` is a static helper that checks a captured snapshot map.

#### VirtualList and VirtualTree

`Viper.GUI.VirtualList.New(rowCount, rowHeight, viewportHeight)` tracks visible row ranges and stable selection by id for large lists. `VisibleRange(scrollY)` returns a map with `start`, exclusive `end`, and `count`; `SetRowId(row, id)`, `SelectId(id)`, `GetSelectedId()`, and `GetSelectedIndex()` keep selection stable when rows are rebuilt with the same ids. Row counts clamp to zero or greater, while row and viewport heights clamp to at least one.

`Viper.GUI.VirtualTree.New()` supports lazy tree models. `AddNode(parentId, id, text)`, `Expand(id)`, `Collapse(id)`, `VisibleRows()`, `SelectId(id)`, and `RefreshSubtree(id)` let a project tree preserve expansion/selection while requesting repopulation only for dirty subtrees. `Expand(id)` returns a map containing `found`, `needsPopulate`, and, when found, `expanded`. Each map in `VisibleRows()` contains `id`, `text`, `parentId`, `depth`, `expanded`, and `needsPopulate`. `AddNode` rejects empty ids, self-parenting, and moves that would put a node under its own descendant; valid reparenting removes the node from its old parent. `RefreshSubtree(id)` removes descendants of the refreshed node and clears selection if the selected id was removed.

#### CommandState and Accessibility

`Viper.GUI.CommandState.New(id, label)` stores command enabled/checked state plus accessible label and description metadata. `Snapshot()` returns a map suitable for command palettes, menus, toolbar buttons, and tests.

`Viper.GUI.Accessibility.ContrastRatio(fgRgb, bgRgb)`, `MeetsContrast(fgRgb, bgRgb, minRatio)`, and `HighContrastTokens()` provide shared contrast checks and a small high-contrast color token set for editor overlays. Colors use the low 24 bits as `0xRRGGBB`. A non-finite or non-positive minimum ratio falls back to `4.5`.

---


## See Also

- [Core & Application](core.md)
- [Layout Widgets](layout.md)
- [Basic Widgets](widgets.md)
- [Containers & Advanced](containers.md)
- [GUI Widgets Overview](README.md)
- [Viper Runtime Library](../README.md)
