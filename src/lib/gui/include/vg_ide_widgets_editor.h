//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/include/vg_ide_widgets_editor.h
// Purpose: CodeEditor, FindReplaceBar, and Minimap widget declarations.
// Key invariants:
//   - All create functions return NULL on allocation failure.
//   - String parameters are copied internally unless documented otherwise.
// Ownership/Lifetime:
//   - Widgets are owned by their parent in the widget tree.
// Links: vg_ide_widgets_common.h, vg_widget.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vg_ide_widgets_common.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// CodeEditor Widget
//=============================================================================

/// @brief Edit operation types for undo/redo
typedef enum vg_edit_op_type {
    VG_EDIT_INSERT, ///< Text inserted
    VG_EDIT_DELETE, ///< Text deleted
    VG_EDIT_REPLACE ///< Text replaced (delete + insert)
} vg_edit_op_type_t;

/// @brief Single edit operation for undo/redo history
typedef struct vg_edit_op {
    vg_edit_op_type_t type; ///< Operation type
    int cursor_id;          ///< 0 = primary cursor, 1+ = extra cursor slot

    // Position info
    int start_line; ///< Start line
    int start_col;  ///< Start column
    int end_line;   ///< End line
    int end_col;    ///< End column

    // Text data
    char *old_text; ///< Text before operation (for DELETE/REPLACE)
    char *new_text; ///< Text after operation (for INSERT/REPLACE)

    // Cursor position to restore
    int cursor_line_before;
    int cursor_col_before;
    int cursor_line_after;
    int cursor_col_after;

    // Grouping for compound operations
    uint32_t group_id; ///< Non-zero if part of a group
} vg_edit_op_t;

/// @brief Undo/redo history
typedef struct vg_edit_history {
    vg_edit_op_t **operations; ///< Array of edit operations
    size_t count;              ///< Number of operations
    size_t capacity;           ///< Allocated capacity
    size_t current_index;      ///< Points to next redo operation
    uint32_t next_group_id;    ///< Counter for grouping
    bool is_grouping;          ///< Currently recording a group
    uint32_t current_group;    ///< Active group ID
} vg_edit_history_t;

/// @brief Line information
typedef struct vg_code_line {
    char *text;             ///< Line text (owned)
    size_t length;          ///< Text length
    size_t capacity;        ///< Buffer capacity
    uint32_t *colors;       ///< Per-character colors (owned, optional)
    size_t colors_capacity; ///< Allocated entries in colors array
    bool modified;          ///< Line modified since last save
} vg_code_line_t;

/// @brief Selection range
typedef struct vg_selection {
    int start_line;
    int start_col;
    int end_line;
    int end_col;
} vg_selection_t;

/// @brief Syntax highlighter callback
typedef void (*vg_syntax_callback_t)(
    vg_widget_t *editor, int line_num, const char *text, uint32_t *colors, void *user_data);

