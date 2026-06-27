//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/core/vg_widget.c
// Purpose: Widget base class implementation — creation, parenting, destruction,
//          two-pass layout dispatch, hit-testing, focus, and modal-root API.
// Key invariants:
//   - A widget's parent pointer is always consistent with its siblings list.
//   - vg_widget_destroy recursively destroys all children before the parent.
//   - Widget IDs are assigned from a monotonically increasing global counter.
// Ownership/Lifetime:
//   - The base widget owns its name string (vg_strdup'd on creation).
//   - impl_data ownership depends on the widget subtype's vtable destroy().
// Links: lib/gui/include/vg_widget.h,
//        lib/gui/include/vg_layout.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../include/vg_widget.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_layout.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <limits.h>
#include <math.h>
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

static uint64_t g_next_widget_id = 1;
#define VG_WIDGET_MAGIC UINT64_C(0x5647505749444745)
#define VG_WIDGET_DESTROYED_MAGIC UINT64_C(0x5647505744524F50)
static vg_widget_t *g_focused_widget = NULL;
static vg_widget_t *g_input_capture_widget = NULL;
static vg_widget_t *g_modal_root = NULL;
static vg_widget_t *g_hovered_widget = NULL;
static vg_widget_t *g_last_click_widget = NULL;
static uint64_t g_last_click_time_ms = 0;
static int32_t g_last_click_button = -1;
static int32_t g_last_click_count = 0;
static float g_last_click_screen_x = 0.0f;
static float g_last_click_screen_y = 0.0f;
static vg_widget_t *g_reported_click_widget = NULL;
static uint64_t g_reported_click_time_ms = 0;
static vg_widget_t *g_live_widgets = NULL;
static vg_widget_t **g_live_widget_table = NULL;
static size_t g_live_widget_table_cap = 0;
static size_t g_live_widget_table_count = 0;

#define VG_WIDGET_LIVE_TABLE_MIN_CAP 256u
#define VG_WIDGET_LIVE_TABLE_TOMBSTONE ((vg_widget_t *)(uintptr_t)UINTPTR_MAX)

/// @brief Hash a widget pointer for the open-addressed live-widget table.
/// @param widget Widget pointer to hash.
/// @return Mixed pointer hash suitable for power-of-two table capacities.
static size_t widget_live_hash(const vg_widget_t *widget) {
    uintptr_t x = (uintptr_t)widget >> 4;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x;
}

/// @brief Insert @p widget into an already-allocated live-widget table.
/// @param table Destination table.
/// @param cap Power-of-two table capacity.
/// @param widget Live widget pointer to insert.
static void widget_live_table_insert_raw(vg_widget_t **table, size_t cap, vg_widget_t *widget) {
    size_t mask = cap - 1u;
    size_t index = widget_live_hash(widget) & mask;
    while (table[index] && table[index] != VG_WIDGET_LIVE_TABLE_TOMBSTONE)
        index = (index + 1u) & mask;
    table[index] = widget;
}

/// @brief Rebuild the live-widget hash table with at least @p new_cap slots.
/// @details The doubly-linked live list remains authoritative; allocation
///          failure leaves the old table in place or disables the hash table so
///          vg_widget_is_live can safely fall back to the list.
/// @param new_cap Requested table capacity.
/// @return true when the table was rebuilt, false on allocation failure.
static bool widget_live_table_rehash(size_t new_cap) {
    if (new_cap < VG_WIDGET_LIVE_TABLE_MIN_CAP)
        new_cap = VG_WIDGET_LIVE_TABLE_MIN_CAP;
    size_t cap = 1u;
    while (cap < new_cap) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }

    vg_widget_t **table = (vg_widget_t **)calloc(cap, sizeof(*table));
    if (!table)
        return false;
    size_t count = 0;
    for (vg_widget_t *live = g_live_widgets; live; live = live->_live_next) {
        widget_live_table_insert_raw(table, cap, live);
        count++;
    }
    free(g_live_widget_table);
    g_live_widget_table = table;
    g_live_widget_table_cap = cap;
    g_live_widget_table_count = count;
    return true;
}

/// @brief Insert @p widget into the hash registry, growing when needed.
/// @param widget Widget to insert.
static void widget_live_table_insert(vg_widget_t *widget) {
    if (!widget)
        return;
    if (!g_live_widget_table ||
        (g_live_widget_table_count + 1u) * 4u >= g_live_widget_table_cap * 3u) {
        size_t requested =
            g_live_widget_table_cap ? g_live_widget_table_cap * 2u : VG_WIDGET_LIVE_TABLE_MIN_CAP;
        if (!widget_live_table_rehash(requested)) {
            free(g_live_widget_table);
            g_live_widget_table = NULL;
            g_live_widget_table_cap = 0;
            g_live_widget_table_count = 0;
            return;
        }
    }
    widget_live_table_insert_raw(g_live_widget_table, g_live_widget_table_cap, widget);
    g_live_widget_table_count++;
}

/// @brief Remove @p widget from the hash registry if the table is active.
/// @param widget Widget to remove.
static void widget_live_table_remove(vg_widget_t *widget) {
    if (!widget || !g_live_widget_table || g_live_widget_table_cap == 0)
        return;
    size_t mask = g_live_widget_table_cap - 1u;
    size_t index = widget_live_hash(widget) & mask;
    while (g_live_widget_table[index]) {
        if (g_live_widget_table[index] == widget) {
            g_live_widget_table[index] = VG_WIDGET_LIVE_TABLE_TOMBSTONE;
            if (g_live_widget_table_count > 0)
                g_live_widget_table_count--;
            return;
        }
        index = (index + 1u) & mask;
    }
}

/// @brief Inserts @p widget at the head of the global live-widget doubly-linked list.
static void widget_register_live(vg_widget_t *widget) {
    if (!widget)
        return;
    widget_live_table_insert(widget);
    widget->_live_prev = NULL;
    widget->_live_next = g_live_widgets;
    if (g_live_widgets)
        g_live_widgets->_live_prev = widget;
    g_live_widgets = widget;
}

/// @brief Removes @p widget from the global live-widget list; called immediately before free().
static void widget_unregister_live(vg_widget_t *widget) {
    if (!widget)
        return;
    widget_live_table_remove(widget);
    if (widget->_live_prev)
        widget->_live_prev->_live_next = widget->_live_next;
    else if (g_live_widgets == widget)
        g_live_widgets = widget->_live_next;
    if (widget->_live_next)
        widget->_live_next->_live_prev = widget->_live_prev;
    widget->_live_prev = NULL;
    widget->_live_next = NULL;
}

