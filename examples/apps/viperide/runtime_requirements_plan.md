# ViperIDE Runtime Requirements Implementation Plan

**Project:** Viper Runtime GUI Extensions for ViperIDE
**Status:** Phase 1-8 Implemented
**Last Updated:** 2026-01-14

---

## Implementation Status

| Phase | Feature | Status |
|-------|---------|--------|
| **Phase 1** | Clipboard API | ✅ Implemented |
| **Phase 1** | Keyboard Shortcuts | ✅ Implemented |
| **Phase 1** | Window Management | ✅ Implemented (partial - platform stubs) |
| **Phase 1** | Cursor Styles | ✅ Implemented (partial - platform stubs) |
| **Phase 2** | MenuItem | ✅ Implemented |
| **Phase 2** | Menu | ✅ Implemented |
| **Phase 2** | MenuBar | ✅ Implemented |
| **Phase 2** | ContextMenu | ✅ Implemented |
| **Phase 3** | StatusBar | ✅ Implemented |
| **Phase 3** | StatusBarItem | ✅ Implemented |
| **Phase 3** | Toolbar | ✅ Implemented |
| **Phase 3** | ToolbarItem | ✅ Implemented |
| **Phase 4** | Syntax Highlighting API | ✅ Implemented (stubs) |
| **Phase 4** | Gutter & Line Numbers | ✅ Implemented (partial) |
| **Phase 4** | Code Folding | ✅ Implemented (stubs) |
| **Phase 4** | Multiple Cursors | ✅ Implemented (single cursor only) |
| **Phase 5** | MessageBox | ✅ Implemented |
| **Phase 5** | FileDialog | ✅ Implemented |
| **Phase 6** | FindBar | ✅ Implemented |
| **Phase 6** | CommandPalette | ✅ Implemented |
| **Phase 7** | Tooltip | ✅ Implemented |
| **Phase 7** | Toast/Notifications | ✅ Implemented |
| **Phase 8** | Breadcrumb | ✅ Implemented |
| **Phase 8** | Minimap | ✅ Implemented (markers stubbed) |
| **Phase 8** | Drag and Drop | ✅ Implemented (stubs - requires widget extension) |

---

## Executive Summary

This document specifies the implementation plan for all runtime features required by ViperIDE. Each feature follows the established Viper GUI runtime patterns with consistent naming conventions and architecture.

---

## Table of Contents