/// @brief CodeEditor widget structure
typedef struct vg_codeeditor {
    vg_widget_t base;

    // Document
    vg_code_line_t *lines; ///< Array of lines
    int line_count;        ///< Number of lines
    int line_capacity;     ///< Allocated capacity

    // Cursor and selection
    int cursor_line;          ///< Cursor line (0-based)
    int cursor_col;           ///< Cursor column (0-based)
    vg_selection_t selection; ///< Current selection
    bool has_selection;       ///< Is there an active selection

    // Scroll
    float scroll_x;         ///< Horizontal scroll
    float scroll_y;         ///< Vertical scroll
    int visible_first_line; ///< First visible line
    int visible_line_count; ///< Number of visible lines

    // Font
    vg_font_t *font;   ///< Monospace font
    float font_size;   ///< Font size
    float char_width;  ///< Character width (monospace)
    float line_height; ///< Line height

    // Gutter
    bool show_line_numbers;     ///< Show line number gutter
    float gutter_width;         ///< Gutter width
    uint32_t gutter_bg;         ///< Gutter background color
    uint32_t line_number_color; ///< Line number color

    // Appearance
    uint32_t bg_color;        ///< Background color
    uint32_t text_color;      ///< Default text color
    uint32_t cursor_color;    ///< Cursor color
    uint32_t selection_color; ///< Selection color
    uint32_t current_line_bg; ///< Current line highlight

    // Syntax highlighting
    vg_syntax_callback_t syntax_highlighter;
    void *syntax_data;

    // Token colors (overridable per-editor; indices: 0=default, 1=keyword, 2=type,
    // 3=string, 4=comment, 5=number). Zero means "use theme default".
    uint32_t token_colors[6];

    // Custom keywords (comma-separated list parsed into array; checked after
    // language keywords in the syntax callback). Owned by the editor.
    char **custom_keywords;
    int custom_keyword_count;

    // Auto fold detection
    bool auto_fold_detection; ///< Detect fold regions from indentation

    // Editing options
    bool read_only;   ///< Read-only mode
    bool insert_mode; ///< Insert vs overwrite mode
    int tab_width;    ///< Tab width in spaces
    bool use_spaces;  ///< Use spaces for tabs
    bool auto_indent; ///< Auto-indent on enter
    bool word_wrap;   ///< Word wrapping

    // State
    bool cursor_visible;     ///< Cursor blink state
    float cursor_blink_time; ///< Cursor blink timer
    bool modified;           ///< Document modified since last save

    // Undo/redo history
    vg_edit_history_t *history; ///< Edit history for undo/redo

    // Scrollbar drag state
    bool scrollbar_dragging;           ///< True while user is dragging the scroll thumb
    float scrollbar_drag_offset;       ///< Mouse Y at drag start (widget-relative)
    float scrollbar_drag_start_scroll; ///< scroll_y value at drag start

    // Highlight spans (arbitrary colored regions overlaid on text)
    struct vg_highlight_span {
        int start_line, start_col; ///< Inclusive start position
        int end_line, end_col;     ///< Exclusive end position
        uint32_t color;            ///< Background highlight color (0x00RRGGBB)
    } *highlight_spans;            ///< Owned array; NULL when unused

    int highlight_span_count; ///< Active span count
    int highlight_span_cap;   ///< Allocated capacity

    // Gutter icons (breakpoints, diagnostics, etc.)
    struct vg_gutter_icon {
        int line;       ///< 0-based line number
        int type;       ///< 0=breakpoint, 1=warning, 2=error, 3=info
        uint32_t color; ///< Icon color (0x00RRGGBB)
    } *gutter_icons;    ///< Owned array; NULL when unused

    int gutter_icon_count; ///< Active icon count
    int gutter_icon_cap;   ///< Allocated capacity

    // Per-editor gutter click state (edge-triggered, cleared after read)
    bool gutter_clicked;     ///< A gutter click occurred this frame
    int gutter_clicked_line; ///< Line that was clicked (-1 if none)
    int gutter_clicked_slot; ///< Slot that was clicked (-1 if none)

    // Fold gutter & regions
    bool show_fold_gutter; ///< Show fold indicators in gutter

    struct vg_fold_region {
        int start_line; ///< First line of the foldable block
        int end_line;   ///< Last line of the foldable block (inclusive)
        bool folded;    ///< Whether the region is currently collapsed
    } *fold_regions;    ///< Owned array; NULL when unused

    int fold_region_count; ///< Active region count
    int fold_region_cap;   ///< Allocated capacity

    // Extra cursors (multi-cursor editing state)
    struct vg_extra_cursor {
        int line;                ///< 0-based line number
        int col;                 ///< 0-based column number
        vg_selection_t selection; ///< Per-cursor selection range
        bool has_selection;      ///< Whether this cursor owns an active selection
    } *extra_cursors; ///< Owned array; NULL when unused

    int extra_cursor_count; ///< Active extra cursor count
    int extra_cursor_cap;   ///< Allocated capacity
} vg_codeeditor_t;

/// @brief Create a new code editor widget
vg_codeeditor_t *vg_codeeditor_create(vg_widget_t *parent);

/// @brief Set editor text content
void vg_codeeditor_set_text(vg_codeeditor_t *editor, const char *text);

/// @brief Get editor text content (caller must free)
char *vg_codeeditor_get_text(vg_codeeditor_t *editor);

/// @brief Get selected text (caller must free, or NULL if no selection)
char *vg_codeeditor_get_selection(vg_codeeditor_t *editor);

/// @brief Set cursor position
void vg_codeeditor_set_cursor(vg_codeeditor_t *editor, int line, int col);

/// @brief Get cursor position
void vg_codeeditor_get_cursor(vg_codeeditor_t *editor, int *out_line, int *out_col);

/// @brief Set selection range
void vg_codeeditor_set_selection(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col);

