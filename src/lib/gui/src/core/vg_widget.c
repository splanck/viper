// vg_widget.c - Widget base class implementation
#include "../../include/vg_widget.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Global State
//=============================================================================

static uint32_t g_next_widget_id = 1;
static vg_widget_t* g_focused_widget = NULL;

//=============================================================================
// ID Generation
//=============================================================================

uint32_t vg_widget_next_id(void) {
    return g_next_widget_id++;
}

//=============================================================================
// Default VTable Functions
//=============================================================================

static void default_destroy(vg_widget_t* self) {
    // Default: do nothing (subclass should clean up impl_data)
}

static void default_measure(vg_widget_t* self, float available_width, float available_height) {
    // Default: use preferred size or measure children
    float w = self->constraints.preferred_width;
    float h = self->constraints.preferred_height;

    // If no preferred size, measure children
    if (w == 0) w = self->constraints.min_width;
    if (h == 0) h = self->constraints.min_height;

    self->measured_width = w;
    self->measured_height = h;
}

static void default_arrange(vg_widget_t* self, float x, float y, float width, float height) {
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
}

static void default_paint(vg_widget_t* self, void* canvas) {
    // Default: paint nothing (container just paints children)
}

static bool default_handle_event(vg_widget_t* self, vg_event_t* event) {
    return false;  // Not handled
}

static bool default_can_focus(vg_widget_t* self) {
    return false;  // Most widgets can't focus by default
}

