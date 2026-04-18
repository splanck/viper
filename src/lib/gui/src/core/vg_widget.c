//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/core/vg_widget.c
//
//===----------------------------------------------------------------------===//
// vg_widget.c - Widget base class implementation
#include "../../include/vg_widget.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration to avoid pulling the entire IDE-widgets header into the
// widget core. Defined in vg_tooltip.c — clears tooltip-manager pointers when
// a widget is destroyed (fixes dangling-pointer dereference).
extern void vg_tooltip_manager_widget_destroyed(vg_widget_t *widget);
extern void vg_tooltip_manager_widget_hidden(vg_widget_t *widget);

//=============================================================================
// Global State
//=============================================================================

static uint32_t g_next_widget_id = 1;
static vg_widget_t *g_focused_widget = NULL;
static vg_widget_t *g_input_capture_widget = NULL;
static vg_widget_t *g_modal_root = NULL;
static vg_widget_t *g_hovered_widget = NULL;

static bool widget_paints_children_internally(const vg_widget_t *widget) {
    if (!widget)
        return false;
    if (widget->type == VG_WIDGET_SCROLLVIEW)
        return true;
    return widget->type == VG_WIDGET_CUSTOM && widget->vtable && widget->vtable->paint_overlay;
}

static void paint_widget_normal_tree(vg_widget_t *root, void *canvas) {
    if (!root || !root->visible || !canvas)
        return;

    if (root->vtable && root->vtable->paint) {
        root->vtable->paint(root, canvas);
    }

    if (widget_paints_children_internally(root))
        return;

    VG_FOREACH_CHILD(root, child) {
        paint_widget_normal_tree(child, canvas);
    }
}

static void paint_widget_overlay_tree(vg_widget_t *root, void *canvas) {
    if (!root || !root->visible || !canvas)
        return;

    if (root->vtable && root->vtable->paint_overlay) {
        root->vtable->paint_overlay(root, canvas);
        if (widget_paints_children_internally(root))
            return;
    }

    VG_FOREACH_CHILD(root, child) {
        paint_widget_overlay_tree(child, canvas);
    }
}

//=============================================================================
// ID Generation
//=============================================================================

uint32_t vg_widget_next_id(void) {
    return g_next_widget_id++;
}

//=============================================================================
// Default VTable Functions
//=============================================================================

static void default_destroy(vg_widget_t *self) {
    // Default: do nothing (subclass should clean up impl_data)
}

static void default_measure(vg_widget_t *self, float available_width, float available_height) {
    // Default: use preferred size or measure children
    float w = self->constraints.preferred_width;
    float h = self->constraints.preferred_height;

    // If no preferred size, measure children
    if (w == 0)
        w = self->constraints.min_width;
    if (h == 0)
        h = self->constraints.min_height;

    self->measured_width = w;
    self->measured_height = h;
}

static void default_arrange(vg_widget_t *self, float x, float y, float width, float height) {
    // Apply constraints
    if (self->constraints.min_width > 0 && width < self->constraints.min_width) {
        width = self->constraints.min_width;
    }
    if (self->constraints.max_width > 0 && width > self->constraints.max_width) {
        width = self->constraints.max_width;
    }
    if (self->constraints.min_height > 0 && height < self->constraints.min_height) {
        height = self->constraints.min_height;
    }
    if (self->constraints.max_height > 0 && height > self->constraints.max_height) {
        height = self->constraints.max_height;
    }

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    // Position children within content area (simple stacking for plain containers).
    // Layout containers (VBox, HBox, SplitPane) override arrange and handle
    // children themselves, so this only runs for default containers.
    // Children are stretched to fill the content area (like CSS stretch).
    float cx = self->layout.padding_left;
    float cy = self->layout.padding_top;
    float content_w = width - self->layout.padding_left - self->layout.padding_right;
    float content_h = height - self->layout.padding_top - self->layout.padding_bottom;

    VG_FOREACH_VISIBLE_CHILD(self, child) {
        float cw = content_w - child->layout.margin_left - child->layout.margin_right;
        float ch = content_h - child->layout.margin_top - child->layout.margin_bottom;
        vg_widget_arrange(child,
                          cx + child->layout.margin_left,
                          cy + child->layout.margin_top,
                          cw > 0 ? cw : child->measured_width,
                          ch > 0 ? ch : child->measured_height);
    }
}