/// @brief Mark @p widget and every ancestor as needing layout and paint.
/// @details Parent-chain propagation prevents nested layout containers from
///          keeping stale measurements when a descendant is inserted, removed,
///          hidden, or has layout-affecting parameters changed.
/// @param widget First widget whose layout is dirty; may be NULL.
static void widget_mark_layout_dirty(vg_widget_t *widget) {
    for (vg_widget_t *current = widget; current; current = current->parent) {
        current->needs_layout = true;
        current->needs_paint = true;
    }
}

/// @brief Returns true for widget types that paint their own children (ScrollView and custom
/// widgets with paint_overlay).
static bool widget_paints_children_internally(const vg_widget_t *widget) {
    if (!widget)
        return false;
    if (widget->type == VG_WIDGET_SCROLLVIEW)
        return true;
    return widget->type == VG_WIDGET_CUSTOM && widget->vtable && widget->vtable->paint_overlay;
}

/// @brief Returns @p value if it is finite and positive, otherwise 0.
static float widget_nonnegative_finite(float value) {
    return (isfinite(value) && value > 0.0f) ? value : 0.0f;
}

/// @brief Clamps constraint fields to be non-negative, finite, and self-consistent (max >= min,
/// preferred within [min, max]).
static void widget_normalize_constraints(vg_constraints_t *constraints) {
    if (!constraints)
        return;
    constraints->min_width = widget_nonnegative_finite(constraints->min_width);
    constraints->min_height = widget_nonnegative_finite(constraints->min_height);
    constraints->max_width = widget_nonnegative_finite(constraints->max_width);
    constraints->max_height = widget_nonnegative_finite(constraints->max_height);
    constraints->preferred_width = widget_nonnegative_finite(constraints->preferred_width);
    constraints->preferred_height = widget_nonnegative_finite(constraints->preferred_height);

    if (constraints->max_width > 0.0f && constraints->max_width < constraints->min_width)
        constraints->max_width = constraints->min_width;
    if (constraints->max_height > 0.0f && constraints->max_height < constraints->min_height)
        constraints->max_height = constraints->min_height;
    if (constraints->preferred_width > 0.0f &&
        constraints->preferred_width < constraints->min_width)
        constraints->preferred_width = constraints->min_width;
    if (constraints->preferred_height > 0.0f &&
        constraints->preferred_height < constraints->min_height)
        constraints->preferred_height = constraints->min_height;
    if (constraints->max_width > 0.0f && constraints->preferred_width > constraints->max_width)
        constraints->preferred_width = constraints->max_width;
    if (constraints->max_height > 0.0f && constraints->preferred_height > constraints->max_height)
        constraints->preferred_height = constraints->max_height;
}

/// @brief Recursively clears the needs_paint flag on @p root and all its descendants.
static void clear_paint_flag_recursive(vg_widget_t *root) {
    if (!root)
        return;
    root->needs_paint = false;
    VG_FOREACH_CHILD(root, child) {
        clear_paint_flag_recursive(child);
    }
}

/// @brief Move @p cur toward @p target by one frame's worth of easing.
static float vg__advance(float cur, float target, float dt_ms, float dur_ms) {
    if (dur_ms <= 0.0f)
        return target;
    float step = dt_ms / dur_ms;
    if (cur < target) {
        cur += step;
        if (cur > target)
            cur = target;
    } else if (cur > target) {
        cur -= step;
        if (cur < target)
            cur = target;
    }
    return cur;
}

void vg_widget_anim_tick(vg_widget_t *widget, void *canvas) {
    if (!widget)
        return;
    float hover_t = (widget->state & VG_STATE_HOVERED) ? 1.0f : 0.0f;
    float press_t = (widget->state & VG_STATE_PRESSED) ? 1.0f : 0.0f;
    float focus_t = (widget->state & VG_STATE_FOCUSED) ? 1.0f : 0.0f;

    vg_theme_t *theme = vg_theme_get_current();
    if (!theme || !theme->motion.enabled) {
        widget->anim_hover = hover_t;
        widget->anim_press = press_t;
        widget->anim_focus = focus_t;
        return;
    }

    // Advance by a nominal per-frame step (~60fps). We deliberately do NOT poll
    // vgfx_frame_time_ms here: paint may run against a non-window canvas (e.g.
    // headless widget tests), and dereferencing it as a window would crash.
    (void)canvas;
    float dt = 16.0f;
    widget->anim_hover = vg__advance(widget->anim_hover, hover_t, dt, theme->motion.hover_ms);
    widget->anim_press = vg__advance(widget->anim_press, press_t, dt, theme->motion.press_ms);
    widget->anim_focus = vg__advance(widget->anim_focus, focus_t, dt, theme->motion.focus_ms);
}

/// @brief Recursively paints the widget tree in pre-order, converting each widget's position to
/// screen space before calling its paint vtable.
static void paint_widget_normal_tree(vg_widget_t *root, void *canvas) {
    if (!root || !root->visible || !canvas)
        return;

    if (root->vtable && root->vtable->paint) {
        vg_widget_anim_tick(root, canvas);
        float rel_x = root->x;
        float rel_y = root->y;
        float screen_x = rel_x;
        float screen_y = rel_y;
        vg_widget_get_screen_bounds(root, &screen_x, &screen_y, NULL, NULL);
        root->x = screen_x;
        root->y = screen_y;
        bool was_screen_space = root->_paint_screen_space;
        root->_paint_screen_space = true;
        root->vtable->paint(root, canvas);
        root->_paint_screen_space = was_screen_space;
        root->x = rel_x;
        root->y = rel_y;
    }

    if (widget_paints_children_internally(root))
        return;

    VG_FOREACH_CHILD(root, child) {
        paint_widget_normal_tree(child, canvas);
    }
}

/// @brief Recursively invokes paint_overlay vtable on the entire tree in pre-order, used for
/// tooltips and over-widget overlays.
static void paint_widget_overlay_tree(vg_widget_t *root, void *canvas) {
    if (!root || !root->visible || !canvas)
        return;

    if (root->vtable && root->vtable->paint_overlay) {
        root->vtable->paint_overlay(root, canvas);
        if (widget_paints_children_internally(root))
            return;
    }

    if (widget_paints_children_internally(root))
        return;

    VG_FOREACH_CHILD(root, child) {
        paint_widget_overlay_tree(child, canvas);
    }
}

//=============================================================================
// ID Generation
//=============================================================================

/// @brief Returns a unique, monotonically increasing widget ID, skipping 0 to preserve the sentinel
/// value.
uint64_t vg_widget_next_id(void) {
    if (g_next_widget_id == 0)
        g_next_widget_id = 1;
    uint64_t id = g_next_widget_id++;
    if (g_next_widget_id == 0)
        g_next_widget_id = 1;
    return id;
}

//=============================================================================
// Default VTable Functions
//=============================================================================

/// @brief Default vtable destroy — no-op; the base destroy path frees impl_data itself for plain
/// containers.
static void default_destroy(vg_widget_t *self) {
    // Default: do nothing. The base destroy path owns impl_data by default.
}