static void default_on_focus(vg_widget_t* self, bool gained) {
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

//=============================================================================
// Widget Initialization
//=============================================================================

void vg_widget_init(vg_widget_t* widget, vg_widget_type_t type, const vg_widget_vtable_t* vtable) {
    if (!widget) return;

    memset(widget, 0, sizeof(vg_widget_t));

    widget->type = type;
    widget->vtable = vtable ? vtable : &g_default_vtable;
    widget->id = vg_widget_next_id();
    widget->visible = true;
    widget->enabled = true;
    widget->needs_layout = true;
    widget->needs_paint = true;
}

//=============================================================================
// Widget Creation/Destruction
//=============================================================================

vg_widget_t* vg_widget_create(vg_widget_type_t type) {
    vg_widget_t* widget = calloc(1, sizeof(vg_widget_t));
    if (!widget) return NULL;

    vg_widget_init(widget, type, NULL);

    return widget;
}

void vg_widget_destroy(vg_widget_t* widget) {
    if (!widget) return;

    // Recursively destroy children
    vg_widget_t* child = widget->first_child;
    while (child) {
        vg_widget_t* next = child->next_sibling;
        vg_widget_destroy(child);
        child = next;
    }

    // Call type-specific destructor
    if (widget->vtable && widget->vtable->destroy) {
        widget->vtable->destroy(widget);
    }

    // Free impl data if allocated
    if (widget->impl_data) {
        free(widget->impl_data);
    }

    // Free name
    if (widget->name) {
        free(widget->name);
    }

    // Clear focused widget if this is it
    if (g_focused_widget == widget) {
        g_focused_widget = NULL;
    }

    free(widget);
}

//=============================================================================
// Hierarchy Management
//=============================================================================

void vg_widget_add_child(vg_widget_t* parent, vg_widget_t* child) {
    if (!parent || !child) return;

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

void vg_widget_insert_child(vg_widget_t* parent, vg_widget_t* child, int index) {
    if (!parent || !child || index < 0) return;

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
    vg_widget_t* at = parent->first_child;
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

void vg_widget_remove_child(vg_widget_t* parent, vg_widget_t* child) {
    if (!parent || !child || child->parent != parent) return;

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

void vg_widget_clear_children(vg_widget_t* parent) {
    if (!parent) return;

    vg_widget_t* child = parent->first_child;
    while (child) {
        vg_widget_t* next = child->next_sibling;
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

vg_widget_t* vg_widget_get_child(vg_widget_t* parent, int index) {
    if (!parent || index < 0 || index >= parent->child_count) return NULL;

    vg_widget_t* child = parent->first_child;
    for (int i = 0; i < index && child; i++) {
        child = child->next_sibling;
    }

    return child;
}

vg_widget_t* vg_widget_find_by_name(vg_widget_t* root, const char* name) {
    if (!root || !name) return NULL;

    if (root->name && strcmp(root->name, name) == 0) {
        return root;
    }

    for (vg_widget_t* child = root->first_child; child; child = child->next_sibling) {
        vg_widget_t* found = vg_widget_find_by_name(child, name);
        if (found) return found;
    }

    return NULL;
}

vg_widget_t* vg_widget_find_by_id(vg_widget_t* root, uint32_t id) {
    if (!root) return NULL;

    if (root->id == id) {
        return root;
    }

    for (vg_widget_t* child = root->first_child; child; child = child->next_sibling) {
        vg_widget_t* found = vg_widget_find_by_id(child, id);
        if (found) return found;
    }

    return NULL;
}

//=============================================================================
// Geometry & Constraints
//=============================================================================

void vg_widget_set_constraints(vg_widget_t* widget, vg_constraints_t constraints) {
    if (!widget) return;
    widget->constraints = constraints;
    widget->needs_layout = true;
}

void vg_widget_set_min_size(vg_widget_t* widget, float width, float height) {
    if (!widget) return;
    widget->constraints.min_width = width;
    widget->constraints.min_height = height;
    widget->needs_layout = true;
}

void vg_widget_set_max_size(vg_widget_t* widget, float width, float height) {
    if (!widget) return;
    widget->constraints.max_width = width;
    widget->constraints.max_height = height;
    widget->needs_layout = true;
}

void vg_widget_set_preferred_size(vg_widget_t* widget, float width, float height) {
    if (!widget) return;
    widget->constraints.preferred_width = width;
    widget->constraints.preferred_height = height;
    widget->needs_layout = true;
}

void vg_widget_set_fixed_size(vg_widget_t* widget, float width, float height) {
    if (!widget) return;
    widget->constraints.min_width = width;
    widget->constraints.max_width = width;
    widget->constraints.preferred_width = width;
    widget->constraints.min_height = height;
    widget->constraints.max_height = height;
    widget->constraints.preferred_height = height;
    widget->needs_layout = true;
}

void vg_widget_get_bounds(vg_widget_t* widget, float* x, float* y, float* width, float* height) {
    if (!widget) return;
    if (x) *x = widget->x;
    if (y) *y = widget->y;
    if (width) *width = widget->width;
    if (height) *height = widget->height;
}

void vg_widget_get_screen_bounds(vg_widget_t* widget, float* x, float* y, float* width, float* height) {
    if (!widget) return;

    float sx = widget->x;
    float sy = widget->y;

    // Walk up the parent chain to get screen coordinates
    vg_widget_t* p = widget->parent;
    while (p) {
        sx += p->x + p->layout.padding_left;
        sy += p->y + p->layout.padding_top;
        p = p->parent;
    }

    if (x) *x = sx;
    if (y) *y = sy;
    if (width) *width = widget->width;
    if (height) *height = widget->height;
}

//=============================================================================
// Layout Parameters
//=============================================================================

void vg_widget_set_flex(vg_widget_t* widget, float flex) {
    if (!widget) return;
    widget->layout.flex = flex;
    if (widget->parent) widget->parent->needs_layout = true;
}

void vg_widget_set_margin(vg_widget_t* widget, float margin) {
    if (!widget) return;
    widget->layout.margin_left = margin;
    widget->layout.margin_top = margin;
    widget->layout.margin_right = margin;
    widget->layout.margin_bottom = margin;
    if (widget->parent) widget->parent->needs_layout = true;
}

void vg_widget_set_margins(vg_widget_t* widget, float left, float top, float right, float bottom) {
    if (!widget) return;
    widget->layout.margin_left = left;
    widget->layout.margin_top = top;
    widget->layout.margin_right = right;
    widget->layout.margin_bottom = bottom;
    if (widget->parent) widget->parent->needs_layout = true;
}

void vg_widget_set_padding(vg_widget_t* widget, float padding) {
    if (!widget) return;
    widget->layout.padding_left = padding;
    widget->layout.padding_top = padding;
    widget->layout.padding_right = padding;
    widget->layout.padding_bottom = padding;
    widget->needs_layout = true;
}

void vg_widget_set_paddings(vg_widget_t* widget, float left, float top, float right, float bottom) {
    if (!widget) return;
    widget->layout.padding_left = left;
    widget->layout.padding_top = top;
    widget->layout.padding_right = right;
    widget->layout.padding_bottom = bottom;
    widget->needs_layout = true;
}

//=============================================================================
// State Management
//=============================================================================

void vg_widget_set_enabled(vg_widget_t* widget, bool enabled) {
    if (!widget) return;
    widget->enabled = enabled;
    if (enabled) {
        widget->state &= ~VG_STATE_DISABLED;
    } else {
        widget->state |= VG_STATE_DISABLED;
    }
    widget->needs_paint = true;
}

bool vg_widget_is_enabled(vg_widget_t* widget) {
    return widget && widget->enabled;
}

void vg_widget_set_visible(vg_widget_t* widget, bool visible) {
    if (!widget) return;
    widget->visible = visible;
    if (widget->parent) widget->parent->needs_layout = true;
    widget->needs_paint = true;
}

bool vg_widget_is_visible(vg_widget_t* widget) {
    return widget && widget->visible;
}

bool vg_widget_has_state(vg_widget_t* widget, vg_widget_state_t state) {
    return widget && (widget->state & state);
}

void vg_widget_set_name(vg_widget_t* widget, const char* name) {
    if (!widget) return;

    if (widget->name) {
        free(widget->name);
        widget->name = NULL;
    }

    if (name) {
        widget->name = strdup(name);
    }
}

const char* vg_widget_get_name(vg_widget_t* widget) {
    return widget ? widget->name : NULL;
}

//=============================================================================
// Layout & Rendering
//=============================================================================

void vg_widget_measure(vg_widget_t* root, float available_width, float available_height) {
    if (!root || !root->visible) return;

    // Measure children first
    for (vg_widget_t* child = root->first_child; child; child = child->next_sibling) {
        if (child->visible) {
            vg_widget_measure(child, available_width, available_height);
        }
    }

    // Then measure this widget
    if (root->vtable && root->vtable->measure) {
        root->vtable->measure(root, available_width, available_height);
    }
}

void vg_widget_arrange(vg_widget_t* root, float x, float y, float width, float height) {
    if (!root || !root->visible) return;

    // Arrange this widget
    if (root->vtable && root->vtable->arrange) {
        root->vtable->arrange(root, x, y, width, height);
    }

    // Arrange children (this is usually overridden by layout containers)
    float cx = root->layout.padding_left;
    float cy = root->layout.padding_top;

    for (vg_widget_t* child = root->first_child; child; child = child->next_sibling) {
        if (child->visible) {
            vg_widget_arrange(child,
                              cx + child->layout.margin_left,
                              cy + child->layout.margin_top,
                              child->measured_width,
                              child->measured_height);
        }
    }

    root->needs_layout = false;
}

void vg_widget_layout(vg_widget_t* root, float available_width, float available_height) {
    vg_widget_measure(root, available_width, available_height);
    vg_widget_arrange(root, 0, 0, available_width, available_height);
}

void vg_widget_paint(vg_widget_t* root, void* canvas) {
    if (!root || !root->visible || !canvas) return;

    // Paint this widget
    if (root->vtable && root->vtable->paint) {
        root->vtable->paint(root, canvas);
    }

    // Paint children
    for (vg_widget_t* child = root->first_child; child; child = child->next_sibling) {
        vg_widget_paint(child, canvas);
    }

    root->needs_paint = false;
}

void vg_widget_invalidate(vg_widget_t* widget) {
    if (!widget) return;
    widget->needs_paint = true;

    // Also invalidate parent chain (for clipping regions)
    vg_widget_t* p = widget->parent;
    while (p) {
        p->needs_paint = true;
        p = p->parent;
    }
}

void vg_widget_invalidate_layout(vg_widget_t* widget) {
    if (!widget) return;
    widget->needs_layout = true;
    widget->needs_paint = true;

    // Also invalidate parent chain
    vg_widget_t* p = widget->parent;
    while (p) {
        p->needs_layout = true;
        p->needs_paint = true;
        p = p->parent;
    }
}

//=============================================================================
// Hit Testing
//=============================================================================

vg_widget_t* vg_widget_hit_test(vg_widget_t* root, float x, float y) {
    if (!root || !root->visible || !root->enabled) return NULL;

    // Get screen bounds
    float sx, sy, sw, sh;
    vg_widget_get_screen_bounds(root, &sx, &sy, &sw, &sh);

    // Check if point is inside
    if (x < sx || x >= sx + sw || y < sy || y >= sy + sh) {
        return NULL;
    }

    // Check children in reverse order (topmost first)
    for (vg_widget_t* child = root->last_child; child; child = child->prev_sibling) {
        vg_widget_t* hit = vg_widget_hit_test(child, x, y);
        if (hit) return hit;
    }

    return root;
}

bool vg_widget_contains_point(vg_widget_t* widget, float x, float y) {
    if (!widget) return false;

    float sx, sy, sw, sh;
    vg_widget_get_screen_bounds(widget, &sx, &sy, &sw, &sh);

    return x >= sx && x < sx + sw && y >= sy && y < sy + sh;
}

//=============================================================================
// Focus Management
//=============================================================================

void vg_widget_set_focus(vg_widget_t* widget) {
    if (!widget) return;
    if (!widget->enabled || !widget->visible) return;
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

vg_widget_t* vg_widget_get_focused(vg_widget_t* root) {
    (void)root;  // Currently using global focus
    return g_focused_widget;
}

static vg_widget_t* find_next_focusable(vg_widget_t* root, vg_widget_t* after, bool* found) {
    if (!root) return NULL;

    for (vg_widget_t* child = root->first_child; child; child = child->next_sibling) {
        if (!child->visible || !child->enabled) continue;

        if (*found) {
            // Looking for next focusable after the 'after' widget
            if (child->vtable && child->vtable->can_focus && child->vtable->can_focus(child)) {
                return child;
            }
        } else if (child == after) {
            *found = true;
        }

        // Recurse into children
        vg_widget_t* next = find_next_focusable(child, after, found);
        if (next) return next;
    }

    return NULL;
}

void vg_widget_focus_next(vg_widget_t* root) {
    if (!root) return;

    bool found = (g_focused_widget == NULL);
    vg_widget_t* next = find_next_focusable(root, g_focused_widget, &found);

    if (!next && g_focused_widget) {
        // Wrap around to beginning
        found = true;
        next = find_next_focusable(root, NULL, &found);
    }

    if (next) {
        vg_widget_set_focus(next);
    }
}

void vg_widget_focus_prev(vg_widget_t* root) {
    // For simplicity, collect all focusable widgets and find previous
    // A more efficient implementation would walk the tree backwards
    if (!root) return;

    // For now, just focus next (proper implementation would be backwards)
    vg_widget_focus_next(root);
}