static void default_paint(vg_widget_t *self, void *canvas) {
    // Default: paint nothing (container just paints children)
}

static bool default_handle_event(vg_widget_t *self, vg_event_t *event) {
    return false; // Not handled
}

static bool default_can_focus(vg_widget_t *self) {
    return false; // Most widgets can't focus by default
}

static void default_on_focus(vg_widget_t *self, bool gained) {
    if (gained) {
        self->state |= VG_STATE_FOCUSED;
    } else {
        self->state &= ~VG_STATE_FOCUSED;
    }
}

//=============================================================================
// Default VTable
//=============================================================================

static const vg_widget_vtable_t g_default_vtable = {
    .destroy = default_destroy,
    .measure = default_measure,
    .arrange = default_arrange,
    .paint = default_paint,
    .handle_event = default_handle_event,
    .can_focus = default_can_focus,
    .on_focus = default_on_focus,
};

static bool widget_is_ancestor(const vg_widget_t *ancestor, const vg_widget_t *widget) {
    for (const vg_widget_t *current = widget; current; current = current->parent) {
        if (current == ancestor)
            return true;
    }
    return false;
}

static void clear_interactive_state_recursive(vg_widget_t *widget) {
    if (!widget)
        return;

    widget->state &= ~(VG_STATE_HOVERED | VG_STATE_PRESSED | VG_STATE_FOCUSED);
    switch (widget->type) {
        case VG_WIDGET_SCROLLVIEW: {
            vg_scrollview_t *scroll = (vg_scrollview_t *)widget;
            scroll->h_scrollbar_hovered = false;
            scroll->v_scrollbar_hovered = false;
            scroll->h_scrollbar_dragging = false;
            scroll->v_scrollbar_dragging = false;
            break;
        }
        case VG_WIDGET_TABBAR: {
            vg_tabbar_t *tabbar = (vg_tabbar_t *)widget;
            tabbar->hovered_tab = NULL;
            tabbar->close_button_hovered = false;
            tabbar->dragging = false;
            tabbar->drag_tab = NULL;
            break;
        }
        case VG_WIDGET_SPLITPANE: {
            vg_splitpane_t *split = (vg_splitpane_t *)widget;
            split->splitter_hovered = false;
            split->dragging = false;
            break;
        }
        case VG_WIDGET_MENUBAR: {
            vg_menubar_t *menubar = (vg_menubar_t *)widget;
            if (menubar->open_menu)
                menubar->open_menu->open = false;
            menubar->open_menu = NULL;
            menubar->highlighted = NULL;
            menubar->menu_active = false;
            break;
        }
        case VG_WIDGET_TOOLBAR: {
            vg_toolbar_t *toolbar = (vg_toolbar_t *)widget;
            toolbar->pressed_item = NULL;
            break;
        }
        case VG_WIDGET_DIALOG:
            ((vg_dialog_t *)widget)->is_dragging = false;
            break;
        case VG_WIDGET_CODEEDITOR:
            ((vg_codeeditor_t *)widget)->scrollbar_dragging = false;
            break;
        default:
            break;
    }
    VG_FOREACH_CHILD(widget, child) {
        clear_interactive_state_recursive(child);
    }
}

static bool point_in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    if (rw <= 0.0f || rh <= 0.0f)
        return false;
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void scrollview_get_viewport_screen_bounds(const vg_scrollview_t *scroll,
                                                  float *x,
                                                  float *y,
                                                  float *width,
                                                  float *height) {
    if (!scroll) {
        if (x)
            *x = 0.0f;
        if (y)
            *y = 0.0f;
        if (width)
            *width = 0.0f;
        if (height)
            *height = 0.0f;
        return;
    }

    float sx = 0.0f;
    float sy = 0.0f;
    float sw = 0.0f;
    float sh = 0.0f;
    vg_widget_get_screen_bounds((vg_widget_t *)&scroll->base, &sx, &sy, &sw, &sh);
    if (scroll->show_v_scrollbar)
        sw -= scroll->scrollbar_width;
    if (scroll->show_h_scrollbar)
        sh -= scroll->scrollbar_width;
    if (sw < 0.0f)
        sw = 0.0f;
    if (sh < 0.0f)
        sh = 0.0f;

    if (x)
        *x = sx;
    if (y)
        *y = sy;
    if (width)
        *width = sw;
    if (height)
        *height = sh;
}

