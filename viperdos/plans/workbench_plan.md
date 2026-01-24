# ViperDOS Workbench - Complete Implementation Plan

## Vision

Create a fully-featured Amiga-inspired Workbench desktop environment for ViperDOS that provides:

- Intuitive graphical file system navigation
- Application launching and management
- System configuration utilities
- A cohesive, retro-modern aesthetic

The Workbench will serve as the primary GUI interface for ViperDOS, bringing the beloved Amiga desktop experience to a
modern microkernel operating system.

---

## Current State (January 2025)

### Completed Infrastructure

| Component         | Status               | Description                                                 |
|-------------------|----------------------|-------------------------------------------------------------|
| displayd          | **Complete**         | Window compositor with double buffering, z-order management |
| libgui            | **Complete**         | Client library for window creation, drawing, events         |
| consoled          | **Complete**         | GUI terminal emulator with ANSI support                     |
| workbench         | **Phase 2 Complete** | File browser, context menus, file ops, rename editor        |
| prefs             | **Phase 3 Complete** | GUI Preferences with Amiga-style category sidebar           |
| guisysinfo        | **Phase 3 Complete** | GUI System Information utility                              |
| taskman           | **Phase 3 Complete** | GUI Task Manager with process list                          |
| Event System      | **Complete**         | Mouse/keyboard events routed to windows                     |
| Window Management | **Complete**         | Focus, z-order, minimize/maximize, dragging                 |
| Color System      | **Complete**         | Centralized in `user/include/viper_colors.h`                |
| Resolution        | **Complete**         | Updated to 1024x768 (from 800x600)                          |

### Phase 1 Deliverables (COMPLETED)

- [x] Amiga-style blue backdrop (`#0055AA`)
- [x] Menu bar with Workbench/Window/Tools menus
- [x] Desktop icons with pixel art (SYS:, Shell, Prefs, About)
- [x] Single-click selection with orange highlight
- [x] Double-click to launch applications
- [x] Shell icon launches consoled on demand
- [x] SYSTEM surface z-order (desktop stays behind windows)
- [x] Double buffering eliminates visual flicker
- [x] Window decorations (title bar, close/min/max buttons)

### Phase 1.5 Deliverables (COMPLETE)

- [x] Resolution increased to 1024x768
- [x] Centralized color definitions (`user/include/viper_colors.h`)
- [x] Consistent white-on-blue color scheme throughout boot
- [x] Workbench refactored from C to C++ with modular architecture
- [x] Separated into modules: desktop.cpp, icons.cpp, filebrowser.cpp
- [x] File browser window implementation (FUNCTIONAL - not just skeleton)
- [x] Directory listing via opendir/readdir/stat
- [x] File type detection (Directory, Executable, Text, Image, Unknown)
- [x] Icon rendering per file type
- [x] Double-click navigation into directories
- [x] Double-click launches executables
- [x] Parent directory navigation
- [x] Dynamic drive icons showing mounted volumes (via assign_list syscall)
- [x] Amiga-style icon updates (Prefs, question mark About)

### Phase 2 Deliverables (COMPLETE)

- [x] Right-click context menus
- [x] File operations: delete, copy, cut, paste
- [x] Create new folder
- [x] Keyboard shortcuts: Enter, Delete, F5, C, V, N, F2
- [x] Enhanced status bar with file info and hints
- [x] Inline rename editor with text input
- [x] Dynamic drive discovery via assign_list syscall

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      Desktop Applications                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐            │
│  │Workbench │ │ FileMgr  │ │  Prefs   │ │  VEdit   │ ...        │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘            │
├───────┴────────────┴────────────┴────────────┴──────────────────┤
│                     libwidget (Phase 4)                          │
│     Button, Label, TextBox, ListView, Menu, Dialog, Tree        │
├─────────────────────────────────────────────────────────────────┤
│                          libgui                                  │
│   Windows, Drawing Primitives, Events, Fonts, Shared Memory     │
├─────────────────────────────────────────────────────────────────┤
│                         displayd                                 │
│   Compositor, Window Manager, Input Router, Cursor, Themes      │
├─────────────────────────────────────────────────────────────────┤
│                          Kernel                                  │
│   Framebuffer (ramfb/virtio-gpu) │ Input (VirtIO-input)         │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 2: File System Integration

**Priority:** HIGH
**Estimated Effort:** Medium
**Dependencies:** Phase 1 (Complete)

### 2.1 Drive Icons on Desktop

Display mounted filesystems as desktop icons similar to Amiga Workbench.

#### Implementation

**File:** `user/workbench/src/desktop.cpp` (refactored from workbench.c)

```cpp
// Dynamic drive discovery
struct DriveInfo {
    char name[32];        // "SYS:", "WORK:", etc.
    char mount_point[64]; // "/sys", "/work"
    char volume_name[32]; // Volume label
    uint64_t total_size;
    uint64_t free_size;
    bool removable;
    bool mounted;
};

static constexpr int MAX_DRIVES = 8;
static DriveInfo g_drives[MAX_DRIVES];
static int g_drive_count = 0;

// Query fsd for mounted volumes
void refresh_drives() {
    g_drive_count = 0;

    // Query filesystem server for mounts
    // Add SYS: as primary drive
    // Scan for additional volumes (WORK:, RAM:, etc.)
}
```

#### Drive Icon Features

- Different icons for different drive types (floppy, hard disk, RAM disk)
- Show volume name below icon
- Double-click opens file browser window
- Right-click context menu (Eject, Info, Format)
- Drag files to/from drive icons

### 2.2 File Browser Windows

When a drive or folder is opened, display contents in a new window.