/// @brief Default vtable measure — derives the container's measured size from its preferred
/// constraints and the maximum child extents.
static void default_measure(vg_widget_t *self, float available_width, float available_height) {
    (void)available_width;
    (void)available_height;

    float padding_w = self->layout.padding_left + self->layout.padding_right;
    float padding_h = self->layout.padding_top + self->layout.padding_bottom;
    float content_w = 0.0f;
    float content_h = 0.0f;

    VG_FOREACH_VISIBLE_CHILD(self, child) {
        float child_w =
            child->measured_width + child->layout.margin_left + child->layout.margin_right;
        float child_h =
            child->measured_height + child->layout.margin_top + child->layout.margin_bottom;
        if (child_w > content_w)
            content_w = child_w;
        content_h += child_h;
    }

    float w = self->constraints.preferred_width > 0.0f
                  ? self->constraints.preferred_width
                  : (content_w > 0.0f ? content_w + padding_w : self->constraints.min_width);
    float h = self->constraints.preferred_height > 0.0f
                  ? self->constraints.preferred_height
                  : (content_h > 0.0f ? content_h + padding_h : self->constraints.min_height);

    if (w < self->constraints.min_width)
        w = self->constraints.min_width;
    if (h < self->constraints.min_height)
        h = self->constraints.min_height;
    if (self->constraints.max_width > 0.0f && w > self->constraints.max_width)
        w = self->constraints.max_width;
    if (self->constraints.max_height > 0.0f && h > self->constraints.max_height)
        h = self->constraints.max_height;

    self->measured_width = w;
    self->measured_height = h;
}

/// @brief Default vtable arrange — applies constraints, positions this widget, and flows visible
/// children vertically with content padding.
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

    // Position children within content area using a vertical flow with flex
    // support. Specialized layout widgets override arrange and handle their own
    // child placement; this default path is only for plain containers (the app
    // root and SplitPane panes among them). Fixed children keep their measured
    // height; flex children share the remaining height so a flex child fills its
    // plain-container parent instead of the container collapsing to content
    // height (which would push later siblings off-screen).
    float cx = self->layout.padding_left;
    float cy = self->layout.padding_top;
    float content_w = width - self->layout.padding_left - self->layout.padding_right;
    if (content_w < 0.0f)
        content_w = 0.0f;
    float content_h = height - self->layout.padding_top - self->layout.padding_bottom;
    if (content_h < 0.0f)
        content_h = 0.0f;

    float total_fixed = 0.0f;
    float total_flex = 0.0f;
    VG_FOREACH_VISIBLE_CHILD(self, child) {
        total_fixed += child->layout.margin_top + child->layout.margin_bottom;
        if (child->layout.flex > 0.0f)
            total_flex += child->layout.flex;
        else
            total_fixed += child->measured_height;
    }
    float flex_avail = content_h - total_fixed;
    float flex_unit = (total_flex > 0.0f && flex_avail > 0.0f) ? flex_avail / total_flex : 0.0f;

    VG_FOREACH_VISIBLE_CHILD(self, child) {
        float cw = content_w - child->layout.margin_left - child->layout.margin_right;
        float ch =
            (child->layout.flex > 0.0f) ? flex_unit * child->layout.flex : child->measured_height;
        if (cw < 0.0f)
            cw = child->measured_width;
        if (ch < 0.0f)
            ch = 0.0f;
        vg_widget_arrange(
            child, cx + child->layout.margin_left, cy + child->layout.margin_top, cw, ch);
        cy += child->layout.margin_top + ch + child->layout.margin_bottom;
    }
}

/// @brief Default vtable paint — no-op; containers rely on the recursive tree walk to paint
/// children.
static void default_paint(vg_widget_t *self, void *canvas) {
    // Default: paint nothing (container just paints children)
}

/// @brief Default vtable handle_event — returns false (not handled); concrete widgets override
/// this.
static bool default_handle_event(vg_widget_t *self, vg_event_t *event) {
    return false; // Not handled
}

/// @brief Default vtable can_focus — returns false; interactive widgets (button, textinput, etc.)
/// override to true.
static bool default_can_focus(vg_widget_t *self) {
    return false; // Most widgets can't focus by default
}

/// @brief Default vtable on_focus — sets or clears VG_STATE_FOCUSED on the widget.
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

/// @brief Returns true if @p ancestor is equal to or is a parent/grandparent of @p widget.
static bool widget_is_ancestor(const vg_widget_t *ancestor, const vg_widget_t *widget) {
    for (const vg_widget_t *current = widget; current; current = current->parent) {
        if (current == ancestor)
            return true;
    }
    return false;
}

/// @brief Returns true when @p widget and every ancestor are live, visible, and enabled.
static bool widget_chain_accepts_focus(const vg_widget_t *widget) {
    for (const vg_widget_t *current = widget; current; current = current->parent) {
        if (!vg_widget_is_live(current))
            return false;
        if (!current->visible || !current->enabled)
            return false;
    }
    return true;
}

/// @brief Clears transient visual/interactive state (hover, pressed, focused, drag flags) on @p
/// widget and all descendants.
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
            tabbar->pressed_tab = NULL;
            tabbar->pressed_close_tab = NULL;
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
            ((vg_codeeditor_t *)widget)->selection_dragging = false;
            break;
        default:
            break;
    }
    VG_FOREACH_CHILD(widget, child) {
        clear_interactive_state_recursive(child);
    }
}

/// @brief Clears global focus, capture, hover, modal, and click references that point into @p
/// widget's subtree; optionally notifies the tooltip manager.
static void clear_runtime_references_for_subtree(vg_widget_t *widget, bool notify_hidden) {
    if (!widget)
        return;

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
    if (g_last_click_widget && widget_is_ancestor(widget, g_last_click_widget)) {
        g_last_click_widget = NULL;
        g_last_click_time_ms = 0;
        g_last_click_button = -1;
        g_last_click_count = 0;
        g_last_click_screen_x = 0.0f;
        g_last_click_screen_y = 0.0f;
    }
    if (g_reported_click_widget && widget_is_ancestor(widget, g_reported_click_widget)) {
        g_reported_click_widget = NULL;
        g_reported_click_time_ms = 0;
    }
    if (notify_hidden)
        vg_tooltip_manager_widget_hidden(widget);
    vg_event_forget_widget_subtree(widget);
    clear_interactive_state_recursive(widget);
}