static bool widget_point_within_ancestor_clips(const vg_widget_t *widget, float x, float y) {
    for (const vg_widget_t *ancestor = widget ? widget->parent : NULL; ancestor;
         ancestor = ancestor->parent) {
        float sx = 0.0f;
        float sy = 0.0f;
        float sw = 0.0f;
        float sh = 0.0f;
        if (ancestor->type == VG_WIDGET_SCROLLVIEW) {
            scrollview_get_viewport_screen_bounds((const vg_scrollview_t *)ancestor,
                                                  &sx,
                                                  &sy,
                                                  &sw,
                                                  &sh);
        } else {
            vg_widget_get_screen_bounds((vg_widget_t *)ancestor, &sx, &sy, &sw, &sh);
        }
        if (!point_in_rect(x, y, sx, sy, sw, sh))
            return false;
    }
    return true;
}

//=============================================================================
// Widget Initialization
//=============================================================================

void vg_widget_init(vg_widget_t *widget, vg_widget_type_t type, const vg_widget_vtable_t *vtable) {
    if (!widget)
        return;

    memset(widget, 0, sizeof(vg_widget_t));

    widget->type = type;
    widget->vtable = vtable ? vtable : &g_default_vtable;
    widget->id = vg_widget_next_id();
    widget->visible = true;
    widget->enabled = true;
    widget->needs_layout = true;
    widget->needs_paint = true;
    widget->tab_index = -1; // -1 = natural traversal order
}

//=============================================================================
// Widget Creation/Destruction
//=============================================================================

vg_widget_t *vg_widget_create(vg_widget_type_t type) {
    vg_widget_t *widget = calloc(1, sizeof(vg_widget_t));
    if (!widget)
        return NULL;

    vg_widget_init(widget, type, NULL);

    return widget;
}

/// @brief Widget destroy.
void vg_widget_destroy(vg_widget_t *widget) {
    if (!widget)
        return;

    if (widget->parent) {
        vg_widget_remove_child(widget->parent, widget);
    }

    // Recursively destroy children
    while (widget->first_child) {
        vg_widget_destroy(widget->first_child);
    }

    // Call type-specific destructor
    if (widget->vtable && widget->vtable->destroy) {
        widget->vtable->destroy(widget);
    }

    // Free impl data if allocated
    if (widget->impl_data) {
        free(widget->impl_data);
    }

    if (widget->tooltip_text) {
        free(widget->tooltip_text);
    }

    // Free name
    if (widget->name) {
        free(widget->name);
    }

    // Clear focused widget if this is it
    if (g_focused_widget == widget) {
        g_focused_widget = NULL;
    }

    // Clear input capture if this widget holds it
    if (g_input_capture_widget == widget) {
        g_input_capture_widget = NULL;
    }

    // Clear modal root if this widget was the modal
    if (g_modal_root == widget) {
        g_modal_root = NULL;
    }

    if (g_hovered_widget == widget) {
        g_hovered_widget = NULL;
    }

    // Notify tooltip manager so it does not retain a dangling pointer.
    vg_tooltip_manager_widget_destroyed(widget);

    widget->parent = NULL;
    widget->prev_sibling = NULL;
    widget->next_sibling = NULL;
    free(widget);
}

//=============================================================================
// Hierarchy Management
//=============================================================================

void vg_widget_add_child(vg_widget_t *parent, vg_widget_t *child) {
    if (!parent || !child)
        return;
    if (parent == child || widget_is_ancestor(child, parent))
        return;

    // Remove from previous parent if any
    if (child->parent) {
        vg_widget_remove_child(child->parent, child);
    }

    child->parent = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;

    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }

    parent->last_child = child;
    parent->child_count++;

    parent->needs_layout = true;
}