#### Window Layout

```
┌─────────────────────────────────────────────────────────┐
│ ≡ SYS:                                          _ □ X  │
├─────────────────────────────────────────────────────────┤
│ Path: SYS:                                    [↑][Home] │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  [📁]       [📁]       [📁]       [📄]       [📄]      │
│  Devs      Libs       Prefs     Startup    README      │
│                                                         │
│  [📁]       [📄]       [📄]       [📄]                 │
│  Tools    vinit.sys  blkd.sys  netd.sys               │
│                                                         │
├─────────────────────────────────────────────────────────┤
│ 8 items, 2.1 MB free                                    │
└─────────────────────────────────────────────────────────┘
```

#### File Types and Icons

| Type       | Extension  | Icon     | Action             |
|------------|------------|----------|--------------------|
| Directory  | (dir)      | Folder   | Open in new window |
| Executable | .sys, .prg | Program  | Execute            |
| Text       | .txt, .md  | Document | Open in editor     |
| Image      | .bmp       | Picture  | Open in viewer     |
| Archive    | .zip, .lha | Package  | Show contents      |
| Unknown    | *          | Generic  | Show properties    |

#### Navigation Features

- Parent directory button (↑)
- Home button (returns to root)
- Address bar with editable path
- Breadcrumb navigation
- Back/Forward history

### 2.3 File Operations

#### Drag and Drop

- Drag files between windows to copy/move
- Drag to Trash icon to delete
- Visual feedback during drag (ghost icon)
- Hold Shift while dragging to move instead of copy

#### Context Menu (Right-Click)

```
┌──────────────────┐
│ Open             │
│ Open With...     │
├──────────────────┤
│ Cut              │
│ Copy             │
│ Paste            │
├──────────────────┤
│ Rename           │
│ Delete           │
├──────────────────┤
│ Properties       │
└──────────────────┘
```

#### Keyboard Shortcuts

| Key    | Action          |
|--------|-----------------|
| Enter  | Open selected   |
| Delete | Delete selected |
| Ctrl+C | Copy            |
| Ctrl+X | Cut             |
| Ctrl+V | Paste           |
| Ctrl+A | Select all      |
| F2     | Rename          |
| F5     | Refresh         |

### 2.4 File Type Associations

**File:** `user/workbench/filetypes.c`

```c
typedef struct {
    const char *extension;
    const char *mime_type;
    const char *default_app;
    const char *icon_name;
} filetype_t;

static filetype_t g_filetypes[] = {
    { ".txt",  "text/plain",       "/sys/edit.prg",    "icon_text" },
    { ".md",   "text/markdown",    "/sys/edit.prg",    "icon_text" },
    { ".c",    "text/x-c",         "/sys/edit.prg",    "icon_source" },
    { ".h",    "text/x-c-header",  "/sys/edit.prg",    "icon_source" },
    { ".bmp",  "image/bmp",        "/sys/viewer.prg",  "icon_image" },
    { ".sys",  "application/x-viper", NULL,            "icon_program" },
    { ".prg",  "application/x-viper", NULL,            "icon_program" },
    { NULL, NULL, NULL, NULL }
};
```

### 2.5 Files to Create/Modify

| File                           | Purpose                            |
|--------------------------------|------------------------------------|
| `user/workbench/drives.c`      | Drive discovery and management     |
| `user/workbench/filebrowser.c` | File browser window implementation |
| `user/workbench/filetypes.c`   | File type associations             |
| `user/workbench/fileops.c`     | Copy, move, delete operations      |
| `user/workbench/icons/`        | Additional file type icons         |

---

## Phase 3: Preferences and System Utilities

**Priority:** MEDIUM
**Estimated Effort:** Medium
**Dependencies:** Phase 2

### 3.1 Preferences Application (Amiga-style "Prefs")

**File:** `user/prefs/main.c`

A centralized preferences application similar to Amiga Prefs. Uses Amiga terminology and design patterns.

#### Preferences Categories

```
┌─────────────────────────────────────────────────────────┐
│ ≡ Preferences                                   _ □ X  │
├─────────────────────────────────────────────────────────┤
│ ┌─────────────┐                                         │
│ │ [🖥] Screen  │  Screen Preferences                    │
│ │ [🎨] Palette │  ──────────────────                    │
│ │ [🖱] Pointer │                                        │
│ │ [⌨] Input   │  Resolution: [1024x768 ▼]              │
│ │ [🔊] Sound  │                                        │
│ │ [🌐] Network│  Backdrop:   [Solid Blue ▼] [Choose...] │
│ │ [📅] Time   │                                        │
│ │ [?] About   │  □ Show icons on desktop               │
│ └─────────────┘  □ Double-click to open                │
│                                                         │
│                  [Use]  [Cancel]  [Save]                │
└─────────────────────────────────────────────────────────┘
```

Note: The About icon uses an Amiga-style question mark "?" as was traditional on AmigaOS.

#### 3.1.1 Screen Preferences

- Screen resolution (query available modes)
- Backdrop selection (solid color or pattern, Amiga-style)
- Icon arrangement (grid size, auto-arrange)
- Workbench palette (window colors)

#### 3.1.2 Input Preferences

**Pointer:**

- Pointer speed (acceleration)
- Double-click delay
- Left/right hand mode (swap buttons)
- Scroll wheel speed

**Input:**

- Key repeat delay
- Key repeat rate
- Keyboard layout (future)

#### 3.1.3 Locale Preferences

- Set system date and time
- Time zone selection
- Clock format (12/24 hour)
- Show clock in title bar

#### 3.1.4 Network Preferences

