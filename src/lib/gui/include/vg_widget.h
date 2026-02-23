//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file vg_widget.h
/// @brief Widget base class, hierarchy management, and core widget operations.
///
/// @details This header defines the foundational widget abstraction for the
///          Viper GUI toolkit. Every visible UI element in the system derives
///          from the vg_widget base structure, which provides a tree-based
///          hierarchy (parent/child/sibling links), a two-pass layout engine
///          (measure then arrange), virtual-dispatch for rendering and events,
///          state flags, size constraints, and hit-testing.
///
///          The widget tree is the single authoritative representation of the
///          UI at any point in time. Layout is performed top-down in two passes:
///          a measure pass that computes desired sizes, followed by an arrange
///          pass that assigns final positions. Painting traverses the tree in
///          depth-first order so that children paint on top of their parents.
///
/// Key invariants:
///   - A widget has at most one parent; adding it to a new parent detaches it
///     from any previous parent automatically.
///   - Widget IDs are unique and monotonically increasing within a session.
///   - The vtable pointer must remain valid for the lifetime of the widget.
///
/// Ownership/Lifetime:
///   - vg_widget_destroy recursively destroys all children.
///   - vg_widget_remove_child detaches without destroying; the caller assumes
///     ownership and must destroy the child eventually.
///   - The `name` string is owned by the widget and freed on destroy.
///
/// Links:
///   - vg_layout.h  -- layout containers (VBox, HBox, Flex, Grid, Dock)
///   - vg_event.h   -- event creation and dispatch
///   - vg_theme.h   -- theming and color scheme
///   - vg_widgets.h -- concrete widget implementations (Label, Button, etc.)
///
//===----------------------------------------------------------------------===//
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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

    /// @brief Discriminator for the concrete type of a widget.
    ///
    /// @details Every widget carries a type tag so that generic traversal code
    ///          (hit testing, serialization, debugging) can distinguish between
    ///          widget kinds without relying solely on the vtable. The type is
    ///          set once at construction time and never changes.
    typedef enum vg_widget_type
    {
        VG_WIDGET_CONTAINER,    ///< Generic container with no visual representation.
        VG_WIDGET_LABEL,        ///< Static or dynamic text label.
        VG_WIDGET_BUTTON,       ///< Clickable push button.
        VG_WIDGET_TEXTINPUT,    ///< Single-line or multi-line text entry field.
        VG_WIDGET_CHECKBOX,     ///< Two-state or tri-state checkbox.
        VG_WIDGET_RADIO,        ///< Radio button (mutually exclusive within a group).
        VG_WIDGET_SLIDER,       ///< Horizontal or vertical value slider.
        VG_WIDGET_PROGRESS,     ///< Progress bar (determinate or indeterminate).
        VG_WIDGET_SCROLLVIEW,   ///< Scrollable viewport that clips its children.
        VG_WIDGET_LISTVIEW,     ///< Virtualised list with recycled rows.
        VG_WIDGET_LISTBOX,      ///< Non-virtual selectable item list.
        VG_WIDGET_DROPDOWN,     ///< Combo-box / drop-down selector.
        VG_WIDGET_TREEVIEW,     ///< Hierarchical tree with expand/collapse.
        VG_WIDGET_TABBAR,       ///< Horizontal tab strip for switching panes.
        VG_WIDGET_SPLITPANE,    ///< Resizable two-pane splitter.
        VG_WIDGET_MENUBAR,      ///< Application menu bar.
        VG_WIDGET_MENU,         ///< Drop-down or context menu.
        VG_WIDGET_MENUITEM,     ///< Individual item inside a menu.
        VG_WIDGET_TOOLBAR,      ///< Icon/button toolbar strip.
        VG_WIDGET_STATUSBAR,    ///< IDE-style status bar at the bottom.
        VG_WIDGET_DIALOG,       ///< Modal or modeless dialog window.
        VG_WIDGET_CODEEDITOR,   ///< Source code editor with syntax highlighting.
        VG_WIDGET_IMAGE,        ///< Raster image display.
        VG_WIDGET_SPINNER,      ///< Numeric up/down spinner control.
        VG_WIDGET_COLORSWATCH,  ///< Single-color preview swatch.
        VG_WIDGET_COLORPALETTE, ///< Grid of color swatches for quick selection.
        VG_WIDGET_COLORPICKER,  ///< Full RGB(A) color picker with sliders.
        VG_WIDGET_CUSTOM,       ///< Application-defined custom widget.
    } vg_widget_type_t;

    //=============================================================================
    // Widget State Flags
    //=============================================================================

    /// @brief Bit-field flags representing the current interactive state of a widget.
    ///
    /// @details Multiple flags can be active simultaneously (e.g. a widget can be
    ///          both VG_STATE_FOCUSED and VG_STATE_HOVERED). The flags are combined
    ///          with bitwise OR and stored in vg_widget::state. Rendering code
    ///          inspects these flags to choose the appropriate visual appearance.
    typedef enum vg_widget_state
    {
        VG_STATE_NORMAL = 0,        ///< No special state -- idle / default appearance.
        VG_STATE_HOVERED = 1 << 0,  ///< Mouse cursor is over the widget.
        VG_STATE_PRESSED = 1 << 1,  ///< Mouse button is held down on the widget.
        VG_STATE_FOCUSED = 1 << 2,  ///< Widget has keyboard focus.
        VG_STATE_DISABLED = 1 << 3, ///< Widget is disabled and ignores input.
        VG_STATE_SELECTED = 1 << 4, ///< Widget is in a selected state (e.g. list item).
        VG_STATE_CHECKED = 1 << 5,  ///< Widget is checked (checkbox, toggle).
    } vg_widget_state_t;

    //=============================================================================
    // Size Constraints
    //=============================================================================

    /// @brief Describes the minimum, maximum, and preferred dimensions for a widget.
    ///
    /// @details The layout engine uses these constraints during the measure pass
    ///          to determine how much space a widget should occupy. A value of 0
    ///          for max_width / max_height means "unconstrained", and a value of 0
    ///          for preferred_width / preferred_height means "compute automatically".
    typedef struct vg_constraints
    {
        float min_width;        ///< Minimum allowable width in pixels.
        float min_height;       ///< Minimum allowable height in pixels.
        float max_width;        ///< Maximum allowable width (0 = no maximum).
        float max_height;       ///< Maximum allowable height (0 = no maximum).
        float preferred_width;  ///< Desired width hint for the layout engine (0 = auto).
        float preferred_height; ///< Desired height hint for the layout engine (0 = auto).
    } vg_constraints_t;

    //=============================================================================
    // Layout Parameters
    //=============================================================================

    /// @brief Per-widget parameters consumed by the parent's layout algorithm.
    ///
    /// @details These values control how the widget participates in its parent's
    ///          layout. The flex factor determines how remaining space is
    ///          distributed among siblings in VBox/HBox/Flex containers. Margins
    ///          and paddings follow the CSS box-model convention: margin is
    ///          outside the widget's border box, padding is inside.
    typedef struct vg_layout_params
    {
        float flex;           ///< Flex grow factor (0 = fixed size, >0 = proportional growth).
        float margin_left;    ///< Left margin in pixels.
        float margin_top;     ///< Top margin in pixels.
        float margin_right;   ///< Right margin in pixels.
        float margin_bottom;  ///< Bottom margin in pixels.
        float padding_left;   ///< Left padding in pixels.
        float padding_top;    ///< Top padding in pixels.
        float padding_right;  ///< Right padding in pixels.
        float padding_bottom; ///< Bottom padding in pixels.
    } vg_layout_params_t;

    //=============================================================================
    // Virtual Function Table
    //=============================================================================

    /// @brief Virtual dispatch table providing per-type widget behaviour.
    ///
    /// @details Each concrete widget type supplies its own vtable instance at
    ///          construction time. The base widget code calls through these
    ///          function pointers for lifecycle management, layout, rendering,
    ///          event handling, and focus negotiation. All pointers are optional;
    ///          a NULL entry means the widget uses default (no-op) behaviour for
    ///          that operation.
    typedef struct vg_widget_vtable
    {
        // Lifecycle

        /// @brief Destructor -- called by vg_widget_destroy to release type-specific resources.
        /// @param self The widget being destroyed.
        void (*destroy)(vg_widget_t *self);

        // Layout

        /// @brief Measure pass -- compute desired size given available space.
        /// @param self             The widget to measure.
        /// @param available_width  Maximum width offered by the parent layout.
        /// @param available_height Maximum height offered by the parent layout.
        void (*measure)(vg_widget_t *self, float available_width, float available_height);

        /// @brief Arrange pass -- assign the widget's final position and size.
        /// @param self   The widget to arrange.
        /// @param x      Assigned X position relative to parent.
        /// @param y      Assigned Y position relative to parent.
        /// @param width  Assigned width.
        /// @param height Assigned height.
        void (*arrange)(vg_widget_t *self, float x, float y, float width, float height);

        // Rendering

        /// @brief Primary paint -- render the widget onto the canvas.
        /// @param self   The widget to paint.
        /// @param canvas Opaque canvas handle (platform-specific renderer).
        void (*paint)(vg_widget_t *self, void *canvas);

        /// @brief Overlay paint -- render popups, dropdowns, or tooltips that must
        ///        appear above the normal widget Z-order.
        /// @param self   The widget to paint.
        /// @param canvas Opaque canvas handle (platform-specific renderer).
        void (*paint_overlay)(vg_widget_t *self, void *canvas);

        // Events

        /// @brief Event handler -- process a dispatched event.
        /// @param self  The target widget.
        /// @param event The event to handle.
        /// @return true if the event was consumed and should not propagate further.
        bool (*handle_event)(vg_widget_t *self, vg_event_t *event);

        // Focus

        /// @brief Query whether this widget can receive keyboard focus.
        /// @param self The widget being queried.
        /// @return true if the widget is focusable.
        bool (*can_focus)(vg_widget_t *self);

        /// @brief Notification that the widget has gained or lost keyboard focus.
        /// @param self   The widget whose focus state changed.
        /// @param gained true if focus was gained, false if lost.
        void (*on_focus)(vg_widget_t *self, bool gained);
    } vg_widget_vtable_t;

    //=============================================================================
    // Widget Base Structure
    //=============================================================================

    /// @brief The base structure that every widget in the Viper GUI inherits from.
    ///
    /// @details Concrete widgets embed a vg_widget_t as their first member so
    ///          that a pointer to the concrete type can be safely cast to
    ///          vg_widget_t* for generic operations. The structure maintains the
    ///          widget tree topology, geometry produced by the layout engine,
    ///          interactive state flags, size constraints, layout parameters,
    ///          user-supplied callbacks, and an opaque pointer to implementation-
    ///          specific data.
    struct vg_widget
    {
        // Type and vtable
        vg_widget_type_t type;            ///< Runtime type discriminator.
        const vg_widget_vtable_t *vtable; ///< Virtual dispatch table for this widget type.

        // Identity
        uint32_t id; ///< Unique auto-generated widget identifier.
        char *name;  ///< Optional human-readable name for lookup (owned, may be NULL).

        // Hierarchy
        vg_widget_t *parent;       ///< Parent widget (NULL for root).
        vg_widget_t *first_child;  ///< First child in the doubly-linked child list.
        vg_widget_t *last_child;   ///< Last child in the doubly-linked child list.
        vg_widget_t *next_sibling; ///< Next sibling in parent's child list.
        vg_widget_t *prev_sibling; ///< Previous sibling in parent's child list.
        int child_count;           ///< Number of direct children.

        // Geometry (set by layout)
        float x, y;          ///< Position relative to parent's content area.
        float width, height; ///< Actual size assigned by the arrange pass.

        // Measured size (set by measure pass)
        float measured_width;  ///< Desired width computed during the measure pass.
        float measured_height; ///< Desired height computed during the measure pass.

        // Constraints
        vg_constraints_t constraints; ///< Min/max/preferred size constraints.

        // Layout parameters
        vg_layout_params_t layout; ///< Flex, margin, and padding values used by the parent layout.

        // State
        uint32_t state;    ///< Bitwise OR of vg_widget_state_t flags.
        bool visible;      ///< Whether the widget and its subtree are rendered.
        bool enabled;      ///< Whether the widget accepts user input.
        bool needs_layout; ///< Dirty flag: layout must be recomputed before next paint.
        bool needs_paint;  ///< Dirty flag: widget must be repainted.

        // Tab order
        int tab_index; ///< Explicit tab-stop position. Widgets with tab_index >= 0 are visited in
                       ///< ascending order before those with tab_index == -1 (natural order).
                       ///< Defaults to -1 (use tree traversal order).

        // User data
        void *user_data; ///< Application-supplied opaque pointer (not touched by the framework).

        // Callbacks
        void (*on_click)(vg_widget_t *self, void *user_data);  ///< Generic click callback.
        void (*on_change)(vg_widget_t *self, void *user_data); ///< Generic value-changed callback.
        void (*on_submit)(vg_widget_t *self, void *user_data); ///< Generic submit/enter callback.
        void *callback_data; ///< User data passed to the above callbacks.

        /// @brief Opaque pointer to widget-specific implementation data.
        ///
        /// @details Concrete widget types may allocate additional state and store
        ///          a pointer here. The vtable's destroy function is responsible
        ///          for freeing this data.
        void *impl_data;
    };