/// @brief Returns true if (x, y) is inside the axis-aligned rectangle [rx, rx+rw) × [ry, ry+rh).
static bool point_in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    if (rw <= 0.0f || rh <= 0.0f)
        return false;
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/// @brief Returns the screen-space viewport rectangle of @p scroll (excluding scrollbar gutters).
static void scrollview_get_viewport_screen_bounds(
    const vg_scrollview_t *scroll, float *x, float *y, float *width, float *height) {
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
    vg_widget_get_screen_bounds(&scroll->base, &sx, &sy, &sw, &sh);
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

/// @brief Returns true if (x, y) is within every ancestor's clip rectangle (ScrollView viewport or
/// full bounds).
static bool widget_point_within_ancestor_clips(const vg_widget_t *widget, float x, float y) {
    for (const vg_widget_t *ancestor = widget ? widget->parent : NULL; ancestor;
         ancestor = ancestor->parent) {
        float sx = 0.0f;
        float sy = 0.0f;
        float sw = 0.0f;
        float sh = 0.0f;
        if (ancestor->type == VG_WIDGET_SCROLLVIEW) {
            scrollview_get_viewport_screen_bounds(
                (const vg_scrollview_t *)ancestor, &sx, &sy, &sw, &sh);
        } else {
            vg_widget_get_screen_bounds(ancestor, &sx, &sy, &sw, &sh);
        }
        if (!point_in_rect(x, y, sx, sy, sw, sh))
            return false;
    }
    return true;
}

//=============================================================================
// Widget Initialization
//=============================================================================

/// @brief Zero-initializes @p widget in-place, assigns a unique ID, sets type and vtable, and
/// registers it in the live-widget list.
void vg_widget_init(vg_widget_t *widget, vg_widget_type_t type, const vg_widget_vtable_t *vtable) {
    if (!widget)
        return;

    memset(widget, 0, sizeof(vg_widget_t));

    widget->type = type;
    widget->magic = VG_WIDGET_MAGIC;
    widget->vtable = vtable ? vtable : &g_default_vtable;
    widget->id = vg_widget_next_id();
    widget->visible = true;
    widget->enabled = true;
    widget->needs_layout = true;
    widget->needs_paint = true;
    widget->tab_index = -1; // -1 = natural traversal order
    widget_register_live(widget);
}

/// @brief Returns true if @p widget is in the live-widget list and its magic number is intact
/// (i.e., not destroyed).
bool vg_widget_is_live(const vg_widget_t *widget) {
    if (!widget)
        return false;
    if (g_live_widget_table && g_live_widget_table_cap > 0) {
        size_t mask = g_live_widget_table_cap - 1u;
        size_t index = widget_live_hash(widget) & mask;
        while (g_live_widget_table[index]) {
            if (g_live_widget_table[index] == widget)
                return widget->magic == VG_WIDGET_MAGIC;
            index = (index + 1u) & mask;
        }
        return false;
    }
    for (const vg_widget_t *live = g_live_widgets; live; live = live->_live_next) {
        if (live == widget)
            return live->magic == VG_WIDGET_MAGIC;
    }
    return false;
}

//=============================================================================
// Widget Creation/Destruction
//=============================================================================

/// @brief Heap-allocates and initializes a bare widget of the given type with the default vtable;
/// returns NULL on failure.
vg_widget_t *vg_widget_create(vg_widget_type_t type) {
    vg_widget_t *widget = calloc(1, sizeof(vg_widget_t));
    if (!widget)
        return NULL;

    vg_widget_init(widget, type, NULL);

    return widget;
}

/// @brief Recursively destroys @p widget and all its children, clears global runtime references,
/// calls vtable destroy, frees all owned data.
void vg_widget_destroy(vg_widget_t *widget) {
    if (!vg_widget_is_live(widget))
        return;

    clear_runtime_references_for_subtree(widget, true);

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

    free(widget->drag_type);
    free(widget->drag_data);
    free(widget->accepted_drop_types);
    free(widget->_drop_received_type);
    free(widget->_drop_received_data);

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
    if (g_last_click_widget == widget) {
        g_last_click_widget = NULL;
        g_last_click_time_ms = 0;
        g_last_click_button = -1;
        g_last_click_count = 0;
        g_last_click_screen_x = 0.0f;
        g_last_click_screen_y = 0.0f;
    }
    if (g_reported_click_widget == widget) {
        g_reported_click_widget = NULL;
        g_reported_click_time_ms = 0;
    }

    // Notify tooltip manager so it does not retain a dangling pointer.
    vg_tooltip_manager_widget_destroyed(widget);

    widget->parent = NULL;
    widget->prev_sibling = NULL;
    widget->next_sibling = NULL;
    widget_unregister_live(widget);
    widget->magic = VG_WIDGET_DESTROYED_MAGIC;
    free(widget);
}

/// @brief Transfers ownership of impl_data out of the widget (sets it to NULL) and returns the
/// pointer to the caller.
void *vg_widget_take_impl_data(vg_widget_t *widget) {
    if (!widget)
        return NULL;
    void *data = widget->impl_data;
    widget->impl_data = NULL;
    return data;
}

//=============================================================================
// Hierarchy Management
//=============================================================================

/// @brief Appends @p child to @p parent's child list; re-parents child if it belonged to another
/// widget.
void vg_widget_add_child(vg_widget_t *parent, vg_widget_t *child) {
    if (!parent || !child)
        return;
    if (!vg_widget_is_live(parent) || !vg_widget_is_live(child))
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

    widget_mark_layout_dirty(parent);
}

/// @brief Inserts @p child into @p parent at the given @p index (0 = before first); appends at end
/// if index >= child_count.
void vg_widget_insert_child(vg_widget_t *parent, vg_widget_t *child, int index) {
    if (!parent || !child)
        return;
    if (!vg_widget_is_live(parent) || !vg_widget_is_live(child))
        return;
    if (parent == child || widget_is_ancestor(child, parent))
        return;
    if (index < 0)
        index = 0;

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
    widget_mark_layout_dirty(parent);
}

/// @brief Detaches @p child from @p parent's list, clears runtime references for the subtree, and
/// notifies the layout system.
void vg_widget_remove_child(vg_widget_t *parent, vg_widget_t *child) {
    if (!parent || !child || child->parent != parent)
        return;

    clear_runtime_references_for_subtree(child, true);
    vg_layout_on_child_detached(parent, child);

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
    widget_mark_layout_dirty(parent);
}

/// @brief Detaches all children from @p parent without destroying them; runtime references for each
/// child subtree are cleared.
void vg_widget_clear_children(vg_widget_t *parent) {
    if (!parent)
        return;

    vg_widget_t *child = parent->first_child;
    while (child) {
        vg_widget_t *next = child->next_sibling;
        clear_runtime_references_for_subtree(child, true);
        vg_layout_on_child_detached(parent, child);
        child->parent = NULL;
        child->prev_sibling = NULL;
        child->next_sibling = NULL;
        child = next;
    }

    parent->first_child = NULL;
    parent->last_child = NULL;
    parent->child_count = 0;
    widget_mark_layout_dirty(parent);
}

/// @brief Returns the child widget at the given @p index (0-based), or NULL if out of range.
vg_widget_t *vg_widget_get_child(vg_widget_t *parent, int index) {
    if (!parent || index < 0 || index >= parent->child_count)
        return NULL;

    vg_widget_t *child = parent->first_child;
    for (int i = 0; i < index && child; i++) {
        child = child->next_sibling;
    }

    return child;
}

/// @brief Recursively searches the subtree rooted at @p root for the first widget whose name
/// matches @p name.
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

/// @brief Recursively searches the subtree rooted at @p root for the widget with the given unique
/// @p id.
vg_widget_t *vg_widget_find_by_id(vg_widget_t *root, uint64_t id) {
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

/// @brief Sets all sizing constraints on @p widget at once, normalizing them to be self-consistent.
void vg_widget_set_constraints(vg_widget_t *widget, vg_constraints_t constraints) {
    if (!widget)
        return;
    widget_normalize_constraints(&constraints);
    if (memcmp(&widget->constraints, &constraints, sizeof(widget->constraints)) == 0)
        return;
    widget->constraints = constraints;
    widget_mark_layout_dirty(widget);
}

/// @brief Sets the minimum allowed size for @p widget, re-normalizing other constraints to stay
/// consistent.
void vg_widget_set_min_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    width = widget_nonnegative_finite(width);
    height = widget_nonnegative_finite(height);
    if (widget->constraints.min_width == width && widget->constraints.min_height == height)
        return;
    widget->constraints.min_width = width;
    widget->constraints.min_height = height;
    widget_normalize_constraints(&widget->constraints);
    widget_mark_layout_dirty(widget);
}

/// @brief Sets the maximum allowed size for @p widget, re-normalizing other constraints to stay
/// consistent.
void vg_widget_set_max_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    width = widget_nonnegative_finite(width);
    height = widget_nonnegative_finite(height);
    if (widget->constraints.max_width == width && widget->constraints.max_height == height)
        return;
    widget->constraints.max_width = width;
    widget->constraints.max_height = height;
    widget_normalize_constraints(&widget->constraints);
    widget_mark_layout_dirty(widget);
}