- View IP address
- Configure network (DHCP/static)
- DNS servers
- Hostname

#### 3.1.5 About (Question Mark "?" Icon)

- ViperDOS version
- Kernel version
- Memory usage
- CPU information
- Uptime

### 3.2 System Information Utility

**File:** `user/sysinfo/main.c`

Display detailed system information in a graphical window.

```
┌─────────────────────────────────────────────────────────┐
│ ≡ System Information                            _ □ X  │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ViperDOS v0.4.0                                       │
│  ════════════════                                       │
│                                                         │
│  Kernel:     Viper Microkernel 1.0                     │
│  Platform:   AArch64                                    │
│  CPU:        QEMU Virtual CPU                          │
│  Memory:     128 MB total, 64 MB free                  │
│  Uptime:     1 day, 4:23:15                            │
│                                                         │
│  ── Servers ──────────────────────────────             │
│  displayd    Running  PID 3                            │
│  consoled    Running  PID 5                            │
│  netd        Running  PID 4                            │
│  fsd         Running  PID 6                            │
│                                                         │
│  ── Storage ──────────────────────────────             │
│  SYS:        2.0 MB / 4.0 MB (50%)                     │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 3.3 Task Manager

**File:** `user/taskman/main.c`

View and manage running processes.

```
┌─────────────────────────────────────────────────────────┐
│ ≡ Task Manager                                  _ □ X  │
├─────────────────────────────────────────────────────────┤
│ Process        PID    Memory    CPU    State           │
│ ─────────────────────────────────────────────          │
│ kernel           0      8 MB     2%    Running         │
│ vinit            1     12 MB     1%    Running         │
│ displayd         3      4 MB     5%    Running         │
│ consoled         5      2 MB     1%    Running         │
│ workbench        7      8 MB     3%    Running         │
│ ─────────────────────────────────────────────          │
│                                                         │
│ Total: 5 processes, 34 MB used                         │
│                                                         │
│ [End Task]  [New Task...]  [Refresh]                   │
└─────────────────────────────────────────────────────────┘
```

### 3.4 Configuration Storage

**File:** `user/libprefs/prefs.c`

Simple preferences library for applications.

```c
// Preferences API
int prefs_open(const char *app_name);
int prefs_close(void);

int prefs_get_int(const char *key, int default_val);
const char *prefs_get_string(const char *key, const char *default_val);
int prefs_set_int(const char *key, int value);
int prefs_set_string(const char *key, const char *value);

int prefs_save(void);
```

**Storage format:** Simple INI-style files in `/sys/prefs/`

```ini
# /sys/prefs/workbench.prefs
[display]
wallpaper=none
icon_grid=80
double_click_speed=400

[mouse]
speed=5
swap_buttons=0
```

### 3.5 Files to Create

| File                    | Purpose                    |
|-------------------------|----------------------------|
| `user/prefs/main.c`     | Preferences application    |
| `user/prefs/screen.c`   | Screen preferences panel   |
| `user/prefs/input.c`    | Pointer/input preferences  |
| `user/prefs/locale.c`   | Locale/time preferences    |
| `user/prefs/network.c`  | Network preferences        |
| `user/sysinfo/main.c`   | System information utility |
| `user/taskman/main.c`   | Task manager               |
| `user/libprefs/prefs.c` | Preferences library        |

---

## Phase 4: Widget Toolkit (libwidget)

**Priority:** MEDIUM
**Estimated Effort:** HIGH
**Dependencies:** Phase 1

A lightweight widget toolkit to simplify application development.

### 4.1 Core Widget System

**File:** `user/libwidget/include/widget.h`

```c
// Widget types
typedef enum {
    WIDGET_WINDOW,
    WIDGET_BUTTON,
    WIDGET_LABEL,
    WIDGET_TEXTBOX,
    WIDGET_CHECKBOX,
    WIDGET_LISTVIEW,
    WIDGET_TREEVIEW,
    WIDGET_MENU,
    WIDGET_MENUITEM,
    WIDGET_TOOLBAR,
    WIDGET_STATUSBAR,
    WIDGET_PANEL,
    WIDGET_SCROLLBAR,
    WIDGET_COMBOBOX,
    WIDGET_PROGRESSBAR,
} widget_type_t;

// Base widget structure
typedef struct widget {
    widget_type_t type;
    struct widget *parent;
    struct widget **children;
    int child_count;

    int x, y, width, height;
    bool visible;
    bool enabled;
    bool focused;

    uint32_t bg_color;
    uint32_t fg_color;

    // Event callbacks
    void (*on_paint)(struct widget *w, gui_window_t *win);
    void (*on_click)(struct widget *w, int x, int y, int button);
    void (*on_key)(struct widget *w, int keycode, char ch);
    void (*on_focus)(struct widget *w, bool gained);
    void *user_data;
} widget_t;

// Core functions
widget_t *widget_create(widget_type_t type, widget_t *parent);
void widget_destroy(widget_t *w);
void widget_set_geometry(widget_t *w, int x, int y, int width, int height);
void widget_set_visible(widget_t *w, bool visible);
void widget_set_enabled(widget_t *w, bool enabled);
void widget_repaint(widget_t *w);
void widget_set_focus(widget_t *w);
```

### 4.2 Standard Widgets

#### Button

```c
typedef struct {
    widget_t base;
    char text[64];
    bool pressed;
    void (*on_click)(void *user_data);
} button_t;

button_t *button_create(widget_t *parent, const char *text);
void button_set_text(button_t *btn, const char *text);
void button_set_onclick(button_t *btn, void (*callback)(void*), void *data);
```

#### Label

```c
typedef struct {
    widget_t base;
    char text[256];
    int alignment;  // ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT
} label_t;