//=============================================================================
// Child Iteration Macros
//=============================================================================

/// @brief Iterate over all children of a widget.
/// @param parent Pointer to the parent widget.
/// @param child Loop variable name (declared as vg_widget_t*).
#define VG_FOREACH_CHILD(parent, child)                                                            \
    for (vg_widget_t *child = (parent)->first_child; child; child = child->next_sibling)

/// @brief Iterate over visible children of a widget (skips invisible children).
/// @param parent Pointer to the parent widget.
/// @param child Loop variable name (declared as vg_widget_t*).
/// @note The loop variable is declared inside the macro; use a unique name.
#define VG_FOREACH_VISIBLE_CHILD(parent, child)                                                    \
    for (vg_widget_t *child = (parent)->first_child; child; child = child->next_sibling)           \
        if (!child->visible)                                                                       \
            continue;                                                                              \
        else

    //=============================================================================
    // Widget Creation/Destruction
    //=============================================================================

    /// @brief Initialise the base fields of a widget structure.
    ///
    /// @details Called by every concrete widget constructor before populating
    ///          type-specific fields. Sets the type tag and vtable, generates a
    ///          unique ID, and zeroes out geometry, state, and child pointers.
    ///
    /// @param widget Pointer to the widget to initialise (must not be NULL).
    /// @param type   The concrete widget type tag.
    /// @param vtable Pointer to the virtual function table for this widget type.
    void vg_widget_init(vg_widget_t *widget,
                        vg_widget_type_t type,
                        const vg_widget_vtable_t *vtable);

    /// @brief Allocate and initialise a generic container widget.
    ///
    /// @details Creates a widget with no visual representation of its own. It
    ///          serves purely as a grouping node in the widget tree and as a
    ///          target for a layout algorithm (VBox, HBox, etc.).
    ///
    /// @param type The widget type tag to assign.
    /// @return Newly allocated widget, or NULL if allocation fails.
    vg_widget_t *vg_widget_create(vg_widget_type_t type);

    /// @brief Destroy a widget and recursively destroy all of its descendants.
    ///
    /// @details Calls the vtable destroy function (if present), frees the name
    ///          string, detaches the widget from its parent, and repeats the
    ///          process for every child. After this call the pointer is invalid.
    ///
    /// @param widget The widget to destroy (may be NULL, in which case this is a no-op).
    void vg_widget_destroy(vg_widget_t *widget);

    //=============================================================================
    // Hierarchy Management
    //=============================================================================

    /// @brief Append a child widget to the end of the parent's child list.
    ///
    /// @details If @p child already has a parent it is first removed from that
    ///          parent's child list. The child's parent pointer is updated and
    ///          the parent's child_count is incremented. Triggers a layout
    ///          invalidation on the parent.
    ///
    /// @param parent The parent widget to add the child to.
    /// @param child  The widget to add as a child.
    void vg_widget_add_child(vg_widget_t *parent, vg_widget_t *child);

    /// @brief Insert a child widget at a specific index in the parent's child list.
    ///
    /// @details If @p index is greater than or equal to the current child count
    ///          the child is appended at the end. Negative indices are clamped to
    ///          zero. The child is detached from any previous parent first.
    ///
    /// @param parent The parent widget.
    /// @param child  The widget to insert.
    /// @param index  Zero-based position among the existing children.
    void vg_widget_insert_child(vg_widget_t *parent, vg_widget_t *child, int index);

    /// @brief Remove a child widget from its parent without destroying it.
    ///
    /// @details After removal the child's parent pointer is set to NULL and the
    ///          caller takes ownership. The child must actually be a child of
    ///          @p parent; otherwise behaviour is undefined.
    ///
    /// @param parent The parent widget.
    /// @param child  The child widget to remove.
    void vg_widget_remove_child(vg_widget_t *parent, vg_widget_t *child);

    /// @brief Remove all children from a widget without destroying them.
    ///
    /// @details Every child's parent pointer is set to NULL and the parent's
    ///          child list is emptied. The caller is responsible for eventually
    ///          destroying each removed child.
    ///
    /// @param parent The widget whose children are to be removed.
    void vg_widget_clear_children(vg_widget_t *parent);

    /// @brief Retrieve the child at a given index in the parent's child list.
    ///
    /// @param parent The parent widget.
    /// @param index  Zero-based index of the desired child.
    /// @return Pointer to the child widget, or NULL if @p index is out of range.
    vg_widget_t *vg_widget_get_child(vg_widget_t *parent, int index);

    /// @brief Search the widget tree rooted at @p root for a widget with the
    ///        given name.
    ///
    /// @details Performs a depth-first traversal comparing each widget's name
    ///          field (case-sensitive). Returns the first match found.
    ///
    /// @param root The root of the subtree to search.
    /// @param name The name string to match.
    /// @return Pointer to the matching widget, or NULL if none is found.
    vg_widget_t *vg_widget_find_by_name(vg_widget_t *root, const char *name);

    /// @brief Search the widget tree rooted at @p root for a widget with the
    ///        given unique ID.
    ///
    /// @details Performs a depth-first traversal. Since IDs are unique, the
    ///          search terminates as soon as a match is found.
    ///
    /// @param root The root of the subtree to search.
    /// @param id   The widget ID to find.
    /// @return Pointer to the matching widget, or NULL if none is found.
    vg_widget_t *vg_widget_find_by_id(vg_widget_t *root, uint32_t id);

    //=============================================================================
    // Geometry & Constraints
    //=============================================================================

    /// @brief Replace all size constraints for a widget at once.
    ///
    /// @details Copies the supplied constraints structure into the widget and
    ///          marks the widget's layout as dirty.
    ///
    /// @param widget      The widget to update.
    /// @param constraints The new constraint values.
    void vg_widget_set_constraints(vg_widget_t *widget, vg_constraints_t constraints);

    /// @brief Set only the minimum size constraints, leaving other constraints unchanged.
    ///
    /// @param widget The widget to update.
    /// @param width  Minimum width in pixels.
    /// @param height Minimum height in pixels.
    void vg_widget_set_min_size(vg_widget_t *widget, float width, float height);

    /// @brief Set only the maximum size constraints, leaving other constraints unchanged.
    ///
    /// @param widget The widget to update.
    /// @param width  Maximum width in pixels (0 = unconstrained).
    /// @param height Maximum height in pixels (0 = unconstrained).
    void vg_widget_set_max_size(vg_widget_t *widget, float width, float height);

    /// @brief Set the preferred (hint) size, leaving min/max unchanged.
    ///
    /// @param widget The widget to update.
    /// @param width  Preferred width in pixels (0 = auto).
    /// @param height Preferred height in pixels (0 = auto).
    void vg_widget_set_preferred_size(vg_widget_t *widget, float width, float height);

    /// @brief Lock the widget to an exact size by setting min, max, and preferred
    ///        to the same values.
    ///
    /// @param widget The widget to update.
    /// @param width  Fixed width in pixels.
    /// @param height Fixed height in pixels.
    void vg_widget_set_fixed_size(vg_widget_t *widget, float width, float height);

    /// @brief Retrieve the bounding rectangle of a widget in its parent's coordinate space.
    ///
    /// @param widget The widget to query.
    /// @param[out] x      Receives the X position relative to the parent.
    /// @param[out] y      Receives the Y position relative to the parent.
    /// @param[out] width  Receives the width.
    /// @param[out] height Receives the height.
    void vg_widget_get_bounds(vg_widget_t *widget, float *x, float *y, float *width, float *height);

    /// @brief Retrieve the bounding rectangle of a widget in screen (root-relative)
    ///        coordinate space.
    ///
    /// @details Walks up the parent chain, accumulating offsets, to convert the
    ///          widget's local position to screen coordinates.
    ///
    /// @param widget The widget to query.
    /// @param[out] x      Receives the X position in screen coordinates.
    /// @param[out] y      Receives the Y position in screen coordinates.
    /// @param[out] width  Receives the width.
    /// @param[out] height Receives the height.
    void vg_widget_get_screen_bounds(
        vg_widget_t *widget, float *x, float *y, float *width, float *height);

    //=============================================================================
    // Layout Parameters
    //=============================================================================

    /// @brief Set the flex grow factor for this widget.
    ///
    /// @details In a flex, VBox, or HBox container the flex factor determines
    ///          what proportion of remaining space this widget receives. A value
    ///          of 0 means the widget keeps its measured size.
    ///
    /// @param widget The widget to update.
    /// @param flex   Flex grow factor (>= 0).
    void vg_widget_set_flex(vg_widget_t *widget, float flex);

    /// @brief Set uniform margin on all four sides of the widget.
    ///
    /// @param widget The widget to update.
    /// @param margin Margin value in pixels applied to left, top, right, and bottom.
    void vg_widget_set_margin(vg_widget_t *widget, float margin);

    /// @brief Set individual margin values for each side of the widget.
    ///
    /// @param widget The widget to update.
    /// @param left   Left margin in pixels.
    /// @param top    Top margin in pixels.
    /// @param right  Right margin in pixels.
    /// @param bottom Bottom margin in pixels.
    void vg_widget_set_margins(
        vg_widget_t *widget, float left, float top, float right, float bottom);

    /// @brief Set uniform padding on all four sides of the widget.
    ///
    /// @param widget  The widget to update.
    /// @param padding Padding value in pixels applied to left, top, right, and bottom.
    void vg_widget_set_padding(vg_widget_t *widget, float padding);

    /// @brief Set individual padding values for each side of the widget.
    ///
    /// @param widget The widget to update.
    /// @param left   Left padding in pixels.
    /// @param top    Top padding in pixels.
    /// @param right  Right padding in pixels.
    /// @param bottom Bottom padding in pixels.
    void vg_widget_set_paddings(
        vg_widget_t *widget, float left, float top, float right, float bottom);

    //=============================================================================
    // State Management
    //=============================================================================

    /// @brief Enable or disable a widget.
    ///
    /// @details When disabled the VG_STATE_DISABLED flag is set, the widget stops
    ///          receiving input events, and renderers should draw it in a greyed-out
    ///          style. Disabling a parent does not automatically disable children,
    ///          but event dispatch will stop at the disabled widget.
    ///
    /// @param widget  The widget to update.
    /// @param enabled true to enable, false to disable.
    void vg_widget_set_enabled(vg_widget_t *widget, bool enabled);

    /// @brief Query whether a widget is currently enabled.
    ///
    /// @param widget The widget to query.
    /// @return true if the widget is enabled and accepts input.
    bool vg_widget_is_enabled(vg_widget_t *widget);

    /// @brief Show or hide a widget and its entire subtree.
    ///
    /// @details An invisible widget is skipped during layout, painting, and
    ///          hit-testing. Hiding a widget invalidates the parent's layout.
    ///
    /// @param widget  The widget to update.
    /// @param visible true to show, false to hide.
    void vg_widget_set_visible(vg_widget_t *widget, bool visible);

    /// @brief Query whether a widget is currently visible.
    ///
    /// @param widget The widget to query.
    /// @return true if the widget is visible.
    bool vg_widget_is_visible(vg_widget_t *widget);

    /// @brief Test whether a specific state flag is currently set on a widget.
    ///
    /// @param widget The widget to query.
    /// @param state  The state flag to test (a single vg_widget_state_t value).
    /// @return true if the flag is set.
    bool vg_widget_has_state(vg_widget_t *widget, vg_widget_state_t state);

    /// @brief Assign a human-readable name to the widget for lookup purposes.
    ///
    /// @details The name string is copied internally. Pass NULL to clear any
    ///          existing name. Names do not need to be unique, but
    ///          vg_widget_find_by_name returns only the first match.
    ///
    /// @param widget The widget to update.
    /// @param name   The name string to assign (copied), or NULL to clear.
    void vg_widget_set_name(vg_widget_t *widget, const char *name);

    /// @brief Retrieve the widget's name.
    ///
    /// @param widget The widget to query.
    /// @return The name string (read-only), or NULL if no name has been set.
    const char *vg_widget_get_name(vg_widget_t *widget);

    //=============================================================================
    // Layout & Rendering
    //=============================================================================

    /// @brief Execute the measure pass on the widget tree rooted at @p root.
    ///
    /// @details Recursively calls each widget's vtable measure function, passing
    ///          down the available space. After this pass every widget's
    ///          measured_width and measured_height are up to date.
    ///
    /// @param root             The root widget of the subtree to measure.
    /// @param available_width  Total width available for the root widget.
    /// @param available_height Total height available for the root widget.
    void vg_widget_measure(vg_widget_t *root, float available_width, float available_height);

    /// @brief Execute the arrange pass on the widget tree rooted at @p root.
    ///
    /// @details Recursively calls each widget's vtable arrange function, assigning
    ///          final x, y, width, and height values. Must be called after the
    ///          measure pass.
    ///
    /// @param root   The root widget of the subtree to arrange.
    /// @param x      X position assigned to the root.
    /// @param y      Y position assigned to the root.
    /// @param width  Width assigned to the root.
    /// @param height Height assigned to the root.
    void vg_widget_arrange(vg_widget_t *root, float x, float y, float width, float height);

    /// @brief Perform a full two-pass layout (measure followed by arrange).
    ///
    /// @details Convenience function that calls vg_widget_measure and then
    ///          vg_widget_arrange with position (0, 0). Suitable for laying out
    ///          the root of a window.
    ///
    /// @param root             The root widget of the tree to layout.
    /// @param available_width  Total width available.
    /// @param available_height Total height available.
    void vg_widget_layout(vg_widget_t *root, float available_width, float available_height);

    /// @brief Render the widget tree rooted at @p root onto a canvas.
    ///
    /// @details Traverses the tree depth-first, calling each visible widget's
    ///          vtable paint function, followed by a second pass for paint_overlay
    ///          to draw popups and dropdowns on top of all other content.
    ///
    /// @param root   The root of the widget subtree to paint.
    /// @param canvas Opaque canvas handle (platform-specific renderer).
    void vg_widget_paint(vg_widget_t *root, void *canvas);

    /// @brief Mark a widget as needing to be repainted.
    ///
    /// @details Sets the needs_paint flag on the widget. The application's event
    ///          loop should check this flag to schedule a repaint.
    ///
    /// @param widget The widget to invalidate.
    void vg_widget_invalidate(vg_widget_t *widget);

    /// @brief Mark a widget (and its parent chain) as needing layout recomputation.
    ///
    /// @details Sets the needs_layout flag, which causes the next layout pass to
    ///          re-measure and re-arrange this subtree.
    ///
    /// @param widget The widget whose layout is invalid.
    void vg_widget_invalidate_layout(vg_widget_t *widget);

    //=============================================================================
    // Hit Testing
    //=============================================================================

    /// @brief Find the deepest widget at the given screen coordinates.
    ///
    /// @details Traverses the tree from leaves to root, returning the most deeply
    ///          nested visible and enabled widget whose bounds contain the point.
    ///          If input capture is active the captured widget is returned instead.
    ///
    /// @param root The root of the widget tree to test.
    /// @param x    X coordinate in screen space.
    /// @param y    Y coordinate in screen space.
    /// @return The widget under the point, or NULL if the point is outside all widgets.
    vg_widget_t *vg_widget_hit_test(vg_widget_t *root, float x, float y);

    /// @brief Test whether a point in screen coordinates lies inside a widget's bounds.
    ///
    /// @param widget The widget to test against.
    /// @param x      X coordinate in screen space.
    /// @param y      Y coordinate in screen space.
    /// @return true if the point is within the widget's bounding rectangle.
    bool vg_widget_contains_point(vg_widget_t *widget, float x, float y);

    //=============================================================================
    // Input Capture (for popups/dropdowns)
    //=============================================================================

    /// @brief Begin input capture so that all mouse events are routed to the
    ///        specified widget regardless of hit-test results.
    ///
    /// @details Used by popups, dropdowns, and drag operations that need to
    ///          receive mouse events even when the cursor moves outside the
    ///          widget's bounds. Only one widget can capture input at a time;
    ///          calling this while another widget has capture replaces it.
    ///
    /// @param widget The widget that should receive all mouse events.
    void vg_widget_set_input_capture(vg_widget_t *widget);

    /// @brief Release the current input capture so that mouse events resume
    ///        normal hit-test routing.
    void vg_widget_release_input_capture(void);

    /// @brief Query which widget, if any, currently holds input capture.
    ///
    /// @return The widget capturing input, or NULL if no capture is active.
    vg_widget_t *vg_widget_get_input_capture(void);

    //=============================================================================
    // Focus Management
    //=============================================================================

    /// @brief Move keyboard focus to the specified widget.
    ///
    /// @details If the widget's vtable reports it as focusable (can_focus returns
    ///          true), the previously focused widget receives a focus-lost
    ///          notification and the new widget receives a focus-gained
    ///          notification. Setting focus to NULL clears focus.
    ///
    /// @param widget The widget to focus, or NULL to clear focus.
    void vg_widget_set_focus(vg_widget_t *widget);

    /// @brief Find the widget that currently has keyboard focus within the tree.
    ///
    /// @param root The root of the subtree to search.
    /// @return The focused widget, or NULL if no widget in the tree has focus.
    vg_widget_t *vg_widget_get_focused(vg_widget_t *root);

    /// @brief Advance keyboard focus to the next focusable widget in tab order.
    ///
    /// @details Performs a depth-first traversal starting from the currently
    ///          focused widget and wraps around to the first focusable widget
    ///          if the end of the tree is reached.
    ///
    /// @param root The root of the widget tree.
    void vg_widget_focus_next(vg_widget_t *root);

    /// @brief Move keyboard focus to the previous focusable widget in tab order.
    ///
    /// @details Performs a reverse depth-first traversal from the currently
    ///          focused widget, wrapping to the last focusable widget if the
    ///          beginning of the tree is reached.
    ///
    /// @param root The root of the widget tree.
    void vg_widget_focus_prev(vg_widget_t *root);

    /// @brief Set the explicit tab-stop index for a widget.
    ///
    /// @details Widgets with tab_index >= 0 are visited in ascending order during
    ///          Tab/Shift+Tab navigation before widgets with tab_index == -1, which
    ///          are visited in natural tree-traversal order. Pass -1 to restore the
    ///          default natural-order behaviour.
    ///
    /// @param widget    The widget to update.
    /// @param tab_index The tab stop index (>= 0) or -1 for natural order.
    void vg_widget_set_tab_index(vg_widget_t *widget, int tab_index);

    //=============================================================================
    // Modal Root
    //=============================================================================

    /// @brief Register a widget as the current modal root.
    ///
    /// @details When a modal root is active, mouse hit-testing is restricted to
    ///          the modal widget's subtree, and keyboard events are redirected to
    ///          the modal root if the focused widget lies outside it. Pass NULL to
    ///          clear the modal root and restore normal event routing.
    ///
    /// @param widget The modal root widget, or NULL to clear.
    void vg_widget_set_modal_root(vg_widget_t *widget);

    /// @brief Retrieve the current modal root widget.
    ///
    /// @return The modal root, or NULL if no modal widget is active.
    vg_widget_t *vg_widget_get_modal_root(void);

    //=============================================================================
    // ID Generation
    //=============================================================================

    /// @brief Generate a globally unique widget identifier.
    ///
    /// @details Uses an internal monotonically increasing counter. Thread-safety
    ///          is not guaranteed; all widget operations should occur on a single
    ///          thread.
    ///
    /// @return A new unique uint32_t identifier.
    uint32_t vg_widget_next_id(void);

#ifdef __cplusplus
}
#endif
