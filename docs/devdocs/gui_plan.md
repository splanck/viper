# Viper.GUI Implementation Plan

A comprehensive plan for building a cross-platform GUI toolkit for Zia, designed to support building a professional IDE.

> **Status Update (January 2026):** All phases (1–7) are complete. The `Viper.GUI.*` namespace is now available to Viper programs with 24 widget classes including App, Label, Button, TextInput, Checkbox, Dropdown, Slider, ProgressBar, ListBox, RadioButton, Spinner, Image, TreeView, TabBar, CodeEditor, and layout containers.

**Goals:**
- Zero external dependencies (consistent with Viper philosophy)
- Pixel-identical appearance across Windows, macOS, and Linux
- Code-based API (no markup languages)
- Full-featured enough for professional IDE development

**Foundation:** Built on existing `Viper.Graphics.*` infrastructure.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Font Engine](#2-font-engine)
3. [Widget Framework Core](#3-widget-framework-core)
4. [Layout System](#4-layout-system)
5. [Core Widgets](#5-core-widgets)
6. [IDE Widgets](#6-ide-widgets)
7. [Theming System](#7-theming-system)
8. [Platform Extensions](#8-platform-extensions)
9. [Zia API](#9-zia-api)
10. [Implementation Phases](#10-implementation-phases)

---

## 1. Architecture Overview

### Layer Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Zia API                                │
│              Viper.GUI.Window, Button, TextEdit, etc.               │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      Runtime Bridge (C)                              │
│                  rt_gui.h / rt_gui.c                                │
│         Exposes widget classes to Viper runtime system              │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     Widget Framework (C)                             │
│    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │
│    │   Widgets    │  │    Layout    │  │    Events    │            │
│    │  vg_widget.h │  │  vg_layout.h │  │  vg_event.h  │            │
│    └──────────────┘  └──────────────┘  └──────────────┘            │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      Font Engine (C)                                 │
│    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │
│    │  TTF Parser  │  │  Rasterizer  │  │ Glyph Cache  │            │
│    │ vg_ttf.h     │  │ vg_raster.h  │  │ vg_cache.h   │            │
│    └──────────────┘  └──────────────┘  └──────────────┘            │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                  Viper.Graphics (EXISTING)                           │
│         vgfx.h, rt_graphics.h, rt_canvas, etc.                      │
│    Platform windowing, drawing primitives, input events             │
└─────────────────────────────────────────────────────────────────────┘
```

### File Structure

```
src/
├── lib/
│   └── gui/
│       ├── include/
│       │   ├── vg_event.h            # Event system API
│       │   ├── vg_font.h             # Font engine API
│       │   ├── vg_ide_widgets.h      # IDE-specific widget API
│       │   ├── vg_layout.h           # Layout system
│       │   ├── vg_theme.h            # Theming API
│       │   ├── vg_widget.h           # Widget base class
│       │   └── vg_widgets.h          # All widget APIs (consolidated)
│       ├── src/
│       │   ├── core/
│       │   │   ├── vg_event.c        # Event system
│       │   │   ├── vg_layout.c       # Layout engines
│       │   │   ├── vg_theme.c        # Theme system
│       │   │   └── vg_widget.c       # Widget base
│       │   ├── dialogs/
│       │   │   ├── vg_filedialog_native.h  # Native file dialog header
│       │   │   └── vg_filedialog_native.m  # macOS native file dialog
│       │   ├── font/
│       │   │   ├── vg_cache.c        # Glyph cache
│       │   │   ├── vg_canvas_integration.c # Canvas drawing integration
│       │   │   ├── vg_font.c         # Font loading
│       │   │   ├── vg_raster.c       # Glyph rasterizer
│       │   │   ├── vg_ttf.c          # TTF parser
│       │   │   └── vg_ttf_internal.h # TTF internal structures
│       │   └── widgets/
│       │       ├── vg_breadcrumb.c
│       │       ├── vg_button.c
│       │       ├── vg_checkbox.c
│       │       ├── vg_codeeditor.c
│       │       ├── vg_colorpalette.c
│       │       ├── vg_colorpicker.c
│       │       ├── vg_colorswatch.c
│       │       ├── vg_commandpalette.c
│       │       ├── vg_contextmenu.c
│       │       ├── vg_dialog.c
│       │       ├── vg_dropdown.c
│       │       ├── vg_filedialog.c
│       │       ├── vg_findreplacebar.c
│       │       ├── vg_image.c
│       │       ├── vg_label.c
│       │       ├── vg_listbox.c
│       │       ├── vg_menubar.c
│       │       ├── vg_minimap.c
│       │       ├── vg_notification.c
│       │       ├── vg_outputpane.c
│       │       ├── vg_progressbar.c
│       │       ├── vg_radiobutton.c
│       │       ├── vg_scrollview.c
│       │       ├── vg_slider.c
│       │       ├── vg_spinner.c
│       │       ├── vg_splitpane.c
│       │       ├── vg_statusbar.c
│       │       ├── vg_tabbar.c
│       │       ├── vg_textinput.c
│       │       ├── vg_toolbar.c
│       │       ├── vg_tooltip.c
│       │       └── vg_treeview.c
│       └── tests/
│           └── test_font.c
└── runtime/
    ├── rt_gui.h                      # Runtime declarations (main)
    ├── rt_gui_app.c                  # App/window runtime bridge
    ├── rt_gui_codeeditor.c           # CodeEditor runtime bridge
    ├── rt_gui_features.c             # Feature detection
    ├── rt_gui_internal.h             # Internal runtime helpers
    ├── rt_gui_menus.c                # Menu runtime bridge
    ├── rt_gui_system.c               # System integration
    ├── rt_gui_widgets.c              # Core widget runtime bridge
    └── rt_gui_widgets_complex.c      # Complex widget runtime bridge
```

---

## 2. Font Engine

The font engine provides TTF loading, glyph rasterization, and text rendering.

### 2.1 Core Structures

```c
// vg_font.h

//=============================================================================
// Font Handle
//=============================================================================

typedef struct vg_font vg_font_t;

//=============================================================================
// Glyph Information
//=============================================================================

typedef struct vg_glyph {
    uint32_t codepoint;      // Unicode codepoint
    int width;               // Bitmap width in pixels
    int height;              // Bitmap height in pixels
    int bearing_x;           // Horizontal bearing (left edge offset)
    int bearing_y;           // Vertical bearing (baseline to top)
    int advance;             // Horizontal advance width
    uint8_t* bitmap;         // 8-bit alpha coverage bitmap
} vg_glyph_t;

//=============================================================================
// Font Metrics
//=============================================================================

typedef struct vg_font_metrics {
    int ascent;              // Distance from baseline to top
    int descent;             // Distance from baseline to bottom (negative)
    int line_height;         // Recommended line spacing
    int units_per_em;        // Design units per em
} vg_font_metrics_t;

//=============================================================================
// Text Measurement
//=============================================================================

typedef struct vg_text_metrics {
    float width;             // Total width of text
    float height;            // Height (typically line_height)
    int glyph_count;         // Number of glyphs
} vg_text_metrics_t;
```

### 2.2 Font API

```c
// vg_font.h

//=============================================================================
// Font Loading
//=============================================================================

/// Load font from memory buffer
/// @param data     TTF file data
/// @param size     Size of data in bytes
/// @return         Font handle or NULL on failure
vg_font_t* vg_font_load(const uint8_t* data, size_t size);

/// Load font from file path
/// @param path     Path to TTF file
/// @return         Font handle or NULL on failure
vg_font_t* vg_font_load_file(const char* path);

/// Destroy font and free resources
/// @param font     Font handle
void vg_font_destroy(vg_font_t* font);

//=============================================================================
// Font Information
//=============================================================================

/// Get font metrics at given size
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param metrics  Output metrics structure
void vg_font_get_metrics(vg_font_t* font, float size, vg_font_metrics_t* metrics);

/// Get font family name
/// @param font     Font handle
/// @return         Font family name string
const char* vg_font_get_family(vg_font_t* font);

/// Check if font has a specific glyph
/// @param font     Font handle
/// @param codepoint Unicode codepoint
/// @return         true if glyph exists
bool vg_font_has_glyph(vg_font_t* font, uint32_t codepoint);

//=============================================================================
// Glyph Rasterization
//=============================================================================

/// Get rasterized glyph (cached)
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param codepoint Unicode codepoint
/// @return         Glyph data or NULL if not found
const vg_glyph_t* vg_font_get_glyph(vg_font_t* font, float size, uint32_t codepoint);

/// Get kerning adjustment between two glyphs
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param left     Left codepoint
/// @param right    Right codepoint
/// @return         Kerning adjustment in pixels
float vg_font_get_kerning(vg_font_t* font, float size, uint32_t left, uint32_t right);

//=============================================================================
// Text Measurement
//=============================================================================

/// Measure text string
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param text     UTF-8 text string
/// @param metrics  Output metrics
void vg_font_measure_text(vg_font_t* font, float size, const char* text,
                          vg_text_metrics_t* metrics);

/// Get character index at x position (for cursor placement)
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param text     UTF-8 text string
/// @param x        X position in pixels
/// @return         Character index (0-based) or -1 if past end
int vg_font_hit_test(vg_font_t* font, float size, const char* text, float x);

/// Get x position of character index
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param text     UTF-8 text string
/// @param index    Character index
/// @return         X position in pixels
float vg_font_get_cursor_x(vg_font_t* font, float size, const char* text, int index);

//=============================================================================
// Text Rendering
//=============================================================================

/// Draw text to canvas
/// @param canvas   Canvas to draw on
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param x        X position
/// @param y        Y position (baseline)
/// @param text     UTF-8 text string
/// @param color    Text color (0xAARRGGBB)
void vg_font_draw_text(rt_canvas_t* canvas, vg_font_t* font, float size,
                       float x, float y, const char* text, uint32_t color);
```

### 2.3 TTF Parser Internals

```c
// vg_ttf.c (internal structures)

//=============================================================================
// TTF Table Directory
//=============================================================================

typedef struct ttf_table {
    uint32_t tag;            // 4-byte table identifier
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} ttf_table_t;

//=============================================================================
// Required Tables
//=============================================================================

// 'head' - Font header
typedef struct ttf_head {
    uint16_t units_per_em;
    int16_t x_min, y_min, x_max, y_max;
    int16_t index_to_loc_format;  // 0=short, 1=long
} ttf_head_t;

// 'hhea' - Horizontal header
typedef struct ttf_hhea {
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    uint16_t num_h_metrics;
} ttf_hhea_t;

// 'maxp' - Maximum profile
typedef struct ttf_maxp {
    uint16_t num_glyphs;
} ttf_maxp_t;

// 'cmap' - Character to glyph mapping
typedef struct ttf_cmap {
    uint16_t format;         // We support format 4 (BMP) and 12 (full Unicode)
    // ... format-specific data
} ttf_cmap_t;

// 'glyf' - Glyph outlines
typedef struct ttf_glyph_outline {
    int16_t num_contours;    // -1 = composite glyph
    int16_t x_min, y_min, x_max, y_max;
    // Contour endpoints, flags, coordinates follow
} ttf_glyph_outline_t;

// 'hmtx' - Horizontal metrics
typedef struct ttf_hmtx_entry {
    uint16_t advance_width;
    int16_t left_side_bearing;
} ttf_hmtx_entry_t;

// 'kern' - Kerning (optional)
typedef struct ttf_kern_pair {
    uint16_t left;
    uint16_t right;
    int16_t value;
} ttf_kern_pair_t;

//=============================================================================
// Internal Font Structure
//=============================================================================

struct vg_font {
    // Raw data
    uint8_t* data;
    size_t data_size;
    bool owns_data;          // true if we should free data

    // Parsed tables
    ttf_head_t head;
    ttf_hhea_t hhea;
    ttf_maxp_t maxp;

    // Table offsets
    uint32_t cmap_offset;
    uint32_t glyf_offset;
    uint32_t loca_offset;
    uint32_t hmtx_offset;
    uint32_t kern_offset;

    // Glyph cache (hash map: (size << 20) | codepoint -> vg_glyph_t*)
    vg_glyph_cache_t* cache;

    // Font names
    char family_name[64];
    char style_name[64];
};
```

### 2.4 Rasterizer Algorithm

```c
// vg_raster.c

//=============================================================================
// Outline Point
//=============================================================================

typedef struct {
    float x, y;
    bool on_curve;           // true = line endpoint, false = control point
} outline_point_t;

//=============================================================================
// Rasterization Pipeline
//=============================================================================

// 1. Load glyph outline from 'glyf' table
// 2. Scale outline to target pixel size
// 3. Flatten curves to line segments
// 4. Apply scanline rasterization with coverage-based antialiasing

/// Rasterize a glyph outline to bitmap
/// @param font     Font handle
/// @param glyph_id Glyph index (from cmap)
/// @param size     Target size in pixels
/// @return         Rasterized glyph (caller owns)
vg_glyph_t* vg_rasterize_glyph(vg_font_t* font, uint16_t glyph_id, float size);

//=============================================================================
// Curve Flattening
//=============================================================================

/// Flatten quadratic Bezier curve to line segments
/// @param p0       Start point
/// @param p1       Control point
/// @param p2       End point
/// @param tolerance Maximum error tolerance
/// @param out      Output point array
/// @param count    Output point count
void flatten_quadratic(outline_point_t p0, outline_point_t p1, outline_point_t p2,
                       float tolerance, outline_point_t* out, int* count);

//=============================================================================
// Scanline Rasterization
//=============================================================================

/// Rasterize polygon using even-odd fill rule with antialiasing
/// @param points   Array of polygon points
/// @param count    Number of points
/// @param width    Output bitmap width
/// @param height   Output bitmap height
/// @param bitmap   Output 8-bit alpha bitmap
void scanline_rasterize(outline_point_t* points, int count,
                        int width, int height, uint8_t* bitmap);
```

### 2.5 Glyph Cache

```c
// vg_cache.c

//=============================================================================
// Cache Configuration
//=============================================================================

#define VG_CACHE_INITIAL_SIZE   256
#define VG_CACHE_MAX_SIZE       4096
#define VG_CACHE_LOAD_FACTOR    0.75f

//=============================================================================
// Cache Entry
//=============================================================================

typedef struct vg_cache_entry {
    uint64_t key;            // (size_bits << 32) | codepoint
    vg_glyph_t glyph;
    struct vg_cache_entry* next;  // For collision chaining
} vg_cache_entry_t;

//=============================================================================
// Cache Structure
//=============================================================================

typedef struct vg_glyph_cache {
    vg_cache_entry_t** buckets;
    size_t bucket_count;
    size_t entry_count;
    size_t memory_used;      // Track bitmap memory usage
    size_t max_memory;       // Evict if exceeded (default 32MB)
} vg_glyph_cache_t;

//=============================================================================
// Cache API
//=============================================================================

vg_glyph_cache_t* vg_cache_create(void);
void vg_cache_destroy(vg_glyph_cache_t* cache);

const vg_glyph_t* vg_cache_get(vg_glyph_cache_t* cache, float size, uint32_t codepoint);
void vg_cache_put(vg_glyph_cache_t* cache, float size, uint32_t codepoint,
                  const vg_glyph_t* glyph);
void vg_cache_clear(vg_glyph_cache_t* cache);
```

---

## 3. Widget Framework Core

### 3.1 Widget Base Structure

```c
// vg_widget.h

//=============================================================================
// Forward Declarations
//=============================================================================

typedef struct vg_widget vg_widget_t;
typedef struct vg_event vg_event_t;

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
    void (*paint)(vg_widget_t* self, rt_canvas_t* canvas);

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

    // User data
    void* user_data;

    // Callbacks
    void (*on_click)(vg_widget_t* self);
    void (*on_change)(vg_widget_t* self);
    void (*on_submit)(vg_widget_t* self);
};
```

### 3.2 Widget API

```c
// vg_widget.h

//=============================================================================
// Widget Creation/Destruction
//=============================================================================

/// Create a widget of specified type
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

/// Remove all children
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
void vg_widget_get_bounds(vg_widget_t* widget, float* x, float* y,
                          float* width, float* height);

/// Get bounding rectangle in screen coordinates
void vg_widget_get_screen_bounds(vg_widget_t* widget, float* x, float* y,
                                  float* width, float* height);

//=============================================================================
// Layout Parameters
//=============================================================================

/// Set flex grow factor
void vg_widget_set_flex(vg_widget_t* widget, float flex);

/// Set margin (all sides)
void vg_widget_set_margin(vg_widget_t* widget, float margin);

/// Set margin (individual sides)
void vg_widget_set_margins(vg_widget_t* widget, float left, float top,
                           float right, float bottom);

/// Set padding (all sides)
void vg_widget_set_padding(vg_widget_t* widget, float padding);

/// Set padding (individual sides)
void vg_widget_set_paddings(vg_widget_t* widget, float left, float top,
                            float right, float bottom);

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

//=============================================================================
// Layout & Rendering
//=============================================================================

/// Trigger layout pass on widget tree
void vg_widget_layout(vg_widget_t* root, float available_width, float available_height);

/// Paint widget tree to canvas
void vg_widget_paint(vg_widget_t* root, rt_canvas_t* canvas);

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
```

### 3.3 Event System

```c
// vg_event.h

//=============================================================================
// Event Types
//=============================================================================

typedef enum vg_event_type {
    // Mouse events
    VG_EVENT_MOUSE_MOVE,
    VG_EVENT_MOUSE_DOWN,
    VG_EVENT_MOUSE_UP,
    VG_EVENT_MOUSE_ENTER,
    VG_EVENT_MOUSE_LEAVE,
    VG_EVENT_MOUSE_WHEEL,
    VG_EVENT_CLICK,
    VG_EVENT_DOUBLE_CLICK,

    // Keyboard events
    VG_EVENT_KEY_DOWN,
    VG_EVENT_KEY_UP,
    VG_EVENT_KEY_CHAR,       // Character input (after key translation)

    // Focus events
    VG_EVENT_FOCUS_IN,
    VG_EVENT_FOCUS_OUT,

    // Widget-specific events
    VG_EVENT_VALUE_CHANGED,  // Slider, checkbox, etc.
    VG_EVENT_TEXT_CHANGED,   // Text input
    VG_EVENT_SELECTION_CHANGED,
    VG_EVENT_SUBMIT,         // Enter pressed in text input
    VG_EVENT_CANCEL,         // Escape pressed

    // Window events (bubbled to root)
    VG_EVENT_RESIZE,
    VG_EVENT_CLOSE,
} vg_event_type_t;

//=============================================================================
// Mouse Buttons
//=============================================================================

typedef enum vg_mouse_button {
    VG_MOUSE_LEFT   = 0,
    VG_MOUSE_RIGHT  = 1,
    VG_MOUSE_MIDDLE = 2,
} vg_mouse_button_t;

//=============================================================================
// Modifier Keys
//=============================================================================

typedef enum vg_modifiers {
    VG_MOD_NONE    = 0,
    VG_MOD_SHIFT   = 1 << 0,
    VG_MOD_CTRL    = 1 << 1,
    VG_MOD_ALT     = 1 << 2,
    VG_MOD_SUPER   = 1 << 3,  // Cmd on Mac, Win on Windows
} vg_modifiers_t;

//=============================================================================
// Event Structure
//=============================================================================

struct vg_event {
    vg_event_type_t type;
    vg_widget_t* target;     // Widget that generated the event
    bool handled;            // Set to true to stop propagation
    uint32_t modifiers;      // Modifier key state

    union {
        // Mouse events
        struct {
            float x, y;              // Position relative to target
            float screen_x, screen_y; // Screen coordinates
            vg_mouse_button_t button;
            int click_count;
        } mouse;

        // Mouse wheel
        struct {
            float delta_x;
            float delta_y;
        } wheel;

        // Keyboard events
        struct {
            int key;                 // Key code (VG_KEY_*)
            uint32_t codepoint;      // Unicode codepoint (for KEY_CHAR)
            bool repeat;             // Key repeat
        } key;

        // Value changed
        struct {
            int int_value;
            float float_value;
            bool bool_value;
        } value;

        // Resize
        struct {
            int width;
            int height;
        } resize;
    };
};

//=============================================================================
// Event Dispatch
//=============================================================================

/// Dispatch event to widget tree (with bubbling)
bool vg_event_dispatch(vg_widget_t* root, vg_event_t* event);

/// Send event directly to widget (no bubbling)
bool vg_event_send(vg_widget_t* widget, vg_event_t* event);

/// Translate platform event to GUI event
void vg_event_from_platform(vgfx_event_t* platform, vg_event_t* gui);
```

---

## 4. Layout System

### 4.1 Layout Types

```c
// vg_layout.h

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
```

### 4.2 Layout Container API

```c
// vg_layout.h

//=============================================================================
// VBox Layout
//=============================================================================

typedef struct vg_vbox_layout {
    float spacing;
    vg_align_t align;        // Cross-axis alignment
    vg_justify_t justify;    // Main-axis distribution
} vg_vbox_layout_t;

/// Create vertical box container
vg_widget_t* vg_vbox_create(float spacing);

/// Set VBox properties
void vg_vbox_set_spacing(vg_widget_t* vbox, float spacing);
void vg_vbox_set_align(vg_widget_t* vbox, vg_align_t align);
void vg_vbox_set_justify(vg_widget_t* vbox, vg_justify_t justify);

//=============================================================================
// HBox Layout
//=============================================================================

typedef struct vg_hbox_layout {
    float spacing;
    vg_align_t align;
    vg_justify_t justify;
} vg_hbox_layout_t;

/// Create horizontal box container
vg_widget_t* vg_hbox_create(float spacing);

/// Set HBox properties
void vg_hbox_set_spacing(vg_widget_t* hbox, float spacing);
void vg_hbox_set_align(vg_widget_t* hbox, vg_align_t align);
void vg_hbox_set_justify(vg_widget_t* hbox, vg_justify_t justify);

//=============================================================================
// Flex Layout
//=============================================================================

typedef struct vg_flex_layout {
    vg_direction_t direction;
    vg_align_t align_items;
    vg_justify_t justify_content;
    vg_align_t align_content;
    float gap;
    bool wrap;
} vg_flex_layout_t;

/// Create flex container
vg_widget_t* vg_flex_create(void);

/// Set Flex properties
void vg_flex_set_direction(vg_widget_t* flex, vg_direction_t direction);
void vg_flex_set_align_items(vg_widget_t* flex, vg_align_t align);
void vg_flex_set_justify_content(vg_widget_t* flex, vg_justify_t justify);
void vg_flex_set_gap(vg_widget_t* flex, float gap);
void vg_flex_set_wrap(vg_widget_t* flex, bool wrap);

//=============================================================================
// Grid Layout
//=============================================================================

typedef struct vg_grid_layout {
    int columns;
    int rows;
    float column_gap;
    float row_gap;
    float* column_widths;    // NULL = equal width
    float* row_heights;      // NULL = equal height
} vg_grid_layout_t;

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
// Dock Layout
//=============================================================================

/// Create dock container
vg_widget_t* vg_dock_create(void);

/// Add docked child
void vg_dock_add(vg_widget_t* dock, vg_widget_t* child, vg_dock_t position);
```

---

## 5. Core Widgets

### 5.1 Label

```c
// vg_label.h

//=============================================================================
// Label Widget
//=============================================================================

typedef struct vg_label {
    vg_widget_t base;
    char* text;
    vg_font_t* font;
    float font_size;
    uint32_t color;
    vg_align_t align;        // Horizontal alignment
    vg_align_t valign;       // Vertical alignment
    bool wrap;               // Word wrap
    int max_lines;           // 0 = unlimited
    bool selectable;         // Can text be selected
} vg_label_t;

//=============================================================================
// Label API
//=============================================================================

/// Create label with text
vg_widget_t* vg_label_create(const char* text);

/// Set label text
void vg_label_set_text(vg_widget_t* label, const char* text);
const char* vg_label_get_text(vg_widget_t* label);

/// Set label font
void vg_label_set_font(vg_widget_t* label, vg_font_t* font, float size);

/// Set text color
void vg_label_set_color(vg_widget_t* label, uint32_t color);

/// Set alignment
void vg_label_set_align(vg_widget_t* label, vg_align_t halign, vg_align_t valign);

/// Set word wrap
void vg_label_set_wrap(vg_widget_t* label, bool wrap);
```

### 5.2 Button

```c
// vg_button.h

//=============================================================================
// Button Style
//=============================================================================

typedef enum vg_button_style {
    VG_BUTTON_DEFAULT,       // Standard button
    VG_BUTTON_PRIMARY,       // Emphasized button
    VG_BUTTON_SECONDARY,     // Less emphasis
    VG_BUTTON_DANGER,        // Destructive action
    VG_BUTTON_LINK,          // Text-only, like hyperlink
    VG_BUTTON_ICON,          // Icon only, no text
} vg_button_style_t;

//=============================================================================
// Button Widget
//=============================================================================

typedef struct vg_button {
    vg_widget_t base;
    char* text;
    char* icon;              // Icon name or NULL
    vg_button_style_t style;
    vg_font_t* font;
    float font_size;
    bool toggle;             // Toggle button mode
    bool toggled;            // Current toggle state
} vg_button_t;

//=============================================================================
// Button API
//=============================================================================

/// Create button with text
vg_widget_t* vg_button_create(const char* text);

/// Create icon button
vg_widget_t* vg_button_create_icon(const char* icon);

/// Set button text
void vg_button_set_text(vg_widget_t* button, const char* text);
const char* vg_button_get_text(vg_widget_t* button);

/// Set button icon
void vg_button_set_icon(vg_widget_t* button, const char* icon);

/// Set button style
void vg_button_set_style(vg_widget_t* button, vg_button_style_t style);

/// Set click callback
void vg_button_on_click(vg_widget_t* button, void (*callback)(vg_widget_t*));

/// Toggle button mode
void vg_button_set_toggle(vg_widget_t* button, bool toggle);
bool vg_button_is_toggled(vg_widget_t* button);
void vg_button_set_toggled(vg_widget_t* button, bool toggled);
```

### 5.3 TextInput

```c
// vg_textinput.h

//=============================================================================
// TextInput Widget
//=============================================================================

typedef struct vg_textinput {
    vg_widget_t base;
    char* text;
    char* placeholder;
    vg_font_t* font;
    float font_size;
    int cursor_pos;          // Character index
    int selection_start;     // Selection anchor
    int selection_end;       // Selection end
    int scroll_offset;       // Horizontal scroll in pixels
    int max_length;          // 0 = unlimited
    bool password;           // Hide text with dots
    bool readonly;
    bool multiline;

    // Undo/redo
    struct vg_undo_stack* undo_stack;
} vg_textinput_t;

//=============================================================================
// TextInput API
//=============================================================================

/// Create single-line text input
vg_widget_t* vg_textinput_create(void);

/// Create multi-line text area
vg_widget_t* vg_textarea_create(void);

/// Get/set text
void vg_textinput_set_text(vg_widget_t* input, const char* text);
const char* vg_textinput_get_text(vg_widget_t* input);

/// Set placeholder text
void vg_textinput_set_placeholder(vg_widget_t* input, const char* placeholder);

/// Set password mode
void vg_textinput_set_password(vg_widget_t* input, bool password);

/// Set readonly
void vg_textinput_set_readonly(vg_widget_t* input, bool readonly);

/// Set max length
void vg_textinput_set_max_length(vg_widget_t* input, int max_length);

/// Selection
void vg_textinput_select_all(vg_widget_t* input);
void vg_textinput_select_range(vg_widget_t* input, int start, int end);
void vg_textinput_get_selection(vg_widget_t* input, int* start, int* end);
const char* vg_textinput_get_selected_text(vg_widget_t* input);

/// Cursor
void vg_textinput_set_cursor(vg_widget_t* input, int pos);
int vg_textinput_get_cursor(vg_widget_t* input);

/// Clipboard
void vg_textinput_cut(vg_widget_t* input);
void vg_textinput_copy(vg_widget_t* input);
void vg_textinput_paste(vg_widget_t* input);

/// Undo/Redo
void vg_textinput_undo(vg_widget_t* input);
void vg_textinput_redo(vg_widget_t* input);

/// Callbacks
void vg_textinput_on_change(vg_widget_t* input, void (*callback)(vg_widget_t*));
void vg_textinput_on_submit(vg_widget_t* input, void (*callback)(vg_widget_t*));
```

### 5.4 Checkbox

```c
// vg_checkbox.h

//=============================================================================
// Checkbox Widget
//=============================================================================

typedef struct vg_checkbox {
    vg_widget_t base;
    char* text;
    bool checked;
    bool indeterminate;      // Third state (partially checked)
    vg_font_t* font;
    float font_size;
} vg_checkbox_t;

//=============================================================================
// Checkbox API
//=============================================================================

/// Create checkbox
vg_widget_t* vg_checkbox_create(const char* text);

/// Get/set checked state
void vg_checkbox_set_checked(vg_widget_t* checkbox, bool checked);
bool vg_checkbox_is_checked(vg_widget_t* checkbox);

/// Indeterminate state
void vg_checkbox_set_indeterminate(vg_widget_t* checkbox, bool indeterminate);
bool vg_checkbox_is_indeterminate(vg_widget_t* checkbox);

/// Toggle
void vg_checkbox_toggle(vg_widget_t* checkbox);

/// Callback
void vg_checkbox_on_change(vg_widget_t* checkbox, void (*callback)(vg_widget_t*));
```

### 5.5 ScrollView

```c
// vg_scrollview.h

//=============================================================================
// ScrollBar Policy
//=============================================================================

typedef enum vg_scrollbar_policy {
    VG_SCROLLBAR_AUTO,       // Show when needed
    VG_SCROLLBAR_ALWAYS,     // Always show
    VG_SCROLLBAR_NEVER,      // Never show
} vg_scrollbar_policy_t;

//=============================================================================
// ScrollView Widget
//=============================================================================

typedef struct vg_scrollview {
    vg_widget_t base;
    float scroll_x;
    float scroll_y;
    float content_width;
    float content_height;
    vg_scrollbar_policy_t h_policy;
    vg_scrollbar_policy_t v_policy;
    float scrollbar_width;
    bool smooth_scroll;
} vg_scrollview_t;

//=============================================================================
// ScrollView API
//=============================================================================

/// Create scroll view
vg_widget_t* vg_scrollview_create(void);

/// Set content widget
void vg_scrollview_set_content(vg_widget_t* sv, vg_widget_t* content);

/// Get/set scroll position
void vg_scrollview_set_scroll(vg_widget_t* sv, float x, float y);
void vg_scrollview_get_scroll(vg_widget_t* sv, float* x, float* y);

/// Scroll to make point visible
void vg_scrollview_scroll_to(vg_widget_t* sv, float x, float y);

/// Scroll to make widget visible
void vg_scrollview_scroll_to_widget(vg_widget_t* sv, vg_widget_t* widget);

/// Scrollbar policies
void vg_scrollview_set_h_policy(vg_widget_t* sv, vg_scrollbar_policy_t policy);
void vg_scrollview_set_v_policy(vg_widget_t* sv, vg_scrollbar_policy_t policy);

/// Callbacks
void vg_scrollview_on_scroll(vg_widget_t* sv, void (*callback)(vg_widget_t*));
```

---

## 6. IDE Widgets

### 6.1 TreeView

```c
// vg_treeview.h

//=============================================================================
// Tree Node
//=============================================================================

typedef struct vg_tree_node {
    uint32_t id;
    char* text;
    char* icon;
    void* user_data;
    bool expanded;
    bool selected;
    struct vg_tree_node* parent;
    struct vg_tree_node* first_child;
    struct vg_tree_node* next_sibling;
    int child_count;
} vg_tree_node_t;

//=============================================================================
// TreeView Widget
//=============================================================================

typedef struct vg_treeview {
    vg_widget_t base;
    vg_tree_node_t* root;
    vg_tree_node_t* selected;
    vg_font_t* font;
    float font_size;
    float indent;            // Indent per level
    float row_height;
    bool show_root;          // Show root node or just children
    bool multi_select;
} vg_treeview_t;

//=============================================================================
// TreeView API
//=============================================================================

/// Create tree view
vg_widget_t* vg_treeview_create(void);

/// Node management
vg_tree_node_t* vg_treeview_add_node(vg_widget_t* tv, vg_tree_node_t* parent,
                                      const char* text, const char* icon);
void vg_treeview_remove_node(vg_widget_t* tv, vg_tree_node_t* node);
void vg_treeview_clear(vg_widget_t* tv);

/// Node properties
void vg_treenode_set_text(vg_tree_node_t* node, const char* text);
void vg_treenode_set_icon(vg_tree_node_t* node, const char* icon);
void vg_treenode_set_expanded(vg_tree_node_t* node, bool expanded);

/// Selection
void vg_treeview_select(vg_widget_t* tv, vg_tree_node_t* node);
vg_tree_node_t* vg_treeview_get_selected(vg_widget_t* tv);

/// Expand/collapse
void vg_treeview_expand(vg_widget_t* tv, vg_tree_node_t* node);
void vg_treeview_collapse(vg_widget_t* tv, vg_tree_node_t* node);
void vg_treeview_expand_all(vg_widget_t* tv);
void vg_treeview_collapse_all(vg_widget_t* tv);

/// Callbacks
void vg_treeview_on_select(vg_widget_t* tv, void (*callback)(vg_widget_t*, vg_tree_node_t*));
void vg_treeview_on_expand(vg_widget_t* tv, void (*callback)(vg_widget_t*, vg_tree_node_t*));
void vg_treeview_on_activate(vg_widget_t* tv, void (*callback)(vg_widget_t*, vg_tree_node_t*));
```

### 6.2 TabBar

```c
// vg_tabbar.h

//=============================================================================
// Tab
//=============================================================================

typedef struct vg_tab {
    uint32_t id;
    char* text;
    char* icon;
    char* tooltip;
    bool closable;
    bool modified;           // Show modified indicator
    void* user_data;
} vg_tab_t;

//=============================================================================
// TabBar Widget
//=============================================================================

typedef struct vg_tabbar {
    vg_widget_t base;
    vg_tab_t** tabs;
    int tab_count;
    int tab_capacity;
    int selected_index;
    float tab_min_width;
    float tab_max_width;
    bool scrollable;         // Scroll if too many tabs
    bool reorderable;        // Allow drag reorder
} vg_tabbar_t;

//=============================================================================
// TabBar API
//=============================================================================

/// Create tab bar
vg_widget_t* vg_tabbar_create(void);

/// Tab management
int vg_tabbar_add_tab(vg_widget_t* tb, const char* text, const char* icon);
void vg_tabbar_remove_tab(vg_widget_t* tb, int index);
void vg_tabbar_clear(vg_widget_t* tb);
int vg_tabbar_get_tab_count(vg_widget_t* tb);

/// Tab properties
void vg_tabbar_set_tab_text(vg_widget_t* tb, int index, const char* text);
void vg_tabbar_set_tab_icon(vg_widget_t* tb, int index, const char* icon);
void vg_tabbar_set_tab_modified(vg_widget_t* tb, int index, bool modified);
void vg_tabbar_set_tab_closable(vg_widget_t* tb, int index, bool closable);

/// Selection
void vg_tabbar_select(vg_widget_t* tb, int index);
int vg_tabbar_get_selected(vg_widget_t* tb);

/// Callbacks
void vg_tabbar_on_select(vg_widget_t* tb, void (*callback)(vg_widget_t*, int));
void vg_tabbar_on_close(vg_widget_t* tb, void (*callback)(vg_widget_t*, int));
void vg_tabbar_on_reorder(vg_widget_t* tb, void (*callback)(vg_widget_t*, int, int));
```

### 6.3 SplitPane

```c
// vg_splitpane.h

//=============================================================================
// Split Orientation
//=============================================================================

typedef enum vg_split_orientation {
    VG_SPLIT_HORIZONTAL,     // Side by side
    VG_SPLIT_VERTICAL,       // Top and bottom
} vg_split_orientation_t;

//=============================================================================
// SplitPane Widget
//=============================================================================

typedef struct vg_splitpane {
    vg_widget_t base;
    vg_split_orientation_t orientation;
    float split_pos;         // Position in pixels or ratio
    bool use_ratio;          // If true, split_pos is 0.0-1.0 ratio
    float min_first;         // Minimum size for first pane
    float min_second;        // Minimum size for second pane
    float divider_width;     // Width of divider handle
    bool collapsible_first;  // Can first pane be collapsed
    bool collapsible_second; // Can second pane be collapsed
    bool first_collapsed;
    bool second_collapsed;
} vg_splitpane_t;

//=============================================================================
// SplitPane API
//=============================================================================

/// Create split pane
vg_widget_t* vg_splitpane_create(vg_split_orientation_t orientation);

/// Set pane content (index 0 or 1)
void vg_splitpane_set_pane(vg_widget_t* sp, int index, vg_widget_t* content);

/// Get/set split position
void vg_splitpane_set_position(vg_widget_t* sp, float pos);
float vg_splitpane_get_position(vg_widget_t* sp);

/// Set as ratio (0.0-1.0)
void vg_splitpane_set_ratio(vg_widget_t* sp, float ratio);

/// Minimum sizes
void vg_splitpane_set_min_sizes(vg_widget_t* sp, float first, float second);

/// Collapse/expand
void vg_splitpane_collapse_first(vg_widget_t* sp);
void vg_splitpane_collapse_second(vg_widget_t* sp);
void vg_splitpane_expand(vg_widget_t* sp);

/// Callbacks
void vg_splitpane_on_resize(vg_widget_t* sp, void (*callback)(vg_widget_t*));
```

### 6.4 MenuBar and Menu

```c
// vg_menu.h

//=============================================================================
// Menu Item Type
//=============================================================================

typedef enum vg_menuitem_type {
    VG_MENUITEM_NORMAL,
    VG_MENUITEM_CHECKBOX,
    VG_MENUITEM_RADIO,
    VG_MENUITEM_SEPARATOR,
    VG_MENUITEM_SUBMENU,
} vg_menuitem_type_t;

//=============================================================================
// Menu Item
//=============================================================================

typedef struct vg_menuitem {
    vg_menuitem_type_t type;
    char* text;
    char* icon;
    char* shortcut;          // e.g., "Ctrl+S"
    int shortcut_key;        // Key code
    int shortcut_mods;       // Modifier flags
    bool enabled;
    bool checked;
    char* radio_group;       // For radio items
    struct vg_menu* submenu;
    void (*action)(struct vg_menuitem*);
    void* user_data;
} vg_menuitem_t;

//=============================================================================
// Menu
//=============================================================================

typedef struct vg_menu {
    char* title;
    vg_menuitem_t** items;
    int item_count;
    int item_capacity;
} vg_menu_t;

//=============================================================================
// MenuBar Widget
//=============================================================================

typedef struct vg_menubar {
    vg_widget_t base;
    vg_menu_t** menus;
    int menu_count;
    int menu_capacity;
    int active_menu;         // Currently open menu (-1 if none)
    vg_font_t* font;
    float font_size;
} vg_menubar_t;

//=============================================================================
// MenuBar API
//=============================================================================

/// Create menu bar
vg_widget_t* vg_menubar_create(void);

/// Add menu
vg_menu_t* vg_menubar_add_menu(vg_widget_t* mb, const char* title);

/// Menu item management
vg_menuitem_t* vg_menu_add_item(vg_menu_t* menu, const char* text, const char* shortcut);
vg_menuitem_t* vg_menu_add_checkbox(vg_menu_t* menu, const char* text, bool checked);
vg_menuitem_t* vg_menu_add_separator(vg_menu_t* menu);
vg_menu_t* vg_menu_add_submenu(vg_menu_t* menu, const char* text);

/// Item properties
void vg_menuitem_set_enabled(vg_menuitem_t* item, bool enabled);
void vg_menuitem_set_checked(vg_menuitem_t* item, bool checked);
void vg_menuitem_set_action(vg_menuitem_t* item, void (*action)(vg_menuitem_t*));

/// Show context menu at position
void vg_menu_show_context(vg_menu_t* menu, float x, float y);
```

### 6.5 CodeEditor

```c
// vg_codeeditor.h

//=============================================================================
// Syntax Token Type
//=============================================================================

typedef enum vg_token_type {
    VG_TOKEN_DEFAULT,
    VG_TOKEN_KEYWORD,
    VG_TOKEN_TYPE,
    VG_TOKEN_FUNCTION,
    VG_TOKEN_VARIABLE,
    VG_TOKEN_STRING,
    VG_TOKEN_NUMBER,
    VG_TOKEN_COMMENT,
    VG_TOKEN_OPERATOR,
    VG_TOKEN_BRACKET,
    VG_TOKEN_ERROR,
} vg_token_type_t;

//=============================================================================
// Syntax Token
//=============================================================================

typedef struct vg_token {
    int start;               // Start offset in line
    int length;              // Token length
    vg_token_type_t type;
} vg_token_t;

//=============================================================================
// Syntax Highlighter Interface
//=============================================================================

typedef struct vg_highlighter {
    const char* language;
    void (*tokenize_line)(const char* line, vg_token_t* tokens, int* count, void* state);
    void* (*create_state)(void);
    void (*destroy_state)(void* state);
    void (*update_state)(void* state, const char* line);
} vg_highlighter_t;

//=============================================================================
// Code Editor Widget
//=============================================================================

typedef struct vg_codeeditor {
    vg_widget_t base;

    // Text content (using rope or gap buffer)
    struct vg_text_buffer* buffer;

    // Display
    vg_font_t* font;
    float font_size;
    float line_height;
    float char_width;        // For monospace

    // View state
    int scroll_line;         // First visible line
    int scroll_x;            // Horizontal scroll
    float gutter_width;      // Line number gutter

    // Cursor
    int cursor_line;
    int cursor_column;
    bool cursor_visible;     // For blinking
    float cursor_blink_time;

    // Selection
    int selection_start_line;
    int selection_start_col;
    int selection_end_line;
    int selection_end_col;
    bool has_selection;

    // Features
    bool line_numbers;
    bool word_wrap;
    bool auto_indent;
    int tab_width;
    bool use_spaces;         // Insert spaces for tab

    // Syntax highlighting
    vg_highlighter_t* highlighter;
    void* highlight_state;

    // Undo/redo
    struct vg_undo_stack* undo_stack;

    // Markers (breakpoints, errors, etc.)
    struct vg_marker* markers;
    int marker_count;

    // Minimap (optional)
    bool show_minimap;
    float minimap_width;
} vg_codeeditor_t;

//=============================================================================
// CodeEditor API
//=============================================================================

/// Create code editor
vg_widget_t* vg_codeeditor_create(void);

//-----------------------------------------------------------------------------
// Content
//-----------------------------------------------------------------------------

/// Set text content
void vg_codeeditor_set_text(vg_widget_t* ed, const char* text);

/// Get text content (caller must free)
char* vg_codeeditor_get_text(vg_widget_t* ed);

/// Get line count
int vg_codeeditor_get_line_count(vg_widget_t* ed);

/// Get specific line (caller must free)
char* vg_codeeditor_get_line(vg_widget_t* ed, int line);

/// Insert text at cursor
void vg_codeeditor_insert(vg_widget_t* ed, const char* text);

/// Delete selection or character
void vg_codeeditor_delete(vg_widget_t* ed);

//-----------------------------------------------------------------------------
// Cursor & Selection
//-----------------------------------------------------------------------------

/// Set cursor position
void vg_codeeditor_set_cursor(vg_widget_t* ed, int line, int column);

/// Get cursor position
void vg_codeeditor_get_cursor(vg_widget_t* ed, int* line, int* column);

/// Set selection range
void vg_codeeditor_set_selection(vg_widget_t* ed,
                                  int start_line, int start_col,
                                  int end_line, int end_col);

/// Get selection range
void vg_codeeditor_get_selection(vg_widget_t* ed,
                                  int* start_line, int* start_col,
                                  int* end_line, int* end_col);

/// Select all
void vg_codeeditor_select_all(vg_widget_t* ed);

/// Select word at cursor
void vg_codeeditor_select_word(vg_widget_t* ed);

/// Select line at cursor
void vg_codeeditor_select_line(vg_widget_t* ed);

/// Get selected text (caller must free)
char* vg_codeeditor_get_selected_text(vg_widget_t* ed);

//-----------------------------------------------------------------------------
// Navigation
//-----------------------------------------------------------------------------

/// Go to line
void vg_codeeditor_goto_line(vg_widget_t* ed, int line);

/// Scroll to make line visible
void vg_codeeditor_scroll_to_line(vg_widget_t* ed, int line);

/// Find text
int vg_codeeditor_find(vg_widget_t* ed, const char* text,
                        bool case_sensitive, bool whole_word, bool regex);

/// Find next occurrence
bool vg_codeeditor_find_next(vg_widget_t* ed);

/// Find previous occurrence
bool vg_codeeditor_find_prev(vg_widget_t* ed);

/// Replace current match
void vg_codeeditor_replace(vg_widget_t* ed, const char* replacement);

/// Replace all matches
int vg_codeeditor_replace_all(vg_widget_t* ed, const char* find, const char* replace);

//-----------------------------------------------------------------------------
// Editing
//-----------------------------------------------------------------------------

/// Undo
void vg_codeeditor_undo(vg_widget_t* ed);

/// Redo
void vg_codeeditor_redo(vg_widget_t* ed);

/// Cut selection to clipboard
void vg_codeeditor_cut(vg_widget_t* ed);

/// Copy selection to clipboard
void vg_codeeditor_copy(vg_widget_t* ed);

/// Paste from clipboard
void vg_codeeditor_paste(vg_widget_t* ed);

/// Indent selection
void vg_codeeditor_indent(vg_widget_t* ed);

/// Outdent selection
void vg_codeeditor_outdent(vg_widget_t* ed);

/// Comment/uncomment selection
void vg_codeeditor_toggle_comment(vg_widget_t* ed);

//-----------------------------------------------------------------------------
// Features
//-----------------------------------------------------------------------------

/// Set syntax highlighter
void vg_codeeditor_set_highlighter(vg_widget_t* ed, vg_highlighter_t* highlighter);

/// Show/hide line numbers
void vg_codeeditor_set_line_numbers(vg_widget_t* ed, bool show);

/// Set tab width
void vg_codeeditor_set_tab_width(vg_widget_t* ed, int width);

/// Set use spaces for tabs
void vg_codeeditor_set_use_spaces(vg_widget_t* ed, bool use_spaces);

/// Set word wrap
void vg_codeeditor_set_word_wrap(vg_widget_t* ed, bool wrap);

//-----------------------------------------------------------------------------
// Markers
//-----------------------------------------------------------------------------

/// Add marker (breakpoint, error, etc.)
int vg_codeeditor_add_marker(vg_widget_t* ed, int line, int type, const char* tooltip);

/// Remove marker
void vg_codeeditor_remove_marker(vg_widget_t* ed, int marker_id);

/// Clear all markers
void vg_codeeditor_clear_markers(vg_widget_t* ed);

//-----------------------------------------------------------------------------
// Callbacks
//-----------------------------------------------------------------------------

void vg_codeeditor_on_change(vg_widget_t* ed, void (*callback)(vg_widget_t*));
void vg_codeeditor_on_cursor_change(vg_widget_t* ed, void (*callback)(vg_widget_t*));
void vg_codeeditor_on_selection_change(vg_widget_t* ed, void (*callback)(vg_widget_t*));
```

---

## 7. Theming System

### 7.1 Theme Structure

```c
// vg_theme.h

//=============================================================================
// Color Definitions
//=============================================================================

typedef struct vg_color_scheme {
    // Background colors
    uint32_t bg_primary;
    uint32_t bg_secondary;
    uint32_t bg_tertiary;
    uint32_t bg_hover;
    uint32_t bg_active;
    uint32_t bg_selected;
    uint32_t bg_disabled;

    // Foreground (text) colors
    uint32_t fg_primary;
    uint32_t fg_secondary;
    uint32_t fg_tertiary;
    uint32_t fg_disabled;
    uint32_t fg_placeholder;
    uint32_t fg_link;

    // Accent colors
    uint32_t accent_primary;
    uint32_t accent_secondary;
    uint32_t accent_danger;
    uint32_t accent_warning;
    uint32_t accent_success;
    uint32_t accent_info;

    // Border colors
    uint32_t border_primary;
    uint32_t border_secondary;
    uint32_t border_focus;

    // Syntax highlighting (for code editor)
    uint32_t syntax_keyword;
    uint32_t syntax_type;
    uint32_t syntax_function;
    uint32_t syntax_variable;
    uint32_t syntax_string;
    uint32_t syntax_number;
    uint32_t syntax_comment;
    uint32_t syntax_operator;
    uint32_t syntax_error;
} vg_color_scheme_t;

//=============================================================================
// Typography
//=============================================================================

typedef struct vg_typography {
    vg_font_t* font_regular;
    vg_font_t* font_bold;
    vg_font_t* font_mono;

    float size_small;        // e.g., 11px
    float size_normal;       // e.g., 13px
    float size_large;        // e.g., 16px
    float size_heading;      // e.g., 20px

    float line_height;       // e.g., 1.4
} vg_typography_t;

//=============================================================================
// Spacing
//=============================================================================

typedef struct vg_spacing {
    float xs;                // Extra small (2px)
    float sm;                // Small (4px)
    float md;                // Medium (8px)
    float lg;                // Large (16px)
    float xl;                // Extra large (24px)
} vg_spacing_t;

//=============================================================================
// Widget Styles
//=============================================================================

typedef struct vg_button_theme {
    float height;
    float padding_h;
    float border_radius;
    float border_width;
} vg_button_theme_t;

typedef struct vg_input_theme {
    float height;
    float padding_h;
    float border_radius;
    float border_width;
} vg_input_theme_t;

typedef struct vg_scrollbar_theme {
    float width;
    float min_thumb_size;
    float border_radius;
} vg_scrollbar_theme_t;

//=============================================================================
// Complete Theme
//=============================================================================

typedef struct vg_theme {
    const char* name;
    vg_color_scheme_t colors;
    vg_typography_t typography;
    vg_spacing_t spacing;
    vg_button_theme_t button;
    vg_input_theme_t input;
    vg_scrollbar_theme_t scrollbar;
} vg_theme_t;

//=============================================================================
// Theme API
//=============================================================================

/// Get current theme
vg_theme_t* vg_theme_get_current(void);

/// Set current theme
void vg_theme_set_current(vg_theme_t* theme);

/// Built-in themes
vg_theme_t* vg_theme_dark(void);
vg_theme_t* vg_theme_light(void);

/// Create custom theme (copy from base)
vg_theme_t* vg_theme_create(const char* name, vg_theme_t* base);

/// Destroy custom theme
void vg_theme_destroy(vg_theme_t* theme);
```

---

## 8. Platform Extensions

### 8.1 Multi-Window Support

```c
// Extend vgfx.h

//=============================================================================
// Window Flags
//=============================================================================

typedef enum vgfx_window_flags {
    VGFX_WINDOW_RESIZABLE   = 1 << 0,
    VGFX_WINDOW_BORDERLESS  = 1 << 1,
    VGFX_WINDOW_ALWAYS_TOP  = 1 << 2,
    VGFX_WINDOW_CENTERED    = 1 << 3,
    VGFX_WINDOW_HIDDEN      = 1 << 4,
    VGFX_WINDOW_DIALOG      = 1 << 5,  // Modal dialog style
} vgfx_window_flags_t;

//=============================================================================
// Multi-Window API
//=============================================================================

/// Create additional window
vgfx_window_t* vgfx_window_create(const char* title, int width, int height,
                                   uint32_t flags);

/// Create child/dialog window
vgfx_window_t* vgfx_window_create_child(vgfx_window_t* parent,
                                         const char* title, int width, int height,
                                         uint32_t flags);

/// Destroy window
void vgfx_window_destroy(vgfx_window_t* window);

/// Set window parent (for modal dialogs)
void vgfx_window_set_parent(vgfx_window_t* window, vgfx_window_t* parent);

/// Show/hide window
void vgfx_window_show(vgfx_window_t* window);
void vgfx_window_hide(vgfx_window_t* window);

/// Set window position
void vgfx_window_set_position(vgfx_window_t* window, int x, int y);

/// Center window on screen or parent
void vgfx_window_center(vgfx_window_t* window);
```

### 8.2 Clipboard Support

```c
// vg_clipboard.h

//=============================================================================
// Clipboard API
//=============================================================================

/// Set clipboard text
void vg_clipboard_set_text(const char* text);

/// Get clipboard text (caller must free)
char* vg_clipboard_get_text(void);

/// Check if clipboard has text
bool vg_clipboard_has_text(void);

/// Clear clipboard
void vg_clipboard_clear(void);
```

### 8.3 System Dialogs

```c
// vg_dialogs.h

//=============================================================================
// File Dialog Flags
//=============================================================================

typedef enum vg_file_dialog_flags {
    VG_FILE_DIALOG_OPEN     = 0,
    VG_FILE_DIALOG_SAVE     = 1,
    VG_FILE_DIALOG_FOLDER   = 2,
    VG_FILE_MULTI_SELECT    = 1 << 4,
} vg_file_dialog_flags_t;

//=============================================================================
// File Filter
//=============================================================================

typedef struct vg_file_filter {
    const char* name;        // e.g., "Viper Files"
    const char* pattern;     // e.g., "*.zia;*.vp"
} vg_file_filter_t;

//=============================================================================
// Dialog Results
//=============================================================================

typedef struct vg_file_result {
    char** paths;
    int count;
} vg_file_result_t;

//=============================================================================
// Dialog API
//=============================================================================

/// Show file open dialog
vg_file_result_t* vg_dialog_open_file(const char* title,
                                       const char* default_path,
                                       vg_file_filter_t* filters, int filter_count,
                                       uint32_t flags);

/// Show file save dialog
char* vg_dialog_save_file(const char* title,
                          const char* default_path,
                          const char* default_name,
                          vg_file_filter_t* filters, int filter_count);

/// Show folder select dialog
char* vg_dialog_select_folder(const char* title, const char* default_path);

/// Show message box
int vg_dialog_message(const char* title, const char* message,
                      const char** buttons, int button_count);

/// Free file result
void vg_file_result_free(vg_file_result_t* result);
```

---

## 9. Zia API

### 9.1 Runtime Declarations

```c
// rt_gui.h

//=============================================================================
// Viper.GUI.Window
//=============================================================================

void* rt_gui_window_new(const char* title, int64_t width, int64_t height);
void rt_gui_window_destroy(void* window);
void rt_gui_window_show(void* window);
void rt_gui_window_hide(void* window);
void rt_gui_window_set_title(void* window, const char* title);
int8_t rt_gui_window_is_open(void* window);
void rt_gui_window_set_content(void* window, void* widget);
void rt_gui_window_run(void* window);

//=============================================================================
// Viper.GUI.Button
//=============================================================================

void* rt_gui_button_new(const char* text);
void rt_gui_button_set_text(void* button, const char* text);
const char* rt_gui_button_get_text(void* button);
void rt_gui_button_on_click(void* button, void* callback);

//=============================================================================
// Viper.GUI.Label
//=============================================================================

void* rt_gui_label_new(const char* text);
void rt_gui_label_set_text(void* label, const char* text);
const char* rt_gui_label_get_text(void* label);

//=============================================================================
// Viper.GUI.TextInput
//=============================================================================

void* rt_gui_textinput_new(void);
void rt_gui_textinput_set_text(void* input, const char* text);
const char* rt_gui_textinput_get_text(void* input);
void rt_gui_textinput_set_placeholder(void* input, const char* text);
void rt_gui_textinput_on_change(void* input, void* callback);
void rt_gui_textinput_on_submit(void* input, void* callback);

// ... similar declarations for all widgets
```

### 9.2 Zia Examples

```rust
// Example 1: Simple window with button
module HelloGUI;

func start() {
    var window = Viper.GUI.Window.New("Hello GUI", 400, 300);

    var button = Viper.GUI.Button.New("Click Me!");
    button.OnClick(func() {
        Viper.Terminal.Say("Button clicked!");
    });

    window.SetContent(button);
    window.Run();
}
```

```rust
// Example 2: Form layout
module FormExample;

func start() {
    var window = Viper.GUI.Window.New("User Form", 400, 300);

    var form = Viper.GUI.VBox.New(10);
    form.SetPadding(20);

    // Name field
    var nameRow = Viper.GUI.HBox.New(10);
    nameRow.Add(Viper.GUI.Label.New("Name:"));
    var nameInput = Viper.GUI.TextInput.New();
    nameInput.SetFlex(1);
    nameRow.Add(nameInput);
    form.Add(nameRow);

    // Email field
    var emailRow = Viper.GUI.HBox.New(10);
    emailRow.Add(Viper.GUI.Label.New("Email:"));
    var emailInput = Viper.GUI.TextInput.New();
    emailInput.SetFlex(1);
    emailRow.Add(emailInput);
    form.Add(emailRow);

    // Submit button
    var submit = Viper.GUI.Button.New("Submit");
    submit.OnClick(func() {
        Viper.Terminal.Say("Name: " + nameInput.GetText());
        Viper.Terminal.Say("Email: " + emailInput.GetText());
    });
    form.Add(submit);

    window.SetContent(form);
    window.Run();
}
```

```rust
// Example 3: IDE-like layout
module IDEExample;

func start() {
    var window = Viper.GUI.Window.New("Viper IDE", 1200, 800);

    // Main layout
    var main = Viper.GUI.VBox.New(0);

    // Menu bar
    var menuBar = Viper.GUI.MenuBar.New();
    var fileMenu = menuBar.AddMenu("File");
    fileMenu.AddItem("New", "Ctrl+N", onNew);
    fileMenu.AddItem("Open", "Ctrl+O", onOpen);
    fileMenu.AddItem("Save", "Ctrl+S", onSave);
    fileMenu.AddSeparator();
    fileMenu.AddItem("Exit", "Alt+F4", onExit);
    main.Add(menuBar);

    // Toolbar
    var toolbar = Viper.GUI.ToolBar.New();
    toolbar.AddButton("new", "New File", onNew);
    toolbar.AddButton("open", "Open File", onOpen);
    toolbar.AddButton("save", "Save File", onSave);
    toolbar.AddSeparator();
    toolbar.AddButton("run", "Run", onRun);
    main.Add(toolbar);

    // Content area with split panes
    var content = Viper.GUI.SplitPane.New(Horizontal);
    content.SetFlex(1);

    // File tree on left
    var fileTree = Viper.GUI.TreeView.New();
    fileTree.SetMinWidth(200);
    content.SetPane(0, fileTree);

    // Editor area on right
    var editorPane = Viper.GUI.SplitPane.New(Vertical);

    // Tab bar and editors
    var editorArea = Viper.GUI.VBox.New(0);
    var tabs = Viper.GUI.TabBar.New();
    tabs.AddTab("main.zia", "file");
    editorArea.Add(tabs);

    var editor = Viper.GUI.CodeEditor.New();
    editor.SetHighlighter(Viper.GUI.Highlighters.Viper);
    editor.SetFlex(1);
    editorArea.Add(editor);
    editorPane.SetPane(0, editorArea);

    // Output panel at bottom
    var output = Viper.GUI.TextArea.New();
    output.SetReadonly(true);
    output.SetMinHeight(150);
    editorPane.SetPane(1, output);
    editorPane.SetRatio(0.75);

    content.SetPane(1, editorPane);
    content.SetPosition(250);

    main.Add(content);

    // Status bar
    var statusBar = Viper.GUI.StatusBar.New();
    statusBar.AddLabel("Ready");
    statusBar.AddSpacer();
    statusBar.AddLabel("Line 1, Col 1");
    main.Add(statusBar);

    window.SetContent(main);
    window.Run();
}
```

---

## 10. Implementation Phases

### Phase 1: Font Engine (6-8 weeks)

**Goal:** Load TTF fonts and render text.

**Status:** ✅ **COMPLETE**

**Week 1-2: TTF Parser**
- [x] Read TTF file header and table directory
- [x] Parse `head` table (font metrics)
- [x] Parse `hhea` and `hmtx` tables (horizontal metrics)
- [x] Parse `maxp` table (glyph count)
- [x] Parse `cmap` table (character to glyph mapping, format 4)
- [x] Unit tests for parser

**Week 3-4: Glyph Rasterizer**
- [x] Parse `glyf` table (glyph outlines)
- [x] Parse `loca` table (glyph offsets)
- [x] Implement quadratic Bezier flattening
- [x] Implement scanline rasterization with antialiasing
- [x] Unit tests for rasterizer

**Week 5-6: Glyph Cache & Text Rendering**
- [x] Implement glyph cache (hash map)
- [x] Integrate with existing `rt_canvas`
- [x] Implement `vg_font_draw_text()`
- [x] Implement text measurement functions
- [x] Handle kerning (optional `kern` table)
- [x] Unit tests and visual validation

**Week 7-8: Integration & Polish**
- [x] Runtime bindings (`rt_gui_font_*`)
- [x] Zia API (`Viper.GUI.Font`)
- [x] Documentation
- [x] Performance optimization
- [x] Memory leak testing

**Deliverables:**
- `vg_font.h`, `vg_ttf.c`, `vg_raster.c`, `vg_cache.c`
- `rt_font.h`, `rt_font.c`
- Test suite
- Example: render text at various sizes

---

### Phase 2: Widget Framework Core (6-8 weeks)

**Goal:** Widget base class, hierarchy, layout, and events.

**Status:** ✅ **COMPLETE**

**Week 1-2: Widget Base**
- [x] `vg_widget_t` structure with vtable
- [x] Widget creation/destruction
- [x] Hierarchy management (add/remove children)
- [x] State flags (hover, pressed, focused, disabled)
- [x] Unit tests

**Week 3-4: Layout System**
- [x] Layout interface (measure/arrange passes)
- [x] VBox layout
- [x] HBox layout
- [x] Flex layout
- [x] Grid layout
- [x] Layout tests

**Week 5-6: Event System**
- [x] Event structure and types
- [x] Event dispatch with bubbling
- [x] Mouse event routing (enter/leave/move/click)
- [x] Keyboard event routing
- [x] Focus management (tab navigation)
- [x] Event tests

**Week 7-8: Rendering Integration**
- [x] Paint traversal
- [x] Dirty region tracking
- [x] Clipping during paint
- [x] Integration with `rt_canvas`
- [x] Visual tests

**Deliverables:**
- `vg_widget.h/.c`, `vg_layout.h/.c`, `vg_event.h/.c`
- Test suite
- Example: nested containers with layout

---

### Phase 3: Core Widgets (6-8 weeks)

**Goal:** Basic widget set for general applications.

**Status:** ✅ **COMPLETE**

**Week 1-2: Label & Button**
- [x] Label widget (single/multi-line, alignment)
- [x] Button widget (text, icon, states)
- [x] Button styles (default, primary, danger)
- [x] Click handling
- [x] Visual tests

**Week 3-4: TextInput**
- [x] Single-line text input
- [x] Cursor rendering and movement
- [x] Text selection
- [x] Clipboard (cut/copy/paste)
- [x] Undo/redo stack
- [x] Password mode

**Week 5-6: Checkbox & Other Controls**
- [x] Checkbox widget
- [x] Radio button widget
- [x] Slider widget
- [x] Progress bar widget
- [x] Visual tests

**Week 7-8: ScrollView**
- [x] ScrollView container
- [x] Scrollbar rendering
- [x] Mouse wheel handling
- [x] Scroll thumb dragging
- [x] Smooth scrolling (optional)

**Deliverables:**
- Individual widget files (`vg_button.c`, `vg_label.c`, etc.)
- Test suite
- Example: form with various inputs

---

### Phase 4: Theming & Polish (3-4 weeks)

**Goal:** Professional appearance with dark/light themes.

**Status:** ✅ **COMPLETE**

**Week 1-2: Theme System**
- [x] Theme structure with all color/size definitions
- [x] Dark theme
- [x] Light theme
- [x] Theme switching
- [x] Per-widget style overrides

**Week 3-4: Visual Polish**
- [x] Consistent border radii
- [x] Focus rings
- [x] Hover/press animations (optional)
- [x] Drop shadows (optional)
- [x] Visual validation on all platforms

**Deliverables:**
- `vg_theme.h/.c`
- Built-in dark and light themes
- Theme documentation

---

### Phase 5: IDE Widgets (8-10 weeks)

**Goal:** Widgets needed for IDE.

**Status:** ✅ **COMPLETE**

**Week 1-2: TreeView**
- [x] Tree node structure
- [x] Expand/collapse
- [x] Selection
- [x] Icons
- [x] Virtual scrolling for large trees

**Week 3-4: TabBar & SplitPane**
- [x] TabBar with close buttons
- [x] Tab reordering (drag)
- [x] SplitPane with draggable divider
- [x] Collapsible panes

**Week 5-6: MenuBar & Menus**
- [x] MenuBar widget
- [x] Menu popups
- [x] Menu items (normal, checkbox, separator)
- [x] Submenus
- [x] Keyboard shortcuts
- [x] Context menus

**Week 7-8: ToolBar & StatusBar**
- [x] ToolBar with icon buttons
- [x] StatusBar with sections
- [x] Tooltip support

**Week 9-10: CodeEditor**
- [x] Text buffer (gap buffer or rope)
- [x] Line rendering with syntax highlighting
- [x] Cursor and selection
- [x] Line numbers
- [x] Basic editing operations
- [x] Undo/redo
- [x] Find/replace

**Deliverables:**
- IDE widget files
- Zia syntax highlighter
- Test suite
- Example: minimal IDE layout

---

### Phase 6: Platform Completion (4-6 weeks)

**Goal:** Full Windows and Linux support.

**Status:** ✅ **COMPLETE**

**Week 1-2: Windows (Win32)**
- [x] Window creation with Win32
- [x] Event translation
- [x] Framebuffer blitting (GDI or D2D)
- [x] Clipboard support
- [x] Testing on Windows

**Week 3-4: Linux (X11)**
- [x] Window creation with Xlib
- [x] Event translation
- [x] Framebuffer blitting (XImage/XShm)
- [x] Clipboard support (X selections)
- [x] Testing on Linux

**Week 5-6: Multi-Window & Dialogs**
- [x] Multi-window support
- [x] Modal dialog support
- [x] Native file dialogs (or custom)
- [x] Testing across platforms

**Deliverables:**
- Complete platform implementations
- Cross-platform test suite
- Platform-specific documentation

---

### Phase 7: Runtime & API (3-4 weeks)

**Goal:** Complete Zia integration.

**Status:** ✅ **COMPLETE** (January 2026)

**Week 1-2: Runtime Bindings**
- [x] All widget runtime functions
- [x] Polling-based event handling (WasClicked, IsHovered, etc.)
- [x] Memory management integration
- [x] Error handling

**Week 3-4: API Polish**
- [x] GUI.App class for simplified application framework
- [x] runtime.def entries for all 24 widget classes
- [x] Documentation (Appendix D: Runtime Reference)
- [ ] Example programs (TODO)

**Deliverables:**
- ✅ Complete `rt_gui.h/.c` (~700 lines)
- ✅ `runtime.def` entries (~470 lines)
- ✅ API documentation in bible/appendices/d-runtime-reference.md
- [ ] Tutorial examples (TODO)

---

## Summary

| Phase | Duration | Lines of Code | Key Deliverable | Status |
|-------|----------|---------------|-----------------|--------|
| 1. Font Engine | 6-8 weeks | ~4,500 | TTF rendering | ✅ Complete |
| 2. Widget Core | 6-8 weeks | ~3,000 | Layout & events | ✅ Complete |
| 3. Core Widgets | 6-8 weeks | ~4,000 | Button, TextInput, etc. | ✅ Complete |
| 4. Theming | 3-4 weeks | ~1,000 | Dark/Light themes | ✅ Complete |
| 5. IDE Widgets | 8-10 weeks | ~6,000 | TreeView, CodeEditor | ✅ Complete |
| 6. Platform | 4-6 weeks | ~2,000 | Win32, X11 | ✅ Complete |
| 7. Runtime | 3-4 weeks | ~1,500 | Zia API | ✅ Complete |
| **Total** | **~9-12 months** | **~22,000** | **Full GUI toolkit** | **✅ Complete** |

---

## Appendix: Key Algorithms

### A.1 Quadratic Bezier Flattening

```c
void flatten_quadratic(float x0, float y0, float x1, float y1, float x2, float y2,
                       float tolerance, point_t* out, int* count) {
    // Calculate flatness (distance from control point to line)
    float dx = x2 - x0;
    float dy = y2 - y0;
    float d = fabsf((x1 - x2) * dy - (y1 - y2) * dx) / sqrtf(dx*dx + dy*dy);

    if (d < tolerance) {
        // Flat enough, output line segment
        out[(*count)++] = (point_t){x2, y2};
    } else {
        // Subdivide at midpoint
        float x01 = (x0 + x1) * 0.5f;
        float y01 = (y0 + y1) * 0.5f;
        float x12 = (x1 + x2) * 0.5f;
        float y12 = (y1 + y2) * 0.5f;
        float x012 = (x01 + x12) * 0.5f;
        float y012 = (y01 + y12) * 0.5f;

        flatten_quadratic(x0, y0, x01, y01, x012, y012, tolerance, out, count);
        flatten_quadratic(x012, y012, x12, y12, x2, y2, tolerance, out, count);
    }
}
```

### A.2 Scanline Rasterization

```c
void rasterize_polygon(point_t* points, int count, int width, int height, uint8_t* bitmap) {
    // For each scanline
    for (int y = 0; y < height; y++) {
        // Find all edge intersections with this scanline
        float intersections[256];
        int num_intersections = 0;

        for (int i = 0; i < count; i++) {
            point_t p0 = points[i];
            point_t p1 = points[(i + 1) % count];

            // Check if edge crosses this scanline
            if ((p0.y <= y && p1.y > y) || (p1.y <= y && p0.y > y)) {
                // Calculate x intersection
                float t = (y - p0.y) / (p1.y - p0.y);
                float x = p0.x + t * (p1.x - p0.x);
                intersections[num_intersections++] = x;
            }
        }

        // Sort intersections
        qsort(intersections, num_intersections, sizeof(float), compare_float);

        // Fill between pairs (even-odd rule)
        for (int i = 0; i < num_intersections - 1; i += 2) {
            int x0 = (int)ceilf(intersections[i]);
            int x1 = (int)floorf(intersections[i + 1]);
            for (int x = x0; x <= x1; x++) {
                if (x >= 0 && x < width) {
                    bitmap[y * width + x] = 255;
                }
            }
        }
    }
}
```

### A.3 Flexbox Layout

```c
void layout_flex(vg_widget_t* container, float width, float height) {
    vg_flex_layout_t* flex = container->layout_data;
    bool is_row = (flex->direction == VG_DIRECTION_ROW);

    float main_size = is_row ? width : height;
    float cross_size = is_row ? height : width;

    // First pass: measure children, calculate total flex
    float total_fixed = 0;
    float total_flex = 0;

    for (vg_widget_t* child = container->first_child; child; child = child->next_sibling) {
        vg_widget_measure(child, width, height);
        float child_main = is_row ? child->measured_width : child->measured_height;

        if (child->layout.flex > 0) {
            total_flex += child->layout.flex;
        } else {
            total_fixed += child_main;
        }
    }

    // Calculate available space for flex items
    float gap_total = flex->gap * (container->child_count - 1);
    float available = main_size - total_fixed - gap_total;
    float flex_unit = (total_flex > 0) ? (available / total_flex) : 0;

    // Second pass: arrange children
    float main_pos = 0;

    for (vg_widget_t* child = container->first_child; child; child = child->next_sibling) {
        float child_main, child_cross;

        if (child->layout.flex > 0) {
            child_main = flex_unit * child->layout.flex;
        } else {
            child_main = is_row ? child->measured_width : child->measured_height;
        }

        // Cross-axis sizing based on align
        if (flex->align_items == VG_ALIGN_STRETCH) {
            child_cross = cross_size;
        } else {
            child_cross = is_row ? child->measured_height : child->measured_width;
        }

        // Position based on direction
        float x, y, w, h;
        if (is_row) {
            x = main_pos;
            w = child_main;
            h = child_cross;
            // Cross-axis alignment
            switch (flex->align_items) {
                case VG_ALIGN_START: y = 0; break;
                case VG_ALIGN_CENTER: y = (cross_size - h) / 2; break;
                case VG_ALIGN_END: y = cross_size - h; break;
                default: y = 0; break;
            }
        } else {
            y = main_pos;
            h = child_main;
            w = child_cross;
            switch (flex->align_items) {
                case VG_ALIGN_START: x = 0; break;
                case VG_ALIGN_CENTER: x = (cross_size - w) / 2; break;
                case VG_ALIGN_END: x = cross_size - w; break;
                default: x = 0; break;
            }
        }

        vg_widget_arrange(child, x, y, w, h);
        main_pos += child_main + flex->gap;
    }
}
```

---

*Document created: 2024*
*Last updated: 2024*
*Author: Viper Development Team*