label_t *label_create(widget_t *parent, const char *text);
void label_set_text(label_t *lbl, const char *text);
void label_set_alignment(label_t *lbl, int align);
```

#### TextBox

```c
typedef struct {
    widget_t base;
    char text[1024];
    int cursor_pos;
    int scroll_offset;
    int selection_start;
    int selection_end;
    bool password_mode;
    bool multiline;
    void (*on_change)(void *user_data);
    void (*on_enter)(void *user_data);
} textbox_t;

textbox_t *textbox_create(widget_t *parent);
void textbox_set_text(textbox_t *tb, const char *text);
const char *textbox_get_text(textbox_t *tb);
void textbox_set_password_mode(textbox_t *tb, bool enabled);
```

#### ListView

```c
typedef struct {
    widget_t base;
    char **items;
    int item_count;
    int selected_index;
    int scroll_offset;
    bool multi_select;
    void (*on_select)(int index, void *user_data);
    void (*on_double_click)(int index, void *user_data);
} listview_t;

listview_t *listview_create(widget_t *parent);
void listview_add_item(listview_t *lv, const char *text);
void listview_remove_item(listview_t *lv, int index);
void listview_clear(listview_t *lv);
int listview_get_selected(listview_t *lv);
```

#### TreeView

```c
typedef struct tree_node {
    char text[64];
    struct tree_node *children;
    int child_count;
    bool expanded;
    void *user_data;
} tree_node_t;

typedef struct {
    widget_t base;
    tree_node_t *root;
    tree_node_t *selected;
    int scroll_offset;
    void (*on_select)(tree_node_t *node, void *user_data);
    void (*on_expand)(tree_node_t *node, void *user_data);
} treeview_t;
```

#### Menu System

```c
typedef struct menu_item {
    char text[64];
    char shortcut[16];      // e.g., "Ctrl+S"
    bool separator;
    bool checked;
    bool enabled;
    struct menu *submenu;
    void (*on_click)(void *user_data);
} menu_item_t;

typedef struct menu {
    menu_item_t *items;
    int item_count;
    bool visible;
    int x, y;
} menu_t;

menu_t *menu_create(void);
void menu_add_item(menu_t *m, const char *text, void (*callback)(void*), void *data);
void menu_add_separator(menu_t *m);
void menu_show(menu_t *m, int x, int y);
void menu_hide(menu_t *m);
```

### 4.3 Layout Managers

```c
typedef enum {
    LAYOUT_NONE,        // Manual positioning
    LAYOUT_HORIZONTAL,  // Left to right
    LAYOUT_VERTICAL,    // Top to bottom
    LAYOUT_GRID,        // Row/column grid
    LAYOUT_BORDER,      // North/South/East/West/Center
} layout_type_t;

typedef struct {
    layout_type_t type;
    int spacing;
    int margin_left, margin_top, margin_right, margin_bottom;
    int columns;        // For grid layout
} layout_t;

void widget_set_layout(widget_t *container, layout_t *layout);
void widget_add_to_layout(widget_t *container, widget_t *child, int constraints);
void layout_apply(widget_t *container);
```

### 4.4 Dialog Boxes

```c
// Message box
typedef enum {
    MB_OK,
    MB_OK_CANCEL,
    MB_YES_NO,
    MB_YES_NO_CANCEL,
} msgbox_type_t;

typedef enum {
    MB_ICON_INFO,
    MB_ICON_WARNING,
    MB_ICON_ERROR,
    MB_ICON_QUESTION,
} msgbox_icon_t;

int msgbox_show(const char *title, const char *message,
                msgbox_type_t type, msgbox_icon_t icon);

// File dialog
char *filedialog_open(const char *title, const char *filter, const char *initial_dir);
char *filedialog_save(const char *title, const char *filter, const char *initial_dir);
char *filedialog_folder(const char *title, const char *initial_dir);
```

### 4.5 Amiga-Style 3D Effects

```c
// Draw 3D raised border (button style)
void draw_3d_raised(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow);

// Draw 3D sunken border (input field style)
void draw_3d_sunken(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow);