/// @brief Sets the preferred (natural) size for @p widget; overrides content-derived sizes during
/// measure.
void vg_widget_set_preferred_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    width = widget_nonnegative_finite(width);
    height = widget_nonnegative_finite(height);
    if (widget->constraints.preferred_width == width &&
        widget->constraints.preferred_height == height)
        return;
    widget->constraints.preferred_width = width;
    widget->constraints.preferred_height = height;
    widget_normalize_constraints(&widget->constraints);
    widget_mark_layout_dirty(widget);
}

/// @brief Locks @p widget to an exact pixel size by setting min, max, and preferred to the same
/// value.
void vg_widget_set_fixed_size(vg_widget_t *widget, float width, float height) {
    if (!widget)
        return;
    width = widget_nonnegative_finite(width);
    height = widget_nonnegative_finite(height);
    if (widget->constraints.min_width == width && widget->constraints.max_width == width &&
        widget->constraints.preferred_width == width && widget->constraints.min_height == height &&
        widget->constraints.max_height == height &&
        widget->constraints.preferred_height == height) {
        return;
    }
    widget->constraints.min_width = width;
    widget->constraints.max_width = width;
    widget->constraints.preferred_width = width;
    widget->constraints.min_height = height;
    widget->constraints.max_height = height;
    widget->constraints.preferred_height = height;
    widget_mark_layout_dirty(widget);
}

/// @brief Clamps the widget's measured_width/height to its min/max/preferred constraints after
/// measure.
void vg_widget_apply_constraints(vg_widget_t *widget) {
    if (!widget)
        return;

    if (widget->constraints.preferred_width > 0.0f)
        widget->measured_width = widget->constraints.preferred_width;
    if (widget->constraints.preferred_height > 0.0f)
        widget->measured_height = widget->constraints.preferred_height;
    if (widget->constraints.min_width > 0.0f &&
        widget->measured_width < widget->constraints.min_width)
        widget->measured_width = widget->constraints.min_width;
    if (widget->constraints.min_height > 0.0f &&
        widget->measured_height < widget->constraints.min_height)
        widget->measured_height = widget->constraints.min_height;
    if (widget->constraints.max_width > 0.0f &&
        widget->measured_width > widget->constraints.max_width)
        widget->measured_width = widget->constraints.max_width;
    if (widget->constraints.max_height > 0.0f &&
        widget->measured_height > widget->constraints.max_height)
        widget->measured_height = widget->constraints.max_height;

    widget->measured_width = widget_nonnegative_finite(widget->measured_width);
    widget->measured_height = widget_nonnegative_finite(widget->measured_height);
}

/// @brief Returns the widget's bounds in its parent's coordinate space; corrects for screen-space
/// paint mode.
void vg_widget_get_bounds(vg_widget_t *widget, float *x, float *y, float *width, float *height) {
    if (!widget)
        return;
    float local_x = widget->x;
    float local_y = widget->y;
    if (widget->_paint_screen_space) {
        for (vg_widget_t *p = widget->parent; p; p = p->parent) {
            local_x -= p->x;
            local_y -= p->y;
        }
    }
    if (x)
        *x = local_x;
    if (y)
        *y = local_y;
    if (width)
        *width = widget->width;
    if (height)
        *height = widget->height;
}