/// @brief Widget insechild.
void vg_widget_insert_child(vg_widget_t *parent, vg_widget_t *child, int index) {
    if (!parent || !child || index < 0)
        return;
    if (parent == child || widget_is_ancestor(child, parent))
        return;

    // Remove from previous parent if any
    if (child->parent) {
        vg_widget_remove_child(child->parent, child);
    }

    if (index >= parent->child_count) {
        // Insert at end
        vg_widget_add_child(parent, child);
        return;
    }

    // Find widget at index
    vg_widget_t *at = parent->first_child;
    for (int i = 0; i < index && at; i++) {
        at = at->next_sibling;
    }

    child->parent = parent;

    if (at) {
        // Insert before 'at'
        child->next_sibling = at;
        child->prev_sibling = at->prev_sibling;

        if (at->prev_sibling) {
            at->prev_sibling->next_sibling = child;
        } else {
            parent->first_child = child;
        }

        at->prev_sibling = child;
    } else {
        // Insert at end
        child->prev_sibling = parent->last_child;
        child->next_sibling = NULL;

        if (parent->last_child) {
            parent->last_child->next_sibling = child;
        } else {
            parent->first_child = child;
        }

        parent->last_child = child;
    }

    parent->child_count++;
    parent->needs_layout = true;
}

/// @brief Widget remove child.
void vg_widget_remove_child(vg_widget_t *parent, vg_widget_t *child) {
    if (!parent || !child || child->parent != parent)
        return;

    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else {
        parent->last_child = child->prev_sibling;
    }

    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;

    parent->child_count--;
    parent->needs_layout = true;
}

/// @brief Widget clear children.
void vg_widget_clear_children(vg_widget_t *parent) {
    if (!parent)
        return;

    vg_widget_t *child = parent->first_child;
    while (child) {
        vg_widget_t *next = child->next_sibling;
        child->parent = NULL;
        child->prev_sibling = NULL;
        child->next_sibling = NULL;
        child = next;
    }

    parent->first_child = NULL;
    parent->last_child = NULL;
    parent->child_count = 0;
    parent->needs_layout = true;
}

vg_widget_t *vg_widget_get_child(vg_widget_t *parent, int index) {
    if (!parent || index < 0 || index >= parent->child_count)
        return NULL;

    vg_widget_t *child = parent->first_child;
    for (int i = 0; i < index && child; i++) {
        child = child->next_sibling;
    }

    return child;
}

vg_widget_t *vg_widget_find_by_name(vg_widget_t *root, const char *name) {
    if (!root || !name)
        return NULL;

    if (root->name && strcmp(root->name, name) == 0) {
        return root;
    }

    VG_FOREACH_CHILD(root, child) {
        vg_widget_t *found = vg_widget_find_by_name(child, name);
        if (found)
            return found;
    }

    return NULL;
}

vg_widget_t *vg_widget_find_by_id(vg_widget_t *root, uint32_t id) {
    if (!root)
        return NULL;

    if (root->id == id) {
        return root;
    }

    VG_FOREACH_CHILD(root, child) {
        vg_widget_t *found = vg_widget_find_by_id(child, id);
        if (found)
            return found;
    }

    return NULL;
}

//=============================================================================
// Geometry & Constraints
//=============================================================================

void vg_widget_set_constraints(vg_widget_t *widget, vg_constraints_t constraints) {
    if (!widget)
        return;
    widget->constraints = constraints;
    widget->needs_layout = true;
}

/// @brief Widget set min size.
void vg_widget_set_min_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    widget->constraints.min_width = width;
    widget->constraints.min_height = height;
    widget->needs_layout = true;
}

/// @brief Widget set max size.
void vg_widget_set_max_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    widget->constraints.max_width = width;
    widget->constraints.max_height = height;
    widget->needs_layout = true;
}

/// @brief Widget set preferred size.
void vg_widget_set_preferred_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    widget->constraints.preferred_width = width;
    widget->constraints.preferred_height = height;
    widget->needs_layout = true;
}

/// @brief Widget set fixed size.
void vg_widget_set_fixed_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    widget->constraints.min_width = width;
    widget->constraints.max_width = width;
    widget->constraints.preferred_width = width;
    widget->constraints.min_height = height;
    widget->constraints.max_height = height;
    widget->constraints.preferred_height = height;
    widget->needs_layout = true;
}

/// @brief Widget get bounds.
void vg_widget_get_bounds(vg_widget_t *widget, float *x, float *y, float *width, float *height) {
    if (!widget)
        return;
    if (x)
        *x = widget->x;
    if (y)
        *y = widget->y;
    if (width)
        *width = widget->width;
    if (height)
        *height = widget->height;
}