// Amiga color palette
#define WB_GRAY_LIGHT   0xFFAAAAAA
#define WB_GRAY_MED     0xFF888888
#define WB_GRAY_DARK    0xFF555555
#define WB_BLUE         0xFF0055AA
#define WB_ORANGE       0xFFFF8800
#define WB_WHITE        0xFFFFFFFF
#define WB_BLACK        0xFF000000
```

### 4.6 Files to Create

| File                              | Purpose            |
|-----------------------------------|--------------------|
| `user/libwidget/include/widget.h` | Widget API header  |
| `user/libwidget/src/widget.c`     | Core widget system |
| `user/libwidget/src/button.c`     | Button widget      |
| `user/libwidget/src/label.c`      | Label widget       |
| `user/libwidget/src/textbox.c`    | TextBox widget     |
| `user/libwidget/src/listview.c`   | ListView widget    |
| `user/libwidget/src/treeview.c`   | TreeView widget    |
| `user/libwidget/src/menu.c`       | Menu system        |
| `user/libwidget/src/layout.c`     | Layout managers    |
| `user/libwidget/src/dialog.c`     | Dialog boxes       |
| `user/libwidget/src/draw3d.c`     | 3D drawing effects |

---

## Phase 5: Desktop Applications

**Priority:** LOW
**Estimated Effort:** HIGH
**Dependencies:** Phases 2-4

### 5.1 File Manager (FileMgr)

**File:** `user/filemgr/main.c`

Full-featured file manager application.

#### Features

- Dual-pane view (optional)
- Icon view and list view modes
- Thumbnail previews for images
- File search functionality
- Batch operations (multi-select)
- Archive support (view/extract .zip, .lha)
- FTP/SFTP integration (via netd)

### 5.2 Text Editor (VEdit - "Viper Edit")

**File:** `user/vedit/main.cpp`

A GUI text editor inspired by classic Amiga editors (like ED and CygnusEd). Named "VEdit" following Amiga naming
conventions.

#### Features

- Single document editing (multi-tab is Phase 6 polish)
- Line numbers display
- Find and replace (Amiga: Ctrl+F, Ctrl+R)
- Go to line (Amiga: Ctrl+J for "Jump")
- Word wrap toggle
- Status bar with line/column info
- File changed indicator (asterisk in title)
- Recent files menu

See **Appendix C: VEdit Detailed Specification** for complete implementation details.

### 5.3 Image Viewer (Viewer)

**File:** `user/viewer/main.c`

View image files.

#### Features

- Support BMP format (native)
- Zoom in/out
- Fit to window
- Slideshow mode
- Basic editing (crop, rotate)

### 5.4 Calculator (Calc)

**File:** `user/calc/main.c`

Simple calculator utility.

#### Features

- Basic arithmetic (+, -, *, /)
- Memory functions (M+, M-, MR, MC)
- Scientific mode (optional)
- Programmer mode (hex, binary)

### 5.5 Clock/Calendar (Clock)

**File:** `user/clock/main.c`

Analog/digital clock with calendar.

#### Features

- Analog clock face
- Digital time display
- Monthly calendar view
- Alarm function (optional)

### 5.6 Terminal (Term)

Enhanced terminal emulator beyond consoled.

#### Features

- Multiple terminal tabs
- Scrollback history (configurable)
- Copy/paste support
- Configurable colors and fonts
- Session logging

---

## Phase 6: Polish and Integration

**Priority:** LOW
**Estimated Effort:** MEDIUM
**Dependencies:** Phases 1-5

### 6.1 Visual Polish

#### Theme System

- Configurable color schemes
- Save/load themes from files
- Built-in themes: Classic Amiga, ViperDOS Modern, Dark Mode

#### Improved Icons

- Higher resolution icons (32x32, 48x48)
- Icon scaling for different DPI
- Animated icons (optional, for loading states)

#### Fonts

- Multiple font sizes
- Bold/italic support
- Unicode support (extended character set)

### 6.2 System Integration

#### Startup Sequence

1. Kernel boots, launches vinit
2. vinit launches displayd and workbench
3. Workbench becomes default GUI shell
4. vinit shell available via Shell icon

#### Auto-start Applications

- `/sys/prefs/startup.prefs` lists apps to launch at boot
- Workbench reads and launches startup apps

#### Shutdown Sequence

1. User clicks Shutdown in Workbench menu
2. Workbench sends shutdown signal to all apps
3. Apps save state and exit gracefully
4. Kernel performs shutdown

### 6.3 Clipboard Support

**File:** `user/libgui/clipboard.c`

```c
int clipboard_set_text(const char *text);
char *clipboard_get_text(void);
int clipboard_set_data(const char *mime_type, const void *data, size_t len);
void *clipboard_get_data(const char *mime_type, size_t *len);
int clipboard_has_format(const char *mime_type);
```

### 6.4 Drag and Drop Protocol

**File:** `user/servers/displayd/dnd.c`

```c
// Drag source
int dnd_begin(uint32_t surface_id, const char *mime_types[], void *data, size_t len);
void dnd_set_icon(const uint32_t *pixels, int width, int height);
void dnd_cancel(void);