/// @brief Returns the widget's bounds in screen (root-relative) coordinates by summing ancestor
/// positions.
void vg_widget_get_screen_bounds(
    const vg_widget_t *widget, float *x, float *y, float *width, float *height) {
    if (!widget)
        return;

    float sx = widget->x;
    float sy = widget->y;

    if (!widget->_paint_screen_space) {
        // Child x/y are already stored relative to the arranged content origin of
        // their parent, so screen conversion only adds ancestor positions.
        const vg_widget_t *p = widget->parent;
        while (p) {
            sx += p->x;
            sy += p->y;
            p = p->parent;
        }
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

/// @brief Sets the flex grow factor for @p widget and marks the parent's layout dirty.
void vg_widget_set_flex(vg_widget_t *widget, float flex) {
    if (!widget)
        return;
    flex = widget_nonnegative_finite(flex);
    if (widget->layout.flex == flex)
        return;
    widget->layout.flex = flex;
    widget_mark_layout_dirty(widget->parent);
}

/// @brief Sets all four margins of @p widget to the same value and marks the parent's layout dirty.
void vg_widget_set_margin(vg_widget_t *widget, float margin) {
    if (!widget)
        return;
    margin = widget_nonnegative_finite(margin);
    if (widget->layout.margin_left == margin && widget->layout.margin_top == margin &&
        widget->layout.margin_right == margin && widget->layout.margin_bottom == margin) {
        return;
    }
    widget->layout.margin_left = margin;
    widget->layout.margin_top = margin;
    widget->layout.margin_right = margin;
    widget->layout.margin_bottom = margin;
    widget_mark_layout_dirty(widget->parent);
}

/// @brief Sets per-side margins on @p widget and marks the parent's layout dirty.
void vg_widget_set_margins(vg_widget_t *widget, float left, float top, float right, float bottom) {
    if (!widget)
        return;
    left = widget_nonnegative_finite(left);
    top = widget_nonnegative_finite(top);
    right = widget_nonnegative_finite(right);
    bottom = widget_nonnegative_finite(bottom);
    if (widget->layout.margin_left == left && widget->layout.margin_top == top &&
        widget->layout.margin_right == right && widget->layout.margin_bottom == bottom) {
        return;
    }
    widget->layout.margin_left = left;
    widget->layout.margin_top = top;
    widget->layout.margin_right = right;
    widget->layout.margin_bottom = bottom;
    widget_mark_layout_dirty(widget->parent);
}

/// @brief Sets all four padding sides of @p widget to the same value and marks the layout dirty.
void vg_widget_set_padding(vg_widget_t *widget, float padding) {
    if (!widget)
        return;
    padding = widget_nonnegative_finite(padding);
    if (widget->layout.padding_left == padding && widget->layout.padding_top == padding &&
        widget->layout.padding_right == padding && widget->layout.padding_bottom == padding) {
        return;
    }
    widget->layout.padding_left = padding;
    widget->layout.padding_top = padding;
    widget->layout.padding_right = padding;
    widget->layout.padding_bottom = padding;
    widget_mark_layout_dirty(widget);
}

/// @brief Sets per-side padding on @p widget and marks the layout dirty.
void vg_widget_set_paddings(vg_widget_t *widget, float left, float top, float right, float bottom) {
    if (!widget)
        return;
    left = widget_nonnegative_finite(left);
    top = widget_nonnegative_finite(top);
    right = widget_nonnegative_finite(right);
    bottom = widget_nonnegative_finite(bottom);
    if (widget->layout.padding_left == left && widget->layout.padding_top == top &&
        widget->layout.padding_right == right && widget->layout.padding_bottom == bottom) {
        return;
    }
    widget->layout.padding_left = left;
    widget->layout.padding_top = top;
    widget->layout.padding_right = right;
    widget->layout.padding_bottom = bottom;
    widget_mark_layout_dirty(widget);
}

//=============================================================================
// State Management
//=============================================================================

/// @brief Enables or disables @p widget; on disable clears focus, capture, hover, modal, and click
/// references for the subtree.
void vg_widget_set_enabled(vg_widget_t *widget, bool enabled) {
    if (!widget)
        return;
    if (widget->enabled == enabled)
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
        if (g_last_click_widget && widget_is_ancestor(widget, g_last_click_widget)) {
            g_last_click_widget = NULL;
            g_last_click_time_ms = 0;
            g_last_click_button = -1;
            g_last_click_count = 0;
            g_last_click_screen_x = 0.0f;
            g_last_click_screen_y = 0.0f;
        }
        if (g_reported_click_widget && widget_is_ancestor(widget, g_reported_click_widget)) {
            g_reported_click_widget = NULL;
            g_reported_click_time_ms = 0;
        }
        vg_tooltip_manager_widget_hidden(widget);
        vg_event_forget_widget_subtree(widget);
        clear_interactive_state_recursive(widget);
    }
    widget->needs_paint = true;
}

/// @brief Returns the widget's enabled flag, or false for NULL.
bool vg_widget_is_enabled(vg_widget_t *widget) {
    return widget && widget->enabled;
}

/// @brief Shows or hides @p widget; on hide clears all runtime references for the subtree and marks
/// the parent layout dirty.
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
        if (g_last_click_widget && widget_is_ancestor(widget, g_last_click_widget)) {
            g_last_click_widget = NULL;
            g_last_click_time_ms = 0;
            g_last_click_button = -1;
            g_last_click_count = 0;
            g_last_click_screen_x = 0.0f;
            g_last_click_screen_y = 0.0f;
        }
        if (g_reported_click_widget && widget_is_ancestor(widget, g_reported_click_widget)) {
            g_reported_click_widget = NULL;
            g_reported_click_time_ms = 0;
        }
        vg_tooltip_manager_widget_hidden(widget);
        vg_event_forget_widget_subtree(widget);
        clear_interactive_state_recursive(widget);
    }
    widget_mark_layout_dirty(widget->parent);
    widget->needs_paint = true;
}

/// @brief Returns the widget's visible flag, or false for NULL.
bool vg_widget_is_visible(vg_widget_t *widget) {
    return widget && widget->visible;
}

/// @brief Returns true if the widget's state field has all bits in @p state set.
bool vg_widget_has_state(vg_widget_t *widget, vg_widget_state_t state) {
    return widget && (widget->state & state);
}

/// @brief Sets @p widget's debug name, strdup'ing the string and freeing any previously set name.
void vg_widget_set_name(vg_widget_t *widget, const char *name) {
    if (!widget)
        return;

    char *copy = NULL;
    if (name) {
        copy = vg_strdup(name);
        if (!copy)
            return;
    }

    free(widget->name);
    widget->name = copy;
}

/// @brief Returns the widget's debug name, or NULL if none was set.
const char *vg_widget_get_name(vg_widget_t *widget) {
    return widget ? widget->name : NULL;
}

//=============================================================================
// Layout & Rendering
//=============================================================================

/// @brief Dispatches the measure pass to @p root's vtable; recurses into children first for
/// containers using the default measure.
void vg_widget_measure(vg_widget_t *root, float available_width, float available_height) {
    if (!root || !root->visible)
        return;

    bool fallback_child_layout = root->vtable && !root->vtable->arrange && root->first_child;
    bool recurse_children = !root->vtable || !root->vtable->measure ||
                            root->vtable->measure == default_measure || fallback_child_layout;
    if (recurse_children) {
        VG_FOREACH_VISIBLE_CHILD(root, child) {
            vg_widget_measure(child, available_width, available_height);
        }
    }

    // Then measure this widget
    if (root->vtable && root->vtable->measure) {
        root->vtable->measure(root, available_width, available_height);
    }

    if (fallback_child_layout) {
        float child_w = 0.0f;
        float child_h = 0.0f;
        VG_FOREACH_VISIBLE_CHILD(root, child) {
            float w =
                child->measured_width + child->layout.margin_left + child->layout.margin_right;
            float h =
                child->measured_height + child->layout.margin_top + child->layout.margin_bottom;
            if (w > child_w)
                child_w = w;
            child_h += h;
        }
        float padded_w = child_w + root->layout.padding_left + root->layout.padding_right;
        float padded_h = child_h + root->layout.padding_top + root->layout.padding_bottom;
        if (padded_w > root->measured_width)
            root->measured_width = padded_w;
        if (padded_h > root->measured_height)
            root->measured_height = padded_h;
        vg_widget_apply_constraints(root);
    }
}