/// @brief Widget get screen bounds.
void vg_widget_get_screen_bounds(
    vg_widget_t *widget, float *x, float *y, float *width, float *height) {
    if (!widget)
        return;

    float sx = widget->x;
    float sy = widget->y;

    // Child x/y are already stored relative to the arranged content origin of
    // their parent, so screen conversion only adds ancestor positions.
    vg_widget_t *p = widget->parent;
    while (p) {
        sx += p->x;
        sy += p->y;
        p = p->parent;
    }

    if (x)
        *x = sx;
    if (y)
        *y = sy;
    if (width)
        *width = widget->width;
    if (height)
        *height = widget->height;
}

//=============================================================================
// Layout Parameters
//=============================================================================

void vg_widget_set_flex(vg_widget_t *widget, float flex) {
    if (!widget)
        return;
    widget->layout.flex = flex;
    if (widget->parent)
        widget->parent->needs_layout = true;
}

/// @brief Widget set margin.
void vg_widget_set_margin(vg_widget_t *widget, float margin) {
    if (!widget)
        return;
    widget->layout.margin_left = margin;
    widget->layout.margin_top = margin;
    widget->layout.margin_right = margin;
    widget->layout.margin_bottom = margin;
    if (widget->parent)
        widget->parent->needs_layout = true;
}

/// @brief Widget set margins.
void vg_widget_set_margins(vg_widget_t *widget, float left, float top, float right, float bottom) {
    if (!widget)
        return;
    widget->layout.margin_left = left;
    widget->layout.margin_top = top;
    widget->layout.margin_right = right;
    widget->layout.margin_bottom = bottom;
    if (widget->parent)
        widget->parent->needs_layout = true;
}

/// @brief Widget set padding.
void vg_widget_set_padding(vg_widget_t *widget, float padding) {
    if (!widget)
        return;
    widget->layout.padding_left = padding;
    widget->layout.padding_top = padding;
    widget->layout.padding_right = padding;
    widget->layout.padding_bottom = padding;
    widget->needs_layout = true;
}

/// @brief Widget set paddings.
void vg_widget_set_paddings(vg_widget_t *widget, float left, float top, float right, float bottom) {
    if (!widget)
        return;
    widget->layout.padding_left = left;
    widget->layout.padding_top = top;
    widget->layout.padding_right = right;
    widget->layout.padding_bottom = bottom;
    widget->needs_layout = true;
}

//=============================================================================
// State Management
//=============================================================================

void vg_widget_set_enabled(vg_widget_t *widget, bool enabled) {
    if (!widget)
        return;
    widget->enabled = enabled;
    if (enabled) {
        widget->state &= ~VG_STATE_DISABLED;
    } else {
        widget->state |= VG_STATE_DISABLED;
        if (g_focused_widget && widget_is_ancestor(widget, g_focused_widget)) {
            vg_widget_set_focus(NULL);
        }
        if (g_input_capture_widget && widget_is_ancestor(widget, g_input_capture_widget)) {
            g_input_capture_widget = NULL;
        }
        if (g_hovered_widget && widget_is_ancestor(widget, g_hovered_widget)) {
            g_hovered_widget = NULL;
        }
        if (g_modal_root && widget_is_ancestor(widget, g_modal_root)) {
            g_modal_root = NULL;
        }
        vg_tooltip_manager_widget_hidden(widget);
        clear_interactive_state_recursive(widget);
    }
    widget->needs_paint = true;
}

bool vg_widget_is_enabled(vg_widget_t *widget) {
    return widget && widget->enabled;
}

/// @brief Widget set visible.
void vg_widget_set_visible(vg_widget_t *widget, bool visible) {
    if (!widget)
        return;
    if (widget->visible == visible)
        return;

    widget->visible = visible;
    if (!visible) {
        if (g_focused_widget && widget_is_ancestor(widget, g_focused_widget)) {
            vg_widget_set_focus(NULL);
        }
        if (g_input_capture_widget && widget_is_ancestor(widget, g_input_capture_widget)) {
            g_input_capture_widget = NULL;
        }
        if (g_hovered_widget && widget_is_ancestor(widget, g_hovered_widget)) {
            g_hovered_widget = NULL;
        }
        if (g_modal_root && widget_is_ancestor(widget, g_modal_root)) {
            g_modal_root = NULL;
        }
        vg_tooltip_manager_widget_hidden(widget);
        clear_interactive_state_recursive(widget);
    }
    if (widget->parent)
        widget->parent->needs_layout = true;
    widget->needs_paint = true;
}