// Drop target
void dnd_accept(uint32_t surface_id, const char *mime_types[]);
int dnd_receive(void *buffer, size_t max_len, char *mime_type);
```

### 6.5 Sound Support (Future)

- System sounds for events (click, error, notification)
- Audio playback API
- Volume control in Prefs

---

## Implementation Schedule

### Milestone 0: Infrastructure Improvements (Phase 1.5) - MOSTLY COMPLETE

**Status:** 90% COMPLETE

- [x] Increase resolution to 1024x768
- [x] Centralize color definitions
- [x] Remove scattered color changes for consistent boot appearance
- [x] Refactor workbench to C++ with modular architecture
- [x] File browser window (FUNCTIONAL - directory navigation works)
- [x] Integrate with VFS for directory listing
- [x] File type detection and icons
- [x] Double-click to navigate/launch
- [ ] Amiga-style icon updates (Prefs icon, question mark About)
- [ ] Dynamic drive discovery

### Milestone 1: File System Desktop (Phase 2) - MOSTLY COMPLETE

- [ ] Drive discovery and icons on desktop (dynamic mount detection)
- [x] File browser window (DONE)
- [x] Basic file operations (copy, paste, delete, new folder)
- [x] File type associations (infrastructure ready, apps TBD)
- [x] Context menus (right-click with Open, Copy, Delete, Rename, Properties)
- [x] Keyboard shortcuts (Enter, Delete, F5, C, V, N)
- [x] Status bar with file info and hints
- [ ] Rename with inline editor (placeholder)

### Milestone 2: System Utilities (Phase 3) - MOSTLY COMPLETE

- [x] Preferences application (Amiga-style "Prefs") - `user/prefs/main.cpp`
- [x] System information utility - `user/guisysinfo/main.cpp`
- [x] Task manager - `user/taskman/main.cpp`
- [ ] Preferences library (libprefs) - deferred to Phase 4

### Milestone 3: Widget Toolkit (Phase 4)

**Duration:** 3-4 weeks

- [ ] Core widget system
- [ ] Button, Label, TextBox
- [ ] ListView, TreeView
- [ ] Menu system
- [ ] Layout managers
- [ ] Dialog boxes

### Milestone 4: Applications (Phase 5)

**Duration:** 4+ weeks

- [ ] File Manager
- [ ] Text Editor
- [ ] Image Viewer
- [ ] Calculator
- [ ] Clock/Calendar

### Milestone 5: Polish (Phase 6)

**Duration:** 2 weeks

- [ ] Theme system
- [ ] Clipboard
- [ ] Drag and drop
- [ ] Documentation

---

## File Structure

```
viperdos/
├── user/
│   ├── include/
│   │   └── viper_colors.h       # Centralized color definitions (DONE)
│   │
│   ├── workbench/
│   │   ├── include/
│   │   │   ├── colors.hpp       # Color aliases for workbench (DONE)
│   │   │   ├── desktop.hpp      # Desktop class header (DONE)
│   │   │   ├── filebrowser.hpp  # File browser header (DONE)
│   │   │   ├── icons.hpp        # Icon definitions (DONE)
│   │   │   └── types.hpp        # Common types (DONE)
│   │   ├── src/
│   │   │   ├── main.cpp         # Entry point (DONE)
│   │   │   ├── desktop.cpp      # Desktop implementation (DONE)
│   │   │   ├── filebrowser.cpp  # File browser (FUNCTIONAL)
│   │   │   └── icons.cpp        # Icon rendering (DONE)
│   │   ├── drives.cpp           # Drive management (Phase 2)
│   │   ├── filetypes.cpp        # File associations (Phase 2)
│   │   ├── fileops.cpp          # File operations (Phase 2)
│   │   └── icons/               # Icon resources
│   │
│   ├── prefs/                   # Amiga-style Preferences (DONE)
│   │   └── main.cpp             # Prefs app with category sidebar
│   │
│   ├── guisysinfo/              # GUI System Information (DONE)
│   │   └── main.cpp             # System info utility
│   │
│   ├── taskman/                 # GUI Task Manager (DONE)
│   │   └── main.cpp             # Process viewer with selection
│   │
│   ├── libwidget/
│   │   ├── include/widget.h     # Widget API (Phase 4)
│   │   └── src/
│   │       ├── widget.c
│   │       ├── button.c
│   │       ├── label.c
│   │       ├── textbox.c
│   │       ├── listview.c
│   │       ├── treeview.c
│   │       ├── menu.c
│   │       ├── layout.c
│   │       └── dialog.c
│   │
│   ├── libprefs/
│   │   ├── include/prefs.h      # Preferences API (Phase 3)
│   │   └── src/prefs.c
│   │
│   ├── filemgr/
│   │   └── main.c               # File manager (Phase 5)
│   │
│   ├── edit/
│   │   └── main.c               # Text editor (Phase 5)
│   │
│   ├── viewer/
│   │   └── main.c               # Image viewer (Phase 5)
│   │
│   ├── calc/
│   │   └── main.c               # Calculator (Phase 5)
│   │
│   └── clock/
│       └── main.c               # Clock/calendar (Phase 5)
│
└── plans/
    └── workbench_plan.md        # This document