/// @brief Dispatches the arrange pass to @p root's vtable (or falls back to direct position
/// assignment), then clears needs_layout.
void vg_widget_arrange(vg_widget_t *root, float x, float y, float width, float height) {
    if (!root || !root->visible)
        return;

    if (root->vtable && root->vtable->arrange) {
        // Custom arrange functions (VBox, HBox, SplitPane, default containers)
        // handle positioning self AND children.
        root->vtable->arrange(root, x, y, width, height);
    } else {
        // Widgets without arrange (MenuBar, Toolbar, StatusBar, etc.)
        // just need their position and size set. If runtime code attached
        // children anyway, lay those children out with the base vertical flow
        // so AddChild does not create invisible/orphaned descendants.
        root->x = x;
        root->y = y;
        root->width = width;
        root->height = height;
        if (root->first_child)
            default_arrange(root, x, y, width, height);
    }

    root->needs_layout = false;
}

/// @brief Runs the full two-pass layout (measure then arrange) for @p root at the origin with the
/// given available size.
void vg_widget_layout(vg_widget_t *root, float available_width, float available_height) {
    vg_widget_measure(root, available_width, available_height);
    vg_widget_arrange(root, 0, 0, available_width, available_height);
}

/// @brief Paints the entire widget tree (normal pass then overlay pass) and clears all needs_paint
/// flags.
void vg_widget_paint(vg_widget_t *root, void *canvas) {
    if (!root || !root->visible || !canvas)
        return;

    paint_widget_normal_tree(root, canvas);
    paint_widget_overlay_tree(root, canvas);

    clear_paint_flag_recursive(root);
}

/// @brief Marks @p widget and all its ancestors as needing repaint (for clipping region
/// invalidation).
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

/// @brief Marks @p widget and all its ancestors as needing both layout and repaint.
void vg_widget_invalidate_layout(vg_widget_t *widget) {
    if (!widget)
        return;
    widget_mark_layout_dirty(widget);
}

//=============================================================================
// Hit Testing
//=============================================================================

/// @brief Returns the deepest visible, enabled widget at screen point (x, y) within @p root,
/// respecting ScrollView clip bounds.
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
    bool descend = (x >= child_clip_x && x < child_clip_x + child_clip_w && y >= child_clip_y &&
                    y < child_clip_y + child_clip_h);

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

/// @brief Returns true if screen point (x, y) is within @p widget's screen bounds and within all
/// ancestor clip regions.
bool vg_widget_contains_point(vg_widget_t *widget, float x, float y) {
    if (!widget)
        return false;
    for (vg_widget_t *w = widget; w; w = w->parent) {
        if (!w->visible || !w->enabled)
            return false;
    }

    float sx, sy, sw, sh;
    vg_widget_get_screen_bounds(widget, &sx, &sy, &sw, &sh);

    return point_in_rect(x, y, sx, sy, sw, sh) && widget_point_within_ancestor_clips(widget, x, y);
}

//=============================================================================
// Input Capture
//=============================================================================

/// @brief Routes all subsequent mouse events to @p widget, bypassing hit-testing (used by open
/// dropdowns/menus).
void vg_widget_set_input_capture(vg_widget_t *widget) {
    if (!widget) {
        g_input_capture_widget = NULL;
        return;
    }
    if (!vg_widget_is_live(widget)) {
        g_input_capture_widget = NULL;
        return;
    }
    g_input_capture_widget = widget;
}

/// @brief Releases input capture, restoring normal hit-test routing for mouse events.
void vg_widget_release_input_capture(void) {
    g_input_capture_widget = NULL;
}

/// @brief Returns the widget currently holding input capture, or NULL if none.
vg_widget_t *vg_widget_get_input_capture(void) {
    if (!vg_widget_is_live(g_input_capture_widget)) {
        g_input_capture_widget = NULL;
    }
    return g_input_capture_widget;
}

/// @brief Returns @p widget if it is live and its stored ID matches @p id; otherwise NULL.
static vg_widget_t *runtime_widget_ref(vg_widget_t *widget, uint64_t id) {
    if (!vg_widget_is_live(widget))
        return NULL;
    if (id != 0 && widget->id != id)
        return NULL;
    return widget;
}

/// @brief Snapshots the current global focus, capture, modal, hover, and click state into @p state.
void vg_widget_get_runtime_state(vg_widget_runtime_state_t *state) {
    if (!state)
        return;
    state->focused_widget = g_focused_widget;
    state->focused_widget_id = g_focused_widget ? g_focused_widget->id : 0;
    state->input_capture_widget = g_input_capture_widget;
    state->input_capture_widget_id = g_input_capture_widget ? g_input_capture_widget->id : 0;
    state->modal_root = g_modal_root;
    state->modal_root_id = g_modal_root ? g_modal_root->id : 0;
    state->hovered_widget = g_hovered_widget;
    state->hovered_widget_id = g_hovered_widget ? g_hovered_widget->id : 0;
    state->last_click_widget = g_last_click_widget;
    state->last_click_widget_id = g_last_click_widget ? g_last_click_widget->id : 0;
    state->last_click_time_ms = g_last_click_time_ms;
    state->last_click_button = g_last_click_button;
    state->last_click_count = g_last_click_count;
    state->last_click_screen_x = g_last_click_screen_x;
    state->last_click_screen_y = g_last_click_screen_y;
    state->reported_click_widget = g_reported_click_widget;
    state->reported_click_widget_id = g_reported_click_widget ? g_reported_click_widget->id : 0;
    state->reported_click_time_ms = g_reported_click_time_ms;
}

/// @brief Restores global runtime state from @p state, re-validating each widget pointer to guard
/// against stale references.
void vg_widget_set_runtime_state(const vg_widget_runtime_state_t *state) {
    if (!state) {
        g_focused_widget = NULL;
        g_input_capture_widget = NULL;
        g_modal_root = NULL;
        g_hovered_widget = NULL;
        g_last_click_widget = NULL;
        g_last_click_time_ms = 0;
        g_last_click_button = -1;
        g_last_click_count = 0;
        g_last_click_screen_x = 0.0f;
        g_last_click_screen_y = 0.0f;
        g_reported_click_widget = NULL;
        g_reported_click_time_ms = 0;
        return;
    }
    g_focused_widget = runtime_widget_ref(state->focused_widget, state->focused_widget_id);
    g_input_capture_widget =
        runtime_widget_ref(state->input_capture_widget, state->input_capture_widget_id);
    g_modal_root = runtime_widget_ref(state->modal_root, state->modal_root_id);
    g_hovered_widget = runtime_widget_ref(state->hovered_widget, state->hovered_widget_id);

    g_last_click_widget = runtime_widget_ref(state->last_click_widget, state->last_click_widget_id);
    g_last_click_time_ms = g_last_click_widget ? state->last_click_time_ms : 0;
    g_last_click_button = g_last_click_widget ? state->last_click_button : -1;
    g_last_click_count = g_last_click_widget ? state->last_click_count : 0;
    g_last_click_screen_x = g_last_click_widget ? state->last_click_screen_x : 0.0f;
    g_last_click_screen_y = g_last_click_widget ? state->last_click_screen_y : 0.0f;

    g_reported_click_widget =
        runtime_widget_ref(state->reported_click_widget, state->reported_click_widget_id);
    g_reported_click_time_ms = g_reported_click_widget ? state->reported_click_time_ms : 0;
}