bool vg_widget_is_visible(vg_widget_t *widget) {
    return widget && widget->visible;
}

bool vg_widget_has_state(vg_widget_t *widget, vg_widget_state_t state) {
    return widget && (widget->state & state);
}

/// @brief Widget set name.
void vg_widget_set_name(vg_widget_t *widget, const char *name) {
    if (!widget)
        return;

    if (widget->name) {
        free(widget->name);
        widget->name = NULL;
    }

    if (name) {
        widget->name = strdup(name);
    }
}

const char *vg_widget_get_name(vg_widget_t *widget) {
    return widget ? widget->name : NULL;
}

//=============================================================================
// Layout & Rendering
//=============================================================================

void vg_widget_measure(vg_widget_t *root, float available_width, float available_height) {
    if (!root || !root->visible)
        return;

    bool recurse_children =
        !root->vtable || !root->vtable->measure || root->vtable->measure == default_measure;
    if (recurse_children) {
        VG_FOREACH_VISIBLE_CHILD(root, child) {
            vg_widget_measure(child, available_width, available_height);
        }
    }

    // Then measure this widget
    if (root->vtable && root->vtable->measure) {
        root->vtable->measure(root, available_width, available_height);
    }
}

/// @brief Widget arrange.
void vg_widget_arrange(vg_widget_t *root, float x, float y, float width, float height) {
    if (!root || !root->visible)
        return;

    if (root->vtable && root->vtable->arrange) {
        // Custom arrange functions (VBox, HBox, SplitPane, default containers)
        // handle positioning self AND children.
        root->vtable->arrange(root, x, y, width, height);
    } else {
        // Widgets without arrange (MenuBar, Toolbar, StatusBar, etc.)
        // just need their position and size set.
        root->x = x;
        root->y = y;
        root->width = width;
        root->height = height;
    }

    root->needs_layout = false;
}

/// @brief Widget layout.
void vg_widget_layout(vg_widget_t *root, float available_width, float available_height) {
    vg_widget_measure(root, available_width, available_height);
    vg_widget_arrange(root, 0, 0, available_width, available_height);
}

/// @brief Widget paint.
void vg_widget_paint(vg_widget_t *root, void *canvas) {
    if (!root || !root->visible || !canvas)
        return;

    paint_widget_normal_tree(root, canvas);
    paint_widget_overlay_tree(root, canvas);

    root->needs_paint = false;
}

/// @brief Widget invalidate.
void vg_widget_invalidate(vg_widget_t *widget) {
    if (!widget)
        return;
    widget->needs_paint = true;

    // Also invalidate parent chain (for clipping regions)
    vg_widget_t *p = widget->parent;
    while (p) {
        p->needs_paint = true;
        p = p->parent;
    }
}

/// @brief Widget invalidate layout.
void vg_widget_invalidate_layout(vg_widget_t *widget) {
    if (!widget)
        return;
    widget->needs_layout = true;
    widget->needs_paint = true;

    // Also invalidate parent chain
    vg_widget_t *p = widget->parent;
    while (p) {
        p->needs_layout = true;
        p->needs_paint = true;
        p = p->parent;
    }
}

//=============================================================================
// Hit Testing
//=============================================================================