```

---

## Testing Checklist

### Phase 2 Tests

- [ ] Drive icons appear on desktop (dynamic detection)
- [x] Double-click drive opens file browser
- [x] Navigate into folders
- [x] Navigate to parent directory
- [x] Double-click file launches associated app (.sys/.prg)
- [x] Copy file (via context menu or keyboard)
- [x] Paste file to current directory
- [x] Delete file (via context menu or Delete key)
- [x] Create new folder (via context menu)
- [ ] Rename file (inline editor TBD)
- [x] Context menu appears on right-click
- [x] Status bar shows selected file info
- [x] F5 refreshes directory

### Phase 3 Tests

- [x] Prefs app opens (read-only settings display)
- [ ] Display settings apply correctly (read-only in v1)
- [ ] Mouse speed changes take effect (read-only in v1)
- [x] System info shows correct values (mem_info, task_list, uptime)
- [x] Task manager lists processes (auto-refresh, selection, keyboard nav)
- [ ] Preferences save and load (requires libprefs)

### Phase 4 Tests

- [ ] Button click fires callback
- [ ] TextBox accepts input
- [ ] ListView selection works
- [ ] TreeView expands/collapses
- [ ] Menus open and close
- [ ] Layouts position widgets correctly
- [ ] Dialogs display modally

### Phase 5 Tests

- [ ] File manager navigates filesystem
- [ ] Text editor loads and saves files
- [ ] Image viewer displays BMP files
- [ ] Calculator performs arithmetic
- [ ] Clock shows correct time

---

## Success Criteria

The ViperDOS Workbench will be considered complete when:

1. **Functional Desktop**: Users can navigate the filesystem, launch applications, and manage windows entirely through
   the GUI.

2. **Self-Hosting**: The desktop environment can be used to develop and build ViperDOS itself (edit source files, run
   compiler, etc.).

3. **Stability**: The GUI runs without crashes for extended periods under normal use.

4. **Performance**: Window operations (move, resize, switch) feel responsive with no perceptible lag.

5. **Discoverability**: New users can figure out basic operations without documentation.

6. **Aesthetic**: The desktop maintains a consistent Amiga-inspired visual style throughout.

---

## Appendix A: Amiga Workbench Design Principles

The original Amiga Workbench established several design principles we should follow:

1. **Icons represent objects** - Files, disks, and applications are all icons you can manipulate.

2. **Drag and drop is fundamental** - Moving files should be as simple as dragging icons.

3. **Windows show contents** - Opening a disk or folder shows its contents in a window.

4. **Minimal chrome** - Window decorations are functional but unobtrusive.

5. **Immediate feedback** - Actions show results instantly (selection highlight, drag ghost).

6. **Consistent metaphor** - The desktop metaphor is applied consistently throughout.

7. **Power user features** - CLI is always available for advanced operations.

---

## Appendix B: Color Palette Reference

**All colors are now centralized in `user/include/viper_colors.h`**

### Classic Workbench Colors

```c
// From user/include/viper_colors.h
#define VIPER_COLOR_DESKTOP      0xFF0055AA  // Primary backdrop (Amiga blue)
#define VIPER_COLOR_BORDER       0xFF003366  // Window borders
#define VIPER_COLOR_WHITE        0xFFFFFFFF  // Highlights, text
#define VIPER_COLOR_BLACK        0xFF000000  // Outlines, shadows
#define VIPER_COLOR_ORANGE       0xFFFF8800  // Selection highlight
#define VIPER_COLOR_GRAY_LIGHT   0xFFAAAAAA  // Window backgrounds
#define VIPER_COLOR_GRAY_MED     0xFF888888  // Inactive elements
#define VIPER_COLOR_GRAY_DARK    0xFF555555  // Borders, shadows
```

### Console/Terminal Colors

```c
#define VIPER_COLOR_TEXT         0xFFFFFFFF  // Default text (white)
#define VIPER_COLOR_CONSOLE_BG   0xFF0055AA  // Console background (matches desktop)
```

### ViperDOS Accent Colors

```c
#define VIPER_COLOR_GREEN        0xFF00AA44  // System accent
#define VIPER_COLOR_RED          0xFFFF4444  // Error/warning
#define VIPER_COLOR_YELLOW       0xFFFFFF00  // Caution
#define VIPER_COLOR_BLUE         0xFF4444FF  // Links/info
```

### ANSI Color Palette

The file also defines `ANSI_COLOR_*` macros for terminal emulation.

---

## Appendix C: VEdit (GUI Text Editor) Detailed Specification

VEdit is the primary GUI text editor for ViperDOS, inspired by classic Amiga editors.

### Window Layout

```
┌─────────────────────────────────────────────────────────┐
│ ≡ VEdit: untitled.txt                           _ □ X  │
├─────────────────────────────────────────────────────────┤
│ File  Edit  Search  Settings  Help                      │
├─────────────────────────────────────────────────────────┤
│   1│ This is line one                                   │
│   2│ This is line two with more text                    │
│   3│ Cursor is here: █                                  │
│   4│                                                    │
│   5│                                                    │
│    │                                                    │
│    │                                                    │
│    │                                                    │
├─────────────────────────────────────────────────────────┤
│ Line: 3  Col: 17  │ Modified │ INS │ UTF-8             │
└─────────────────────────────────────────────────────────┘
```

### Menu Structure

**File Menu:**

- New (Ctrl+N)
- Open... (Ctrl+O)
- Save (Ctrl+S)
- Save As... (Ctrl+Shift+S)

- ---

- Print... (future)

- ---

- Quit (Ctrl+Q)

**Edit Menu:**

- Undo (Ctrl+Z)
- Redo (Ctrl+Y)

- ---

- Cut (Ctrl+X)
- Copy (Ctrl+C)
- Paste (Ctrl+V)

- ---

- Select All (Ctrl+A)

**Search Menu:**

- Find... (Ctrl+F)
- Find Next (F3)
- Find Previous (Shift+F3)
- Replace... (Ctrl+R)

- ---

- Go to Line... (Ctrl+J)

**Settings Menu:**

- Word Wrap (toggle)
- Show Line Numbers (toggle)
- Tab Width: 4 (submenu)

- ---

- Font Size (submenu)

**Help Menu:**

- About VEdit...

### Core Implementation

**File:** `user/vedit/`

```
user/vedit/
├── CMakeLists.txt
├── include/
│   ├── buffer.hpp      # Text buffer with gap buffer implementation
│   ├── editor.hpp      # Main editor class
│   ├── view.hpp        # Viewport/rendering
│   └── commands.hpp    # Edit commands
└── src/
    ├── main.cpp        # Entry point
    ├── buffer.cpp      # Gap buffer text storage
    ├── editor.cpp      # Editor logic
    ├── view.cpp        # Rendering to GUI window
    └── commands.cpp    # Command handlers
```

### Text Buffer (Gap Buffer)

```cpp
class TextBuffer {
public:
    bool load(const char *path);
    bool save(const char *path);

    void insert(char ch);
    void insert(const char *text);
    void deleteChar();       // Delete at cursor (Del)
    void backspace();        // Delete before cursor (Backspace)

    void moveCursor(int delta);
    void moveLine(int delta);
    void moveToLineStart();
    void moveToLineEnd();
    void moveToStart();
    void moveToEnd();

    int lineCount() const;
    int currentLine() const;
    int currentColumn() const;
    const char *lineAt(int line) const;

