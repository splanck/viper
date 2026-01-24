# ViperDOS Workbench - Complete Implementation Plan

## Vision

Create a fully-featured Amiga-inspired Workbench desktop environment for ViperDOS that provides:
- Intuitive graphical file system navigation
- Application launching and management
- System configuration utilities
- A cohesive, retro-modern aesthetic

The Workbench will serve as the primary GUI interface for ViperDOS, bringing the beloved Amiga desktop experience to a modern microkernel operating system.

---

## Current State (January 2025)

### Completed Infrastructure

| Component | Status | Description |
|-----------|--------|-------------|
| displayd | **Complete** | Window compositor with double buffering, z-order management |
| libgui | **Complete** | Client library for window creation, drawing, events |
| consoled | **Complete** | GUI terminal emulator with ANSI support |
| workbench | **Phase 1.5 In Progress** | Refactored to C++, modular architecture |
| Event System | **Complete** | Mouse/keyboard events routed to windows |
| Window Management | **Complete** | Focus, z-order, minimize/maximize, dragging |
| Color System | **Complete** | Centralized in `user/include/viper_colors.h` |
| Resolution | **Complete** | Updated to 1024x768 (from 800x600) |

### Phase 1 Deliverables (COMPLETED)

- [x] Amiga-style blue backdrop (`#0055AA`)
- [x] Menu bar with Workbench/Window/Tools menus
- [x] Desktop icons with pixel art (SYS:, Shell, Settings, About)
- [x] Single-click selection with orange highlight
- [x] Double-click to launch applications
- [x] Shell icon launches consoled on demand
- [x] SYSTEM surface z-order (desktop stays behind windows)
- [x] Double buffering eliminates visual flicker
- [x] Window decorations (title bar, close/min/max buttons)

### Phase 1.5 Deliverables (IN PROGRESS)

- [x] Resolution increased to 1024x768
- [x] Centralized color definitions (`user/include/viper_colors.h`)
- [x] Consistent white-on-blue color scheme throughout boot
- [x] Workbench refactored from C to C++ with modular architecture
- [x] Separated into modules: desktop.cpp, icons.cpp, filebrowser.cpp
- [ ] File browser window implementation (skeleton exists)
- [ ] Drive icons showing mounted volumes
- [ ] Basic file/folder navigation

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      Desktop Applications                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐            │
│  │Workbench │ │ FileMgr  │ │ Settings │ │  Editor  │ ...        │
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

| Type | Extension | Icon | Action |
|------|-----------|------|--------|
| Directory | (dir) | Folder | Open in new window |
| Executable | .sys, .prg | Program | Execute |
| Text | .txt, .md | Document | Open in editor |
| Image | .bmp | Picture | Open in viewer |
| Archive | .zip, .lha | Package | Show contents |
| Unknown | * | Generic | Show properties |

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
| Key | Action |
|-----|--------|
| Enter | Open selected |
| Delete | Delete selected |
| Ctrl+C | Copy |
| Ctrl+X | Cut |
| Ctrl+V | Paste |
| Ctrl+A | Select all |
| F2 | Rename |
| F5 | Refresh |

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

| File | Purpose |
|------|---------|
| `user/workbench/drives.c` | Drive discovery and management |
| `user/workbench/filebrowser.c` | File browser window implementation |
| `user/workbench/filetypes.c` | File type associations |
| `user/workbench/fileops.c` | Copy, move, delete operations |
| `user/workbench/icons/` | Additional file type icons |

---

## Phase 3: Settings and System Utilities

**Priority:** MEDIUM
**Estimated Effort:** Medium
**Dependencies:** Phase 2

### 3.1 Settings Application

**File:** `user/settings/main.c`

A centralized preferences application similar to Amiga Prefs.

#### Settings Categories

```
┌─────────────────────────────────────────────────────────┐
│ ≡ Settings                                      _ □ X  │
├─────────────────────────────────────────────────────────┤
│ ┌─────────────┐                                         │
│ │ [🖥] Display │  Display Settings                      │
│ │ [🎨] Colors  │  ─────────────────                     │
│ │ [🖱] Mouse   │                                        │
│ │ [⌨] Keyboard│  Resolution: [1024x768 ▼]              │
│ │ [🔊] Sound  │                                        │
│ │ [🌐] Network│  Wallpaper:  [None ▼] [Browse...]      │
│ │ [📅] DateTime│                                        │
│ │ [ℹ] About   │  □ Show desktop icons                  │
│ └─────────────┘  □ Double-click to open                │
│                                                         │
│                  [Apply]  [Cancel]  [Save]              │
└─────────────────────────────────────────────────────────┘
```

#### 3.1.1 Display Settings
- Screen resolution (query available modes)
- Wallpaper selection (solid color or image)
- Icon arrangement (grid size, auto-arrange)
- Window theme (colors for title bars, borders)

#### 3.1.2 Input Settings