vg_widget_t *vg_widget_hit_test(vg_widget_t *root, float x, float y) {
    if (!root || !root->visible || !root->enabled)
        return NULL;

    // Get screen bounds
    float sx, sy, sw, sh;
    vg_widget_get_screen_bounds(root, &sx, &sy, &sw, &sh);

    // Check if point is inside
    if (!point_in_rect(x, y, sx, sy, sw, sh) || !widget_point_within_ancestor_clips(root, x, y)) {
        return NULL;
    }

    // Containers like scrollviews paint children clipped to a smaller content
    // rect than their full bounds (the scrollbar gutter is excluded). Without
    // this clip, a click on the scrollbar gutter could route to a child whose
    // bounds happen to extend underneath, bypassing the scrollview's own
    // handle_event for scrollbar dragging.
    float child_clip_x = sx;
    float child_clip_y = sy;
    float child_clip_w = sw;
    float child_clip_h = sh;
    if (root->type == VG_WIDGET_SCROLLVIEW) {
        const vg_scrollview_t *scroll = (const vg_scrollview_t *)root;
        if (scroll->show_v_scrollbar)
            child_clip_w -= scroll->scrollbar_width;
        if (scroll->show_h_scrollbar)
            child_clip_h -= scroll->scrollbar_width;
        if (child_clip_w < 0.0f)
            child_clip_w = 0.0f;
        if (child_clip_h < 0.0f)
            child_clip_h = 0.0f;
    }
    bool descend = (x >= child_clip_x && x < child_clip_x + child_clip_w &&
                    y >= child_clip_y && y < child_clip_y + child_clip_h);

    // Check children in reverse order (topmost first)
    if (descend) {
        for (vg_widget_t *child = root->last_child; child; child = child->prev_sibling) {
            vg_widget_t *hit = vg_widget_hit_test(child, x, y);
            if (hit)
                return hit;
        }
    }

    return root;
}

bool vg_widget_contains_point(vg_widget_t *widget, float x, float y) {
    if (!widget)
        return false;

    float sx, sy, sw, sh;
    vg_widget_get_screen_bounds(widget, &sx, &sy, &sw, &sh);

    return point_in_rect(x, y, sx, sy, sw, sh) && widget_point_within_ancestor_clips(widget, x, y);
}

//=============================================================================
// Input Capture
//=============================================================================

void vg_widget_set_input_capture(vg_widget_t *widget) {
    g_input_capture_widget = widget;
}

/// @brief Widget release input capture.
void vg_widget_release_input_capture(void) {
    g_input_capture_widget = NULL;
}

vg_widget_t *vg_widget_get_input_capture(void) {
    return g_input_capture_widget;
}

void vg_widget_get_runtime_state(vg_widget_runtime_state_t *state) {
    if (!state)
        return;
    state->focused_widget = g_focused_widget;
    state->input_capture_widget = g_input_capture_widget;
    state->modal_root = g_modal_root;
    state->hovered_widget = g_hovered_widget;
}

void vg_widget_set_runtime_state(const vg_widget_runtime_state_t *state) {
    if (!state) {
        g_focused_widget = NULL;
        g_input_capture_widget = NULL;
        g_modal_root = NULL;
        g_hovered_widget = NULL;
        return;
    }
    g_focused_widget = state->focused_widget;
    g_input_capture_widget = state->input_capture_widget;
    g_modal_root = state->modal_root;
    g_hovered_widget = state->hovered_widget;
}

//=============================================================================
// Focus Management
//=============================================================================

void vg_widget_set_focus(vg_widget_t *widget) {
    if (!widget) {
        if (g_focused_widget) {
            if (g_focused_widget->vtable && g_focused_widget->vtable->on_focus) {
                g_focused_widget->vtable->on_focus(g_focused_widget, false);
            }
            g_focused_widget->state &= ~VG_STATE_FOCUSED;
            g_focused_widget->needs_paint = true;
            g_focused_widget = NULL;
        }
        return;
    }
    if (!widget->enabled || !widget->visible)
        return;
    if (widget->vtable && widget->vtable->can_focus && !widget->vtable->can_focus(widget)) {
        return;
    }

    // Unfocus previous widget
    if (g_focused_widget && g_focused_widget != widget) {
        if (g_focused_widget->vtable && g_focused_widget->vtable->on_focus) {
            g_focused_widget->vtable->on_focus(g_focused_widget, false);
        }
        g_focused_widget->state &= ~VG_STATE_FOCUSED;
        g_focused_widget->needs_paint = true;
    }

    // Focus new widget
    g_focused_widget = widget;
    widget->state |= VG_STATE_FOCUSED;
    widget->needs_paint = true;

    if (widget->vtable && widget->vtable->on_focus) {
        widget->vtable->on_focus(widget, true);
    }
}

vg_widget_t *vg_widget_get_focused(vg_widget_t *root) {
    (void)root; // Currently using global focus
    return g_focused_widget;
}

// Maximum focusable widgets tracked for tab-order navigation.
#define TAB_ORDER_MAX 512

