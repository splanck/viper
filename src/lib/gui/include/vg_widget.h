// vg_widget.h - Widget base class and hierarchy
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Forward Declarations
//=============================================================================

typedef struct vg_widget vg_widget_t;
typedef struct vg_event vg_event_t;
typedef struct vg_theme vg_theme_t;

//=============================================================================
// Widget Type Enumeration
//=============================================================================

typedef enum vg_widget_type {
    VG_WIDGET_CONTAINER,     // Generic container
    VG_WIDGET_LABEL,
    VG_WIDGET_BUTTON,
    VG_WIDGET_TEXTINPUT,
    VG_WIDGET_CHECKBOX,
    VG_WIDGET_RADIO,
    VG_WIDGET_SLIDER,
    VG_WIDGET_PROGRESS,
    VG_WIDGET_SCROLLVIEW,
    VG_WIDGET_LISTVIEW,
    VG_WIDGET_LISTBOX,
    VG_WIDGET_DROPDOWN,
    VG_WIDGET_TREEVIEW,
    VG_WIDGET_TABBAR,
    VG_WIDGET_SPLITPANE,
    VG_WIDGET_MENUBAR,
    VG_WIDGET_MENU,
    VG_WIDGET_MENUITEM,
    VG_WIDGET_TOOLBAR,
    VG_WIDGET_STATUSBAR,
    VG_WIDGET_DIALOG,
    VG_WIDGET_CODEEDITOR,
    VG_WIDGET_IMAGE,
    VG_WIDGET_SPINNER,
    VG_WIDGET_COLORSWATCH,
    VG_WIDGET_COLORPALETTE,
    VG_WIDGET_COLORPICKER,
    VG_WIDGET_CUSTOM,
} vg_widget_type_t;

//=============================================================================
// Widget State Flags
//=============================================================================

typedef enum vg_widget_state {
    VG_STATE_NORMAL   = 0,
    VG_STATE_HOVERED  = 1 << 0,
    VG_STATE_PRESSED  = 1 << 1,
    VG_STATE_FOCUSED  = 1 << 2,
    VG_STATE_DISABLED = 1 << 3,
    VG_STATE_SELECTED = 1 << 4,
    VG_STATE_CHECKED  = 1 << 5,
} vg_widget_state_t;

//=============================================================================
// Size Constraints
//=============================================================================

typedef struct vg_constraints {
    float min_width;
    float min_height;
    float max_width;         // 0 = no maximum
    float max_height;        // 0 = no maximum
    float preferred_width;   // 0 = auto
    float preferred_height;  // 0 = auto
} vg_constraints_t;

//=============================================================================
// Layout Parameters
//=============================================================================

typedef struct vg_layout_params {
    float flex;              // Flex grow factor (0 = fixed size)
    float margin_left;
    float margin_top;
    float margin_right;
    float margin_bottom;
    float padding_left;
    float padding_top;
    float padding_right;
    float padding_bottom;
} vg_layout_params_t;

//=============================================================================
// Virtual Function Table
//=============================================================================

typedef struct vg_widget_vtable {
    // Lifecycle
    void (*destroy)(vg_widget_t* self);

    // Layout
    void (*measure)(vg_widget_t* self, float available_width, float available_height);
    void (*arrange)(vg_widget_t* self, float x, float y, float width, float height);

    // Rendering
    void (*paint)(vg_widget_t* self, void* canvas);

    // Events
    bool (*handle_event)(vg_widget_t* self, vg_event_t* event);

    // Focus
    bool (*can_focus)(vg_widget_t* self);
    void (*on_focus)(vg_widget_t* self, bool gained);
} vg_widget_vtable_t;

//=============================================================================
// Widget Base Structure
//=============================================================================

struct vg_widget {
    // Type and vtable
    vg_widget_type_t type;
    const vg_widget_vtable_t* vtable;

    // Identity
    uint32_t id;             // Unique widget ID
    char* name;              // Optional name for lookup

    // Hierarchy
    vg_widget_t* parent;
    vg_widget_t* first_child;
    vg_widget_t* last_child;
    vg_widget_t* next_sibling;
    vg_widget_t* prev_sibling;
    int child_count;

    // Geometry (set by layout)
    float x, y;              // Position relative to parent
    float width, height;     // Actual size after layout

    // Measured size (set by measure pass)
    float measured_width;
    float measured_height;

    // Constraints
    vg_constraints_t constraints;

    // Layout parameters
    vg_layout_params_t layout;

    // State
    uint32_t state;          // Combination of vg_widget_state_t flags
    bool visible;
    bool enabled;
    bool needs_layout;
    bool needs_paint;

    // User data
    void* user_data;

    // Callbacks
    void (*on_click)(vg_widget_t* self, void* user_data);
    void (*on_change)(vg_widget_t* self, void* user_data);
    void (*on_submit)(vg_widget_t* self, void* user_data);
    void* callback_data;

    // Widget-specific data follows (via flexible array or separate allocation)
    // This is used for the specific widget implementation data
    void* impl_data;
};

//=============================================================================
// Widget Creation/Destruction
//=============================================================================