**Mouse:**
- Pointer speed (acceleration)
- Double-click speed
- Left/right hand mode (swap buttons)
- Scroll wheel speed

**Keyboard:**
- Repeat delay
- Repeat rate
- Keyboard layout (future)

#### 3.1.3 Date/Time Settings
- Set system date and time
- Time zone selection
- Clock format (12/24 hour)
- Show clock in menu bar

#### 3.1.4 Network Settings
- View IP address
- Configure network (DHCP/static)
- DNS servers
- Hostname

#### 3.1.5 About System
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

| File | Purpose |
|------|---------|
| `user/settings/main.c` | Settings application |
| `user/settings/display.c` | Display settings panel |
| `user/settings/input.c` | Mouse/keyboard settings |
| `user/settings/datetime.c` | Date/time settings |
| `user/settings/network.c` | Network settings |
| `user/sysinfo/main.c` | System information utility |
| `user/taskman/main.c` | Task manager |
| `user/libprefs/prefs.c` | Preferences library |

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

| File | Purpose |
|------|---------|
| `user/libwidget/include/widget.h` | Widget API header |
| `user/libwidget/src/widget.c` | Core widget system |
| `user/libwidget/src/button.c` | Button widget |
| `user/libwidget/src/label.c` | Label widget |
| `user/libwidget/src/textbox.c` | TextBox widget |
| `user/libwidget/src/listview.c` | ListView widget |
| `user/libwidget/src/treeview.c` | TreeView widget |
| `user/libwidget/src/menu.c` | Menu system |
| `user/libwidget/src/layout.c` | Layout managers |
| `user/libwidget/src/dialog.c` | Dialog boxes |
| `user/libwidget/src/draw3d.c` | 3D drawing effects |

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

### 5.2 Text Editor (Edit)

**File:** `user/edit/main.c`

Simple but functional text editor.

#### Features
- Multiple file tabs
- Syntax highlighting (optional, for .c, .h files)
- Line numbers
- Find and replace (Ctrl+F, Ctrl+H)
- Go to line (Ctrl+G)
- Word wrap toggle
- Recent files list

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
- Volume control in Settings

---

## Implementation Schedule

### Milestone 0: Infrastructure Improvements (Phase 1.5) - CURRENT
**Status:** IN PROGRESS

- [x] Increase resolution to 1024x768
- [x] Centralize color definitions
- [x] Remove scattered color changes for consistent boot appearance
- [x] Refactor workbench to C++ with modular architecture
- [ ] Complete file browser window skeleton
- [ ] Integrate with VFS for directory listing

### Milestone 1: File System Desktop (Phase 2)

- [ ] Drive discovery and icons
- [ ] File browser window
- [ ] Basic file operations (open, copy, delete)
- [ ] File type associations
- [ ] Context menus

### Milestone 2: System Utilities (Phase 3)
**Duration:** 2 weeks

- [ ] Settings application (basic panels)
- [ ] System information utility
- [ ] Task manager
- [ ] Preferences library

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
│   │   │   ├── filebrowser.cpp  # File browser (IN PROGRESS)
│   │   │   └── icons.cpp        # Icon rendering (DONE)
│   │   ├── drives.c             # Drive management (Phase 2)
│   │   ├── filetypes.c          # File associations (Phase 2)
│   │   ├── fileops.c            # File operations (Phase 2)
│   │   └── icons/               # Icon resources
│   │
│   ├── settings/
│   │   ├── main.c               # Settings app (Phase 3)
│   │   ├── display.c
│   │   ├── input.c
│   │   └── datetime.c
│   │
│   ├── sysinfo/
│   │   └── main.c               # System info (Phase 3)
│   │
│   ├── taskman/
│   │   └── main.c               # Task manager (Phase 3)
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
- [ ] Drive icons appear on desktop
- [ ] Double-click drive opens file browser
- [ ] Navigate into folders
- [ ] Navigate to parent directory
- [ ] Double-click file launches associated app
- [ ] Copy file between windows
- [ ] Delete file to trash
- [ ] Rename file
- [ ] Context menu appears on right-click

### Phase 3 Tests
- [ ] Settings app opens
- [ ] Display settings apply correctly
- [ ] Mouse speed changes take effect
- [ ] System info shows correct values
- [ ] Task manager lists processes
- [ ] Preferences save and load

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

1. **Functional Desktop**: Users can navigate the filesystem, launch applications, and manage windows entirely through the GUI.

2. **Self-Hosting**: The desktop environment can be used to develop and build ViperDOS itself (edit source files, run compiler, etc.).

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

*Document Version: 1.1*
*Last Updated: January 23, 2025*
*Author: ViperDOS Development Team*

### Changelog

**v1.1 (2025-01-23)**
- Updated to reflect Phase 1.5 progress
- Documented resolution change to 1024x768
- Documented color centralization in `viper_colors.h`
- Updated file structure for C++ refactor
- Added Phase 1.5 deliverables section

**v1.0 (2025-01)**
- Initial plan document
