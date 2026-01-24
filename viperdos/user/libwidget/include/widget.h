//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/include/widget.h
// Purpose: Widget toolkit API for ViperDOS GUI applications.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_WIDGET_H
#define VIPER_WIDGET_H

#include <gui.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Widget Types
//===----------------------------------------------------------------------===//

typedef enum {
    WIDGET_CONTAINER,
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

typedef enum {
    LAYOUT_NONE,       // Manual positioning
    LAYOUT_HORIZONTAL, // Left to right
    LAYOUT_VERTICAL,   // Top to bottom
    LAYOUT_GRID,       // Row/column grid
    LAYOUT_BORDER,     // North/South/East/West/Center
} layout_type_t;

typedef enum {
    ALIGN_LEFT = 0,
    ALIGN_CENTER = 1,
    ALIGN_RIGHT = 2,
} alignment_t;

// Border layout constraints
typedef enum {
    BORDER_NORTH = 0,
    BORDER_SOUTH = 1,
    BORDER_EAST = 2,
    BORDER_WEST = 3,
    BORDER_CENTER = 4,
} border_constraint_t;

//===----------------------------------------------------------------------===//
// Amiga Workbench Colors
//===----------------------------------------------------------------------===//

#define WB_GRAY_LIGHT 0xFFAAAAAA
#define WB_GRAY_MED   0xFF888888
#define WB_GRAY_DARK  0xFF555555
#define WB_BLUE       0xFF0055AA
#define WB_ORANGE     0xFFFF8800
#define WB_WHITE      0xFFFFFFFF
#define WB_BLACK      0xFF000000
#define WB_RED        0xFFFF4444
#define WB_GREEN      0xFF00AA44

//===----------------------------------------------------------------------===//
// Forward Declarations
//===----------------------------------------------------------------------===//

typedef struct widget widget_t;
typedef struct layout layout_t;
typedef struct button button_t;
typedef struct label label_t;
typedef struct textbox textbox_t;
typedef struct checkbox checkbox_t;
typedef struct listview listview_t;
typedef struct treeview treeview_t;
typedef struct tree_node tree_node_t;
typedef struct menu menu_t;
typedef struct menu_item menu_item_t;
typedef struct progressbar progressbar_t;
typedef struct scrollbar scrollbar_t;

//===----------------------------------------------------------------------===//
// Event Callbacks
//===----------------------------------------------------------------------===//

typedef void (*widget_paint_fn)(widget_t *w, gui_window_t *win);
typedef void (*widget_click_fn)(widget_t *w, int x, int y, int button);
typedef void (*widget_key_fn)(widget_t *w, int keycode, char ch);
typedef void (*widget_focus_fn)(widget_t *w, bool gained);
typedef void (*widget_callback_fn)(void *user_data);
typedef void (*listview_select_fn)(int index, void *user_data);
typedef void (*treeview_select_fn)(tree_node_t *node, void *user_data);

//===----------------------------------------------------------------------===//
// Base Widget Structure
//===----------------------------------------------------------------------===//

struct widget {
    widget_type_t type;
    widget_t *parent;
    widget_t **children;
    int child_count;
    int child_capacity;

    int x, y, width, height;
    bool visible;
    bool enabled;
    bool focused;

    uint32_t bg_color;
    uint32_t fg_color;

    // Event callbacks
    widget_paint_fn on_paint;
    widget_click_fn on_click;
    widget_key_fn on_key;
    widget_focus_fn on_focus;
    void *user_data;

    // Layout
    layout_t *layout;
    int layout_constraint; // For border layout
};

//===----------------------------------------------------------------------===//
// Layout Structure
//===----------------------------------------------------------------------===//

struct layout {
    layout_type_t type;
    int spacing;
    int margin_left, margin_top, margin_right, margin_bottom;
    int columns; // For grid layout
    int rows;    // For grid layout
};

//===----------------------------------------------------------------------===//
// Button Widget
//===----------------------------------------------------------------------===//

struct button {
    widget_t base;
    char text[64];
    bool pressed;
    bool hovered;
    widget_callback_fn on_click;
    void *callback_data;
};

//===----------------------------------------------------------------------===//
// Label Widget
//===----------------------------------------------------------------------===//

struct label {
    widget_t base;
    char text[256];
    alignment_t alignment;
};

//===----------------------------------------------------------------------===//
// TextBox Widget
//===----------------------------------------------------------------------===//

struct textbox {
    widget_t base;
    char *text;
    int text_capacity;
    int text_length;
    int cursor_pos;
    int scroll_offset;
    int selection_start;
    int selection_end;
    bool password_mode;
    bool multiline;
    bool readonly;
    widget_callback_fn on_change;
    widget_callback_fn on_enter;
    void *callback_data;
};

//===----------------------------------------------------------------------===//
// Checkbox Widget
//===----------------------------------------------------------------------===//

struct checkbox {
    widget_t base;
    char text[64];
    bool checked;
    widget_callback_fn on_change;
    void *callback_data;
};

//===----------------------------------------------------------------------===//
// ListView Widget
//===----------------------------------------------------------------------===//

struct listview {
    widget_t base;
    char **items;
    int item_count;
    int item_capacity;
    int selected_index;
    int scroll_offset;
    int visible_items;
    bool multi_select;
    bool *selected; // For multi-select
    listview_select_fn on_select;
    listview_select_fn on_double_click;
    void *callback_data;
};

//===----------------------------------------------------------------------===//
// TreeView Widget
//===----------------------------------------------------------------------===//

struct tree_node {
    char text[64];
    tree_node_t *children;
    int child_count;
    int child_capacity;
    tree_node_t *parent;
    bool expanded;
    void *user_data;
};

struct treeview {
    widget_t base;
    tree_node_t *root;
    tree_node_t *selected;
    int scroll_offset;
    int visible_items;
    treeview_select_fn on_select;
    treeview_select_fn on_expand;
    void *callback_data;
};

//===----------------------------------------------------------------------===//
// Menu Structures
//===----------------------------------------------------------------------===//

struct menu_item {
    char text[64];
    char shortcut[16];
    bool separator;
    bool checked;
    bool enabled;
    menu_t *submenu;
    widget_callback_fn on_click;
    void *callback_data;
};

struct menu {
    menu_item_t *items;
    int item_count;
    int item_capacity;
    bool visible;
    int x, y;
    int width, height;
    int hovered_index;
};

//===----------------------------------------------------------------------===//
// ProgressBar Widget
//===----------------------------------------------------------------------===//

struct progressbar {
    widget_t base;
    int value;    // 0-100
    int min_val;
    int max_val;
    bool show_text;
};

//===----------------------------------------------------------------------===//
// Scrollbar Widget
//===----------------------------------------------------------------------===//

struct scrollbar {
    widget_t base;
    bool vertical;
    int value;
    int min_val;
    int max_val;
    int page_size;
    widget_callback_fn on_change;
    void *callback_data;
};

//===----------------------------------------------------------------------===//
// Message Box Types
//===----------------------------------------------------------------------===//

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

typedef enum {
    MB_RESULT_OK = 1,
    MB_RESULT_CANCEL = 2,
    MB_RESULT_YES = 3,
    MB_RESULT_NO = 4,
} msgbox_result_t;

//===----------------------------------------------------------------------===//
// Core Widget Functions
//===----------------------------------------------------------------------===//

// Creation and destruction
widget_t *widget_create(widget_type_t type, widget_t *parent);
void widget_destroy(widget_t *w);

// Geometry
void widget_set_position(widget_t *w, int x, int y);
void widget_set_size(widget_t *w, int width, int height);
void widget_set_geometry(widget_t *w, int x, int y, int width, int height);
void widget_get_geometry(widget_t *w, int *x, int *y, int *width, int *height);

// State
void widget_set_visible(widget_t *w, bool visible);
void widget_set_enabled(widget_t *w, bool enabled);
bool widget_is_visible(widget_t *w);
bool widget_is_enabled(widget_t *w);

// Colors
void widget_set_colors(widget_t *w, uint32_t fg, uint32_t bg);

// Focus
void widget_set_focus(widget_t *w);
bool widget_has_focus(widget_t *w);

// Hierarchy
void widget_add_child(widget_t *parent, widget_t *child);
void widget_remove_child(widget_t *parent, widget_t *child);
widget_t *widget_get_parent(widget_t *w);
int widget_get_child_count(widget_t *w);
widget_t *widget_get_child(widget_t *w, int index);

// Painting
void widget_repaint(widget_t *w);
void widget_paint(widget_t *w, gui_window_t *win);
void widget_paint_children(widget_t *w, gui_window_t *win);

// Events
bool widget_handle_mouse(widget_t *w, int x, int y, int button, int event_type);
bool widget_handle_key(widget_t *w, int keycode, char ch);
widget_t *widget_find_at(widget_t *root, int x, int y);

// User data
void widget_set_user_data(widget_t *w, void *data);
void *widget_get_user_data(widget_t *w);

//===----------------------------------------------------------------------===//
// Button Functions
//===----------------------------------------------------------------------===//

button_t *button_create(widget_t *parent, const char *text);
void button_set_text(button_t *btn, const char *text);
const char *button_get_text(button_t *btn);
void button_set_onclick(button_t *btn, widget_callback_fn callback, void *data);

//===----------------------------------------------------------------------===//
// Label Functions
//===----------------------------------------------------------------------===//

label_t *label_create(widget_t *parent, const char *text);
void label_set_text(label_t *lbl, const char *text);
const char *label_get_text(label_t *lbl);
void label_set_alignment(label_t *lbl, alignment_t align);

//===----------------------------------------------------------------------===//
// TextBox Functions
//===----------------------------------------------------------------------===//

textbox_t *textbox_create(widget_t *parent);
void textbox_set_text(textbox_t *tb, const char *text);
const char *textbox_get_text(textbox_t *tb);
void textbox_set_password_mode(textbox_t *tb, bool enabled);
void textbox_set_multiline(textbox_t *tb, bool enabled);
void textbox_set_readonly(textbox_t *tb, bool readonly);
void textbox_set_onchange(textbox_t *tb, widget_callback_fn callback, void *data);
void textbox_set_onenter(textbox_t *tb, widget_callback_fn callback, void *data);
int textbox_get_cursor_pos(textbox_t *tb);
void textbox_set_cursor_pos(textbox_t *tb, int pos);
void textbox_select_all(textbox_t *tb);
void textbox_clear_selection(textbox_t *tb);

//===----------------------------------------------------------------------===//
// Checkbox Functions
//===----------------------------------------------------------------------===//

checkbox_t *checkbox_create(widget_t *parent, const char *text);
void checkbox_set_text(checkbox_t *cb, const char *text);
void checkbox_set_checked(checkbox_t *cb, bool checked);
bool checkbox_is_checked(checkbox_t *cb);
void checkbox_set_onchange(checkbox_t *cb, widget_callback_fn callback, void *data);

//===----------------------------------------------------------------------===//
// ListView Functions
//===----------------------------------------------------------------------===//

listview_t *listview_create(widget_t *parent);
void listview_add_item(listview_t *lv, const char *text);
void listview_insert_item(listview_t *lv, int index, const char *text);
void listview_remove_item(listview_t *lv, int index);
void listview_clear(listview_t *lv);
int listview_get_count(listview_t *lv);
const char *listview_get_item(listview_t *lv, int index);
void listview_set_item(listview_t *lv, int index, const char *text);
int listview_get_selected(listview_t *lv);
void listview_set_selected(listview_t *lv, int index);
void listview_set_onselect(listview_t *lv, listview_select_fn callback, void *data);
void listview_set_ondoubleclick(listview_t *lv, listview_select_fn callback, void *data);
void listview_ensure_visible(listview_t *lv, int index);

//===----------------------------------------------------------------------===//
// TreeView Functions
//===----------------------------------------------------------------------===//

treeview_t *treeview_create(widget_t *parent);
tree_node_t *treeview_add_node(treeview_t *tv, tree_node_t *parent, const char *text);
void treeview_remove_node(treeview_t *tv, tree_node_t *node);
void treeview_clear(treeview_t *tv);
tree_node_t *treeview_get_root(treeview_t *tv);
tree_node_t *treeview_get_selected(treeview_t *tv);
void treeview_set_selected(treeview_t *tv, tree_node_t *node);
void treeview_expand(treeview_t *tv, tree_node_t *node);
void treeview_collapse(treeview_t *tv, tree_node_t *node);
void treeview_toggle(treeview_t *tv, tree_node_t *node);
void treeview_set_onselect(treeview_t *tv, treeview_select_fn callback, void *data);
void treeview_set_onexpand(treeview_t *tv, treeview_select_fn callback, void *data);

// Tree node functions
void tree_node_set_text(tree_node_t *node, const char *text);
const char *tree_node_get_text(tree_node_t *node);
void tree_node_set_user_data(tree_node_t *node, void *data);
void *tree_node_get_user_data(tree_node_t *node);
int tree_node_get_child_count(tree_node_t *node);
tree_node_t *tree_node_get_child(tree_node_t *node, int index);
tree_node_t *tree_node_get_parent(tree_node_t *node);

//===----------------------------------------------------------------------===//
// Menu Functions
//===----------------------------------------------------------------------===//

menu_t *menu_create(void);
void menu_destroy(menu_t *m);
void menu_add_item(menu_t *m, const char *text, widget_callback_fn callback, void *data);
void menu_add_item_with_shortcut(menu_t *m, const char *text, const char *shortcut,
                                  widget_callback_fn callback, void *data);
void menu_add_separator(menu_t *m);
void menu_add_submenu(menu_t *m, const char *text, menu_t *submenu);
void menu_set_item_enabled(menu_t *m, int index, bool enabled);
void menu_set_item_checked(menu_t *m, int index, bool checked);
void menu_show(menu_t *m, gui_window_t *win, int x, int y);
void menu_hide(menu_t *m);
bool menu_is_visible(menu_t *m);
bool menu_handle_mouse(menu_t *m, int x, int y, int button, int event_type);
void menu_paint(menu_t *m, gui_window_t *win);

//===----------------------------------------------------------------------===//
// ProgressBar Functions
//===----------------------------------------------------------------------===//

progressbar_t *progressbar_create(widget_t *parent);
void progressbar_set_value(progressbar_t *pb, int value);
int progressbar_get_value(progressbar_t *pb);
void progressbar_set_range(progressbar_t *pb, int min_val, int max_val);
void progressbar_set_show_text(progressbar_t *pb, bool show);

//===----------------------------------------------------------------------===//
// Scrollbar Functions
//===----------------------------------------------------------------------===//

scrollbar_t *scrollbar_create(widget_t *parent, bool vertical);
void scrollbar_set_value(scrollbar_t *sb, int value);
int scrollbar_get_value(scrollbar_t *sb);
void scrollbar_set_range(scrollbar_t *sb, int min_val, int max_val);
void scrollbar_set_page_size(scrollbar_t *sb, int page_size);
void scrollbar_set_onchange(scrollbar_t *sb, widget_callback_fn callback, void *data);

//===----------------------------------------------------------------------===//
// Layout Functions
//===----------------------------------------------------------------------===//

layout_t *layout_create(layout_type_t type);
void layout_destroy(layout_t *layout);
void layout_set_spacing(layout_t *layout, int spacing);
void layout_set_margins(layout_t *layout, int left, int top, int right, int bottom);
void layout_set_grid(layout_t *layout, int columns, int rows);

void widget_set_layout(widget_t *container, layout_t *layout);
void widget_set_layout_constraint(widget_t *w, int constraint);
void layout_apply(widget_t *container);

//===----------------------------------------------------------------------===//
// Dialog Functions
//===----------------------------------------------------------------------===//

msgbox_result_t msgbox_show(gui_window_t *parent, const char *title, const char *message,
                            msgbox_type_t type, msgbox_icon_t icon);

char *filedialog_open(gui_window_t *parent, const char *title, const char *filter,
                      const char *initial_dir);
char *filedialog_save(gui_window_t *parent, const char *title, const char *filter,
                      const char *initial_dir);
char *filedialog_folder(gui_window_t *parent, const char *title, const char *initial_dir);

//===----------------------------------------------------------------------===//
// 3D Drawing Functions (Amiga-style)
//===----------------------------------------------------------------------===//

void draw_3d_raised(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow);
void draw_3d_sunken(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow);
void draw_3d_button(gui_window_t *win, int x, int y, int w, int h, bool pressed);
void draw_3d_frame(gui_window_t *win, int x, int y, int w, int h, bool sunken);
void draw_3d_groove(gui_window_t *win, int x, int y, int w, int h);

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

// Widget application loop helper
typedef struct {
    gui_window_t *window;
    widget_t *root;
    widget_t *focused;
    menu_t *active_menu;
    bool running;
} widget_app_t;

widget_app_t *widget_app_create(const char *title, int width, int height);
void widget_app_destroy(widget_app_t *app);
void widget_app_set_root(widget_app_t *app, widget_t *root);
void widget_app_run(widget_app_t *app);
void widget_app_quit(widget_app_t *app);
void widget_app_repaint(widget_app_t *app);

#ifdef __cplusplus
}
#endif

#endif // VIPER_WIDGET_H