/// Initialize a widget base (called by widget constructors)
void vg_widget_init(vg_widget_t* widget, vg_widget_type_t type, const vg_widget_vtable_t* vtable);

/// Create a container widget
vg_widget_t* vg_widget_create(vg_widget_type_t type);

/// Destroy widget and all children
void vg_widget_destroy(vg_widget_t* widget);

//=============================================================================
// Hierarchy Management
//=============================================================================

/// Add child widget
void vg_widget_add_child(vg_widget_t* parent, vg_widget_t* child);

/// Insert child at specific index
void vg_widget_insert_child(vg_widget_t* parent, vg_widget_t* child, int index);

/// Remove child widget (does not destroy)
void vg_widget_remove_child(vg_widget_t* parent, vg_widget_t* child);

/// Remove all children (does not destroy)
void vg_widget_clear_children(vg_widget_t* parent);

/// Get child at index
vg_widget_t* vg_widget_get_child(vg_widget_t* parent, int index);

/// Find widget by name (recursive)
vg_widget_t* vg_widget_find_by_name(vg_widget_t* root, const char* name);

/// Find widget by ID
vg_widget_t* vg_widget_find_by_id(vg_widget_t* root, uint32_t id);

//=============================================================================
// Geometry & Constraints
//=============================================================================

/// Set size constraints
void vg_widget_set_constraints(vg_widget_t* widget, vg_constraints_t constraints);

/// Set minimum size
void vg_widget_set_min_size(vg_widget_t* widget, float width, float height);

/// Set maximum size
void vg_widget_set_max_size(vg_widget_t* widget, float width, float height);

/// Set preferred size
void vg_widget_set_preferred_size(vg_widget_t* widget, float width, float height);

/// Set fixed size (min = max = preferred)
void vg_widget_set_fixed_size(vg_widget_t* widget, float width, float height);

/// Get bounding rectangle in parent coordinates
void vg_widget_get_bounds(vg_widget_t* widget, float* x, float* y, float* width, float* height);

/// Get bounding rectangle in screen coordinates
void vg_widget_get_screen_bounds(vg_widget_t* widget, float* x, float* y, float* width, float* height);

//=============================================================================
// Layout Parameters
//=============================================================================

/// Set flex grow factor
void vg_widget_set_flex(vg_widget_t* widget, float flex);

/// Set margin (all sides)
void vg_widget_set_margin(vg_widget_t* widget, float margin);

/// Set margin (individual sides)
void vg_widget_set_margins(vg_widget_t* widget, float left, float top, float right, float bottom);

/// Set padding (all sides)
void vg_widget_set_padding(vg_widget_t* widget, float padding);

/// Set padding (individual sides)
void vg_widget_set_paddings(vg_widget_t* widget, float left, float top, float right, float bottom);

//=============================================================================
// State Management
//=============================================================================

/// Set widget enabled state
void vg_widget_set_enabled(vg_widget_t* widget, bool enabled);

/// Get widget enabled state
bool vg_widget_is_enabled(vg_widget_t* widget);

/// Set widget visibility
void vg_widget_set_visible(vg_widget_t* widget, bool visible);

/// Get widget visibility
bool vg_widget_is_visible(vg_widget_t* widget);

/// Check if widget has specific state flag
bool vg_widget_has_state(vg_widget_t* widget, vg_widget_state_t state);

/// Set widget name
void vg_widget_set_name(vg_widget_t* widget, const char* name);

/// Get widget name
const char* vg_widget_get_name(vg_widget_t* widget);

//=============================================================================
// Layout & Rendering
//=============================================================================

/// Trigger measure pass on widget tree
void vg_widget_measure(vg_widget_t* root, float available_width, float available_height);

/// Trigger arrange pass on widget tree
void vg_widget_arrange(vg_widget_t* root, float x, float y, float width, float height);

/// Trigger full layout (measure + arrange)
void vg_widget_layout(vg_widget_t* root, float available_width, float available_height);

/// Paint widget tree to canvas
void vg_widget_paint(vg_widget_t* root, void* canvas);

/// Mark widget as needing repaint
void vg_widget_invalidate(vg_widget_t* widget);

/// Mark widget as needing layout
void vg_widget_invalidate_layout(vg_widget_t* widget);

//=============================================================================
// Hit Testing
//=============================================================================

/// Find widget at screen coordinates
vg_widget_t* vg_widget_hit_test(vg_widget_t* root, float x, float y);

/// Check if point is inside widget
bool vg_widget_contains_point(vg_widget_t* widget, float x, float y);

//=============================================================================
// Focus Management
//=============================================================================

/// Set focus to widget
void vg_widget_set_focus(vg_widget_t* widget);

/// Get currently focused widget in tree
vg_widget_t* vg_widget_get_focused(vg_widget_t* root);

/// Move focus to next focusable widget
void vg_widget_focus_next(vg_widget_t* root);

/// Move focus to previous focusable widget
void vg_widget_focus_prev(vg_widget_t* root);

//=============================================================================
// ID Generation
//=============================================================================

/// Generate unique widget ID
uint32_t vg_widget_next_id(void);

#ifdef __cplusplus
}
#endif