/// @brief Insert text at cursor
void vg_codeeditor_insert_text(vg_codeeditor_t *editor, const char *text);

/// @brief Delete selected text
void vg_codeeditor_delete_selection(vg_codeeditor_t *editor);

/// @brief Scroll to line
void vg_codeeditor_scroll_to_line(vg_codeeditor_t *editor, int line);

/// @brief Set syntax highlighter
void vg_codeeditor_set_syntax(vg_codeeditor_t *editor,
                              vg_syntax_callback_t callback,
                              void *user_data);

/// @brief Undo last action
void vg_codeeditor_undo(vg_codeeditor_t *editor);

/// @brief Redo last undone action
void vg_codeeditor_redo(vg_codeeditor_t *editor);

/// @brief Copy selected text to clipboard
bool vg_codeeditor_copy(vg_codeeditor_t *editor);

/// @brief Cut selected text to clipboard
bool vg_codeeditor_cut(vg_codeeditor_t *editor);

/// @brief Paste text from clipboard
bool vg_codeeditor_paste(vg_codeeditor_t *editor);

/// @brief Select all text
void vg_codeeditor_select_all(vg_codeeditor_t *editor);

/// @brief Set font for code editor
void vg_codeeditor_set_font(vg_codeeditor_t *editor, vg_font_t *font, float size);

/// @brief Get line count
int vg_codeeditor_get_line_count(vg_codeeditor_t *editor);

/// @brief Check if document is modified
bool vg_codeeditor_is_modified(vg_codeeditor_t *editor);

/// @brief Clear modified flag
void vg_codeeditor_clear_modified(vg_codeeditor_t *editor);

//=============================================================================
// FindReplaceBar Widget
//=============================================================================

/// @brief Search options
typedef struct vg_search_options {
    bool case_sensitive; ///< Case-sensitive search
    bool whole_word;     ///< Match whole words only
    bool use_regex;      ///< Use regular expressions
    bool in_selection;   ///< Search within selection only
    bool wrap_around;    ///< Wrap to beginning when reaching end
} vg_search_options_t;

/// @brief Search match
typedef struct vg_search_match {
    uint32_t line;      ///< Line number (0-based)
    uint32_t start_col; ///< Start column
    uint32_t end_col;   ///< End column
} vg_search_match_t;

/// @brief FindReplaceBar widget structure
typedef struct vg_findreplacebar {
    vg_widget_t base;

    // Mode
    bool show_replace; ///< Show replace controls

    // Child widgets (void* to avoid circular deps)
    void *find_input;        ///< Find text input
    void *replace_input;     ///< Replace text input
    void *find_prev_btn;     ///< Find previous button
    void *find_next_btn;     ///< Find next button
    void *replace_btn;       ///< Replace button
    void *replace_all_btn;   ///< Replace all button
    void *close_btn;         ///< Close button
    void *case_sensitive_cb; ///< Case sensitive checkbox
    void *whole_word_cb;     ///< Whole word checkbox
    void *regex_cb;          ///< Regex checkbox

    // Search state
    vg_search_options_t options; ///< Search options
    vg_search_match_t *matches;  ///< All matches in document
    size_t match_count;          ///< Number of matches
    size_t match_capacity;       ///< Match array capacity
    size_t current_match;        ///< Index of current match

    // Target editor
    struct vg_codeeditor *target_editor; ///< Editor to search in

    // Result display
    char result_text[64]; ///< "3 of 42" or "No results"

    // Font
    vg_font_t *font; ///< Font for text
    float font_size; ///< Font size

    // Colors
    uint32_t bg_color;          ///< Background color
    uint32_t border_color;      ///< Border color
    uint32_t match_highlight;   ///< Match highlight color
    uint32_t current_highlight; ///< Current match highlight

    // Callbacks
    void *user_data; ///< User data
    void (*on_find)(struct vg_findreplacebar *bar,
                    const char *query,
                    vg_search_options_t *options,
                    void *user_data);
    void (*on_replace)(struct vg_findreplacebar *bar,
                       const char *find,
                       const char *replace,
                       void *user_data);
    void (*on_replace_all)(struct vg_findreplacebar *bar,
                           const char *find,
                           const char *replace,
                           void *user_data);
    void (*on_close)(struct vg_findreplacebar *bar, void *user_data);
} vg_findreplacebar_t;