1. [Implementation Architecture](#1-implementation-architecture)
2. [Naming Conventions](#2-naming-conventions)
3. [Widget Additions](#3-widget-additions)
4. [CodeEditor Enhancements](#4-codeeditor-enhancements)
5. [System Services](#5-system-services)
6. [Implementation Order](#6-implementation-order)
7. [File Changes Required](#7-file-changes-required)
8. [API Reference](#8-api-reference)

---

## 1. Implementation Architecture

### 1.1 Current Pattern

All GUI widgets follow this architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│                    Zia/BASIC Runtime                            │
│         Viper.GUI.MenuBar, Viper.GUI.Toolbar, etc.              │
├─────────────────────────────────────────────────────────────────┤
│                    C Runtime Bridge                             │
│         rt_gui.h / rt_gui.c (opaque handles)                    │
├─────────────────────────────────────────────────────────────────┤
│                    GUI Library (C++)                            │
│         vg_menubar.h/c, vg_toolbar.h/c, etc.                    │
├─────────────────────────────────────────────────────────────────┤
│                    Graphics Backend                             │
│         vgfx (rendering, input, windowing)                      │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Widget Lifecycle

1. **Creation**: `rt_[widget]_new(parent, ...)` → returns `void*` handle
2. **Configuration**: `rt_[widget]_set_[property](handle, value)`
3. **State Query**: `rt_[widget]_get_[property](handle)` or `rt_[widget]_is_[state](handle)`
4. **Event Check**: `rt_[widget]_was_clicked(handle)` (poll-based, not callback)
5. **Destruction**: Automatic when parent destroyed, or explicit `rt_[widget]_destroy(handle)`

---

## 2. Naming Conventions

### 2.1 Runtime API Naming (C)

| Pattern | Example | Description |
|---------|---------|-------------|
| `rt_[widget]_new` | `rt_menubar_new` | Create widget |
| `rt_[widget]_destroy` | `rt_menubar_destroy` | Destroy widget |
| `rt_[widget]_set_[prop]` | `rt_menubar_set_visible` | Set property |
| `rt_[widget]_get_[prop]` | `rt_menu_get_title` | Get property |
| `rt_[widget]_[action]` | `rt_menu_add_item` | Perform action |
| `rt_[widget]_is_[state]` | `rt_menuitem_is_checked` | Query state |
| `rt_[widget]_was_[event]` | `rt_menuitem_was_clicked` | Check event |

### 2.2 Viper Runtime Class Naming

| C Function | Viper Class.Method |
|------------|-------------------|
| `rt_menubar_new` | `Viper.GUI.MenuBar.New(parent)` |
| `rt_menubar_add_menu` | `menuBar.AddMenu(title)` |
| `rt_menu_add_item` | `menu.AddItem(text)` |
| `rt_menuitem_was_clicked` | `menuItem.WasClicked` |

### 2.3 GUI Library Naming (C)

| Pattern | Example | Description |
|---------|---------|-------------|
| `vg_[widget]_t` | `vg_menubar_t` | Type definition |
| `vg_[widget]_create` | `vg_menubar_create` | Constructor |
| `vg_[widget]_destroy` | `vg_menubar_destroy` | Destructor |
| `vg_[widget]_[action]` | `vg_menubar_add_menu` | Method |
| `VG_[WIDGET]_[CONST]` | `VG_MENUITEM_SEPARATOR` | Constants |

---

## 3. Widget Additions

### 3.1 Menu System

#### 3.1.1 Viper.GUI.MenuBar

**Purpose:** Horizontal menu bar at top of window

**C API (rt_gui.h):**
```c
// Creation
void* rt_menubar_new(void* parent);

// Menu management
void* rt_menubar_add_menu(void* menubar, rt_string title);
void rt_menubar_remove_menu(void* menubar, void* menu);
int64_t rt_menubar_get_menu_count(void* menubar);
void* rt_menubar_get_menu(void* menubar, int64_t index);

// Visibility
void rt_menubar_set_visible(void* menubar, int64_t visible);
int64_t rt_menubar_is_visible(void* menubar);
```

**Viper API:**
```
Viper.GUI.MenuBar
├── New(parent: Widget) -> MenuBar
├── AddMenu(title: String) -> Menu
├── RemoveMenu(menu: Menu)
├── GetMenuCount() -> Integer
├── GetMenu(index: Integer) -> Menu
├── SetVisible(visible: Integer)
└── IsVisible -> Integer
```

---

#### 3.1.2 Viper.GUI.Menu

**Purpose:** Dropdown menu containing items

**C API (rt_gui.h):**
```c
// Note: Menus are created via rt_menubar_add_menu or rt_menu_add_submenu

// Item management
void* rt_menu_add_item(void* menu, rt_string text);
void* rt_menu_add_item_with_shortcut(void* menu, rt_string text, rt_string shortcut);
void* rt_menu_add_separator(void* menu);
void* rt_menu_add_submenu(void* menu, rt_string title);
void rt_menu_remove_item(void* menu, void* item);
void rt_menu_clear(void* menu);

// Properties
void rt_menu_set_title(void* menu, rt_string title);
rt_string rt_menu_get_title(void* menu);
int64_t rt_menu_get_item_count(void* menu);
void* rt_menu_get_item(void* menu, int64_t index);

// State
void rt_menu_set_enabled(void* menu, int64_t enabled);
int64_t rt_menu_is_enabled(void* menu);
```

**Viper API:**
```
Viper.GUI.Menu
├── AddItem(text: String) -> MenuItem
├── AddItemWithShortcut(text: String, shortcut: String) -> MenuItem
├── AddSeparator() -> MenuItem
├── AddSubmenu(title: String) -> Menu
├── RemoveItem(item: MenuItem)
├── Clear()
├── SetTitle(title: String)
├── GetTitle() -> String
├── GetItemCount() -> Integer
├── GetItem(index: Integer) -> MenuItem
├── SetEnabled(enabled: Integer)
└── IsEnabled -> Integer
```

---

#### 3.1.3 Viper.GUI.MenuItem

**Purpose:** Individual menu item (action, checkbox, or separator)

**C API (rt_gui.h):**
```c
// Note: Items are created via rt_menu_add_item, etc.

// Properties
void rt_menuitem_set_text(void* item, rt_string text);
rt_string rt_menuitem_get_text(void* item);
void rt_menuitem_set_shortcut(void* item, rt_string shortcut);
rt_string rt_menuitem_get_shortcut(void* item);
void rt_menuitem_set_icon(void* item, void* pixels);

// Checkbox mode
void rt_menuitem_set_checkable(void* item, int64_t checkable);
int64_t rt_menuitem_is_checkable(void* item);
void rt_menuitem_set_checked(void* item, int64_t checked);
int64_t rt_menuitem_is_checked(void* item);

// State
void rt_menuitem_set_enabled(void* item, int64_t enabled);
int64_t rt_menuitem_is_enabled(void* item);
int64_t rt_menuitem_is_separator(void* item);

// Events
int64_t rt_menuitem_was_clicked(void* item);
```

**Viper API:**
```
Viper.GUI.MenuItem
├── SetText(text: String)
├── GetText() -> String
├── SetShortcut(shortcut: String)
├── GetShortcut() -> String
├── SetIcon(pixels: Pixels)
├── SetCheckable(checkable: Integer)
├── IsCheckable -> Integer
├── SetChecked(checked: Integer)
├── IsChecked -> Integer
├── SetEnabled(enabled: Integer)
├── IsEnabled -> Integer
├── IsSeparator -> Integer
└── WasClicked -> Integer
```

---

#### 3.1.4 Viper.GUI.ContextMenu

**Purpose:** Right-click popup menu

**C API (rt_gui.h):**
```c
// Creation
void* rt_contextmenu_new(void);

// Item management (same as Menu)
void* rt_contextmenu_add_item(void* menu, rt_string text);
void* rt_contextmenu_add_item_with_shortcut(void* menu, rt_string text, rt_string shortcut);
void* rt_contextmenu_add_separator(void* menu);
void* rt_contextmenu_add_submenu(void* menu, rt_string title);
void rt_contextmenu_clear(void* menu);

// Display
void rt_contextmenu_show(void* menu, int64_t x, int64_t y);
void rt_contextmenu_hide(void* menu);
int64_t rt_contextmenu_is_visible(void* menu);

// Query selected item
void* rt_contextmenu_get_clicked_item(void* menu);
```

**Viper API:**
```
Viper.GUI.ContextMenu
├── New() -> ContextMenu
├── AddItem(text: String) -> MenuItem
├── AddItemWithShortcut(text: String, shortcut: String) -> MenuItem
├── AddSeparator() -> MenuItem
├── AddSubmenu(title: String) -> Menu
├── Clear()
├── Show(x: Integer, y: Integer)
├── Hide()
├── IsVisible -> Integer
└── GetClickedItem() -> MenuItem
```

---

### 3.2 Toolbar

#### 3.2.1 Viper.GUI.Toolbar

**Purpose:** Row of action buttons/controls

**C API (rt_gui.h):**
```c
// Creation
void* rt_toolbar_new(void* parent);
void* rt_toolbar_new_vertical(void* parent);

// Item management
void* rt_toolbar_add_button(void* toolbar, rt_string icon_path, rt_string tooltip);
void* rt_toolbar_add_button_with_text(void* toolbar, rt_string icon_path, rt_string text, rt_string tooltip);
void* rt_toolbar_add_toggle(void* toolbar, rt_string icon_path, rt_string tooltip);
void* rt_toolbar_add_separator(void* toolbar);
void* rt_toolbar_add_spacer(void* toolbar);
void* rt_toolbar_add_dropdown(void* toolbar, rt_string tooltip);
void rt_toolbar_remove_item(void* toolbar, void* item);

// Properties
void rt_toolbar_set_icon_size(void* toolbar, int64_t size);
int64_t rt_toolbar_get_icon_size(void* toolbar);
void rt_toolbar_set_style(void* toolbar, int64_t style); // 0=icon, 1=text, 2=both
int64_t rt_toolbar_get_item_count(void* toolbar);
void* rt_toolbar_get_item(void* toolbar, int64_t index);
```

**Viper API:**
```
Viper.GUI.Toolbar
├── New(parent: Widget) -> Toolbar
├── NewVertical(parent: Widget) -> Toolbar
├── AddButton(iconPath: String, tooltip: String) -> ToolbarItem
├── AddButtonWithText(iconPath: String, text: String, tooltip: String) -> ToolbarItem
├── AddToggle(iconPath: String, tooltip: String) -> ToolbarItem
├── AddSeparator() -> ToolbarItem
├── AddSpacer() -> ToolbarItem
├── AddDropdown(tooltip: String) -> ToolbarDropdown
├── RemoveItem(item: ToolbarItem)
├── SetIconSize(size: Integer)
├── GetIconSize() -> Integer
├── SetStyle(style: Integer)  // 0=icon, 1=text, 2=both
├── GetItemCount() -> Integer
└── GetItem(index: Integer) -> ToolbarItem
```

---

#### 3.2.2 Viper.GUI.ToolbarItem

**Purpose:** Individual toolbar button/toggle

**C API (rt_gui.h):**
```c
// Properties
void rt_toolbaritem_set_icon(void* item, rt_string icon_path);
void rt_toolbaritem_set_icon_pixels(void* item, void* pixels);
void rt_toolbaritem_set_text(void* item, rt_string text);
void rt_toolbaritem_set_tooltip(void* item, rt_string tooltip);
void rt_toolbaritem_set_enabled(void* item, int64_t enabled);
int64_t rt_toolbaritem_is_enabled(void* item);

// Toggle state (for toggle buttons)
void rt_toolbaritem_set_toggled(void* item, int64_t toggled);
int64_t rt_toolbaritem_is_toggled(void* item);

// Events
int64_t rt_toolbaritem_was_clicked(void* item);
```

**Viper API:**
```
Viper.GUI.ToolbarItem
├── SetIcon(iconPath: String)
├── SetIconPixels(pixels: Pixels)
├── SetText(text: String)
├── SetTooltip(tooltip: String)
├── SetEnabled(enabled: Integer)
├── IsEnabled -> Integer
├── SetToggled(toggled: Integer)
├── IsToggled -> Integer
└── WasClicked -> Integer
```

---

### 3.3 StatusBar

#### 3.3.1 Viper.GUI.StatusBar

**Purpose:** Information bar at bottom of window

**C API (rt_gui.h):**
```c
// Creation
void* rt_statusbar_new(void* parent);

// Zone-based text (left, center, right)
void rt_statusbar_set_left_text(void* bar, rt_string text);
void rt_statusbar_set_center_text(void* bar, rt_string text);
void rt_statusbar_set_right_text(void* bar, rt_string text);
rt_string rt_statusbar_get_left_text(void* bar);
rt_string rt_statusbar_get_center_text(void* bar);
rt_string rt_statusbar_get_right_text(void* bar);

// Item-based (flexible)
void* rt_statusbar_add_text(void* bar, rt_string text, int64_t zone); // 0=left, 1=center, 2=right
void* rt_statusbar_add_button(void* bar, rt_string text, int64_t zone);
void* rt_statusbar_add_progress(void* bar, int64_t zone);
void* rt_statusbar_add_separator(void* bar, int64_t zone);
void* rt_statusbar_add_spacer(void* bar, int64_t zone);
void rt_statusbar_remove_item(void* bar, void* item);
void rt_statusbar_clear(void* bar);

// Visibility
void rt_statusbar_set_visible(void* bar, int64_t visible);
int64_t rt_statusbar_is_visible(void* bar);
```

**Viper API:**
```
Viper.GUI.StatusBar
├── New(parent: Widget) -> StatusBar
├── SetLeftText(text: String)
├── SetCenterText(text: String)
├── SetRightText(text: String)
├── GetLeftText() -> String
├── GetCenterText() -> String
├── GetRightText() -> String
├── AddText(text: String, zone: Integer) -> StatusBarItem
├── AddButton(text: String, zone: Integer) -> StatusBarItem
├── AddProgress(zone: Integer) -> StatusBarItem
├── AddSeparator(zone: Integer) -> StatusBarItem
├── AddSpacer(zone: Integer) -> StatusBarItem
├── RemoveItem(item: StatusBarItem)
├── Clear()
├── SetVisible(visible: Integer)
└── IsVisible -> Integer
```

---

#### 3.3.2 Viper.GUI.StatusBarItem

**Purpose:** Individual status bar element

**C API (rt_gui.h):**
```c
void rt_statusbaritem_set_text(void* item, rt_string text);
rt_string rt_statusbaritem_get_text(void* item);
void rt_statusbaritem_set_tooltip(void* item, rt_string tooltip);
void rt_statusbaritem_set_progress(void* item, double value); // 0.0-1.0
double rt_statusbaritem_get_progress(void* item);
void rt_statusbaritem_set_visible(void* item, int64_t visible);
int64_t rt_statusbaritem_was_clicked(void* item);
```

**Viper API:**
```
Viper.GUI.StatusBarItem
├── SetText(text: String)
├── GetText() -> String
├── SetTooltip(tooltip: String)
├── SetProgress(value: Number)
├── GetProgress() -> Number
├── SetVisible(visible: Integer)
└── WasClicked -> Integer
```

---

### 3.4 Dialogs

#### 3.4.1 Viper.GUI.MessageBox

**Purpose:** Simple modal message with buttons

**C API (rt_gui.h):**
```c
// Quick dialogs (blocking, returns button index)
int64_t rt_messagebox_info(rt_string title, rt_string message);
int64_t rt_messagebox_warning(rt_string title, rt_string message);
int64_t rt_messagebox_error(rt_string title, rt_string message);
int64_t rt_messagebox_question(rt_string title, rt_string message); // Yes=1, No=0
int64_t rt_messagebox_confirm(rt_string title, rt_string message);  // OK=1, Cancel=0

// Custom dialog
void* rt_messagebox_new(rt_string title, rt_string message, int64_t type);
void rt_messagebox_add_button(void* box, rt_string text, int64_t id);
void rt_messagebox_set_default_button(void* box, int64_t id);
int64_t rt_messagebox_show(void* box); // Returns clicked button id
void rt_messagebox_destroy(void* box);
```

**Viper API:**
```
Viper.GUI.MessageBox
├── Info(title: String, message: String) -> Integer
├── Warning(title: String, message: String) -> Integer
├── Error(title: String, message: String) -> Integer
├── Question(title: String, message: String) -> Integer  // Yes=1, No=0
├── Confirm(title: String, message: String) -> Integer   // OK=1, Cancel=0
├── New(title: String, message: String, type: Integer) -> MessageBox
├── AddButton(text: String, id: Integer)
├── SetDefaultButton(id: Integer)
├── Show() -> Integer
└── Destroy()
```

---

#### 3.4.2 Viper.GUI.FileDialog

**Purpose:** Native file open/save dialog

**C API (rt_gui.h):**
```c
// Quick dialogs (blocking, returns path or empty string)
rt_string rt_filedialog_open(rt_string title, rt_string default_path, rt_string filter);
rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter);
rt_string rt_filedialog_save(rt_string title, rt_string default_path, rt_string filter, rt_string default_name);
rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path);

// Custom dialog (more control)
void* rt_filedialog_new(int64_t type); // 0=open, 1=save, 2=folder
void rt_filedialog_set_title(void* dialog, rt_string title);
void rt_filedialog_set_path(void* dialog, rt_string path);
void rt_filedialog_set_filter(void* dialog, rt_string name, rt_string pattern);
void rt_filedialog_add_filter(void* dialog, rt_string name, rt_string pattern);
void rt_filedialog_set_default_name(void* dialog, rt_string name);
void rt_filedialog_set_multiple(void* dialog, int64_t multiple);
int64_t rt_filedialog_show(void* dialog); // 1=OK, 0=Cancel
rt_string rt_filedialog_get_path(void* dialog);
int64_t rt_filedialog_get_path_count(void* dialog);
rt_string rt_filedialog_get_path_at(void* dialog, int64_t index);
void rt_filedialog_destroy(void* dialog);
```

**Viper API:**
```
Viper.GUI.FileDialog
├── Open(title: String, defaultPath: String, filter: String) -> String
├── OpenMultiple(title: String, defaultPath: String, filter: String) -> String
├── Save(title: String, defaultPath: String, filter: String, defaultName: String) -> String
├── SelectFolder(title: String, defaultPath: String) -> String
├── New(type: Integer) -> FileDialog  // 0=open, 1=save, 2=folder
├── SetTitle(title: String)
├── SetPath(path: String)
├── SetFilter(name: String, pattern: String)
├── AddFilter(name: String, pattern: String)
├── SetDefaultName(name: String)
├── SetMultiple(multiple: Integer)
├── Show() -> Integer  // 1=OK, 0=Cancel
├── GetPath() -> String
├── GetPathCount() -> Integer
├── GetPathAt(index: Integer) -> String
└── Destroy()
```

---

### 3.5 Search & Commands

#### 3.5.1 Viper.GUI.FindBar

**Purpose:** Inline find/replace bar for text editing

**C API (rt_gui.h):**
```c
// Creation
void* rt_findbar_new(void* parent);

// Binding to editor
void rt_findbar_bind_editor(void* bar, void* editor);
void rt_findbar_unbind_editor(void* bar);

// Mode
void rt_findbar_set_replace_mode(void* bar, int64_t replace); // 0=find, 1=replace
int64_t rt_findbar_is_replace_mode(void* bar);

// Search text
void rt_findbar_set_find_text(void* bar, rt_string text);
rt_string rt_findbar_get_find_text(void* bar);
void rt_findbar_set_replace_text(void* bar, rt_string text);
rt_string rt_findbar_get_replace_text(void* bar);

// Options
void rt_findbar_set_case_sensitive(void* bar, int64_t sensitive);
int64_t rt_findbar_is_case_sensitive(void* bar);
void rt_findbar_set_whole_word(void* bar, int64_t whole);
int64_t rt_findbar_is_whole_word(void* bar);
void rt_findbar_set_regex(void* bar, int64_t regex);
int64_t rt_findbar_is_regex(void* bar);

// Actions
int64_t rt_findbar_find_next(void* bar);
int64_t rt_findbar_find_previous(void* bar);
int64_t rt_findbar_replace(void* bar);
int64_t rt_findbar_replace_all(void* bar);

// Results
int64_t rt_findbar_get_match_count(void* bar);
int64_t rt_findbar_get_current_match(void* bar);

// Visibility
void rt_findbar_set_visible(void* bar, int64_t visible);
int64_t rt_findbar_is_visible(void* bar);
void rt_findbar_focus(void* bar);
```

**Viper API:**
```
Viper.GUI.FindBar
├── New(parent: Widget) -> FindBar
├── BindEditor(editor: CodeEditor)
├── UnbindEditor()
├── SetReplaceMode(replace: Integer)
├── IsReplaceMode -> Integer
├── SetFindText(text: String)
├── GetFindText() -> String
├── SetReplaceText(text: String)
├── GetReplaceText() -> String
├── SetCaseSensitive(sensitive: Integer)
├── IsCaseSensitive -> Integer
├── SetWholeWord(whole: Integer)
├── IsWholeWord -> Integer
├── SetRegex(regex: Integer)
├── IsRegex -> Integer
├── FindNext() -> Integer
├── FindPrevious() -> Integer
├── Replace() -> Integer
├── ReplaceAll() -> Integer
├── GetMatchCount() -> Integer
├── GetCurrentMatch() -> Integer
├── SetVisible(visible: Integer)
├── IsVisible -> Integer
└── Focus()
```

---

#### 3.5.2 Viper.GUI.CommandPalette

**Purpose:** Quick command search and execution

**C API (rt_gui.h):**
```c
// Creation
void* rt_commandpalette_new(void* parent);

// Command registration
void rt_commandpalette_add_command(void* palette, rt_string id, rt_string label, rt_string category);
void rt_commandpalette_add_command_with_shortcut(void* palette, rt_string id, rt_string label, rt_string category, rt_string shortcut);
void rt_commandpalette_remove_command(void* palette, rt_string id);
void rt_commandpalette_clear(void* palette);

// Display
void rt_commandpalette_show(void* palette);
void rt_commandpalette_hide(void* palette);
int64_t rt_commandpalette_is_visible(void* palette);

// Placeholder
void rt_commandpalette_set_placeholder(void* palette, rt_string text);

// Result
rt_string rt_commandpalette_get_selected_command(void* palette);
int64_t rt_commandpalette_was_command_selected(void* palette);
```

**Viper API:**
```
Viper.GUI.CommandPalette
├── New(parent: Widget) -> CommandPalette
├── AddCommand(id: String, label: String, category: String)
├── AddCommandWithShortcut(id: String, label: String, category: String, shortcut: String)
├── RemoveCommand(id: String)
├── Clear()
├── Show()
├── Hide()
├── IsVisible -> Integer
├── SetPlaceholder(text: String)
├── GetSelectedCommand() -> String
└── WasCommandSelected -> Integer
```

---

### 3.6 Tooltips & Notifications

#### 3.6.1 Viper.GUI.Tooltip

**Purpose:** Hover information popups

**C API (rt_gui.h):**
```c
// Global tooltip manager
void rt_tooltip_show(rt_string text, int64_t x, int64_t y);
void rt_tooltip_show_rich(rt_string title, rt_string body, int64_t x, int64_t y);
void rt_tooltip_hide(void);
void rt_tooltip_set_delay(int64_t delay_ms);

// Widget-specific tooltips
void rt_widget_set_tooltip(void* widget, rt_string text);
void rt_widget_set_tooltip_rich(void* widget, rt_string title, rt_string body);
void rt_widget_clear_tooltip(void* widget);
```

**Viper API:**
```
Viper.GUI.Tooltip
├── Show(text: String, x: Integer, y: Integer)
├── ShowRich(title: String, body: String, x: Integer, y: Integer)
├── Hide()
└── SetDelay(delayMs: Integer)

// On any Widget:
widget.SetTooltip(text: String)
widget.SetTooltipRich(title: String, body: String)
widget.ClearTooltip()
```

---

#### 3.6.2 Viper.GUI.Toast

**Purpose:** Non-blocking notification popups

**C API (rt_gui.h):**
```c
// Quick notifications
void rt_toast_info(rt_string message);
void rt_toast_success(rt_string message);
void rt_toast_warning(rt_string message);
void rt_toast_error(rt_string message);

// Custom toast
void* rt_toast_new(rt_string message, int64_t type, int64_t duration_ms);
void rt_toast_set_action(void* toast, rt_string label);
int64_t rt_toast_was_action_clicked(void* toast);
int64_t rt_toast_was_dismissed(void* toast);
void rt_toast_dismiss(void* toast);

// Toast manager
void rt_toast_set_position(int64_t position); // 0=top-right, 1=top-left, 2=bottom-right, 3=bottom-left
void rt_toast_set_max_visible(int64_t count);
void rt_toast_dismiss_all(void);
```

**Viper API:**
```
Viper.GUI.Toast
├── Info(message: String)
├── Success(message: String)
├── Warning(message: String)
├── Error(message: String)
├── New(message: String, type: Integer, durationMs: Integer) -> Toast
├── SetAction(label: String)
├── WasActionClicked -> Integer
├── WasDismissed -> Integer
├── Dismiss()
├── SetPosition(position: Integer)  // 0=top-right, etc.
├── SetMaxVisible(count: Integer)
└── DismissAll()
```

---

### 3.7 Navigation Widgets

#### 3.7.1 Viper.GUI.Breadcrumb

**Purpose:** Path navigation trail

**C API (rt_gui.h):**
```c
// Creation
void* rt_breadcrumb_new(void* parent);

// Path management
void rt_breadcrumb_set_path(void* crumb, rt_string path, rt_string separator);
void rt_breadcrumb_set_items(void* crumb, rt_string items); // Comma-separated
void rt_breadcrumb_add_item(void* crumb, rt_string text, rt_string data);
void rt_breadcrumb_clear(void* crumb);

// Events
int64_t rt_breadcrumb_was_item_clicked(void* crumb);
int64_t rt_breadcrumb_get_clicked_index(void* crumb);
rt_string rt_breadcrumb_get_clicked_data(void* crumb);

// Style
void rt_breadcrumb_set_separator(void* crumb, rt_string sep);
void rt_breadcrumb_set_max_items(void* crumb, int64_t max);
```

**Viper API:**
```
Viper.GUI.Breadcrumb
├── New(parent: Widget) -> Breadcrumb
├── SetPath(path: String, separator: String)
├── SetItems(items: String)  // Comma-separated
├── AddItem(text: String, data: String)
├── Clear()
├── WasItemClicked -> Integer
├── GetClickedIndex() -> Integer
├── GetClickedData() -> String
├── SetSeparator(sep: String)
└── SetMaxItems(max: Integer)
```

---

#### 3.7.2 Viper.GUI.Minimap

**Purpose:** Code overview/navigation

**C API (rt_gui.h):**
```c
// Creation
void* rt_minimap_new(void* parent);

// Binding
void rt_minimap_bind_editor(void* minimap, void* editor);
void rt_minimap_unbind_editor(void* minimap);

// Appearance
void rt_minimap_set_width(void* minimap, int64_t width);
int64_t rt_minimap_get_width(void* minimap);
void rt_minimap_set_scale(void* minimap, double scale);
void rt_minimap_set_show_slider(void* minimap, int64_t show);

// Markers
void rt_minimap_add_marker(void* minimap, int64_t line, int64_t color, int64_t type);
void rt_minimap_remove_markers(void* minimap, int64_t line);
void rt_minimap_clear_markers(void* minimap);
```

**Viper API:**
```
Viper.GUI.Minimap
├── New(parent: Widget) -> Minimap
├── BindEditor(editor: CodeEditor)
├── UnbindEditor()
├── SetWidth(width: Integer)
├── GetWidth() -> Integer
├── SetScale(scale: Number)
├── SetShowSlider(show: Integer)
├── AddMarker(line: Integer, color: Integer, type: Integer)
├── RemoveMarkers(line: Integer)
└── ClearMarkers()
```

---

## 4. CodeEditor Enhancements

### 4.1 Syntax Highlighting API

**C API (rt_gui.h):**
```c
// Token types
#define RT_TOKEN_NONE       0
#define RT_TOKEN_KEYWORD    1
#define RT_TOKEN_TYPE       2
#define RT_TOKEN_STRING     3
#define RT_TOKEN_NUMBER     4
#define RT_TOKEN_COMMENT    5
#define RT_TOKEN_OPERATOR   6
#define RT_TOKEN_FUNCTION   7
#define RT_TOKEN_VARIABLE   8
#define RT_TOKEN_CONSTANT   9
#define RT_TOKEN_ERROR      10

// Highlight configuration
void rt_codeeditor_set_language(void* editor, rt_string language); // "zia", "basic", "il"
void rt_codeeditor_set_token_color(void* editor, int64_t token_type, int64_t color);
void rt_codeeditor_set_custom_keywords(void* editor, rt_string keywords); // Comma-separated

// Manual highlighting (for custom languages)
void rt_codeeditor_clear_highlights(void* editor);
void rt_codeeditor_add_highlight(void* editor, int64_t start_line, int64_t start_col,
                                  int64_t end_line, int64_t end_col, int64_t token_type);
void rt_codeeditor_refresh_highlights(void* editor);
```

**Viper API:**
```
Viper.GUI.CodeEditor (additions)
├── SetLanguage(language: String)  // "zia", "basic", "il"
├── SetTokenColor(tokenType: Integer, color: Integer)
├── SetCustomKeywords(keywords: String)
├── ClearHighlights()
├── AddHighlight(startLine: Integer, startCol: Integer, endLine: Integer, endCol: Integer, tokenType: Integer)
└── RefreshHighlights()
```

---

### 4.2 Gutter & Line Numbers

**C API (rt_gui.h):**
```c
// Line numbers
void rt_codeeditor_set_show_line_numbers(void* editor, int64_t show);
int64_t rt_codeeditor_get_show_line_numbers(void* editor);
void rt_codeeditor_set_line_number_width(void* editor, int64_t width);

// Gutter icons/markers
void rt_codeeditor_set_gutter_icon(void* editor, int64_t line, void* pixels, int64_t slot);
void rt_codeeditor_clear_gutter_icon(void* editor, int64_t line, int64_t slot);
void rt_codeeditor_clear_all_gutter_icons(void* editor, int64_t slot);

// Gutter events
int64_t rt_codeeditor_was_gutter_clicked(void* editor);
int64_t rt_codeeditor_get_gutter_clicked_line(void* editor);
int64_t rt_codeeditor_get_gutter_clicked_slot(void* editor);

// Folding indicators in gutter
void rt_codeeditor_set_show_fold_gutter(void* editor, int64_t show);
```

**Viper API:**
```
Viper.GUI.CodeEditor (additions)
├── SetShowLineNumbers(show: Integer)
├── GetShowLineNumbers() -> Integer
├── SetLineNumberWidth(width: Integer)
├── SetGutterIcon(line: Integer, pixels: Pixels, slot: Integer)
├── ClearGutterIcon(line: Integer, slot: Integer)
├── ClearAllGutterIcons(slot: Integer)
├── WasGutterClicked -> Integer
├── GetGutterClickedLine() -> Integer
├── GetGutterClickedSlot() -> Integer
└── SetShowFoldGutter(show: Integer)
```

---

### 4.3 Code Folding

**C API (rt_gui.h):**
```c
// Fold regions
void rt_codeeditor_add_fold_region(void* editor, int64_t start_line, int64_t end_line);
void rt_codeeditor_remove_fold_region(void* editor, int64_t start_line);
void rt_codeeditor_clear_fold_regions(void* editor);

// Fold state
void rt_codeeditor_fold(void* editor, int64_t line);
void rt_codeeditor_unfold(void* editor, int64_t line);
void rt_codeeditor_toggle_fold(void* editor, int64_t line);
int64_t rt_codeeditor_is_folded(void* editor, int64_t line);
void rt_codeeditor_fold_all(void* editor);
void rt_codeeditor_unfold_all(void* editor);

// Auto-detection
void rt_codeeditor_set_auto_fold_detection(void* editor, int64_t enable);
```

**Viper API:**
```
Viper.GUI.CodeEditor (additions)
├── AddFoldRegion(startLine: Integer, endLine: Integer)
├── RemoveFoldRegion(startLine: Integer)
├── ClearFoldRegions()
├── Fold(line: Integer)
├── Unfold(line: Integer)
├── ToggleFold(line: Integer)
├── IsFolded(line: Integer) -> Integer
├── FoldAll()
├── UnfoldAll()
└── SetAutoFoldDetection(enable: Integer)
```

---

### 4.4 Multiple Cursors

**C API (rt_gui.h):**
```c
// Cursor management
int64_t rt_codeeditor_get_cursor_count(void* editor);
void rt_codeeditor_add_cursor(void* editor, int64_t line, int64_t col);
void rt_codeeditor_remove_cursor(void* editor, int64_t index);
void rt_codeeditor_clear_extra_cursors(void* editor);

// Cursor positions
int64_t rt_codeeditor_get_cursor_line(void* editor, int64_t index);
int64_t rt_codeeditor_get_cursor_col(void* editor, int64_t index);
void rt_codeeditor_set_cursor_position(void* editor, int64_t index, int64_t line, int64_t col);

// Selection per cursor
void rt_codeeditor_set_cursor_selection(void* editor, int64_t index,
                                         int64_t start_line, int64_t start_col,
                                         int64_t end_line, int64_t end_col);
int64_t rt_codeeditor_cursor_has_selection(void* editor, int64_t index);
```

**Viper API:**
```
Viper.GUI.CodeEditor (additions)
├── GetCursorCount() -> Integer
├── AddCursor(line: Integer, col: Integer)
├── RemoveCursor(index: Integer)
├── ClearExtraCursors()
├── GetCursorLine(index: Integer) -> Integer
├── GetCursorCol(index: Integer) -> Integer
├── SetCursorPosition(index: Integer, line: Integer, col: Integer)
├── SetCursorSelection(index: Integer, startLine: Integer, startCol: Integer, endLine: Integer, endCol: Integer)
└── CursorHasSelection(index: Integer) -> Integer
```

---

### 4.5 Markers & Decorations

**C API (rt_gui.h):**
```c
// Marker types
#define RT_MARKER_ERROR      0
#define RT_MARKER_WARNING    1
#define RT_MARKER_INFO       2
#define RT_MARKER_HINT       3
#define RT_MARKER_SEARCH     4
#define RT_MARKER_HIGHLIGHT  5
#define RT_MARKER_MODIFIED   6

// Line markers (full line highlight)
void rt_codeeditor_add_line_marker(void* editor, int64_t line, int64_t type, int64_t color);
void rt_codeeditor_remove_line_marker(void* editor, int64_t line, int64_t type);
void rt_codeeditor_clear_line_markers(void* editor, int64_t type);
void rt_codeeditor_clear_all_markers(void* editor);

// Text decorations (underlines, squiggles)
void rt_codeeditor_add_decoration(void* editor, int64_t start_line, int64_t start_col,
                                   int64_t end_line, int64_t end_col,
                                   int64_t type, rt_string message);
void rt_codeeditor_clear_decorations(void* editor, int64_t type);
void rt_codeeditor_clear_all_decorations(void* editor);

// Inline hints
void rt_codeeditor_add_inline_hint(void* editor, int64_t line, int64_t col, rt_string text);
void rt_codeeditor_clear_inline_hints(void* editor);
```

**Viper API:**
```
Viper.GUI.CodeEditor (additions)
├── AddLineMarker(line: Integer, type: Integer, color: Integer)
├── RemoveLineMarker(line: Integer, type: Integer)
├── ClearLineMarkers(type: Integer)
├── ClearAllMarkers()
├── AddDecoration(startLine: Integer, startCol: Integer, endLine: Integer, endCol: Integer, type: Integer, message: String)
├── ClearDecorations(type: Integer)
├── ClearAllDecorations()
├── AddInlineHint(line: Integer, col: Integer, text: String)
└── ClearInlineHints()
```

---

### 4.6 Events & Callbacks

**C API (rt_gui.h):**
```c
// Content change
int64_t rt_codeeditor_was_modified(void* editor);
int64_t rt_codeeditor_get_change_start_line(void* editor);
int64_t rt_codeeditor_get_change_end_line(void* editor);

// Cursor/selection change
int64_t rt_codeeditor_was_cursor_moved(void* editor);
int64_t rt_codeeditor_was_selection_changed(void* editor);

// Scroll
int64_t rt_codeeditor_was_scrolled(void* editor);
int64_t rt_codeeditor_get_scroll_top_line(void* editor);
int64_t rt_codeeditor_get_visible_line_count(void* editor);

// Focus
int64_t rt_codeeditor_was_focused(void* editor);
int64_t rt_codeeditor_was_blurred(void* editor);

// Hover
int64_t rt_codeeditor_get_hover_line(void* editor);
int64_t rt_codeeditor_get_hover_col(void* editor);
int64_t rt_codeeditor_is_hovering(void* editor);
```

**Viper API:**
```
Viper.GUI.CodeEditor (additions)
├── WasModified -> Integer
├── GetChangeStartLine() -> Integer
├── GetChangeEndLine() -> Integer
├── WasCursorMoved -> Integer
├── WasSelectionChanged -> Integer
├── WasScrolled -> Integer
├── GetScrollTopLine() -> Integer
├── GetVisibleLineCount() -> Integer
├── WasFocused -> Integer
├── WasBlurred -> Integer
├── GetHoverLine() -> Integer
├── GetHoverCol() -> Integer
└── IsHovering -> Integer
```

---

## 5. System Services

### 5.1 Keyboard Shortcuts (Viper.Input.Shortcuts)

**C API (rt_input.h):**
```c
// Shortcut registration
void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description);
void rt_shortcuts_unregister(rt_string id);
void rt_shortcuts_clear(void);

// Check for triggered shortcut
int64_t rt_shortcuts_was_triggered(rt_string id);
rt_string rt_shortcuts_get_triggered(void); // Returns id of triggered shortcut, or empty

// Enable/disable
void rt_shortcuts_set_enabled(rt_string id, int64_t enabled);
int64_t rt_shortcuts_is_enabled(rt_string id);

// Global enable (for dialogs, text input)
void rt_shortcuts_set_global_enabled(int64_t enabled);
int64_t rt_shortcuts_get_global_enabled(void);

// Key string format: "Ctrl+S", "Ctrl+Shift+P", "Alt+F4", "F5", etc.
```

**Viper API:**
```
Viper.Input.Shortcuts
├── Register(id: String, keys: String, description: String)
├── Unregister(id: String)
├── Clear()
├── WasTriggered(id: String) -> Integer
├── GetTriggered() -> String
├── SetEnabled(id: String, enabled: Integer)
├── IsEnabled(id: String) -> Integer
├── SetGlobalEnabled(enabled: Integer)
└── GetGlobalEnabled() -> Integer
```

---

### 5.2 Clipboard (Viper.System.Clipboard)

**C API (rt_system.h):**
```c
// Text clipboard
void rt_clipboard_set_text(rt_string text);
rt_string rt_clipboard_get_text(void);
int64_t rt_clipboard_has_text(void);

// Image clipboard (future)
void rt_clipboard_set_image(void* pixels);
void* rt_clipboard_get_image(void);
int64_t rt_clipboard_has_image(void);

// Clear
void rt_clipboard_clear(void);
```

**Viper API:**
```
Viper.System.Clipboard
├── SetText(text: String)
├── GetText() -> String
├── HasText() -> Integer
├── SetImage(pixels: Pixels)
├── GetImage() -> Pixels
├── HasImage() -> Integer
└── Clear()
```

---

### 5.3 Drag and Drop (Viper.GUI.DragDrop)

**C API (rt_gui.h):**
```c
// Drag source
void rt_widget_set_draggable(void* widget, int64_t draggable);
void rt_widget_set_drag_data(void* widget, rt_string type, rt_string data);
int64_t rt_widget_is_being_dragged(void* widget);

// Drop target
void rt_widget_set_drop_target(void* widget, int64_t target);
void rt_widget_set_accepted_drop_types(void* widget, rt_string types); // Comma-separated
int64_t rt_widget_is_drag_over(void* widget);
int64_t rt_widget_was_dropped(void* widget);
rt_string rt_widget_get_drop_type(void* widget);
rt_string rt_widget_get_drop_data(void* widget);

// File drops (from OS)
int64_t rt_app_was_file_dropped(void* app);
int64_t rt_app_get_dropped_file_count(void* app);
rt_string rt_app_get_dropped_file(void* app, int64_t index);
```

**Viper API:**
```
// On any Widget:
widget.SetDraggable(draggable: Integer)
widget.SetDragData(type: String, data: String)
widget.IsBeingDragged -> Integer
widget.SetDropTarget(target: Integer)
widget.SetAcceptedDropTypes(types: String)
widget.IsDragOver -> Integer
widget.WasDropped -> Integer
widget.GetDropType() -> String
widget.GetDropData() -> String

// On App:
app.WasFileDropped -> Integer
app.GetDroppedFileCount() -> Integer
app.GetDroppedFile(index: Integer) -> String
```

---

### 5.4 Window Management (Viper.GUI.Window)

**C API (rt_gui.h):**
```c
// Window state
void rt_app_set_title(void* app, rt_string title);
rt_string rt_app_get_title(void* app);
void rt_app_set_size(void* app, int64_t width, int64_t height);
int64_t rt_app_get_width(void* app);
int64_t rt_app_get_height(void* app);
void rt_app_set_position(void* app, int64_t x, int64_t y);
int64_t rt_app_get_x(void* app);
int64_t rt_app_get_y(void* app);

// Window state
void rt_app_minimize(void* app);
void rt_app_maximize(void* app);
void rt_app_restore(void* app);
int64_t rt_app_is_minimized(void* app);
int64_t rt_app_is_maximized(void* app);
void rt_app_set_fullscreen(void* app, int64_t fullscreen);
int64_t rt_app_is_fullscreen(void* app);

// Focus
void rt_app_focus(void* app);
int64_t rt_app_is_focused(void* app);

// Close prevention
void rt_app_set_prevent_close(void* app, int64_t prevent);
int64_t rt_app_was_close_requested(void* app);
```

**Viper API:**
```
Viper.GUI.App (additions)
├── SetTitle(title: String)
├── GetTitle() -> String
├── SetSize(width: Integer, height: Integer)
├── GetWidth() -> Integer
├── GetHeight() -> Integer
├── SetPosition(x: Integer, y: Integer)
├── GetX() -> Integer
├── GetY() -> Integer
├── Minimize()
├── Maximize()
├── Restore()
├── IsMinimized -> Integer
├── IsMaximized -> Integer
├── SetFullscreen(fullscreen: Integer)
├── IsFullscreen -> Integer
├── Focus()
├── IsFocused -> Integer
├── SetPreventClose(prevent: Integer)
└── WasCloseRequested -> Integer
```

---

### 5.5 Cursor Styles (Viper.GUI.Cursor)

**C API (rt_gui.h):**
```c
// Cursor types
#define RT_CURSOR_ARROW     0
#define RT_CURSOR_IBEAM     1
#define RT_CURSOR_WAIT      2
#define RT_CURSOR_CROSSHAIR 3
#define RT_CURSOR_HAND      4
#define RT_CURSOR_RESIZE_H  5
#define RT_CURSOR_RESIZE_V  6
#define RT_CURSOR_RESIZE_NE 7
#define RT_CURSOR_RESIZE_NW 8
#define RT_CURSOR_MOVE      9
#define RT_CURSOR_NOT_ALLOWED 10

// Global cursor
void rt_cursor_set(int64_t type);
void rt_cursor_reset(void);
void rt_cursor_set_visible(int64_t visible);

// Widget cursor
void rt_widget_set_cursor(void* widget, int64_t type);
void rt_widget_reset_cursor(void* widget);

// Custom cursor (from pixels)
void* rt_cursor_create(void* pixels, int64_t hotspot_x, int64_t hotspot_y);
void rt_cursor_set_custom(void* cursor);
void rt_cursor_destroy(void* cursor);
```

**Viper API:**
```
Viper.GUI.Cursor
├── Set(type: Integer)
├── Reset()
├── SetVisible(visible: Integer)
├── Create(pixels: Pixels, hotspotX: Integer, hotspotY: Integer) -> Cursor
├── SetCustom(cursor: Cursor)
└── Destroy(cursor: Cursor)

// On any Widget:
widget.SetCursor(type: Integer)
widget.ResetCursor()
```

---

## 6. Implementation Order

### Phase 1: Foundation (Week 1-2)

**Priority: Critical for basic IDE**

| Item | Effort | Dependencies |
|------|--------|--------------|
| Clipboard | Small | None |
| Keyboard Shortcuts | Medium | None |
| Window Management | Small | None |
| Cursor Styles | Small | None |

---

### Phase 2: Menu System (Week 3-4)

**Priority: Critical for IDE UX**

| Item | Effort | Dependencies |
|------|--------|--------------|
| MenuItem | Medium | None |
| Menu | Medium | MenuItem |
| MenuBar | Medium | Menu |
| ContextMenu | Medium | Menu |

---

### Phase 3: Toolbar & StatusBar (Week 5-6)

**Priority: High for IDE UX**

| Item | Effort | Dependencies |
|------|--------|--------------|
| ToolbarItem | Small | None |
| Toolbar | Medium | ToolbarItem |
| StatusBarItem | Small | None |
| StatusBar | Medium | StatusBarItem |

---

### Phase 4: Dialogs (Week 7-8)

**Priority: High for file operations**

| Item | Effort | Dependencies |
|------|--------|--------------|
| MessageBox | Medium | None |
| FileDialog | Large | Platform integration |

---

### Phase 5: CodeEditor Enhancements (Week 9-12)

**Priority: Critical for code editing**

| Item | Effort | Dependencies |
|------|--------|--------------|
| Syntax Highlighting API | Large | None |
| Gutter & Line Numbers | Medium | None |
| Markers & Decorations | Medium | None |
| Code Folding | Large | None |
| Multiple Cursors | Large | None |
| Events & Callbacks | Medium | None |

---

### Phase 6: Search & Commands (Week 13-14)

**Priority: High for productivity**

| Item | Effort | Dependencies |
|------|--------|--------------|
| FindBar | Large | CodeEditor |
| CommandPalette | Medium | Keyboard Shortcuts |

---

### Phase 7: Tooltips & Notifications (Week 15-16)

**Priority: Medium for polish**

| Item | Effort | Dependencies |
|------|--------|--------------|
| Tooltip | Medium | None |
| Toast | Medium | None |

---

### Phase 8: Navigation & Advanced (Week 17-18)

**Priority: Medium for advanced features**

| Item | Effort | Dependencies |
|------|--------|--------------|
| Breadcrumb | Medium | None |
| Minimap | Large | CodeEditor |
| Drag and Drop | Large | None |

---

## 7. File Changes Required

### 7.1 Runtime Bridge (src/runtime/)

| File | Changes |
|------|---------|
| `rt_gui.h` | Add all new function declarations |
| `rt_gui.c` | Implement all runtime bridge functions |
| `rt_input.h` | Add keyboard shortcuts API |
| `rt_input.c` | Implement keyboard shortcuts |
| `rt_system.h` | Add clipboard API |
| `rt_system.c` | Implement clipboard (platform-specific) |

### 7.2 GUI Library (src/lib/gui/)

| File | Changes |
|------|---------|
| `include/vg_menubar.h` | MenuBar/Menu/MenuItem definitions |
| `include/vg_toolbar.h` | Toolbar/ToolbarItem definitions |
| `include/vg_statusbar.h` | StatusBar definitions |
| `include/vg_dialogs.h` | MessageBox/FileDialog definitions |
| `include/vg_findbar.h` | FindBar definitions |
| `include/vg_command.h` | CommandPalette definitions |
| `include/vg_tooltip.h` | Tooltip definitions |
| `include/vg_toast.h` | Toast notifications |
| `include/vg_breadcrumb.h` | Breadcrumb definitions |
| `include/vg_minimap.h` | Minimap definitions |
| `include/vg_codeeditor.h` | CodeEditor enhancements |
| `src/widgets/vg_menubar.c` | MenuBar implementation |
| `src/widgets/vg_toolbar.c` | Toolbar implementation |
| `src/widgets/vg_statusbar.c` | StatusBar implementation |
| `src/widgets/vg_dialogs.c` | Dialog implementations |
| `src/widgets/vg_findbar.c` | FindBar implementation |
| `src/widgets/vg_command.c` | CommandPalette implementation |
| `src/widgets/vg_tooltip.c` | Tooltip implementation |
| `src/widgets/vg_toast.c` | Toast implementation |
| `src/widgets/vg_breadcrumb.c` | Breadcrumb implementation |
| `src/widgets/vg_minimap.c` | Minimap implementation |
| `src/widgets/vg_codeeditor.c` | CodeEditor enhancements |

### 7.3 Documentation (docs/viperlib/)

| File | Changes |
|------|---------|
| `gui.md` | Add all new widget documentation |
| `input.md` | Add keyboard shortcuts documentation |
| `system.md` | Add clipboard documentation |

### 7.4 Build System

| File | Changes |
|------|---------|
| `src/lib/gui/CMakeLists.txt` | Add new source files |
| `src/runtime/CMakeLists.txt` | Update if new files added |

---

## 8. API Reference

### 8.1 Complete Widget List

| Widget | Namespace | Status |
|--------|-----------|--------|
| App | Viper.GUI | Existing |
| Label | Viper.GUI | Existing |
| Button | Viper.GUI | Existing |
| TextInput | Viper.GUI | Existing |
| Checkbox | Viper.GUI | Existing |
| RadioButton | Viper.GUI | Existing |
| RadioGroup | Viper.GUI | Existing |
| Slider | Viper.GUI | Existing |
| Spinner | Viper.GUI | Existing |
| ProgressBar | Viper.GUI | Existing |
| Dropdown | Viper.GUI | Existing |
| ListBox | Viper.GUI | Existing |
| TreeView | Viper.GUI | Existing |
| TabBar | Viper.GUI | Existing |
| Image | Viper.GUI | Existing |
| CodeEditor | Viper.GUI | Existing (to enhance) |
| VBox | Viper.GUI | Existing |
| HBox | Viper.GUI | Existing |
| ScrollView | Viper.GUI | Existing |
| SplitPane | Viper.GUI | Existing |
| Font | Viper.GUI | Existing |
| **MenuBar** | Viper.GUI | **New** |
| **Menu** | Viper.GUI | **New** |
| **MenuItem** | Viper.GUI | **New** |
| **ContextMenu** | Viper.GUI | **New** |
| **Toolbar** | Viper.GUI | **New** |
| **ToolbarItem** | Viper.GUI | **New** |
| **StatusBar** | Viper.GUI | **New** |
| **StatusBarItem** | Viper.GUI | **New** |
| **MessageBox** | Viper.GUI | **New** |
| **FileDialog** | Viper.GUI | **New** |
| **FindBar** | Viper.GUI | **New** |
| **CommandPalette** | Viper.GUI | **New** |
| **Tooltip** | Viper.GUI | **New** |
| **Toast** | Viper.GUI | **New** |
| **Breadcrumb** | Viper.GUI | **New** |
| **Minimap** | Viper.GUI | **New** |
| **Cursor** | Viper.GUI | **New** |

### 8.2 New System Services

| Service | Namespace | Status |
|---------|-----------|--------|
| **Shortcuts** | Viper.Input | **New** |
| **Clipboard** | Viper.System | **New** |
| **DragDrop** | Viper.GUI | **New** |

### 8.3 CodeEditor Enhancement Summary

| Feature | Status |
|---------|--------|
| Basic editing | Existing |
| SetText/GetText | Existing |
| SetCursor/ScrollToLine | Existing |
| GetLineCount/IsModified | Existing |
| **Syntax Highlighting API** | **New** |
| **Gutter & Line Numbers** | **New** |
| **Code Folding** | **New** |
| **Multiple Cursors** | **New** |
| **Markers & Decorations** | **New** |
| **Events (scroll, hover, etc.)** | **New** |

---

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| 2026-01-14 | 0.1.0 | Initial implementation plan |

---

*This plan provides a roadmap for implementing all ViperIDE runtime requirements. Implementation should follow the phased approach to deliver incremental value while building toward the complete feature set.*