// Collect all focusable, visible, enabled descendants into arr[].
// Returns the number of widgets collected.
static int collect_focusable(vg_widget_t *root, vg_widget_t **arr, int max) {
    if (!root)
        return 0;

    int count = 0;
    for (vg_widget_t *child = root->first_child; child; child = child->next_sibling) {
        if (!child->visible || !child->enabled)
            continue;

        if (child->vtable && child->vtable->can_focus && child->vtable->can_focus(child)) {
            if (count < max)
                arr[count++] = child;
        }

        int sub = collect_focusable(child, arr + count, max - count);
        count += sub;
    }
    return count;
}

// Stable merge sort for tab order — O(n log n), preserves DFS order for equal keys.
// Uses a temporary scratch buffer of the same size.
static void tab_merge(vg_widget_t **arr, vg_widget_t **tmp, int lo, int mid, int hi) {
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        const int ia = arr[i]->tab_index;
        const int ib = arr[j]->tab_index;
        // Natural-order (-1) sorts after explicit (>=0)
        bool left_wins;
        if (ia >= 0 && ib < 0)
            left_wins = true;
        else if (ia < 0 && ib >= 0)
            left_wins = false;
        else if (ia >= 0 && ib >= 0)
            left_wins = ia <= ib; // stable: equal goes left
        else
            left_wins = true; // both natural-order: preserve DFS
        tmp[k++] = left_wins ? arr[i++] : arr[j++];
    }
    while (i < mid)
        tmp[k++] = arr[i++];
    while (j < hi)
        tmp[k++] = arr[j++];
    for (int m = lo; m < hi; m++)
        arr[m] = tmp[m];
}

static void tab_merge_sort(vg_widget_t **arr, vg_widget_t **tmp, int lo, int hi) {
    if (hi - lo <= 1)
        return;
    int mid = (lo + hi) / 2;
    tab_merge_sort(arr, tmp, lo, mid);
    tab_merge_sort(arr, tmp, mid, hi);
    tab_merge(arr, tmp, lo, mid, hi);
}

// Build sorted tab-order array for the widget tree rooted at root.
// Returns the number of focusable widgets found (0..TAB_ORDER_MAX).
static int build_tab_order(vg_widget_t *root, vg_widget_t **arr) {
    int count = collect_focusable(root, arr, TAB_ORDER_MAX);

    if (count > 1) {
        // Stable merge sort by tab_index — O(n log n) vs previous O(n²) insertion sort.
        vg_widget_t **tmp = malloc(count * sizeof(vg_widget_t *));
        if (tmp) {
            tab_merge_sort(arr, tmp, 0, count);
            free(tmp);
        }
        // If malloc fails, order is preserved from DFS (still usable, just unsorted)
    }

    return count;
}

/// @brief Widget focus next.
void vg_widget_focus_next(vg_widget_t *root) {
    if (!root)
        return;

    vg_widget_t *arr[TAB_ORDER_MAX];
    int count = build_tab_order(root, arr);
    if (count == 0)
        return;

    // Find current focused widget in the sorted list
    int cur = -1;
    for (int i = 0; i < count; i++) {
        if (arr[i] == g_focused_widget) {
            cur = i;
            break;
        }
    }

    // Advance to next (with wraparound)
    int next = (cur + 1) % count;
    vg_widget_set_focus(arr[next]);
}

/// @brief Widget focus prev.
void vg_widget_focus_prev(vg_widget_t *root) {
    if (!root)
        return;

    vg_widget_t *arr[TAB_ORDER_MAX];
    int count = build_tab_order(root, arr);
    if (count == 0)
        return;

    // Find current focused widget in the sorted list
    int cur = -1;
    for (int i = 0; i < count; i++) {
        if (arr[i] == g_focused_widget) {
            cur = i;
            break;
        }
    }

    // Step back (with wraparound)
    int prev = (cur <= 0) ? (count - 1) : (cur - 1);
    vg_widget_set_focus(arr[prev]);
}

//=============================================================================
// Tab Index
//=============================================================================

void vg_widget_set_tab_index(vg_widget_t *widget, int tab_index) {
    if (!widget)
        return;
    widget->tab_index = tab_index;
}

//=============================================================================
// Modal Root
//=============================================================================

void vg_widget_set_modal_root(vg_widget_t *widget) {
    g_modal_root = widget;
}

vg_widget_t *vg_widget_get_modal_root(void) {
    return g_modal_root;
}