    bool isModified() const;
    void clearModified();

private:
    char *m_buffer;
    size_t m_bufferSize;
    size_t m_gapStart;
    size_t m_gapEnd;
    bool m_modified;
};
```

### Keyboard Handling

| Key          | Action                         |
|--------------|--------------------------------|
| Arrow keys   | Move cursor                    |
| Home         | Go to line start               |
| End          | Go to line end                 |
| Ctrl+Home    | Go to file start               |
| Ctrl+End     | Go to file end                 |
| Page Up/Down | Scroll by page                 |
| Insert       | Toggle insert/overwrite        |
| Delete       | Delete character at cursor     |
| Backspace    | Delete character before cursor |
| Enter        | Insert newline                 |
| Tab          | Insert tab (or spaces)         |

### File Types

VEdit should handle `.txt` and source files:

- `.txt`, `.md` - Plain text
- `.c`, `.cpp`, `.h`, `.hpp` - C/C++ source (syntax highlighting future)
- `.sh` - Shell scripts
- `.conf`, `.ini` - Config files

### Dependencies

VEdit requires:

- libgui for window/drawing
- libwidget (Phase 4) for menus and dialogs (or can use simple custom implementation initially)

### Testing Checklist

- [ ] Create new empty document
- [ ] Open existing file
- [ ] Edit text (insert, delete, backspace)
- [ ] Save file
- [ ] Save As new filename
- [ ] Modified indicator (*) in title
- [ ] Line/column display updates
- [ ] Word wrap toggle works
- [ ] Find text
- [ ] Find and replace
- [ ] Go to line number
- [ ] Handle large files (1MB+)
- [ ] Undo/redo basic operations

---

## Appendix D: Amiga-Style Icon Specifications

### Desktop Icons Update

The following icon changes are needed for Amiga authenticity:

| Current       | New        | Description                          |
|---------------|------------|--------------------------------------|
| `settings_24` | `prefs_24` | Wrench/tool icon for Preferences     |
| `about_24`    | `help_24`  | Question mark "?" icon (Amiga-style) |

### Question Mark Icon Design (24x24)

Classic Amiga help icon is a bold "?" in a rounded rectangle or speech bubble:

```
........????????........
......??........??......
.....?..........???.....
....??...........??.....
....??...........??.....
....??..........????.....
.....??........???......
......??......???........
.......??.....??........
........??...??..........
.........??.??...........
..........???............
...........??.............
..........???............
.........????.............
.........????.............
..........???............
...........??...........
............??...........
...........???...........
...........???...........
...........???...........
............??...........
..........................
```

The icon should use:

- White "?" glyph
- Dark blue or gray background
- 3D beveled edge (Amiga style)

### Preferences Icon Design (24x24)

A wrench or gear icon:

- Gray metal wrench
- 3D shading for depth
- Matches Amiga Prefs drawer icon style

### Implementation

Update `user/workbench/src/icons.cpp` with new pixel arrays:

- `const uint32_t help_24[24 * 24]` - Question mark icon
- `const uint32_t prefs_24[24 * 24]` - Preferences/wrench icon

Update `user/workbench/src/desktop.cpp`:

```cpp
// Before:
m_icons[2] = { 0, 0, "Settings", nullptr, icons::settings_24, ... };
m_icons[3] = { 0, 0, "About",    nullptr, icons::about_24,    ... };

// After:
m_icons[2] = { 0, 0, "Prefs",  nullptr, icons::prefs_24, ... };
m_icons[3] = { 0, 0, "Help",   nullptr, icons::help_24,  ... };
```

---

*Document Version: 1.5*
*Last Updated: January 24, 2025*
*Author: ViperDOS Development Team*

### Changelog

**v1.5 (2025-01-24)**

- Phase 3 MOSTLY COMPLETE: GUI system utilities implemented
- Created guisysinfo (GUI System Information): displays memory, tasks, uptime
- Created taskman (GUI Task Manager): process list with selection, auto-refresh
- Created prefs (GUI Preferences): Amiga-style category sidebar (Screen, Input, Time, About)
- All three utilities use syscalls: mem_info(), task_list(), uptime()
- Programs added to user.img at c/guisysinfo.prg, c/taskman.prg, c/prefs.prg
- Cleaned up user.img: removed test programs (faulttest, fsd_smoke, tls_smoke, mathtest, hello)
- libprefs deferred to Phase 4 (not needed for read-only settings display)

**v1.4 (2025-01-24)**

- Phase 2 COMPLETE: All file system integration features implemented
- Added dynamic drive discovery via assign_list() syscall
- Implemented inline rename editor with full keyboard input
- Desktop dynamically populates volume icons from kernel assigns
- F2 key initiates rename for selected files
- RenameEditor struct tracks cursor, selection, and edit buffer
- Marked Phase 1.5 and Phase 2 as complete

**v1.3 (2025-01-24)**

- Phase 2 implementation: context menus, file operations, keyboard shortcuts
- Implemented: delete, copy, paste, new folder operations
- Added right-click context menu with file-specific options
- Added keyboard shortcuts: Enter (open), Delete, F5 (refresh), C (copy), V (paste), N (new folder)
- Enhanced status bar with file size info and keyboard hints
- File type associations infrastructure (ready for VEdit, Viewer)
- Updated Phase 2 testing checklist with completed items

**v1.2 (2025-01-24)**

- Updated Phase 1.5 status to reflect file browser is FUNCTIONAL (not skeleton)
- Changed "Settings" to "Prefs" throughout (Amiga terminology)
- Added question mark icon specification for Help/About (Amiga style)
- Added Appendix C: VEdit GUI Text Editor detailed specification
- Added Appendix D: Amiga-Style Icon Specifications
- Renamed user/settings to user/prefs throughout

**v1.1 (2025-01-23)**

- Updated to reflect Phase 1.5 progress
- Documented resolution change to 1024x768
- Documented color centralization in `viper_colors.h`
- Updated file structure for C++ refactor
- Added Phase 1.5 deliverables section

**v1.0 (2025-01)**

- Initial plan document