/// @brief Records @p widget as the last click recipient at @p timestamp_ms for double-click
/// reporting.
void vg_widget_note_click(vg_widget_t *widget, uint64_t timestamp_ms) {
    if (!vg_widget_is_live(widget))
        return;
    g_reported_click_widget = widget;
    g_reported_click_time_ms = timestamp_ms;
}

/// @brief Clears the last reported-click widget and timestamp.
void vg_widget_clear_reported_click(void) {
    g_reported_click_widget = NULL;
    g_reported_click_time_ms = 0;
}

//=============================================================================
// Focus Management
//=============================================================================

/// @brief Focuses @p widget (or clears focus if NULL), calling on_focus callbacks for both the old
/// and new focused widgets.
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
    if (!widget_chain_accepts_focus(widget))
        return;
    if (!widget->vtable || !widget->vtable->can_focus || !widget->vtable->can_focus(widget)) {
        return;
    }
    if (g_focused_widget == widget)
        return;

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

/// @brief Returns the focused widget if it is live and within @p root's subtree; NULL if none or
/// out of scope.
vg_widget_t *vg_widget_get_focused(vg_widget_t *root) {
    if (!vg_widget_is_live(g_focused_widget))
        return NULL;
    if (!root)
        return g_focused_widget;
    if (!vg_widget_is_live(root))
        return NULL;
    return widget_is_ancestor(root, g_focused_widget) ? g_focused_widget : NULL;
}

typedef struct focus_list {
    vg_widget_t **items;
    int count;
    int capacity;
} focus_list_t;

/// @brief Appends @p widget to the dynamic focus list, growing the backing array as needed.
static bool focus_list_append(focus_list_t *list, vg_widget_t *widget) {
    if (!list || !widget)
        return false;
    if (list->count == list->capacity) {
        if (list->capacity > INT_MAX / 2)
            return false;
        int new_capacity = list->capacity > 0 ? list->capacity * 2 : 32;
        if (new_capacity <= list->capacity ||
            (size_t)new_capacity > SIZE_MAX / sizeof(vg_widget_t *)) {
            return false;
        }
        vg_widget_t **items = realloc(list->items, (size_t)new_capacity * sizeof(vg_widget_t *));
        if (!items)
            return false;
        list->items = items;
        list->capacity = new_capacity;
    }

    list->items[list->count++] = widget;
    return true;
}

/// @brief Recursively collects all visible, enabled, focusable descendants of @p root into @p list.
static bool collect_focusable(vg_widget_t *root, focus_list_t *list) {
    if (!root)
        return true;

    for (vg_widget_t *child = root->first_child; child; child = child->next_sibling) {
        if (!child->visible || !child->enabled)
            continue;

        if (child->vtable && child->vtable->can_focus && child->vtable->can_focus(child)) {
            if (!focus_list_append(list, child))
                return false;
        }

        if (!collect_focusable(child, list))
            return false;
    }
    return true;
}

/// @brief Stable merge of arr[lo..mid) and arr[mid..hi) into arr[lo..hi) using @p tmp scratch;
/// natural-order (-1) sorts after explicit indices.
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

/// @brief Recursive merge sort entry-point for tab-order sorting; O(n log n) with stable DFS order
/// for equal keys.
static void tab_merge_sort(vg_widget_t **arr, vg_widget_t **tmp, int lo, int hi) {
    if (hi - lo <= 1)
        return;
    int mid = (lo + hi) / 2;
    tab_merge_sort(arr, tmp, lo, mid);
    tab_merge_sort(arr, tmp, mid, hi);
    tab_merge(arr, tmp, lo, mid, hi);
}

/// @brief Builds a sorted array of focusable widgets from @p root's subtree in tab order; caller
/// must free *out_items.
static int build_tab_order(vg_widget_t *root, vg_widget_t ***out_items) {
    if (!out_items)
        return 0;
    *out_items = NULL;

    focus_list_t list = {0};
    if (!collect_focusable(root, &list)) {
        free(list.items);
        return 0;
    }

    if (list.count > 1) {
        // Stable merge sort by tab_index — O(n log n) vs previous O(n²) insertion sort.
        vg_widget_t **tmp = malloc((size_t)list.count * sizeof(vg_widget_t *));
        if (tmp) {
            tab_merge_sort(list.items, tmp, 0, list.count);
            free(tmp);
        }
        // If malloc fails, order is preserved from DFS (still usable, just unsorted)
    }

    *out_items = list.items;
    return list.count;
}

/// @brief Moves focus to the next widget in tab order within @p root, wrapping around at the end.
void vg_widget_focus_next(vg_widget_t *root) {
    if (!root)
        return;

    vg_widget_t **arr = NULL;
    int count = build_tab_order(root, &arr);
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
    free(arr);
}

/// @brief Moves focus to the previous widget in tab order within @p root, wrapping around at the
/// start.
void vg_widget_focus_prev(vg_widget_t *root) {
    if (!root)
        return;

    vg_widget_t **arr = NULL;
    int count = build_tab_order(root, &arr);
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
    free(arr);
}

//=============================================================================
// Tab Index
//=============================================================================

/// @brief Sets the explicit tab-index for @p widget; -1 means natural DFS order after all
/// explicitly indexed widgets.
void vg_widget_set_tab_index(vg_widget_t *widget, int tab_index) {
    if (!widget)
        return;
    widget->tab_index = tab_index;
}

//=============================================================================
// Modal Root
//=============================================================================

/// @brief Sets @p widget as the application's modal root; all event routing is restricted to its
/// subtree until cleared.
void vg_widget_set_modal_root(vg_widget_t *widget) {
    if (!widget) {
        g_modal_root = NULL;
        return;
    }
    if (!vg_widget_is_live(widget) || !widget->visible) {
        g_modal_root = NULL;
        return;
    }
    g_modal_root = widget;
}

/// @brief Returns the currently active modal root, or NULL if no modal is active.
vg_widget_t *vg_widget_get_modal_root(void) {
    if (!vg_widget_is_live(g_modal_root) || (g_modal_root && !g_modal_root->visible))
        g_modal_root = NULL;
    return g_modal_root;
}
