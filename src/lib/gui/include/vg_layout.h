// vg_layout.h - Layout system for widget positioning
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Forward Declarations
//=============================================================================

typedef struct vg_widget vg_widget_t;

//=============================================================================
// Layout Types
//=============================================================================

typedef enum vg_layout_type {
    VG_LAYOUT_NONE,          // Manual positioning
    VG_LAYOUT_VBOX,          // Vertical stack
    VG_LAYOUT_HBOX,          // Horizontal stack
    VG_LAYOUT_FLEX,          // Flexbox-like
    VG_LAYOUT_GRID,          // Grid layout
    VG_LAYOUT_ABSOLUTE,      // Absolute positioning
    VG_LAYOUT_DOCK,          // Dock layout (for IDE panels)
} vg_layout_type_t;

//=============================================================================
// Alignment
//=============================================================================

typedef enum vg_align {
    VG_ALIGN_START,
    VG_ALIGN_CENTER,
    VG_ALIGN_END,
    VG_ALIGN_STRETCH,
    VG_ALIGN_BASELINE,       // For text alignment
} vg_align_t;

// Horizontal text alignment
typedef enum vg_h_align {
    VG_ALIGN_H_LEFT,
    VG_ALIGN_H_CENTER,
    VG_ALIGN_H_RIGHT,
} vg_h_align_t;

// Vertical text alignment
typedef enum vg_v_align {
    VG_ALIGN_V_TOP,
    VG_ALIGN_V_CENTER,
    VG_ALIGN_V_BOTTOM,
    VG_ALIGN_V_BASELINE,
} vg_v_align_t;

//=============================================================================
// Justify Content
//=============================================================================

typedef enum vg_justify {
    VG_JUSTIFY_START,
    VG_JUSTIFY_CENTER,
    VG_JUSTIFY_END,
    VG_JUSTIFY_SPACE_BETWEEN,
    VG_JUSTIFY_SPACE_AROUND,
    VG_JUSTIFY_SPACE_EVENLY,
} vg_justify_t;

//=============================================================================
// Direction
//=============================================================================

typedef enum vg_direction {
    VG_DIRECTION_ROW,
    VG_DIRECTION_ROW_REVERSE,
    VG_DIRECTION_COLUMN,
    VG_DIRECTION_COLUMN_REVERSE,
} vg_direction_t;

//=============================================================================
// Dock Position
//=============================================================================

typedef enum vg_dock {
    VG_DOCK_NONE,
    VG_DOCK_LEFT,
    VG_DOCK_TOP,
    VG_DOCK_RIGHT,
    VG_DOCK_BOTTOM,
    VG_DOCK_FILL,
} vg_dock_t;

//=============================================================================
// VBox Layout Data
//=============================================================================

typedef struct vg_vbox_layout {
    float spacing;
    vg_align_t align;        // Cross-axis alignment
    vg_justify_t justify;    // Main-axis distribution
} vg_vbox_layout_t;

//=============================================================================
// HBox Layout Data
//=============================================================================

typedef struct vg_hbox_layout {
    float spacing;
    vg_align_t align;
    vg_justify_t justify;
} vg_hbox_layout_t;

//=============================================================================
// Flex Layout Data
//=============================================================================

typedef struct vg_flex_layout {
    vg_direction_t direction;
    vg_align_t align_items;
    vg_justify_t justify_content;
    vg_align_t align_content;
    float gap;
    bool wrap;
} vg_flex_layout_t;

//=============================================================================
// Grid Layout Data
//=============================================================================

typedef struct vg_grid_layout {
    int columns;
    int rows;
    float column_gap;
    float row_gap;
    float* column_widths;    // NULL = equal width
    float* row_heights;      // NULL = equal height
} vg_grid_layout_t;

//=============================================================================
// Grid Item Data (stored in widget)
//=============================================================================

typedef struct vg_grid_item {
    int column;
    int row;
    int col_span;
    int row_span;
} vg_grid_item_t;

//=============================================================================
// VBox Layout API
//=============================================================================

/// Create vertical box container
vg_widget_t* vg_vbox_create(float spacing);

/// Set VBox properties
void vg_vbox_set_spacing(vg_widget_t* vbox, float spacing);
void vg_vbox_set_align(vg_widget_t* vbox, vg_align_t align);
void vg_vbox_set_justify(vg_widget_t* vbox, vg_justify_t justify);

//=============================================================================
// HBox Layout API
//=============================================================================

/// Create horizontal box container
vg_widget_t* vg_hbox_create(float spacing);

/// Set HBox properties
void vg_hbox_set_spacing(vg_widget_t* hbox, float spacing);
void vg_hbox_set_align(vg_widget_t* hbox, vg_align_t align);
void vg_hbox_set_justify(vg_widget_t* hbox, vg_justify_t justify);

//=============================================================================
// Flex Layout API
//=============================================================================

/// Create flex container
vg_widget_t* vg_flex_create(void);

/// Set Flex properties
void vg_flex_set_direction(vg_widget_t* flex, vg_direction_t direction);
void vg_flex_set_align_items(vg_widget_t* flex, vg_align_t align);
void vg_flex_set_justify_content(vg_widget_t* flex, vg_justify_t justify);
void vg_flex_set_gap(vg_widget_t* flex, float gap);
void vg_flex_set_wrap(vg_widget_t* flex, bool wrap);

//=============================================================================
// Grid Layout API
//=============================================================================

/// Create grid container
vg_widget_t* vg_grid_create(int columns, int rows);

/// Set Grid properties
void vg_grid_set_columns(vg_widget_t* grid, int columns);
void vg_grid_set_rows(vg_widget_t* grid, int rows);
void vg_grid_set_gap(vg_widget_t* grid, float column_gap, float row_gap);
void vg_grid_set_column_width(vg_widget_t* grid, int column, float width);
void vg_grid_set_row_height(vg_widget_t* grid, int row, float height);

/// Place child in specific cell
void vg_grid_place(vg_widget_t* grid, vg_widget_t* child,
                   int column, int row, int col_span, int row_span);

//=============================================================================
// Dock Layout API
//=============================================================================

/// Create dock container
vg_widget_t* vg_dock_create(void);

/// Add docked child
void vg_dock_add(vg_widget_t* dock, vg_widget_t* child, vg_dock_t position);

//=============================================================================
// Layout Engine Functions (internal use)
//=============================================================================

/// Perform layout for vbox
void vg_layout_vbox(vg_widget_t* container, float width, float height);

/// Perform layout for hbox
void vg_layout_hbox(vg_widget_t* container, float width, float height);

/// Perform layout for flex
void vg_layout_flex(vg_widget_t* container, float width, float height);

/// Perform layout for grid
void vg_layout_grid(vg_widget_t* container, float width, float height);

/// Perform layout for dock
void vg_layout_dock(vg_widget_t* container, float width, float height);

#ifdef __cplusplus
}
#endif