/// @brief Create a new find/replace bar widget
vg_findreplacebar_t *vg_findreplacebar_create(void);

/// @brief Destroy a find/replace bar widget
void vg_findreplacebar_destroy(vg_findreplacebar_t *bar);

/// @brief Set target editor for searching
void vg_findreplacebar_set_target(vg_findreplacebar_t *bar, struct vg_codeeditor *editor);

/// @brief Show or hide replace controls
void vg_findreplacebar_set_show_replace(vg_findreplacebar_t *bar, bool show);

/// @brief Set search options
void vg_findreplacebar_set_options(vg_findreplacebar_t *bar, vg_search_options_t *options);

/// @brief Perform search with query
void vg_findreplacebar_find(vg_findreplacebar_t *bar, const char *query);

/// @brief Find next match
void vg_findreplacebar_find_next(vg_findreplacebar_t *bar);

/// @brief Find previous match
void vg_findreplacebar_find_prev(vg_findreplacebar_t *bar);

/// @brief Replace current match
void vg_findreplacebar_replace_current(vg_findreplacebar_t *bar);

/// @brief Replace all matches
void vg_findreplacebar_replace_all(vg_findreplacebar_t *bar);

/// @brief Get match count
size_t vg_findreplacebar_get_match_count(vg_findreplacebar_t *bar);

/// @brief Get current match index
size_t vg_findreplacebar_get_current_match(vg_findreplacebar_t *bar);

/// @brief Focus the find input
void vg_findreplacebar_focus(vg_findreplacebar_t *bar);

/// @brief Set find text
void vg_findreplacebar_set_find_text(vg_findreplacebar_t *bar, const char *text);

/// @brief Set close callback
void vg_findreplacebar_set_on_close(vg_findreplacebar_t *bar,
                                    void (*callback)(vg_findreplacebar_t *, void *),
                                    void *user_data);

/// @brief Set font for find/replace bar
void vg_findreplacebar_set_font(vg_findreplacebar_t *bar, vg_font_t *font, float size);

//=============================================================================
// Minimap Widget
//=============================================================================

/// @brief Minimap widget structure
typedef struct vg_minimap {
    vg_widget_t base;

    // Source editor
    vg_codeeditor_t *editor; ///< Editor to display

    // Rendering
    uint32_t char_width;  ///< Width per character (1-2 pixels)
    uint32_t line_height; ///< Height per line (1-2 pixels)
    bool show_viewport;   ///< Show visible region indicator
    float scale;          ///< Scale factor (default: 0.1)

    // Cached render
    uint8_t *render_buffer; ///< RGBA pixels
    uint32_t buffer_width;
    uint32_t buffer_height;
    bool buffer_dirty; ///< Needs re-render

    // Viewport indicator
    uint32_t viewport_start_line;
    uint32_t viewport_end_line;
    uint32_t viewport_color;

    // Styling
    uint32_t bg_color;
    uint32_t text_color;

    // Interaction
    bool dragging;    ///< Dragging viewport
    int drag_start_y; ///< Drag start Y position

    // Markers (colored horizontal lines overlaid on the minimap)
    struct vg_minimap_marker {
        int line;       ///< Source editor line number (0-based)
        uint32_t color; ///< Marker color (0x00RRGGBB)
        int type;       ///< Marker type (user-defined category)
    } *markers;         ///< Owned array; NULL when unused

    int marker_count; ///< Active marker count
    int marker_cap;   ///< Allocated capacity
} vg_minimap_t;

/// @brief Create a new minimap widget
vg_minimap_t *vg_minimap_create(vg_codeeditor_t *editor);

/// @brief Destroy a minimap widget
void vg_minimap_destroy(vg_minimap_t *minimap);

/// @brief Set editor for minimap
void vg_minimap_set_editor(vg_minimap_t *minimap, vg_codeeditor_t *editor);

/// @brief Set scale factor
void vg_minimap_set_scale(vg_minimap_t *minimap, float scale);

/// @brief Set viewport indicator visibility
void vg_minimap_set_show_viewport(vg_minimap_t *minimap, bool show);

/// @brief Invalidate entire minimap (needs re-render)
void vg_minimap_invalidate(vg_minimap_t *minimap);

/// @brief Invalidate specific lines
void vg_minimap_invalidate_lines(vg_minimap_t *minimap, uint32_t start_line, uint32_t end_line);

#ifdef __cplusplus
}
#endif
