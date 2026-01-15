# GUI Level-Up Implementation Plan

**Goal:** Prepare the Viper GUI widget library for IDE-quality development
**Target:** JetBrains/VSCode-level widget capabilities
**Document Created:** 2026-01-14

---

## Table of Contents

1. [Phase 1 - Critical Foundation](#phase-1---critical-foundation)
2. [Phase 2 - File Operations](#phase-2---file-operations)
3. [Phase 3 - Editor Features](#phase-3---editor-features)
4. [Phase 4 - Polish](#phase-4---polish)
5. [Appendix A - Data Structures](#appendix-a---data-structures)
6. [Appendix B - Testing Strategy](#appendix-b---testing-strategy)

---

## Phase 1 - Critical Foundation

### 1.1 StatusBar Widget

**Files to Create:**
- `src/lib/gui/include/vg_statusbar.h`
- `src/lib/gui/src/widgets/vg_statusbar.c`

**Files to Modify:**
- `src/lib/gui/include/vg_widget.h` - Already has `VG_WIDGET_STATUSBAR` in enum
- `src/lib/gui/src/vg_widget.c` - Add vtable registration
- `src/lib/gui/CMakeLists.txt` - Add source file

#### Data Structures

```c
// Status bar item types
typedef enum {
    VG_STATUSBAR_ITEM_TEXT,      // Simple text label
    VG_STATUSBAR_ITEM_BUTTON,    // Clickable text
    VG_STATUSBAR_ITEM_PROGRESS,  // Progress indicator
    VG_STATUSBAR_ITEM_SEPARATOR, // Visual separator
    VG_STATUSBAR_ITEM_SPACER     // Flexible space
} vg_statusbar_item_type_t;

// Individual status bar item
typedef struct {
    vg_statusbar_item_type_t type;
    char* text;                  // Display text (owned, strdup'd)
    char* tooltip;               // Hover tooltip (owned)
    uint32_t min_width;          // Minimum width in pixels (0 = auto)
    uint32_t max_width;          // Maximum width (0 = unlimited)
    bool visible;                // Whether item is shown
    float progress;              // 0.0-1.0 for progress items
    void* user_data;             // Application data
    void (*on_click)(struct vg_statusbar_item_t* item, void* user_data);
} vg_statusbar_item_t;

// Status bar zones
typedef enum {
    VG_STATUSBAR_ZONE_LEFT,
    VG_STATUSBAR_ZONE_CENTER,
    VG_STATUSBAR_ZONE_RIGHT
} vg_statusbar_zone_t;

// Main status bar widget
typedef struct {
    vg_widget_t base;            // Inherit from widget

    // Items organized by zone
    vg_statusbar_item_t** left_items;
    size_t left_count;
    size_t left_capacity;

    vg_statusbar_item_t** center_items;
    size_t center_count;
    size_t center_capacity;

    vg_statusbar_item_t** right_items;
    size_t right_count;
    size_t right_capacity;

    // Styling
    uint32_t height;             // Bar height (default: 24)
    uint32_t item_padding;       // Padding between items (default: 8)
    uint32_t separator_width;    // Separator line width (default: 1)

    // State
    vg_statusbar_item_t* hovered_item;
} vg_statusbar_t;
```

#### API Functions

```c
// Creation/destruction
vg_statusbar_t* vg_statusbar_create(void);
void vg_statusbar_destroy(vg_statusbar_t* sb);

// Item management
vg_statusbar_item_t* vg_statusbar_add_text(vg_statusbar_t* sb,
    vg_statusbar_zone_t zone, const char* text);
vg_statusbar_item_t* vg_statusbar_add_button(vg_statusbar_t* sb,
    vg_statusbar_zone_t zone, const char* text,
    void (*on_click)(vg_statusbar_item_t*, void*), void* user_data);
vg_statusbar_item_t* vg_statusbar_add_progress(vg_statusbar_t* sb,
    vg_statusbar_zone_t zone);
vg_statusbar_item_t* vg_statusbar_add_separator(vg_statusbar_t* sb,
    vg_statusbar_zone_t zone);
vg_statusbar_item_t* vg_statusbar_add_spacer(vg_statusbar_t* sb,
    vg_statusbar_zone_t zone);

void vg_statusbar_remove_item(vg_statusbar_t* sb, vg_statusbar_item_t* item);
void vg_statusbar_clear_zone(vg_statusbar_t* sb, vg_statusbar_zone_t zone);

// Item updates
void vg_statusbar_item_set_text(vg_statusbar_item_t* item, const char* text);
void vg_statusbar_item_set_tooltip(vg_statusbar_item_t* item, const char* tooltip);
void vg_statusbar_item_set_progress(vg_statusbar_item_t* item, float progress);
void vg_statusbar_item_set_visible(vg_statusbar_item_t* item, bool visible);

// Convenience for common IDE status items
void vg_statusbar_set_cursor_position(vg_statusbar_t* sb, int line, int col);
void vg_statusbar_set_encoding(vg_statusbar_t* sb, const char* encoding);
void vg_statusbar_set_file_type(vg_statusbar_t* sb, const char* type);
void vg_statusbar_set_message(vg_statusbar_t* sb, const char* message, int timeout_ms);
```

#### Implementation Notes

1. **Layout Algorithm:**
   - Left zone: items flow left-to-right from left edge
   - Right zone: items flow right-to-left from right edge
   - Center zone: items centered in remaining space
   - Spacers expand to fill available space (flex: 1)

2. **Rendering:**
   - Background: `theme->colors.bg_secondary`
   - Text: `theme->colors.fg_secondary`
   - Hovered item: `theme->colors.bg_hover`
   - Separator: `theme->colors.border_primary`
   - Top border line (1px): `theme->colors.border_secondary`

3. **Event Handling:**
   - Track mouse position for hover state
   - On click: find item under cursor, invoke callback
   - Tooltip: integrate with tooltip system (Phase 4)

4. **vtable Functions:**
   ```c
   static vg_widget_vtable_t statusbar_vtable = {
       .destroy = statusbar_destroy,
       .measure = statusbar_measure,
       .arrange = statusbar_arrange,
       .paint = statusbar_paint,
       .handle_event = statusbar_handle_event
   };
   ```

---

### 1.2 Toolbar Widget

**Files to Create:**
- `src/lib/gui/include/vg_toolbar.h`
- `src/lib/gui/src/widgets/vg_toolbar.c`

**Files to Modify:**
- `src/lib/gui/include/vg_widget.h` - Already has `VG_WIDGET_TOOLBAR` in enum
- `src/lib/gui/src/vg_widget.c` - Add vtable registration
- `src/lib/gui/CMakeLists.txt` - Add source file

#### Data Structures

```c
// Toolbar item types
typedef enum {
    VG_TOOLBAR_ITEM_BUTTON,      // Standard button
    VG_TOOLBAR_ITEM_TOGGLE,      // Toggle button (checkable)
    VG_TOOLBAR_ITEM_DROPDOWN,    // Button with dropdown menu
    VG_TOOLBAR_ITEM_SEPARATOR,   // Vertical line separator
    VG_TOOLBAR_ITEM_SPACER,      // Flexible spacer
    VG_TOOLBAR_ITEM_WIDGET       // Custom embedded widget
} vg_toolbar_item_type_t;

// Icon specification (supports multiple sources)
typedef struct {
    enum {
        VG_ICON_NONE,
        VG_ICON_GLYPH,           // Unicode character (e.g., emoji or icon font)
        VG_ICON_IMAGE,           // Pixel data
        VG_ICON_PATH             // File path to load
    } type;
    union {
        uint32_t glyph;          // Unicode codepoint
        struct {
            uint8_t* pixels;     // RGBA pixel data
            uint32_t width;
            uint32_t height;
        } image;
        char* path;              // File path
    } data;
} vg_icon_t;

// Toolbar item
typedef struct vg_toolbar_item_t {
    vg_toolbar_item_type_t type;
    char* id;                    // Unique identifier
    char* label;                 // Text label (optional, can be icon-only)
    char* tooltip;               // Hover tooltip
    vg_icon_t icon;              // Icon specification
    bool enabled;                // Grayed out if false
    bool checked;                // For toggle items
    bool show_label;             // Show text label alongside icon

    // Dropdown menu (for DROPDOWN type)
    struct vg_menu_t* dropdown_menu;

    // Custom widget (for WIDGET type)
    vg_widget_t* custom_widget;

    // Callbacks
    void* user_data;
    void (*on_click)(struct vg_toolbar_item_t* item, void* user_data);
    void (*on_toggle)(struct vg_toolbar_item_t* item, bool checked, void* user_data);
} vg_toolbar_item_t;

// Toolbar orientation
typedef enum {
    VG_TOOLBAR_HORIZONTAL,
    VG_TOOLBAR_VERTICAL
} vg_toolbar_orientation_t;

// Toolbar icon size presets
typedef enum {
    VG_TOOLBAR_ICONS_SMALL,      // 16x16
    VG_TOOLBAR_ICONS_MEDIUM,     // 24x24
    VG_TOOLBAR_ICONS_LARGE       // 32x32
} vg_toolbar_icon_size_t;

// Main toolbar widget
typedef struct {
    vg_widget_t base;

    vg_toolbar_item_t** items;
    size_t item_count;
    size_t item_capacity;

    // Configuration
    vg_toolbar_orientation_t orientation;
    vg_toolbar_icon_size_t icon_size;
    uint32_t item_padding;       // Padding around items (default: 4)
    uint32_t item_spacing;       // Space between items (default: 2)
    bool show_labels;            // Global label visibility
    bool overflow_menu;          // Show overflow items in dropdown

    // State
    vg_toolbar_item_t* hovered_item;
    vg_toolbar_item_t* pressed_item;
    int overflow_start_index;    // First item in overflow (-1 if none)
} vg_toolbar_t;
```

#### API Functions

```c
// Creation/destruction
vg_toolbar_t* vg_toolbar_create(vg_toolbar_orientation_t orientation);
void vg_toolbar_destroy(vg_toolbar_t* tb);

// Item management
vg_toolbar_item_t* vg_toolbar_add_button(vg_toolbar_t* tb,
    const char* id, const char* label, vg_icon_t icon,
    void (*on_click)(vg_toolbar_item_t*, void*), void* user_data);
vg_toolbar_item_t* vg_toolbar_add_toggle(vg_toolbar_t* tb,
    const char* id, const char* label, vg_icon_t icon, bool initial_checked,
    void (*on_toggle)(vg_toolbar_item_t*, bool, void*), void* user_data);
vg_toolbar_item_t* vg_toolbar_add_dropdown(vg_toolbar_t* tb,
    const char* id, const char* label, vg_icon_t icon,
    struct vg_menu_t* menu);
vg_toolbar_item_t* vg_toolbar_add_separator(vg_toolbar_t* tb);
vg_toolbar_item_t* vg_toolbar_add_spacer(vg_toolbar_t* tb);
vg_toolbar_item_t* vg_toolbar_add_widget(vg_toolbar_t* tb,
    const char* id, vg_widget_t* widget);

void vg_toolbar_remove_item(vg_toolbar_t* tb, const char* id);
vg_toolbar_item_t* vg_toolbar_get_item(vg_toolbar_t* tb, const char* id);

// Item state
void vg_toolbar_item_set_enabled(vg_toolbar_item_t* item, bool enabled);
void vg_toolbar_item_set_checked(vg_toolbar_item_t* item, bool checked);
void vg_toolbar_item_set_tooltip(vg_toolbar_item_t* item, const char* tooltip);
void vg_toolbar_item_set_icon(vg_toolbar_item_t* item, vg_icon_t icon);

// Configuration
void vg_toolbar_set_icon_size(vg_toolbar_t* tb, vg_toolbar_icon_size_t size);
void vg_toolbar_set_show_labels(vg_toolbar_t* tb, bool show);

// Icon helpers
vg_icon_t vg_icon_from_glyph(uint32_t codepoint);
vg_icon_t vg_icon_from_pixels(uint8_t* rgba, uint32_t w, uint32_t h);
vg_icon_t vg_icon_from_file(const char* path);
void vg_icon_destroy(vg_icon_t* icon);
```

#### Implementation Notes

1. **Button Rendering:**
   - Normal: transparent background
   - Hover: `theme->colors.bg_hover` with rounded corners
   - Pressed: `theme->colors.bg_active`
   - Disabled: 50% opacity
   - Toggle checked: `theme->colors.accent_primary` background tint

2. **Icon Sizing:**
   - Small: 16x16, button padding 4px
   - Medium: 24x24, button padding 6px
   - Large: 32x32, button padding 8px

3. **Overflow Handling:**
   - Measure all items, track which fit
   - If overflow: show "..." button at end
   - Click "..." opens menu with remaining items

4. **Dropdown Integration:**
   - Arrow indicator next to dropdown buttons
   - On click: position menu below button, show it

---

### 1.3 Dialog Widget

**Files to Create:**
- `src/lib/gui/include/vg_dialog.h`
- `src/lib/gui/src/widgets/vg_dialog.c`

**Files to Modify:**
- `src/lib/gui/include/vg_widget.h` - Already has `VG_WIDGET_DIALOG` in enum
- `src/lib/gui/src/vg_widget.c` - Add vtable registration
- `src/lib/gui/CMakeLists.txt` - Add source file

#### Data Structures

```c
// Dialog button presets
typedef enum {
    VG_DIALOG_BUTTONS_NONE,
    VG_DIALOG_BUTTONS_OK,
    VG_DIALOG_BUTTONS_OK_CANCEL,
    VG_DIALOG_BUTTONS_YES_NO,
    VG_DIALOG_BUTTONS_YES_NO_CANCEL,
    VG_DIALOG_BUTTONS_RETRY_CANCEL,
    VG_DIALOG_BUTTONS_CUSTOM
} vg_dialog_buttons_t;

// Dialog result codes
typedef enum {
    VG_DIALOG_RESULT_NONE,       // Still open
    VG_DIALOG_RESULT_OK,
    VG_DIALOG_RESULT_CANCEL,
    VG_DIALOG_RESULT_YES,
    VG_DIALOG_RESULT_NO,
    VG_DIALOG_RESULT_RETRY,
    VG_DIALOG_RESULT_CUSTOM_1,
    VG_DIALOG_RESULT_CUSTOM_2,
    VG_DIALOG_RESULT_CUSTOM_3
} vg_dialog_result_t;

// Dialog icon presets
typedef enum {
    VG_DIALOG_ICON_NONE,
    VG_DIALOG_ICON_INFO,
    VG_DIALOG_ICON_WARNING,
    VG_DIALOG_ICON_ERROR,
    VG_DIALOG_ICON_QUESTION,
    VG_DIALOG_ICON_CUSTOM
} vg_dialog_icon_t;

// Custom button definition
typedef struct {
    char* label;
    vg_dialog_result_t result;
    bool is_default;             // Activated on Enter
    bool is_cancel;              // Activated on Escape
} vg_dialog_button_def_t;

// Dialog widget
typedef struct {
    vg_widget_t base;

    // Title bar
    char* title;
    bool show_close_button;
    bool draggable;

    // Content
    vg_widget_t* content;        // Child widget for content area
    vg_dialog_icon_t icon;
    vg_icon_t custom_icon;
    char* message;               // Simple text message (alternative to content widget)

    // Buttons
    vg_dialog_buttons_t button_preset;
    vg_dialog_button_def_t* custom_buttons;
    size_t custom_button_count;

    // Sizing
    uint32_t min_width;          // Default: 300
    uint32_t min_height;         // Default: 150
    uint32_t max_width;          // Default: 800
    uint32_t max_height;         // Default: 600
    bool resizable;

    // Modal behavior
    bool modal;                  // Block input to parent
    vg_widget_t* modal_parent;   // Parent to block

    // State
    vg_dialog_result_t result;
    bool is_open;

    // Drag state
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;

    // Callbacks
    void* user_data;
    void (*on_result)(struct vg_dialog_t* dialog, vg_dialog_result_t result, void* user_data);
    void (*on_close)(struct vg_dialog_t* dialog, void* user_data);
} vg_dialog_t;
```

#### API Functions

```c
// Creation
vg_dialog_t* vg_dialog_create(const char* title);
void vg_dialog_destroy(vg_dialog_t* dialog);

// Configuration
void vg_dialog_set_title(vg_dialog_t* dialog, const char* title);
void vg_dialog_set_content(vg_dialog_t* dialog, vg_widget_t* content);
void vg_dialog_set_message(vg_dialog_t* dialog, const char* message);
void vg_dialog_set_icon(vg_dialog_t* dialog, vg_dialog_icon_t icon);
void vg_dialog_set_custom_icon(vg_dialog_t* dialog, vg_icon_t icon);
void vg_dialog_set_buttons(vg_dialog_t* dialog, vg_dialog_buttons_t buttons);
void vg_dialog_set_custom_buttons(vg_dialog_t* dialog,
    vg_dialog_button_def_t* buttons, size_t count);
void vg_dialog_set_resizable(vg_dialog_t* dialog, bool resizable);
void vg_dialog_set_size_constraints(vg_dialog_t* dialog,
    uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h);

// Modal control
void vg_dialog_set_modal(vg_dialog_t* dialog, bool modal, vg_widget_t* parent);

// Show/hide
void vg_dialog_show(vg_dialog_t* dialog);
void vg_dialog_show_centered(vg_dialog_t* dialog, vg_widget_t* relative_to);
void vg_dialog_hide(vg_dialog_t* dialog);
void vg_dialog_close(vg_dialog_t* dialog, vg_dialog_result_t result);

// Result
vg_dialog_result_t vg_dialog_get_result(vg_dialog_t* dialog);
bool vg_dialog_is_open(vg_dialog_t* dialog);

// Callbacks
void vg_dialog_set_on_result(vg_dialog_t* dialog,
    void (*callback)(vg_dialog_t*, vg_dialog_result_t, void*), void* user_data);
void vg_dialog_set_on_close(vg_dialog_t* dialog,
    void (*callback)(vg_dialog_t*, void*), void* user_data);

// Convenience constructors for common dialogs
vg_dialog_t* vg_dialog_message(const char* title, const char* message,
    vg_dialog_icon_t icon, vg_dialog_buttons_t buttons);
vg_dialog_t* vg_dialog_confirm(const char* title, const char* message,
    void (*on_confirm)(void*), void* user_data);
vg_dialog_t* vg_dialog_input(const char* title, const char* prompt,
    const char* initial_value,
    void (*on_submit)(const char* value, void*), void* user_data);
```

#### Implementation Notes

1. **Layout Structure:**
   ```
   +--[Title Bar]------------------[X]--+
   |                                    |
   |  [Icon]  [Content/Message Area]    |
   |                                    |
   +------------------------------------+
   |        [Button1] [Button2]         |
   +------------------------------------+
   ```

2. **Modal Implementation:**
   - When modal, capture all events before they reach other widgets
   - Draw semi-transparent overlay behind dialog
   - Focus trap: Tab cycles through dialog controls only

3. **Title Bar:**
   - Height: 32px
   - Background: `theme->colors.bg_tertiary`
   - Title: centered or left-aligned, bold font
   - Close button: right side, "X" glyph

4. **Button Bar:**
   - Height: 48px (including padding)
   - Buttons right-aligned
   - Default button: `theme->colors.accent_primary`
   - Cancel button: `theme->colors.bg_secondary`

5. **Dragging:**
   - Click and drag title bar to move
   - Track drag offset on mouse down
   - Update position on mouse move

6. **Keyboard:**
   - Enter: activate default button
   - Escape: activate cancel button (or close)
   - Tab: cycle through focusable elements

---

### 1.4 CodeEditor Fixes

**Files to Modify:**
- `src/lib/gui/include/vg_ide_widgets.h` - Add undo/redo structures
- `src/lib/gui/src/widgets/vg_codeeditor.c` - Implement missing functions

#### 1.4.1 Undo/Redo System

**Add to vg_ide_widgets.h:**

```c
// Edit operation types
typedef enum {
    VG_EDIT_INSERT,              // Text inserted
    VG_EDIT_DELETE,              // Text deleted
    VG_EDIT_REPLACE              // Text replaced (delete + insert)
} vg_edit_op_type_t;

// Single edit operation (for undo/redo)
typedef struct {
    vg_edit_op_type_t type;

    // Position info
    uint32_t start_line;
    uint32_t start_col;
    uint32_t end_line;
    uint32_t end_col;

    // Text data
    char* old_text;              // Text that was there before (for DELETE/REPLACE)
    char* new_text;              // Text that is there now (for INSERT/REPLACE)

    // Cursor position to restore
    uint32_t cursor_line_before;
    uint32_t cursor_col_before;
    uint32_t cursor_line_after;
    uint32_t cursor_col_after;

    // Grouping for compound operations
    uint32_t group_id;           // Non-zero if part of a group
} vg_edit_op_t;

// Undo/redo history
typedef struct {
    vg_edit_op_t** operations;
    size_t count;
    size_t capacity;
    size_t current_index;        // Points to next redo operation
    uint32_t next_group_id;      // Counter for grouping
    bool is_grouping;            // Currently recording a group
    uint32_t current_group;      // Active group ID
} vg_edit_history_t;

// Add to vg_codeeditor_t struct:
// vg_edit_history_t* history;
```

**Implementation Functions:**

```c
// Internal history management
static vg_edit_history_t* edit_history_create(void);
static void edit_history_destroy(vg_edit_history_t* history);
static void edit_history_clear(vg_edit_history_t* history);
static void edit_history_push(vg_edit_history_t* history, vg_edit_op_t* op);
static vg_edit_op_t* edit_history_pop_undo(vg_edit_history_t* history);
static vg_edit_op_t* edit_history_pop_redo(vg_edit_history_t* history);

// Grouping for compound operations
static void edit_history_begin_group(vg_edit_history_t* history);
static void edit_history_end_group(vg_edit_history_t* history);

// Public API implementation
void vg_codeeditor_undo(vg_codeeditor_t* editor)
{
    if (!editor || !editor->history) return;

    vg_edit_op_t* op = edit_history_pop_undo(editor->history);
    if (!op) return;

    // Handle grouped operations
    uint32_t group = op->group_id;
    do {
        switch (op->type) {
            case VG_EDIT_INSERT:
                // Undo insert = delete the inserted text
                delete_text_range(editor, op->start_line, op->start_col,
                    op->end_line, op->end_col);
                break;
            case VG_EDIT_DELETE:
                // Undo delete = re-insert the deleted text
                insert_text_at(editor, op->start_line, op->start_col, op->old_text);
                break;
            case VG_EDIT_REPLACE:
                // Undo replace = delete new text, insert old text
                delete_text_range(editor, op->start_line, op->start_col,
                    op->end_line, op->end_col);
                insert_text_at(editor, op->start_line, op->start_col, op->old_text);
                break;
        }

        // Restore cursor
        editor->cursor_line = op->cursor_line_before;
        editor->cursor_col = op->cursor_col_before;

        // Check for more operations in same group
        if (group != 0) {
            op = edit_history_peek_undo(editor->history);
            if (op && op->group_id == group) {
                op = edit_history_pop_undo(editor->history);
            } else {
                break;
            }
        }
    } while (group != 0 && op);

    editor->modified = true;
    vg_widget_invalidate((vg_widget_t*)editor);
}

void vg_codeeditor_redo(vg_codeeditor_t* editor)
{
    // Similar to undo but applies operations forward
    // ... implementation follows same pattern
}
```

#### 1.4.2 Selection Functions

**Fix `vg_codeeditor_get_selection()`:**

```c
char* vg_codeeditor_get_selection(vg_codeeditor_t* editor)
{
    if (!editor || !editor->has_selection) return NULL;

    // Normalize selection (start before end)
    uint32_t start_line, start_col, end_line, end_col;
    normalize_selection(editor, &start_line, &start_col, &end_line, &end_col);

    // Calculate buffer size needed
    size_t total_len = 0;
    for (uint32_t line = start_line; line <= end_line; line++) {
        vg_text_line_t* tl = &editor->lines[line];
        uint32_t col_start = (line == start_line) ? start_col : 0;
        uint32_t col_end = (line == end_line) ? end_col : tl->length;
        total_len += (col_end - col_start);
        if (line < end_line) total_len++; // newline
    }

    // Allocate and copy
    char* result = malloc(total_len + 1);
    if (!result) return NULL;

    char* ptr = result;
    for (uint32_t line = start_line; line <= end_line; line++) {
        vg_text_line_t* tl = &editor->lines[line];
        uint32_t col_start = (line == start_line) ? start_col : 0;
        uint32_t col_end = (line == end_line) ? end_col : tl->length;
        size_t len = col_end - col_start;
        memcpy(ptr, tl->text + col_start, len);
        ptr += len;
        if (line < end_line) *ptr++ = '\n';
    }
    *ptr = '\0';

    return result;
}
```

**Fix `vg_codeeditor_delete_selection()`:**

```c
void vg_codeeditor_delete_selection(vg_codeeditor_t* editor)
{
    if (!editor || !editor->has_selection) return;

    // Get selected text for undo
    char* old_text = vg_codeeditor_get_selection(editor);

    // Normalize selection
    uint32_t start_line, start_col, end_line, end_col;
    normalize_selection(editor, &start_line, &start_col, &end_line, &end_col);

    // Record for undo
    vg_edit_op_t* op = create_edit_op(VG_EDIT_DELETE,
        start_line, start_col, end_line, end_col,
        old_text, NULL,
        editor->cursor_line, editor->cursor_col,
        start_line, start_col);
    edit_history_push(editor->history, op);

    // Perform deletion
    if (start_line == end_line) {
        // Single line: remove characters
        vg_text_line_t* tl = &editor->lines[start_line];
        memmove(tl->text + start_col, tl->text + end_col,
            tl->length - end_col + 1);
        tl->length -= (end_col - start_col);
    } else {
        // Multi-line: join first and last, remove middle
        vg_text_line_t* first = &editor->lines[start_line];
        vg_text_line_t* last = &editor->lines[end_line];

        // Keep first part of first line + last part of last line
        size_t new_len = start_col + (last->length - end_col);
        char* new_text = malloc(new_len + 1);
        memcpy(new_text, first->text, start_col);
        memcpy(new_text + start_col, last->text + end_col,
            last->length - end_col);
        new_text[new_len] = '\0';

        // Replace first line
        free(first->text);
        first->text = new_text;
        first->length = new_len;

        // Remove lines from start_line+1 to end_line
        for (uint32_t i = start_line + 1; i <= end_line; i++) {
            free(editor->lines[i].text);
        }
        size_t lines_to_remove = end_line - start_line;
        memmove(&editor->lines[start_line + 1],
            &editor->lines[end_line + 1],
            (editor->line_count - end_line - 1) * sizeof(vg_text_line_t));
        editor->line_count -= lines_to_remove;
    }

    // Update cursor and clear selection
    editor->cursor_line = start_line;
    editor->cursor_col = start_col;
    editor->has_selection = false;
    editor->modified = true;

    free(old_text);
    vg_widget_invalidate((vg_widget_t*)editor);
}
```

---

### 1.5 TabBar Callback Fixes

**Files to Modify:**
- `src/lib/gui/include/vg_ide_widgets.h` - Verify callback types
- `src/lib/gui/src/widgets/vg_tabbar.c` - Add setter functions

**Add Missing Functions:**

```c
void vg_tabbar_set_on_close(vg_tabbar_t* tabbar,
    vg_tab_close_callback_t callback, void* user_data)
{
    if (!tabbar) return;
    tabbar->on_close = callback;
    tabbar->close_user_data = user_data;
}

void vg_tabbar_set_on_reorder(vg_tabbar_t* tabbar,
    vg_tab_reorder_callback_t callback, void* user_data)
{
    if (!tabbar) return;
    tabbar->on_reorder = callback;
    tabbar->reorder_user_data = user_data;
}

void vg_tabbar_set_on_select(vg_tabbar_t* tabbar,
    vg_tab_select_callback_t callback, void* user_data)
{
    if (!tabbar) return;
    tabbar->on_select = callback;
    tabbar->select_user_data = user_data;
}
```

**Fix Close Button Rendering (around line 234):**

```c
// In tabbar_paint(), after drawing tab text:
if (tab->closable && tabbar->hovered_tab == tab) {
    // Draw close button (X)
    int close_x = tab_right - 20;
    int close_y = tab_y + (tab_height - 16) / 2;
    int close_size = 16;

    // Close button background on hover
    if (tabbar->hovered_close == tab) {
        vgfx_fill_rounded_rect(ctx, close_x, close_y, close_size, close_size,
            4, theme->colors.bg_hover);
    }

    // Draw X
    uint32_t x_color = (tabbar->hovered_close == tab)
        ? theme->colors.fg_primary
        : theme->colors.fg_secondary;
    int pad = 4;
    vgfx_draw_line(ctx, close_x + pad, close_y + pad,
        close_x + close_size - pad, close_y + close_size - pad, x_color, 2);
    vgfx_draw_line(ctx, close_x + close_size - pad, close_y + pad,
        close_x + pad, close_y + close_size - pad, x_color, 2);
}
```

---

## Phase 2 - File Operations

### 2.1 FileDialog Widget

**Files to Create:**
- `src/lib/gui/include/vg_filedialog.h`
- `src/lib/gui/src/widgets/vg_filedialog.c`

**Files to Modify:**
- `src/lib/gui/include/vg_widget.h` - Add `VG_WIDGET_FILEDIALOG` to enum
- `src/lib/gui/src/vg_widget.c` - Add vtable registration
- `src/lib/gui/CMakeLists.txt` - Add source file

#### Data Structures

```c
// File dialog mode
typedef enum {
    VG_FILEDIALOG_OPEN,          // Select existing file(s)
    VG_FILEDIALOG_SAVE,          // Select location to save
    VG_FILEDIALOG_SELECT_FOLDER  // Select directory
} vg_filedialog_mode_t;

// File filter
typedef struct {
    char* name;                  // Display name (e.g., "Viper Files")
    char* pattern;               // Glob pattern (e.g., "*.zia;*.vpr")
} vg_file_filter_t;

// File/directory entry
typedef struct {
    char* name;                  // File name
    char* full_path;             // Full path
    bool is_directory;
    uint64_t size;               // File size in bytes
    uint64_t modified_time;      // Unix timestamp
} vg_file_entry_t;

// Bookmark entry
typedef struct {
    char* name;                  // Display name
    char* path;                  // Full path
    vg_icon_t icon;              // Optional icon
} vg_bookmark_t;

// File dialog widget (extends Dialog)
typedef struct {
    vg_dialog_t base;            // Inherit from dialog

    vg_filedialog_mode_t mode;

    // Current state
    char* current_path;          // Current directory
    vg_file_entry_t** entries;   // Files in current directory
    size_t entry_count;

    // Selection
    int* selected_indices;       // Selected file indices
    size_t selection_count;
    bool multi_select;           // Allow multiple selection (open mode only)

    // Filters
    vg_file_filter_t* filters;
    size_t filter_count;
    size_t active_filter;        // Currently selected filter

    // Bookmarks
    vg_bookmark_t* bookmarks;
    size_t bookmark_count;

    // Configuration
    bool show_hidden;            // Show hidden files
    bool confirm_overwrite;      // Ask before overwriting (save mode)
    char* default_filename;      // Pre-filled filename (save mode)
    char* default_extension;     // Auto-add extension

    // Child widgets
    vg_textinput_t* path_input;  // Path text input
    vg_listbox_t* file_list;     // File listing (or could be tree)
    vg_textinput_t* filename_input; // Filename input (save mode)
    vg_dropdown_t* filter_dropdown; // Filter selector
    vg_listbox_t* bookmark_list; // Sidebar bookmarks

    // Result
    char** selected_files;       // Result: array of selected paths
    size_t selected_file_count;

    // Callbacks
    void* user_data;
    void (*on_select)(struct vg_filedialog_t* dialog,
        char** paths, size_t count, void* user_data);
    void (*on_cancel)(struct vg_filedialog_t* dialog, void* user_data);
} vg_filedialog_t;
```

#### API Functions

```c
// Creation
vg_filedialog_t* vg_filedialog_create(vg_filedialog_mode_t mode);
void vg_filedialog_destroy(vg_filedialog_t* dialog);

// Configuration
void vg_filedialog_set_title(vg_filedialog_t* dialog, const char* title);
void vg_filedialog_set_initial_path(vg_filedialog_t* dialog, const char* path);
void vg_filedialog_set_filename(vg_filedialog_t* dialog, const char* filename);
void vg_filedialog_set_multi_select(vg_filedialog_t* dialog, bool multi);
void vg_filedialog_set_show_hidden(vg_filedialog_t* dialog, bool show);
void vg_filedialog_set_confirm_overwrite(vg_filedialog_t* dialog, bool confirm);

// Filters
void vg_filedialog_add_filter(vg_filedialog_t* dialog,
    const char* name, const char* pattern);
void vg_filedialog_clear_filters(vg_filedialog_t* dialog);
void vg_filedialog_set_default_extension(vg_filedialog_t* dialog, const char* ext);

// Bookmarks
void vg_filedialog_add_bookmark(vg_filedialog_t* dialog,
    const char* name, const char* path);
void vg_filedialog_add_default_bookmarks(vg_filedialog_t* dialog); // Home, Desktop, etc.
void vg_filedialog_clear_bookmarks(vg_filedialog_t* dialog);

// Show/result
void vg_filedialog_show(vg_filedialog_t* dialog);
char** vg_filedialog_get_selected_paths(vg_filedialog_t* dialog, size_t* count);
char* vg_filedialog_get_selected_path(vg_filedialog_t* dialog); // Single file convenience

// Callbacks
void vg_filedialog_set_on_select(vg_filedialog_t* dialog,
    void (*callback)(vg_filedialog_t*, char**, size_t, void*), void* user_data);
void vg_filedialog_set_on_cancel(vg_filedialog_t* dialog,
    void (*callback)(vg_filedialog_t*, void*), void* user_data);

// Convenience functions
char* vg_filedialog_open_file(const char* title, const char* initial_path,
    const char* filter_name, const char* filter_pattern);
char* vg_filedialog_save_file(const char* title, const char* initial_path,
    const char* default_name, const char* filter_name, const char* filter_pattern);
char* vg_filedialog_select_folder(const char* title, const char* initial_path);
```

#### Implementation Notes

1. **Layout:**
   ```
   +--[Title: Open File]---------------[X]--+
   | +----------+ +------------------------+ |
   | | Bookmarks| | Path: /home/user/code  | |
   | |----------| |------------------------| |
   | | [Home]   | | Name        Size  Date | |
   | | [Desktop]| | [folder]              > | |
   | | [Docs]   | | file1.zia  2KB  1/14 | |
   | |          | | file2.zia  4KB  1/13 | |
   | +----------+ +------------------------+ |
   | Filename: [                          ]  |
   | Filter: [Viper Files (*.zia)     v]  |
   +----------------------------------------+
   |                    [Cancel]  [Open]    |
   +----------------------------------------+
   ```

2. **Directory Listing:**
   - Use POSIX `opendir`/`readdir` or platform abstraction
   - Sort: directories first, then by name
   - Filter by active filter pattern
   - Handle hidden files (`.` prefix)

3. **Navigation:**
   - Double-click directory: navigate into
   - Double-click file: select and confirm
   - Path input: type path and press Enter
   - Bookmark click: navigate to bookmark path
   - Up button: go to parent directory

4. **Platform Considerations:**
   - Path separator: `/` on Unix, `\` on Windows
   - Home directory: `$HOME` on Unix, `%USERPROFILE%` on Windows
   - Default bookmarks: Home, Desktop, Documents

---

### 2.2 ContextMenu Widget

**Files to Create:**
- `src/lib/gui/include/vg_contextmenu.h`
- `src/lib/gui/src/widgets/vg_contextmenu.c`

**Files to Modify:**
- `src/lib/gui/include/vg_widget.h` - Add `VG_WIDGET_CONTEXTMENU` to enum
- `src/lib/gui/src/vg_widget.c` - Add vtable registration
- `src/lib/gui/CMakeLists.txt` - Add source file

#### Data Structures

```c
// Reuse menu item structure from MenuBar
// (vg_menu_item_t already exists in vg_ide_widgets.h)

// Context menu widget
typedef struct {
    vg_widget_t base;

    // Menu items (same structure as MenuBar menus)
    vg_menu_item_t** items;
    size_t item_count;
    size_t item_capacity;

    // Positioning
    int anchor_x;                // Screen X where menu appears
    int anchor_y;                // Screen Y where menu appears

    // State
    bool is_visible;
    int hovered_index;           // -1 if none
    vg_contextmenu_t* active_submenu; // Open submenu

    // Styling
    uint32_t min_width;          // Minimum menu width (default: 150)
    uint32_t max_height;         // Maximum height before scrolling

    // Callbacks
    void* user_data;
    void (*on_select)(struct vg_contextmenu_t* menu,
        vg_menu_item_t* item, void* user_data);
    void (*on_dismiss)(struct vg_contextmenu_t* menu, void* user_data);
} vg_contextmenu_t;
```

#### API Functions

```c
// Creation
vg_contextmenu_t* vg_contextmenu_create(void);
void vg_contextmenu_destroy(vg_contextmenu_t* menu);

// Item management (similar to MenuBar)
vg_menu_item_t* vg_contextmenu_add_item(vg_contextmenu_t* menu,
    const char* label, const char* shortcut,
    void (*action)(vg_menu_item_t*, void*), void* user_data);
vg_menu_item_t* vg_contextmenu_add_submenu(vg_contextmenu_t* menu,
    const char* label, vg_contextmenu_t* submenu);
void vg_contextmenu_add_separator(vg_contextmenu_t* menu);
void vg_contextmenu_clear(vg_contextmenu_t* menu);

// Item state
void vg_contextmenu_item_set_enabled(vg_menu_item_t* item, bool enabled);
void vg_contextmenu_item_set_checked(vg_menu_item_t* item, bool checked);
void vg_contextmenu_item_set_icon(vg_menu_item_t* item, vg_icon_t icon);

// Show/hide
void vg_contextmenu_show_at(vg_contextmenu_t* menu, int x, int y);
void vg_contextmenu_show_for_widget(vg_contextmenu_t* menu,
    vg_widget_t* widget, int offset_x, int offset_y);
void vg_contextmenu_dismiss(vg_contextmenu_t* menu);

// Callbacks
void vg_contextmenu_set_on_select(vg_contextmenu_t* menu,
    void (*callback)(vg_contextmenu_t*, vg_menu_item_t*, void*), void* user_data);
void vg_contextmenu_set_on_dismiss(vg_contextmenu_t* menu,
    void (*callback)(vg_contextmenu_t*, void*), void* user_data);

// Global context menu handling
void vg_contextmenu_register_for_widget(vg_widget_t* widget,
    vg_contextmenu_t* menu);
void vg_contextmenu_unregister_for_widget(vg_widget_t* widget);
```

#### Implementation Notes

1. **Rendering:**
   - Background: `theme->colors.bg_primary` with border
   - Shadow: offset drop shadow for depth
   - Item height: 28px
   - Hover: `theme->colors.bg_hover`
   - Disabled: 50% opacity text
   - Separator: 1px line with padding
   - Submenu arrow: ">" glyph on right

2. **Positioning:**
   - Open at mouse position
   - Adjust if would go off-screen (flip direction)
   - Submenus open to the right (or left if no space)

3. **Event Handling:**
   - Click outside: dismiss menu
   - Click on item: invoke action, dismiss
   - Hover on submenu item: show submenu after 200ms delay
   - Escape key: dismiss menu
   - Arrow keys: navigate items

4. **Integration with TreeView/ListBox:**
   ```c
   // Example usage in TreeView:
   void treeview_handle_event(vg_widget_t* w, vg_event_t* e) {
       if (e->type == VG_EVENT_MOUSE_DOWN && e->mouse.button == 2) {
           // Right click
           vg_treeview_node_t* node = find_node_at(tree, e->mouse.y);
           if (node && tree->context_menu) {
               // Update menu items based on node
               vg_contextmenu_show_at(tree->context_menu,
                   e->mouse.screen_x, e->mouse.screen_y);
               e->handled = true;
           }
       }
   }
   ```

---

### 2.3 Clipboard Integration

**Files to Modify:**
- `src/lib/gfx/include/vgfx.h` - Add clipboard API
- `src/lib/gfx/src/vgfx_macos.m` - Implement macOS clipboard
- `src/lib/gfx/src/vgfx_linux.c` - Implement X11 clipboard (stub initially)
- `src/lib/gfx/src/vgfx_win32.c` - Implement Windows clipboard (stub initially)
- `src/lib/gui/src/widgets/vg_codeeditor.c` - Use clipboard
- `src/lib/gui/src/widgets/vg_textinput.c` - Use clipboard

#### VGFX Clipboard API

```c
// Add to vgfx.h

// Clipboard format types
typedef enum {
    VGFX_CLIPBOARD_TEXT,         // Plain text (UTF-8)
    VGFX_CLIPBOARD_HTML,         // HTML formatted text
    VGFX_CLIPBOARD_IMAGE,        // Image data
    VGFX_CLIPBOARD_FILES         // File paths
} vgfx_clipboard_format_t;

// Clipboard operations
bool vgfx_clipboard_has_format(vgfx_clipboard_format_t format);
char* vgfx_clipboard_get_text(void);  // Returns malloc'd string, caller frees
void vgfx_clipboard_set_text(const char* text);
void vgfx_clipboard_clear(void);

// For images (future)
// uint8_t* vgfx_clipboard_get_image(uint32_t* width, uint32_t* height);
// void vgfx_clipboard_set_image(const uint8_t* rgba, uint32_t width, uint32_t height);
```

#### macOS Implementation (vgfx_macos.m)

```objc
#import <AppKit/AppKit.h>

bool vgfx_clipboard_has_format(vgfx_clipboard_format_t format) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    switch (format) {
        case VGFX_CLIPBOARD_TEXT:
            return [pb availableTypeFromArray:@[NSPasteboardTypeString]] != nil;
        case VGFX_CLIPBOARD_HTML:
            return [pb availableTypeFromArray:@[NSPasteboardTypeHTML]] != nil;
        default:
            return false;
    }
}

char* vgfx_clipboard_get_text(void) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* str = [pb stringForType:NSPasteboardTypeString];
    if (!str) return NULL;

    const char* utf8 = [str UTF8String];
    return strdup(utf8);
}

void vgfx_clipboard_set_text(const char* text) {
    if (!text) return;

    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:[NSString stringWithUTF8String:text]
          forType:NSPasteboardTypeString];
}

void vgfx_clipboard_clear(void) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
}
```

#### CodeEditor Integration

```c
// Add keyboard handlers in codeeditor_handle_event:

case VG_KEY_C:
    if (e->keyboard.modifiers & VG_MOD_CTRL) {
        // Copy
        if (editor->has_selection) {
            char* text = vg_codeeditor_get_selection(editor);
            if (text) {
                vgfx_clipboard_set_text(text);
                free(text);
            }
        }
        e->handled = true;
    }
    break;

case VG_KEY_X:
    if (e->keyboard.modifiers & VG_MOD_CTRL) {
        // Cut
        if (editor->has_selection) {
            char* text = vg_codeeditor_get_selection(editor);
            if (text) {
                vgfx_clipboard_set_text(text);
                free(text);
                vg_codeeditor_delete_selection(editor);
            }
        }
        e->handled = true;
    }
    break;

case VG_KEY_V:
    if (e->keyboard.modifiers & VG_MOD_CTRL) {
        // Paste
        char* text = vgfx_clipboard_get_text();
        if (text) {
            if (editor->has_selection) {
                vg_codeeditor_delete_selection(editor);
            }
            vg_codeeditor_insert_text(editor, text);
            free(text);
        }
        e->handled = true;
    }
    break;
```

---

## Phase 3 - Editor Features

### 3.1 FindReplaceBar Widget

**Files to Create:**
- `src/lib/gui/include/vg_findreplacebar.h`
- `src/lib/gui/src/widgets/vg_findreplacebar.c`

**Files to Modify:**
- `src/lib/gui/include/vg_widget.h` - Add enum
- `src/lib/gui/src/vg_widget.c` - Add vtable
- `src/lib/gui/CMakeLists.txt` - Add source

#### Data Structures

```c
// Search options
typedef struct {
    bool case_sensitive;
    bool whole_word;
    bool use_regex;
    bool in_selection;           // Search within selection only
    bool wrap_around;            // Wrap to beginning when reaching end
} vg_search_options_t;

// Search match
typedef struct {
    uint32_t line;
    uint32_t start_col;
    uint32_t end_col;
} vg_search_match_t;

// Find/replace bar widget
typedef struct {
    vg_widget_t base;

    // Mode
    bool show_replace;           // Show replace controls

    // Child widgets
    vg_textinput_t* find_input;
    vg_textinput_t* replace_input;
    vg_button_t* find_prev_btn;
    vg_button_t* find_next_btn;
    vg_button_t* replace_btn;
    vg_button_t* replace_all_btn;
    vg_button_t* close_btn;
    vg_checkbox_t* case_sensitive_cb;
    vg_checkbox_t* whole_word_cb;
    vg_checkbox_t* regex_cb;

    // Search state
    vg_search_options_t options;
    vg_search_match_t* matches;  // All matches in document
    size_t match_count;
    size_t current_match;        // Index of current match (-1 if none)

    // Target editor
    vg_codeeditor_t* target_editor;

    // Result display
    char result_text[64];        // "3 of 42" or "No results"

    // Callbacks
    void* user_data;
    void (*on_find)(struct vg_findreplacebar_t* bar,
        const char* query, vg_search_options_t* options, void* user_data);
    void (*on_replace)(struct vg_findreplacebar_t* bar,
        const char* find, const char* replace, void* user_data);
    void (*on_replace_all)(struct vg_findreplacebar_t* bar,
        const char* find, const char* replace, void* user_data);
    void (*on_close)(struct vg_findreplacebar_t* bar, void* user_data);
} vg_findreplacebar_t;
```

#### API Functions

```c
// Creation
vg_findreplacebar_t* vg_findreplacebar_create(void);
void vg_findreplacebar_destroy(vg_findreplacebar_t* bar);

// Configuration
void vg_findreplacebar_set_target(vg_findreplacebar_t* bar,
    vg_codeeditor_t* editor);
void vg_findreplacebar_set_show_replace(vg_findreplacebar_t* bar, bool show);
void vg_findreplacebar_set_options(vg_findreplacebar_t* bar,
    vg_search_options_t* options);

// Search operations
void vg_findreplacebar_find(vg_findreplacebar_t* bar, const char* query);
void vg_findreplacebar_find_next(vg_findreplacebar_t* bar);
void vg_findreplacebar_find_prev(vg_findreplacebar_t* bar);
void vg_findreplacebar_replace_current(vg_findreplacebar_t* bar);
void vg_findreplacebar_replace_all(vg_findreplacebar_t* bar);

// Results
size_t vg_findreplacebar_get_match_count(vg_findreplacebar_t* bar);
size_t vg_findreplacebar_get_current_match(vg_findreplacebar_t* bar);

// Focus
void vg_findreplacebar_focus(vg_findreplacebar_t* bar);
void vg_findreplacebar_set_find_text(vg_findreplacebar_t* bar, const char* text);

// Callbacks
void vg_findreplacebar_set_on_close(vg_findreplacebar_t* bar,
    void (*callback)(vg_findreplacebar_t*, void*), void* user_data);
```

#### Implementation Notes

1. **Layout:**
   ```
   Find mode:
   +--[Find: [_______________] [<] [>] [x] [Aa] [W] [.*] "3 of 42"--+

   Replace mode:
   +--[Find:    [_______________] [<] [>] [Aa] [W] [.*] "3 of 42"--+
   |  [Replace: [_______________] [Replace] [All]              [x]  |
   +---------------------------------------------------------------+
   ```

2. **Search Algorithm:**
   ```c
   void perform_search(vg_findreplacebar_t* bar) {
       const char* query = vg_textinput_get_text(bar->find_input);
       if (!query || !*query) {
           clear_matches(bar);
           return;
       }

       // Clear previous matches
       free(bar->matches);
       bar->matches = NULL;
       bar->match_count = 0;

       // Search through editor lines
       vg_codeeditor_t* ed = bar->target_editor;
       for (uint32_t line = 0; line < ed->line_count; line++) {
           const char* text = ed->lines[line].text;
           const char* pos = text;

           while ((pos = find_in_line(pos, query, &bar->options)) != NULL) {
               // Add match
               add_match(bar, line, pos - text, pos - text + strlen(query));
               pos++;
           }
       }

       // Update result text
       if (bar->match_count > 0) {
           bar->current_match = 0;
           snprintf(bar->result_text, sizeof(bar->result_text),
               "1 of %zu", bar->match_count);
           highlight_current_match(bar);
       } else {
           snprintf(bar->result_text, sizeof(bar->result_text), "No results");
       }
   }
   ```

3. **Keyboard Shortcuts:**
   - Enter in find field: find next
   - Shift+Enter: find previous
   - Escape: close bar
   - Ctrl+H: toggle replace mode

4. **Integration with CodeEditor:**
   - Highlight all matches with subtle background color
   - Current match highlighted more prominently
   - Scroll editor to show current match

---

### 3.2 TreeView Enhancements

**Files to Modify:**
- `src/lib/gui/include/vg_ide_widgets.h` - Add icon fields
- `src/lib/gui/src/widgets/vg_treeview.c` - Implement features

#### Icon Support

```c
// Add to vg_treeview_node_t:
typedef struct vg_treeview_node_t {
    // ... existing fields ...

    vg_icon_t icon;              // Node icon
    vg_icon_t expanded_icon;     // Icon when expanded (optional, for folders)
    bool icon_loaded;            // Track if icon loaded from path
} vg_treeview_node_t;

// API additions
void vg_treeview_node_set_icon(vg_treeview_node_t* node, vg_icon_t icon);
void vg_treeview_node_set_expanded_icon(vg_treeview_node_t* node, vg_icon_t icon);

// Render icon in treeview_paint_node():
if (node->icon.type != VG_ICON_NONE) {
    vg_icon_t* icon = node->expanded && node->expanded_icon.type != VG_ICON_NONE
        ? &node->expanded_icon
        : &node->icon;

    int icon_x = x + tree->icon_gap;
    int icon_y = y + (row_height - tree->icon_size) / 2;

    render_icon(ctx, icon, icon_x, icon_y, tree->icon_size, tree->icon_size);
    x += tree->icon_size + tree->icon_gap;
}
```

#### Drag and Drop

```c
// Add to vg_treeview_t:
typedef struct {
    // ... existing fields ...

    // Drag state
    bool drag_enabled;
    vg_treeview_node_t* drag_node;
    int drag_start_x;
    int drag_start_y;
    bool is_dragging;
    vg_treeview_node_t* drop_target;
    enum { DROP_BEFORE, DROP_AFTER, DROP_INTO } drop_position;

    // Callbacks
    bool (*can_drag)(vg_treeview_node_t* node, void* user_data);
    bool (*can_drop)(vg_treeview_node_t* source, vg_treeview_node_t* target,
        int position, void* user_data);
    void (*on_drop)(vg_treeview_node_t* source, vg_treeview_node_t* target,
        int position, void* user_data);
} vg_treeview_t;

// API additions
void vg_treeview_set_drag_enabled(vg_treeview_t* tree, bool enabled);
void vg_treeview_set_drag_callbacks(vg_treeview_t* tree,
    bool (*can_drag)(vg_treeview_node_t*, void*),
    bool (*can_drop)(vg_treeview_node_t*, vg_treeview_node_t*, int, void*),
    void (*on_drop)(vg_treeview_node_t*, vg_treeview_node_t*, int, void*),
    void* user_data);
```

#### Lazy Loading

```c
// Add to vg_treeview_t:
typedef struct {
    // ... existing fields ...

    // Lazy loading callback
    void (*on_expand)(vg_treeview_t* tree, vg_treeview_node_t* node, void* user_data);
    void* expand_user_data;
} vg_treeview_t;

// API
void vg_treeview_set_on_expand(vg_treeview_t* tree,
    void (*callback)(vg_treeview_t*, vg_treeview_node_t*, void*), void* user_data);
void vg_treeview_node_set_has_children(vg_treeview_node_t* node, bool has);
void vg_treeview_node_set_loading(vg_treeview_node_t* node, bool loading);

// In expand handler:
if (node->has_children && node->child_count == 0 && tree->on_expand) {
    node->loading = true;
    tree->on_expand(tree, node, tree->expand_user_data);
    // Callback should call vg_treeview_node_add_child() then set loading=false
}
```

---

### 3.3 ListBox Virtual Scrolling

**Files to Modify:**
- `src/lib/gui/include/vg_widgets.h` - Add virtual mode fields
- `src/lib/gui/src/widgets/vg_listbox.c` - Implement virtual scrolling

#### Data Structures

```c
// Add to vg_listbox_t:
typedef struct {
    // ... existing fields ...

    // Virtual mode
    bool virtual_mode;           // Enable virtual scrolling
    size_t total_item_count;     // Total items (when virtual)
    uint32_t item_height;        // Fixed item height (required for virtual)

    // Data provider callback
    void (*get_item_data)(vg_listbox_t* list, size_t index,
        char** text, vg_icon_t* icon, void* user_data);
    void* data_provider_user_data;

    // Visible range
    size_t visible_start;
    size_t visible_count;

    // Cache for visible items
    struct {
        char* text;
        vg_icon_t icon;
        bool selected;
    }* visible_cache;
    size_t cache_capacity;
} vg_listbox_t;
```

#### API Functions

```c
// Enable virtual mode
void vg_listbox_set_virtual_mode(vg_listbox_t* list, bool virtual,
    size_t total_count, uint32_t item_height);

// Set data provider
void vg_listbox_set_data_provider(vg_listbox_t* list,
    void (*get_item)(vg_listbox_t*, size_t, char**, vg_icon_t*, void*),
    void* user_data);

// Update total count (e.g., after filtering)
void vg_listbox_set_total_count(vg_listbox_t* list, size_t count);

// Invalidate cache (force refresh)
void vg_listbox_invalidate_items(vg_listbox_t* list);
void vg_listbox_invalidate_item(vg_listbox_t* list, size_t index);
```

#### Implementation

```c
void listbox_paint_virtual(vg_listbox_t* list, vgfx_context_t* ctx) {
    // Calculate visible range
    int scroll_y = list->scroll_offset_y;
    size_t first_visible = scroll_y / list->item_height;
    size_t visible_count = (list->base.height / list->item_height) + 2;

    // Clamp to total
    if (first_visible + visible_count > list->total_item_count) {
        visible_count = list->total_item_count - first_visible;
    }

    // Update cache if range changed
    if (first_visible != list->visible_start ||
        visible_count != list->visible_count) {
        update_visible_cache(list, first_visible, visible_count);
    }

    // Render visible items
    int y = -scroll_y % list->item_height;
    for (size_t i = 0; i < list->visible_count; i++) {
        size_t actual_index = list->visible_start + i;
        render_item(list, ctx, actual_index, 0, y,
            &list->visible_cache[i]);
        y += list->item_height;
    }
}
```

---

### 3.4 MenuBar Keyboard Accelerators

**Files to Modify:**
- `src/lib/gui/include/vg_ide_widgets.h` - Add accelerator tracking
- `src/lib/gui/src/widgets/vg_menubar.c` - Implement accelerator handling

#### Accelerator System

```c
// Accelerator definition
typedef struct {
    uint32_t key;                // VG_KEY_* code
    uint32_t modifiers;          // VG_MOD_CTRL | VG_MOD_SHIFT etc.
} vg_accelerator_t;

// Parse shortcut string to accelerator
vg_accelerator_t vg_parse_accelerator(const char* shortcut);
// Examples: "Ctrl+S", "Ctrl+Shift+N", "F5", "Alt+Enter"

// Add to vg_menu_item_t:
typedef struct {
    // ... existing fields ...
    vg_accelerator_t accelerator; // Parsed from shortcut string
} vg_menu_item_t;

// Add to vg_menubar_t:
typedef struct {
    // ... existing fields ...

    // Accelerator lookup table
    struct {
        vg_accelerator_t accel;
        vg_menu_item_t* item;
    }* accelerators;
    size_t accel_count;
    size_t accel_capacity;
} vg_menubar_t;
```

#### Implementation

```c
// Build accelerator table when menu items added
void rebuild_accelerator_table(vg_menubar_t* menubar) {
    // Clear existing
    menubar->accel_count = 0;

    // Traverse all menus and items
    for (size_t m = 0; m < menubar->menu_count; m++) {
        vg_menu_t* menu = menubar->menus[m];
        for (size_t i = 0; i < menu->item_count; i++) {
            vg_menu_item_t* item = menu->items[i];
            if (item->shortcut && *item->shortcut) {
                item->accelerator = vg_parse_accelerator(item->shortcut);
                add_to_accel_table(menubar, &item->accelerator, item);
            }
            // Recurse into submenus
            if (item->submenu) {
                add_submenu_accelerators(menubar, item->submenu);
            }
        }
    }
}

// Handle key events at application level
bool vg_menubar_handle_accelerator(vg_menubar_t* menubar, vg_event_t* e) {
    if (e->type != VG_EVENT_KEY_DOWN) return false;

    for (size_t i = 0; i < menubar->accel_count; i++) {
        vg_accelerator_t* accel = &menubar->accelerators[i].accel;
        if (accel->key == e->keyboard.key &&
            accel->modifiers == e->keyboard.modifiers) {
            vg_menu_item_t* item = menubar->accelerators[i].item;
            if (item->enabled && item->action) {
                item->action(item, item->user_data);
                return true;
            }
        }
    }
    return false;
}
```

---

## Phase 4 - Polish

### 4.1 Tooltip Widget

**Files to Create:**
- `src/lib/gui/include/vg_tooltip.h`
- `src/lib/gui/src/widgets/vg_tooltip.c`

#### Data Structures

```c
typedef struct {
    vg_widget_t base;

    // Content
    char* text;                  // Plain text
    vg_widget_t* content;        // Rich content (alternative)

    // Timing
    uint32_t show_delay_ms;      // Delay before showing (default: 500)
    uint32_t hide_delay_ms;      // Delay before hiding on leave (default: 100)
    uint32_t duration_ms;        // Auto-hide after (0 = stay until leave)

    // Positioning
    enum {
        VG_TOOLTIP_FOLLOW_CURSOR,
        VG_TOOLTIP_ANCHOR_WIDGET
    } position_mode;
    int offset_x;
    int offset_y;
    vg_widget_t* anchor_widget;

    // Styling
    uint32_t max_width;          // Max width before wrapping (default: 300)
    uint32_t padding;
    uint32_t corner_radius;

    // State
    bool is_visible;
    uint64_t show_timer;
    uint64_t hide_timer;
} vg_tooltip_t;
```

#### Global Tooltip Manager

```c
// Singleton tooltip manager
typedef struct {
    vg_tooltip_t* active_tooltip;
    vg_widget_t* hovered_widget;
    uint64_t hover_start_time;
    bool pending_show;
} vg_tooltip_manager_t;

// Integration points
void vg_tooltip_manager_update(vg_tooltip_manager_t* mgr, uint64_t now_ms);
void vg_tooltip_manager_on_hover(vg_tooltip_manager_t* mgr,
    vg_widget_t* widget, int x, int y);
void vg_tooltip_manager_on_leave(vg_tooltip_manager_t* mgr);

// Widget tooltip registration
void vg_widget_set_tooltip(vg_widget_t* widget, const char* text);
void vg_widget_set_tooltip_widget(vg_widget_t* widget, vg_tooltip_t* tooltip);
```

---

### 4.2 CommandPalette Widget

**Files to Create:**
- `src/lib/gui/include/vg_commandpalette.h`
- `src/lib/gui/src/widgets/vg_commandpalette.c`

#### Data Structures

```c
// Command definition
typedef struct {
    char* id;                    // Unique ID
    char* label;                 // Display text
    char* description;           // Optional description
    char* shortcut;              // Keyboard shortcut display
    char* category;              // Category for grouping
    vg_icon_t icon;
    bool enabled;
    void* user_data;
    void (*action)(struct vg_command_t* cmd, void* user_data);
} vg_command_t;

// Command palette widget
typedef struct {
    vg_widget_t base;

    // Commands
    vg_command_t** commands;
    size_t command_count;
    size_t command_capacity;

    // Filtered results
    vg_command_t** filtered;
    size_t filtered_count;

    // UI components
    vg_textinput_t* search_input;
    vg_listbox_t* result_list;

    // State
    bool is_visible;
    int selected_index;
    char* current_query;

    // Callbacks
    void (*on_execute)(vg_command_t* cmd, void* user_data);
    void (*on_dismiss)(void* user_data);
    void* user_data;
} vg_commandpalette_t;
```

#### API Functions

```c
// Creation
vg_commandpalette_t* vg_commandpalette_create(void);
void vg_commandpalette_destroy(vg_commandpalette_t* palette);

// Command registration
void vg_commandpalette_add_command(vg_commandpalette_t* palette, vg_command_t* cmd);
void vg_commandpalette_remove_command(vg_commandpalette_t* palette, const char* id);
vg_command_t* vg_commandpalette_get_command(vg_commandpalette_t* palette, const char* id);

// Show/hide
void vg_commandpalette_show(vg_commandpalette_t* palette);
void vg_commandpalette_hide(vg_commandpalette_t* palette);
void vg_commandpalette_toggle(vg_commandpalette_t* palette);

// Filtering
void vg_commandpalette_set_filter(vg_commandpalette_t* palette, const char* prefix);
// e.g., ">" for commands, "@" for symbols, ":" for go to line
```

#### Fuzzy Matching Algorithm

```c
// Simple fuzzy match score
int fuzzy_match_score(const char* pattern, const char* text) {
    int score = 0;
    int pattern_idx = 0;
    int pattern_len = strlen(pattern);
    int consecutive_bonus = 0;

    for (int i = 0; text[i] && pattern_idx < pattern_len; i++) {
        char p = tolower(pattern[pattern_idx]);
        char t = tolower(text[i]);

        if (p == t) {
            score += 10 + consecutive_bonus;
            consecutive_bonus += 5;  // Bonus for consecutive matches
            pattern_idx++;

            // Bonus for matching at word start
            if (i == 0 || text[i-1] == ' ' || text[i-1] == '_') {
                score += 15;
            }
        } else {
            consecutive_bonus = 0;
        }
    }

    // All pattern chars must match
    if (pattern_idx < pattern_len) return -1;

    return score;
}
```

---

### 4.3 Terminal/OutputPane Widget

**Files to Create:**
- `src/lib/gui/include/vg_outputpane.h`
- `src/lib/gui/src/widgets/vg_outputpane.c`

#### Data Structures

```c
// ANSI color codes
typedef enum {
    VG_ANSI_DEFAULT = 0,
    VG_ANSI_BLACK = 30, VG_ANSI_RED, VG_ANSI_GREEN, VG_ANSI_YELLOW,
    VG_ANSI_BLUE, VG_ANSI_MAGENTA, VG_ANSI_CYAN, VG_ANSI_WHITE,
    VG_ANSI_BRIGHT_BLACK = 90, VG_ANSI_BRIGHT_RED, /* ... etc */
} vg_ansi_color_t;

// Styled text segment
typedef struct {
    char* text;
    uint32_t fg_color;
    uint32_t bg_color;
    bool bold;
    bool italic;
    bool underline;
} vg_styled_segment_t;

// Output line
typedef struct {
    vg_styled_segment_t* segments;
    size_t segment_count;
    uint64_t timestamp;
} vg_output_line_t;

// Output pane widget
typedef struct {
    vg_widget_t base;

    // Lines
    vg_output_line_t* lines;
    size_t line_count;
    size_t line_capacity;
    size_t max_lines;            // Ring buffer limit (default: 10000)

    // Scrolling
    vg_scrollview_t* scroll;
    bool auto_scroll;            // Scroll to bottom on new output
    bool scroll_locked;          // User scrolled up, don't auto-scroll

    // Selection
    bool has_selection;
    uint32_t sel_start_line, sel_start_col;
    uint32_t sel_end_line, sel_end_col;

    // Styling
    uint32_t line_height;
    vg_font_t* font;             // Monospace font

    // ANSI parser state
    struct {
        uint32_t current_fg;
        uint32_t current_bg;
        bool bold;
        bool in_escape;
        char escape_buf[32];
        int escape_len;
    } ansi_state;

    // Callbacks
    void (*on_line_click)(vg_output_line_t* line, int col, void* user_data);
    void* user_data;
} vg_outputpane_t;
```

#### API Functions

```c
// Creation
vg_outputpane_t* vg_outputpane_create(void);
void vg_outputpane_destroy(vg_outputpane_t* pane);

// Output
void vg_outputpane_append(vg_outputpane_t* pane, const char* text);
void vg_outputpane_append_line(vg_outputpane_t* pane, const char* text);
void vg_outputpane_append_styled(vg_outputpane_t* pane, const char* text,
    uint32_t fg, uint32_t bg, bool bold);
void vg_outputpane_clear(vg_outputpane_t* pane);

// Scrolling
void vg_outputpane_scroll_to_bottom(vg_outputpane_t* pane);
void vg_outputpane_scroll_to_top(vg_outputpane_t* pane);
void vg_outputpane_set_auto_scroll(vg_outputpane_t* pane, bool auto_scroll);

// Selection
char* vg_outputpane_get_selection(vg_outputpane_t* pane);
void vg_outputpane_select_all(vg_outputpane_t* pane);

// Configuration
void vg_outputpane_set_max_lines(vg_outputpane_t* pane, size_t max);
void vg_outputpane_set_font(vg_outputpane_t* pane, vg_font_t* font);
```

---

### 4.4 Breadcrumb Widget

**Files to Create:**
- `src/lib/gui/include/vg_breadcrumb.h`
- `src/lib/gui/src/widgets/vg_breadcrumb.c`

#### Data Structures

```c
// Breadcrumb item
typedef struct {
    char* label;
    char* tooltip;
    vg_icon_t icon;
    void* user_data;

    // Dropdown items (for expandable crumbs)
    struct {
        char* label;
        void* data;
    }* dropdown_items;
    size_t dropdown_count;
} vg_breadcrumb_item_t;

// Breadcrumb widget
typedef struct {
    vg_widget_t base;

    vg_breadcrumb_item_t* items;
    size_t item_count;
    size_t item_capacity;

    // Styling
    char* separator;             // Default: ">"
    uint32_t item_padding;
    uint32_t separator_padding;

    // State
    int hovered_index;
    bool dropdown_open;
    int dropdown_index;

    // Callbacks
    void (*on_click)(vg_breadcrumb_t* bc, int index, void* user_data);
    void (*on_dropdown_select)(vg_breadcrumb_t* bc, int crumb_index,
        int dropdown_index, void* user_data);
    void* user_data;
} vg_breadcrumb_t;
```

#### API Functions

```c
// Creation
vg_breadcrumb_t* vg_breadcrumb_create(void);
void vg_breadcrumb_destroy(vg_breadcrumb_t* bc);

// Items
void vg_breadcrumb_set_items(vg_breadcrumb_t* bc,
    vg_breadcrumb_item_t* items, size_t count);
void vg_breadcrumb_push(vg_breadcrumb_t* bc, const char* label, void* data);
void vg_breadcrumb_pop(vg_breadcrumb_t* bc);
void vg_breadcrumb_clear(vg_breadcrumb_t* bc);

// Dropdowns
void vg_breadcrumb_item_add_dropdown(vg_breadcrumb_item_t* item,
    const char* label, void* data);

// Configuration
void vg_breadcrumb_set_separator(vg_breadcrumb_t* bc, const char* sep);

// Callbacks
void vg_breadcrumb_set_on_click(vg_breadcrumb_t* bc,
    void (*callback)(vg_breadcrumb_t*, int, void*), void* user_data);
```

---

### 4.5 Minimap Widget

**Files to Create:**
- `src/lib/gui/include/vg_minimap.h`
- `src/lib/gui/src/widgets/vg_minimap.c`

#### Data Structures

```c
typedef struct {
    vg_widget_t base;

    // Source editor
    vg_codeeditor_t* editor;

    // Rendering
    uint32_t char_width;         // Width per character (1-2 pixels)
    uint32_t line_height;        // Height per line (1-2 pixels)
    bool show_viewport;          // Show visible region indicator
    float scale;                 // Scale factor (default: 0.1)

    // Cached render
    uint8_t* render_buffer;      // RGBA pixels
    uint32_t buffer_width;
    uint32_t buffer_height;
    bool buffer_dirty;

    // Viewport indicator
    uint32_t viewport_start_line;
    uint32_t viewport_end_line;

    // Interaction
    bool dragging;               // Dragging viewport
} vg_minimap_t;
```

#### API Functions

```c
// Creation
vg_minimap_t* vg_minimap_create(vg_codeeditor_t* editor);
void vg_minimap_destroy(vg_minimap_t* minimap);

// Configuration
void vg_minimap_set_editor(vg_minimap_t* minimap, vg_codeeditor_t* editor);
void vg_minimap_set_scale(vg_minimap_t* minimap, float scale);
void vg_minimap_set_show_viewport(vg_minimap_t* minimap, bool show);

// Update (call when editor content changes)
void vg_minimap_invalidate(vg_minimap_t* minimap);
void vg_minimap_invalidate_lines(vg_minimap_t* minimap,
    uint32_t start_line, uint32_t end_line);
```

---

### 4.6 Notification Widget

**Files to Create:**
- `src/lib/gui/include/vg_notification.h`
- `src/lib/gui/src/widgets/vg_notification.c`

#### Data Structures

```c
// Notification type
typedef enum {
    VG_NOTIFICATION_INFO,
    VG_NOTIFICATION_SUCCESS,
    VG_NOTIFICATION_WARNING,
    VG_NOTIFICATION_ERROR
} vg_notification_type_t;

// Single notification
typedef struct {
    uint32_t id;
    vg_notification_type_t type;
    char* title;
    char* message;
    uint32_t duration_ms;        // 0 = sticky (requires dismiss)
    uint64_t created_at;

    // Actions
    char* action_label;
    void (*action_callback)(uint32_t notification_id, void* user_data);
    void* action_user_data;

    // State
    float opacity;               // For fade animation
    bool dismissed;
} vg_notification_t;

// Notification manager (renders stack of notifications)
typedef struct {
    vg_widget_t base;

    vg_notification_t** notifications;
    size_t notification_count;
    size_t notification_capacity;

    // Positioning
    enum {
        VG_NOTIFICATION_TOP_RIGHT,
        VG_NOTIFICATION_TOP_LEFT,
        VG_NOTIFICATION_BOTTOM_RIGHT,
        VG_NOTIFICATION_BOTTOM_LEFT,
        VG_NOTIFICATION_TOP_CENTER,
        VG_NOTIFICATION_BOTTOM_CENTER
    } position;

    // Styling
    uint32_t max_visible;        // Max notifications shown (default: 5)
    uint32_t notification_width;
    uint32_t spacing;
    uint32_t margin;

    // Animation
    uint32_t fade_duration_ms;
    uint32_t slide_duration_ms;
} vg_notification_manager_t;
```

#### API Functions

```c
// Manager
vg_notification_manager_t* vg_notification_manager_create(void);
void vg_notification_manager_destroy(vg_notification_manager_t* mgr);
void vg_notification_manager_update(vg_notification_manager_t* mgr, uint64_t now_ms);

// Show notifications
uint32_t vg_notify(vg_notification_manager_t* mgr, vg_notification_type_t type,
    const char* title, const char* message, uint32_t duration_ms);
uint32_t vg_notify_with_action(vg_notification_manager_t* mgr,
    vg_notification_type_t type, const char* title, const char* message,
    uint32_t duration_ms, const char* action_label,
    void (*action)(uint32_t, void*), void* user_data);

// Convenience
uint32_t vg_notify_info(vg_notification_manager_t* mgr, const char* message);
uint32_t vg_notify_success(vg_notification_manager_t* mgr, const char* message);
uint32_t vg_notify_warning(vg_notification_manager_t* mgr, const char* message);
uint32_t vg_notify_error(vg_notification_manager_t* mgr, const char* message);

// Dismiss
void vg_notification_dismiss(vg_notification_manager_t* mgr, uint32_t id);
void vg_notification_dismiss_all(vg_notification_manager_t* mgr);
```

---

## Appendix A - Data Structures

### Common Icon Structure (shared across widgets)

```c
// In vg_widget.h or new vg_icon.h

typedef enum {
    VG_ICON_NONE,
    VG_ICON_GLYPH,               // Unicode codepoint
    VG_ICON_IMAGE,               // RGBA pixel data
    VG_ICON_PATH                 // File path to load
} vg_icon_type_t;

typedef struct {
    vg_icon_type_t type;
    union {
        uint32_t glyph;
        struct {
            uint8_t* pixels;     // RGBA, owned
            uint32_t width;
            uint32_t height;
        } image;
        char* path;              // File path, owned
    } data;
} vg_icon_t;

// Helper functions
vg_icon_t vg_icon_none(void);
vg_icon_t vg_icon_glyph(uint32_t codepoint);
vg_icon_t vg_icon_image(uint8_t* rgba, uint32_t w, uint32_t h);
vg_icon_t vg_icon_path(const char* path);
void vg_icon_free(vg_icon_t* icon);
vg_icon_t vg_icon_clone(const vg_icon_t* icon);
```

### Standard Icon Glyphs (using Unicode)

```c
// Common icons using Unicode symbols
#define VG_ICON_FILE        0x1F4C4  // 📄
#define VG_ICON_FOLDER      0x1F4C1  // 📁
#define VG_ICON_FOLDER_OPEN 0x1F4C2  // 📂
#define VG_ICON_SEARCH      0x1F50D  // 🔍
#define VG_ICON_SETTINGS    0x2699   // ⚙
#define VG_ICON_CLOSE       0x2715   // ✕
#define VG_ICON_CHECK       0x2713   // ✓
#define VG_ICON_WARNING     0x26A0   // ⚠
#define VG_ICON_ERROR       0x274C   // ❌
#define VG_ICON_INFO        0x2139   // ℹ
#define VG_ICON_ARROW_RIGHT 0x25B6   // ▶
#define VG_ICON_ARROW_DOWN  0x25BC   // ▼
#define VG_ICON_PLUS        0x2795   // ➕
#define VG_ICON_MINUS       0x2796   // ➖
```

---

## Appendix B - Testing Strategy

### Unit Tests for Each Widget

Create test files in `src/lib/gui/tests/`:

```
test_statusbar.c
test_toolbar.c
test_dialog.c
test_filedialog.c
test_contextmenu.c
test_findreplacebar.c
test_tooltip.c
test_commandpalette.c
test_outputpane.c
test_breadcrumb.c
test_minimap.c
test_notification.c
test_codeeditor_undo.c
test_treeview_dnd.c
test_listbox_virtual.c
test_clipboard.c
```

### Test Categories

1. **Creation/Destruction Tests**
   - Create widget, verify non-null
   - Destroy widget, verify no leaks (with ASan)

2. **Property Tests**
   - Set properties, verify getters return correct values
   - Test boundary conditions (empty strings, null, max values)

3. **Event Tests**
   - Simulate mouse/keyboard events
   - Verify callbacks invoked with correct parameters

4. **Rendering Tests**
   - Render to test buffer
   - Verify expected pixels at key locations
   - Or use golden image comparison

5. **Integration Tests**
   - Widget interactions (e.g., FindReplaceBar + CodeEditor)
   - Layout with multiple widgets

### Example Test

```c
// test_statusbar.c
#include "vg_statusbar.h"
#include <assert.h>

void test_statusbar_create() {
    vg_statusbar_t* sb = vg_statusbar_create();
    assert(sb != NULL);
    assert(sb->left_count == 0);
    assert(sb->center_count == 0);
    assert(sb->right_count == 0);
    vg_statusbar_destroy(sb);
}

void test_statusbar_add_text() {
    vg_statusbar_t* sb = vg_statusbar_create();

    vg_statusbar_item_t* item = vg_statusbar_add_text(sb,
        VG_STATUSBAR_ZONE_LEFT, "Test");

    assert(item != NULL);
    assert(item->type == VG_STATUSBAR_ITEM_TEXT);
    assert(strcmp(item->text, "Test") == 0);
    assert(sb->left_count == 1);

    vg_statusbar_destroy(sb);
}

void test_statusbar_click_callback() {
    vg_statusbar_t* sb = vg_statusbar_create();

    static bool clicked = false;
    static void* received_data = NULL;

    void callback(vg_statusbar_item_t* item, void* data) {
        clicked = true;
        received_data = data;
    }

    int user_data = 42;
    vg_statusbar_add_button(sb, VG_STATUSBAR_ZONE_RIGHT, "Click Me",
        callback, &user_data);

    // Simulate click event at button position
    // ... (would need to measure layout first)

    vg_statusbar_destroy(sb);
}

int main() {
    test_statusbar_create();
    test_statusbar_add_text();
    test_statusbar_click_callback();
    printf("All statusbar tests passed!\n");
    return 0;
}
```

---

## Implementation Order Summary

### Phase 1 (Foundation) - Estimated: ~2000 lines
1. StatusBar (~300 lines)
2. Toolbar (~400 lines)
3. Dialog (~500 lines)
4. CodeEditor undo/redo + selection fixes (~400 lines)
5. TabBar callback fixes (~100 lines)

### Phase 2 (File Operations) - Estimated: ~1500 lines
1. FileDialog (~600 lines)
2. ContextMenu (~300 lines)
3. Clipboard integration (~200 lines + platform code)

### Phase 3 (Editor Features) - Estimated: ~1500 lines
1. FindReplaceBar (~400 lines)
2. TreeView enhancements (~300 lines)
3. ListBox virtual scrolling (~300 lines)
4. MenuBar accelerators (~200 lines)

### Phase 4 (Polish) - Estimated: ~2000 lines
1. Tooltip (~200 lines)
2. CommandPalette (~400 lines)
3. Terminal/OutputPane (~500 lines)
4. Breadcrumb (~200 lines)
5. Minimap (~400 lines)
6. Notification (~300 lines)

**Total Estimated New Code: ~7000 lines**

---

## Notes

- All widgets should follow existing patterns in `vg_widgets.h` and `vg_ide_widgets.h`
- Use the existing theme system for colors and styling
- Integrate with the existing event system in `vg_event.h`
- Follow the vtable pattern established in `vg_widget.c`
- Add new widget types to `vg_widget_type_t` enum
- Update CMakeLists.txt for each new source file
- Write tests alongside implementation
