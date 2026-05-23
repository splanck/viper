//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_app.c
// Purpose: GUI application lifecycle management for Viper's GUI runtime layer.
//   Creates and owns the ViperGFX window, the root vg_widget container, and the
//   default font. Provides the main loop entry points: rt_gui_app_poll (event
//   dispatch), rt_gui_app_render (layout + paint + present), and
//   rt_gui_app_destroy. Also manages the active modal dialog and a resize
//   callback so the window repaints during macOS live-resize.
//
// Key invariants:
//   - s_current_app is a global pointer valid between app_new and app_destroy;
//     widget constructors use it to inherit the default font.
//   - The root widget must NOT have a fixed size set; layout is driven by the
//     physical window dimensions on every render call.
//   - g_active_dialog is at most one modal dialog; nested dialogs are rejected.
//   - The default font is loaded lazily via rt_gui_ensure_default_font() and
//     uses the embedded font if no file path is configured.
//   - HiDPI scale is applied immediately after window creation; all widget
//     sizes and font sizes are in physical pixels.
//   - Dark theme is applied by default at app creation.
//
// Ownership/Lifetime:
//   - rt_gui_app_t is allocated via rt_obj_new_i64 (GC heap) and zeroed;
//     rt_gui_app_destroy must be called explicitly to release the window and
//     widget tree before GC reclaims the struct.
//   - The root widget and all its children are owned by the vg widget tree;
//     vg_widget_destroy(root) frees the entire subtree.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/runtime/graphics/rt_gui_widgets.c (basic widget implementations),
//        src/lib/gui/include/vg.h (ViperGUI C API),
//        src/lib/graphics/include/vgfx.h (window/event layer)
//
//===----------------------------------------------------------------------===//

#include "fonts/embedded_font.h"
#include "rt_gui_internal.h"
#include "rt_input.h"
#include "rt_platform.h"
#include "rt_time.h"

#ifdef VIPER_ENABLE_GRAPHICS

// Global pointer to the app currently bound to the runtime-facing constructors.
rt_gui_app_t *s_current_app = NULL;
static rt_gui_app_t *s_active_app = NULL;
static rt_gui_app_t **s_registered_apps = NULL;
static int s_registered_app_count = 0;
static int s_registered_app_cap = 0;
static const void **s_destroyed_app_handles = NULL;
static int s_destroyed_app_count = 0;
static int s_destroyed_app_cap = 0;

/// @brief Return the index of `app` in `s_registered_apps`, or -1 if not found.
/// @details Linear search over the live-app registry. The registry is small
///          (typically 1-4 apps) so O(n) is fine.
static int rt_gui_app_index(const rt_gui_app_t *app) {
    if (!app)
        return -1;
    for (int i = 0; i < s_registered_app_count; i++) {
        if (s_registered_apps[i] == app)
            return i;
    }
    return -1;
}

/// @brief Return the index of `handle` in `s_destroyed_app_handles`, or -1 if absent.
/// @details The destroyed-app set lets the runtime reject stale handles (Zia
///          code that holds a reference past app_destroy) without dereferencing
///          a freed pointer.
static int rt_gui_destroyed_app_index(const void *handle) {
    if (!handle)
        return -1;
    for (int i = 0; i < s_destroyed_app_count; i++) {
        if (s_destroyed_app_handles[i] == handle)
            return i;
    }
    return -1;
}

/// @brief Return non-zero if `handle` points to an app that has already been destroyed.
/// @details Used by checked entry points to guard against use-after-destroy
///          without dereferencing the potentially freed pointer.
int rt_gui_is_destroyed_app_handle(const void *handle) {
    return rt_gui_destroyed_app_index(handle) >= 0;
}

/// @brief Return non-zero if `handle` is a currently-live (not destroyed) app handle.
/// @details Checks the current, active, and registered-app lists. Used by
///          `rt_gui_app_from_widget` to confirm that a widget's user_data
///          really is an app pointer before casting it.
int rt_gui_is_app_handle_known(const void *handle) {
    if (!handle)
        return 0;
    if (handle == s_current_app || handle == s_active_app)
        return 1;
    return rt_gui_app_index((const rt_gui_app_t *)handle) >= 0;
}

/// @brief Remove `handle` from the destroyed-app tombstone list.
/// @details Called when an app handle is about to be re-registered (e.g., a
///          new rt_gui_app_new called with the same address from the GC heap).
///          Without this, the recycled address would be permanently rejected.
static void rt_gui_forget_destroyed_app_handle(const void *handle) {
    int idx = rt_gui_destroyed_app_index(handle);
    if (idx < 0)
        return;
    memmove(&s_destroyed_app_handles[idx],
            &s_destroyed_app_handles[idx + 1],
            (size_t)(s_destroyed_app_count - idx - 1) * sizeof(*s_destroyed_app_handles));
    s_destroyed_app_count--;
}

/// @brief Record `handle` in the destroyed-app tombstone list so future lookups reject it.
/// @details The list grows dynamically with geometric capacity. Duplicate entries
///          are skipped. A handle that wasn't found in the live registry should
///          still be noted (e.g., if the GC freed it before unregister ran).
static void rt_gui_note_destroyed_app_handle(const void *handle) {
    if (!handle || rt_gui_destroyed_app_index(handle) >= 0)
        return;
    if (s_destroyed_app_count >= s_destroyed_app_cap) {
        if (s_destroyed_app_cap > INT_MAX / 2)
            return;
        int new_cap = s_destroyed_app_cap ? s_destroyed_app_cap * 2 : 4;
        void *p =
            realloc(s_destroyed_app_handles, (size_t)new_cap * sizeof(*s_destroyed_app_handles));
        if (!p)
            return;
        s_destroyed_app_handles = (const void **)p;
        s_destroyed_app_cap = new_cap;
    }
    s_destroyed_app_handles[s_destroyed_app_count++] = handle;
}

/// @brief Add `app` to the global live-app registry.
/// @details Removes the handle from the destroyed-tombstone list first (in case
///          the GC recycled the address). Duplicate registrations are silently
///          accepted. The registry uses geometric-growth realloc.
/// @return 1 on success, 0 on OOM.
static int rt_gui_register_app(rt_gui_app_t *app) {
    if (!app)
        return 0;
    rt_gui_forget_destroyed_app_handle(app);
    if (rt_gui_app_index(app) >= 0)
        return 1;
    if (s_registered_app_count >= s_registered_app_cap) {
        if (s_registered_app_cap > INT_MAX / 2)
            return 0;
        int new_cap = s_registered_app_cap ? s_registered_app_cap * 2 : 4;
        void *p = realloc(s_registered_apps, (size_t)new_cap * sizeof(*s_registered_apps));
        if (!p)
            return 0;
        s_registered_apps = (rt_gui_app_t **)p;
        s_registered_app_cap = new_cap;
    }
    s_registered_apps[s_registered_app_count++] = app;
    return 1;
}

/// @brief Remove `app` from the live-app registry and add it to the tombstone list.
/// @details After removal the handle is considered destroyed — subsequent calls to
///          `rt_gui_is_app_handle_known` with this address will return false.
static void rt_gui_unregister_app(rt_gui_app_t *app) {
    int idx = rt_gui_app_index(app);
    if (idx < 0)
        return;
    memmove(&s_registered_apps[idx],
            &s_registered_apps[idx + 1],
            (size_t)(s_registered_app_count - idx - 1) * sizeof(*s_registered_apps));
    s_registered_app_count--;
    rt_gui_note_destroyed_app_handle(app);
}

/// @brief Return the current wall-clock time in milliseconds.
/// @details Converts the microsecond-precision platform clock to milliseconds.
///          Used throughout the GUI subsystem for event timestamps, tooltip
///          delays, toast durations, and animation timing.
/// @return Monotonic time in milliseconds (wraps after ~585 million years).
uint64_t rt_gui_now_ms(void) {
    return (uint64_t)(rt_clock_ticks_us() / 1000);
}

/// @brief Reset all global widget runtime state to defaults.
/// @details Zeroes the shared widget runtime state (focus, capture, hover
///          tracking), clears the tooltip manager, and restores the dark theme
///          as the current theme. Called when no app is active (e.g., after the
///          last app is destroyed) so stale pointers from a previous app don't
///          linger in global state.
static void rt_gui_clear_widget_runtime_state(void) {
    vg_widget_runtime_state_t empty = {0};
    vg_widget_set_runtime_state(&empty);
    *vg_tooltip_manager_get() = (vg_tooltip_manager_t){0};
    vg_theme_set_current(vg_theme_dark());
}

/// @brief Snapshot the current global widget state into an app struct.
/// @details The vg widget system uses global state for focus, keyboard capture,
///          and tooltip tracking. When switching between multiple GUI apps, we
///          must save this state so each app gets its own independent focus and
///          tooltip context. This is the "save" half of a save/restore pair.
/// @param app App whose state fields will be overwritten with current globals.
static void rt_gui_save_app_runtime_state(rt_gui_app_t *app) {
    if (!app)
        return;
    vg_widget_get_runtime_state(&app->widget_runtime_state);
    app->tooltip_manager_state = *vg_tooltip_manager_get();
}

/// @brief Restore previously-saved widget state from an app struct.
/// @details The "restore" half of the save/restore pair. Pushes the app's
///          saved focus, capture, and tooltip state back into the global vg
///          widget system. If app is NULL, clears to defaults instead.
/// @param app App whose saved state will become the active global state.
static void rt_gui_restore_app_runtime_state(rt_gui_app_t *app) {
    if (!app) {
        rt_gui_clear_widget_runtime_state();
        return;
    }
    vg_widget_set_runtime_state(&app->widget_runtime_state);
    *vg_tooltip_manager_get() = app->tooltip_manager_state;
}

#ifdef __APPLE__
/// @brief Return non-zero if the macOS native menu bar should be re-synced during an app switch.
/// @details Syncing is needed whenever a window is involved in the transition:
///          when activating an app that has a window, or when deactivating an
///          app that had one. Avoids unnecessary native menu work when both
///          parties are windowless (e.g., headless test apps).
static int rt_gui_should_sync_macos_menu(rt_gui_app_t *incoming, rt_gui_app_t *outgoing) {
    return (incoming && incoming->window) || (!incoming && outgoing && outgoing->window);
}
#endif

/// @brief Return the base (unscaled) theme for a given theme kind.
/// @details Maps the runtime enum to the built-in vg_theme constant. The
///          returned pointer is a static singleton — do not free it.
/// @param kind RT_GUI_THEME_DARK or RT_GUI_THEME_LIGHT.
/// @return Pointer to the corresponding immutable base theme.
static const vg_theme_t *rt_gui_theme_base(rt_gui_theme_kind_t kind) {
    return (kind == RT_GUI_THEME_LIGHT) ? vg_theme_light() : vg_theme_dark();
}

/// @brief Pre-order traversal step over the visible widget tree rooted at @p root.
/// @details Returns the next widget after @p node: its first child when @p node is
///          visible and has children, otherwise the nearest ancestor's next sibling,
///          stopping (NULL) at @p root. Gating descent on visibility lets a hidden
///          container's entire subtree be skipped in a single step.
static vg_widget_t *rt_gui_next_visible_widget(vg_widget_t *root, vg_widget_t *node) {
    if (!root || !node)
        return NULL;
    if (node->visible && node->first_child)
        return node->first_child;
    while (node && node != root && !node->next_sibling)
        node = node->parent;
    if (!node || node == root)
        return NULL;
    return node->next_sibling;
}

/// @brief Iteratively advance per-frame animation state across a widget subtree.
/// @details Called from the app's render loop with the elapsed time since the
///          last frame. Three widget types currently maintain time-based state
///          and need a per-frame tick: TextInput (cursor blink), ProgressBar
///          (indeterminate animation), and CodeEditor (cursor blink + scroll
///          animation). Hidden subtrees and zero/negative `dt` short-circuit
///          so background panels don't burn cycles or accumulate phase drift.
///          When a new widget type grows time-dependent state, add it to the
///          switch — the rest of the tree walk is generic.
static void rt_gui_tick_widget_tree(vg_widget_t *widget, float dt) {
    if (!widget || !widget->visible || dt <= 0.0f)
        return;

    for (vg_widget_t *node = widget; node; node = rt_gui_next_visible_widget(widget, node)) {
        if (!node->visible)
            continue;
        switch (node->type) {
            case VG_WIDGET_TEXTINPUT:
                vg_textinput_tick((vg_textinput_t *)node, dt);
                break;
            case VG_WIDGET_PROGRESS:
                vg_progressbar_tick((vg_progressbar_t *)node, dt);
                break;
            case VG_WIDGET_CODEEDITOR:
                vg_codeeditor_tick((vg_codeeditor_t *)node, dt);
                break;
            default:
                break;
        }
    }
}

/// @brief Test whether any widget in a visible subtree has its layout-dirty flag set.
/// @details Drives the "skip layout pass entirely if nothing changed" fast path
///          in the render loop. Hidden subtrees are skipped because their
///          dirty flags don't affect the visible output until they're shown
///          again (at which point the show transition will re-flag layout).
///          The walk short-circuits on the first dirty descendant — a typical
///          frame finds nothing dirty in O(visible widget count) and the
///          render loop can go straight to paint.
static bool rt_gui_widget_tree_needs_layout(const vg_widget_t *widget) {
    if (!widget || !widget->visible)
        return false;
    for (const vg_widget_t *node = widget; node;) {
        if (node->visible && node->needs_layout)
            return true;
        if (node->visible && node->first_child) {
            node = node->first_child;
            continue;
        }
        while (node && node != widget && !node->next_sibling)
            node = node->parent;
        if (!node || node == widget)
            break;
        node = node->next_sibling;
    }
    return false;
}

/// @brief Test whether any widget in a subtree has its paint-dirty flag set.
/// @details Hidden overlay roots can be paint-dirty after being dismissed; that
///          still requires one full repaint to erase their previous pixels.
static bool rt_gui_widget_tree_needs_paint(const vg_widget_t *widget) {
    if (!widget)
        return false;
    if (widget->needs_paint)
        return true;
    if (!widget->visible)
        return false;
    for (const vg_widget_t *node = widget; node;) {
        if (node != widget && node->needs_paint)
            return true;
        if (node->visible && node->first_child) {
            node = node->first_child;
            continue;
        }
        while (node && node != widget && !node->next_sibling)
            node = node->parent;
        if (!node || node == widget)
            break;
        node = node->next_sibling;
    }
    return false;
}

/// @brief Clear paint-dirty flags after a complete full-window repaint.
static void rt_gui_widget_tree_clear_paint(vg_widget_t *widget) {
    if (!widget)
        return;
    widget->needs_paint = false;
    for (vg_widget_t *child = widget->first_child; child; child = child->next_sibling)
        rt_gui_widget_tree_clear_paint(child);
}

static bool rt_gui_app_overlays_need_paint(const rt_gui_app_t *app) {
    if (!app)
        return false;
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] &&
            rt_gui_widget_tree_needs_paint(&app->command_palettes[i]->base)) {
            return true;
        }
    }
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] && rt_gui_widget_tree_needs_paint(&app->dialog_stack[i]->base))
            return true;
    }
    if (app->notification_manager && rt_gui_widget_tree_needs_paint(&app->notification_manager->base))
        return true;
    vg_tooltip_manager_t *tooltip_mgr = vg_tooltip_manager_get();
    if (tooltip_mgr && tooltip_mgr->active_tooltip &&
        rt_gui_widget_tree_needs_paint(&tooltip_mgr->active_tooltip->base)) {
        return true;
    }
    if (app->manual_tooltip && rt_gui_widget_tree_needs_paint(&app->manual_tooltip->base))
        return true;
    return false;
}

static void rt_gui_app_clear_paint_flags(rt_gui_app_t *app) {
    if (!app)
        return;
    rt_gui_widget_tree_clear_paint(app->root);
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i])
            rt_gui_widget_tree_clear_paint(&app->command_palettes[i]->base);
    }
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i])
            rt_gui_widget_tree_clear_paint(&app->dialog_stack[i]->base);
    }
    if (app->notification_manager)
        rt_gui_widget_tree_clear_paint(&app->notification_manager->base);
    vg_tooltip_manager_t *tooltip_mgr = vg_tooltip_manager_get();
    if (tooltip_mgr && tooltip_mgr->active_tooltip)
        rt_gui_widget_tree_clear_paint(&tooltip_mgr->active_tooltip->base);
    if (app->manual_tooltip)
        rt_gui_widget_tree_clear_paint(&app->manual_tooltip->base);
}

/// @brief Apply a HiDPI scale factor to all size-sensitive theme fields.
/// @details Multiplies typography sizes, spacing constants, button/input
///          heights, padding, and scrollbar width by the given scale. This
///          ensures the UI is sized in physical pixels, not logical points,
///          so it renders crisply on Retina/HiDPI displays. A scale <= 0
///          is clamped to 1.0 (identity) for safety.
/// @param theme Mutable theme to scale in-place.
/// @param scale HiDPI multiplier (e.g., 2.0 on a Retina display).
static void rt_gui_scale_theme(vg_theme_t *theme, float scale) {
    if (!theme)
        return;
    if (!isfinite(scale) || scale <= 0.0f)
        scale = 1.0f;
    theme->ui_scale = scale;
    theme->typography.size_small *= scale;
    theme->typography.size_normal *= scale;
    theme->typography.size_large *= scale;
    theme->typography.size_heading *= scale;
    theme->spacing.xs *= scale;
    theme->spacing.sm *= scale;
    theme->spacing.md *= scale;
    theme->spacing.lg *= scale;
    theme->spacing.xl *= scale;
    theme->button.height *= scale;
    theme->button.padding_h *= scale;
    theme->button.border_radius *= scale;
    theme->button.border_width *= scale;
    theme->input.height *= scale;
    theme->input.padding_h *= scale;
    theme->input.border_radius *= scale;
    theme->input.border_width *= scale;
    theme->scrollbar.width *= scale;
    theme->scrollbar.min_thumb_size *= scale;
    theme->scrollbar.border_radius *= scale;
}

/// @brief Rebuild and activate the app's scaled theme if the base or scale changed.
/// @details Creates a fresh mutable copy of the base theme (dark or light),
///          scales it to the current window's HiDPI factor, and installs it as
///          the active theme. Skips the rebuild if the base theme and scale
///          haven't changed since the last call — this avoids redundant
///          allocations during per-frame render calls. The old theme is
///          destroyed after the new one is installed.
/// @param app App whose theme to refresh (no-op if NULL).
void rt_gui_refresh_theme(rt_gui_app_t *app) {
    if (!app)
        return;

    const vg_theme_t *base = rt_gui_theme_base(app->theme_kind);
    float scale = app->window ? vgfx_window_get_scale(app->window) : 1.0f;
    if (!isfinite(scale) || scale <= 0.0f)
        scale = 1.0f;

    if (app->theme && app->theme_base == base && app->theme_scale == scale) {
        if (s_active_app == app)
            vg_theme_set_current(app->theme);
        return;
    }

    vg_theme_t *theme = vg_theme_create(base->name, base);
    if (!theme)
        return;
    rt_gui_scale_theme(theme, scale);

    vg_theme_t *old_theme = app->theme;
    app->theme = theme;
    app->theme_base = base;
    app->theme_scale = scale;

    if (s_active_app == app)
        vg_theme_set_current(app->theme);
    if (old_theme)
        vg_theme_destroy(old_theme);
}

/// @brief Switch the app's theme between dark and light.
/// @details Resets the cached base/scale so the next rt_gui_refresh_theme call
///          forces a full theme rebuild with the new kind. The refresh is
///          triggered immediately so the change takes effect this frame.
/// @param app Target app (no-op if NULL).
/// @param kind RT_GUI_THEME_DARK or RT_GUI_THEME_LIGHT.
void rt_gui_set_theme_kind(rt_gui_app_t *app, rt_gui_theme_kind_t kind) {
    if (!app)
        return;
    app->theme_kind = kind;
    app->theme_base = NULL;
    app->theme_scale = 0.0f;
    rt_gui_refresh_theme(app);
}

/// @brief Return the currently active GUI app, or NULL if none is active.
/// @details The active app is the one whose widget tree, theme, and runtime
///          state are installed in the global vg widget system. There is at
///          most one active app at a time.
/// @return Pointer to the active app, or NULL.
rt_gui_app_t *rt_gui_get_active_app(void) {
    return s_active_app;
}

/// @brief Make the given app the active GUI app.
/// @details Saves the outgoing app's widget runtime state (focus, capture,
///          tooltips), installs the incoming app's state, refreshes its theme,
///          and syncs the macOS native menu bar. If the app is already active,
///          just refreshes the theme (handles window scale changes). Both
///          s_active_app and s_current_app are updated so widget constructors
///          and font lookups use the correct app context.
/// @param app App to activate. May be NULL to deactivate.
void rt_gui_activate_app(rt_gui_app_t *app) {
    RT_ASSERT_MAIN_THREAD();
#ifdef __APPLE__
    rt_gui_app_t *previous_active = s_active_app;
#endif
    if (app == s_active_app) {
        s_current_app = app;
        rt_gui_refresh_theme(app);
        return;
    }

    if (s_active_app) {
        rt_gui_save_app_runtime_state(s_active_app);
    }

    s_active_app = app;
    s_current_app = app;
    rt_gui_refresh_theme(app);
    rt_gui_restore_app_runtime_state(app);
#ifdef __APPLE__
    if (rt_gui_should_sync_macos_menu(app, previous_active))
        rt_gui_macos_menu_sync_app(app);
#else
    rt_gui_macos_menu_sync_app(app);
#endif
}

/// @brief Resolve the owning app for a given widget by walking the parent chain.
/// @details Widgets don't store a direct back-pointer to their app. Instead,
///          the root widget's user_data is set to the app pointer at creation.
///          This function walks up the parent chain until it finds a root
///          (parentless) widget and returns its user_data. If the pointer is
///          actually an app handle (registry check), it's returned directly.
/// @param widget Any widget in the tree.
/// @return The owning rt_gui_app_t, or NULL if the widget is detached.
rt_gui_app_t *rt_gui_app_from_widget(vg_widget_t *widget) {
    if (rt_gui_is_destroyed_app_handle(widget))
        return NULL;
    if (rt_gui_is_app_handle(widget))
        return (rt_gui_app_t *)widget;
    if (!vg_widget_is_live(widget))
        return NULL;
    for (vg_widget_t *w = widget; w; w = w->parent) {
        if (!w->parent && w->user_data) {
            rt_gui_app_t *candidate = (rt_gui_app_t *)w->user_data;
            if (rt_gui_is_app_handle(candidate))
                return candidate;
        }
    }
    return NULL;
}

/// @brief Double the dialog stack capacity when full (amortized O(1) growth).
/// @details The dialog stack uses a dynamic array with geometric growth. This
///          avoids per-push allocation while keeping memory usage reasonable
///          for the typical case (1-3 nested dialogs).
/// @param app App whose dialog stack to grow.
static int rt_gui_grow_dialog_stack(rt_gui_app_t *app) {
    if (!app || app->dialog_count < app->dialog_cap)
        return app != NULL;
    if (app->dialog_cap > INT_MAX / 2)
        return 0;
    int new_cap = app->dialog_cap ? app->dialog_cap * 2 : 4;
    void *p = realloc(app->dialog_stack, (size_t)new_cap * sizeof(*app->dialog_stack));
    if (!p)
        return 0;
    app->dialog_stack = p;
    app->dialog_cap = new_cap;
    return 1;
}

/// @brief Compact the dialog stack and set the topmost open dialog as modal root.
/// @details Removes closed dialogs from the stack (compacting in-place), then
///          tells the vg widget system which widget is the modal root. When a
///          modal root is set, all input events outside that widget's bounds are
///          blocked — this implements the modal dialog interaction pattern where
///          the user must dismiss the dialog before interacting with the rest
///          of the UI.
/// @param app App whose dialog stack to synchronize.
void rt_gui_sync_modal_root(rt_gui_app_t *app) {
    if (!app) {
        vg_widget_set_modal_root(NULL);
        return;
    }

    vg_dialog_t *top = NULL;
    int write = 0;
    for (int i = 0; i < app->dialog_count; i++) {
        vg_dialog_t *dlg = app->dialog_stack[i];
        if (!dlg || !vg_widget_is_live(&dlg->base) || !dlg->is_open)
            continue;
        app->dialog_stack[write++] = dlg;
        top = dlg;
    }
    app->dialog_count = write;
    vg_widget_set_modal_root(top ? &top->base : NULL);
}

/// @brief Push a dialog onto the app's modal dialog stack.
/// @details Adds the dialog if it isn't already present (dedup check), grows
///          the stack if needed, and syncs the modal root so the new dialog
///          captures input. The dialog's user_data is set to the app pointer
///          so the dialog can find its owning app when needed.
/// @param app Target app.
/// @param dlg Dialog to push (ignored if already on the stack).
void rt_gui_push_dialog(rt_gui_app_t *app, vg_dialog_t *dlg) {
    if (!app || !dlg)
        return;
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] == dlg)
            return;
    }
    if (!rt_gui_grow_dialog_stack(app))
        return;
    if (app->dialog_count < app->dialog_cap) {
        dlg->base.user_data = app;
        app->dialog_stack[app->dialog_count++] = dlg;
        rt_gui_sync_modal_root(app);
    }
}

/// @brief Remove a dialog from the app's modal dialog stack.
/// @details Finds the dialog by pointer identity, shifts subsequent entries
///          down via memmove, and re-syncs the modal root. If the removed
///          dialog was the topmost modal, the next dialog (or NULL) becomes
///          the new modal root.
/// @param app Target app.
/// @param dlg Dialog to remove.
void rt_gui_remove_dialog(rt_gui_app_t *app, vg_dialog_t *dlg) {
    if (!app || !dlg)
        return;
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] != dlg)
            continue;
        memmove(&app->dialog_stack[i],
                &app->dialog_stack[i + 1],
                (size_t)(app->dialog_count - i - 1) * sizeof(*app->dialog_stack));
        app->dialog_count--;
        rt_gui_sync_modal_root(app);
        break;
    }
}

/// @brief Return the topmost open dialog on the stack, or NULL if none.
/// @details Compacts closed dialogs before returning, so the result is always
///          an open dialog or NULL. Used by the poll/render loops to determine
///          the current modal root and event routing target.
/// @param app App to query.
/// @return Topmost open vg_dialog_t, or NULL.
vg_dialog_t *rt_gui_top_dialog(rt_gui_app_t *app) {
    if (!app)
        return NULL;
    rt_gui_sync_modal_root(app);
    return app->dialog_count > 0 ? app->dialog_stack[app->dialog_count - 1] : NULL;
}

/// @brief Double the command palette array capacity when full.
/// @details Same geometric-growth pattern as the dialog stack. Apps rarely have
///          more than 1-2 command palettes, but the dynamic array handles the
///          general case safely.
/// @param app App whose command palette array to grow.
static int rt_gui_grow_command_palette_array(rt_gui_app_t *app) {
    if (!app || app->command_palette_count < app->command_palette_cap)
        return app != NULL;
    if (app->command_palette_cap > INT_MAX / 2)
        return 0;
    int new_cap = app->command_palette_cap ? app->command_palette_cap * 2 : 4;
    void *p = realloc(app->command_palettes, (size_t)new_cap * sizeof(*app->command_palettes));
    if (!p)
        return 0;
    app->command_palettes = p;
    app->command_palette_cap = new_cap;
    return 1;
}

/// @brief Register a command palette with the app for event routing and rendering.
/// @details Command palettes are rendered as overlays above all other content
///          and receive keyboard/mouse events before the widget tree. The app
///          tracks all registered palettes so the poll loop can route events to
///          whichever one is visible. Duplicate registrations are silently ignored.
/// @param app Target app.
/// @param palette Command palette to register.
void rt_gui_register_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette) {
    if (!app || !palette)
        return;
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] == palette)
            return;
    }
    if (!rt_gui_grow_command_palette_array(app))
        return;
    if (app->command_palette_count < app->command_palette_cap) {
        app->command_palettes[app->command_palette_count++] = palette;
    }
}

/// @brief Unregister a command palette from the app.
/// @details Removes the palette from the app's tracking array so it is no
///          longer rendered or receives events. Called during palette destruction.
/// @param app Target app.
/// @param palette Command palette to unregister.
void rt_gui_unregister_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette) {
    if (!app || !palette)
        return;
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] != palette)
            continue;
        memmove(&app->command_palettes[i],
                &app->command_palettes[i + 1],
                (size_t)(app->command_palette_count - i - 1) * sizeof(*app->command_palettes));
        app->command_palette_count--;
        break;
    }
}

/// @brief Find the topmost visible command palette, if any.
/// @details Scans the palette array in reverse (most-recently-registered first)
///          to find one that is both programmatically visible (is_visible) and
///          widget-visible (base.visible). The poll loop uses this to intercept
///          keyboard events before they reach the widget tree.
/// @param app App to search.
/// @return Topmost visible palette, or NULL if none are open.
static vg_commandpalette_t *rt_gui_top_visible_command_palette(rt_gui_app_t *app) {
    if (!app)
        return NULL;
    for (int i = app->command_palette_count - 1; i >= 0; i--) {
        vg_commandpalette_t *palette = app->command_palettes[i];
        if (palette && palette->is_visible && palette->base.visible)
            return palette;
    }
    return NULL;
}

/// @brief Push or pop the active modal dialog.
/// @details When dlg is non-NULL, pushes it onto the dialog stack so it becomes
///          the modal root. When dlg is NULL, pops the topmost dialog. This is
///          the Zia-facing entry point for modal dialog management.
/// @param dlg Dialog to push, or NULL to pop the topmost dialog.
void rt_gui_set_active_dialog(void *dlg) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = s_current_app ? s_current_app : s_active_app;
    if (!app)
        return;
    if (dlg) {
        rt_gui_push_dialog(app, (vg_dialog_t *)dlg);
    } else {
        vg_dialog_t *top = rt_gui_top_dialog(app);
        if (top)
            rt_gui_remove_dialog(app, top);
    }
}

/// @brief Resize callback invoked by the platform during live window resizing.
/// @details On macOS, the Cocoa run loop enters a modal tracking mode during
///          window resize, blocking our main thread. Without this callback, the
///          framebuffer stays black until the drag ends. By registering this as
///          the vgfx resize callback, we re-render the full UI on every resize
///          event, giving smooth live feedback.
/// @param userdata Pointer to the rt_gui_app_t (set at registration time).
/// @param w New window width in physical pixels (unused; render reads it).
/// @param h New window height in physical pixels (unused; render reads it).
static void rt_gui_app_resize_render(void *userdata, int32_t w, int32_t h) {
    (void)w;
    (void)h;
    rt_gui_app_render(userdata);
}

static int rt_gui_widget_tree_uses_font(vg_widget_t *widget, vg_font_t *font);

/// @brief Keep a font alive until the app is destroyed (prevents use-after-free).
/// @details When the user calls App.SetFont or Font.Destroy while a GUI object
///          still references the font, defer destruction to app teardown.
/// @param app App that will own the retired font.
/// @param font Font to retain (must not be NULL).
int rt_gui_retire_font(rt_gui_app_t *app, vg_font_t *font) {
    if (!app || !font)
        return 0;
    for (int i = 0; i < app->retired_font_count; i++) {
        if (app->retired_fonts[i] == font)
            return 1;
    }
    if (app->retired_font_count >= app->retired_font_cap) {
        if (app->retired_font_cap > INT_MAX / 2)
            return 0;
        int new_cap = app->retired_font_cap ? app->retired_font_cap * 2 : 4;
        void *p = realloc(app->retired_fonts, (size_t)new_cap * sizeof(*app->retired_fonts));
        if (!p)
            return 0;
        app->retired_fonts = p;
        app->retired_font_cap = new_cap;
    }
    app->retired_fonts[app->retired_font_count++] = font;
    return 1;
}

/// @brief Return non-zero if any part of the app references `font`.
/// @details Checks the default font, retired font list, entire widget tree
///          (root + all dialogs), command palettes, notification manager, and
///          manual tooltip. Used before deciding whether to destroy a font
///          immediately or defer it to app teardown.
static int rt_gui_app_uses_font(rt_gui_app_t *app, vg_font_t *font) {
    if (!app || !font)
        return 0;
    if (app->default_font == font)
        return 1;
    for (int i = 0; i < app->retired_font_count; i++) {
        if (app->retired_fonts[i] == font)
            return 1;
    }
    if (rt_gui_widget_tree_uses_font(app->root, font))
        return 1;
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] && app->dialog_stack[i]->font == font)
            return 1;
        if (app->dialog_stack[i] &&
            rt_gui_widget_tree_uses_font(&app->dialog_stack[i]->base, font))
            return 1;
    }
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] && app->command_palettes[i]->font == font)
            return 1;
    }
    if (app->notification_manager && app->notification_manager->font == font)
        return 1;
    if (app->manual_tooltip && app->manual_tooltip->font == font)
        return 1;
    return 0;
}

/// @brief Retire `font` into any app that currently holds a reference to it.
/// @details Scans the active app, current app, and all registered apps in order.
///          The first app that claims the font via `rt_gui_app_uses_font` takes
///          ownership of it for deferred destruction. Returns 1 if retired, 0 if
///          no app claims the font (safe to destroy immediately).
int rt_gui_retire_font_if_in_use(vg_font_t *font) {
    if (!font)
        return 0;
    int used = 0;
    rt_gui_app_t *candidates[2] = {s_active_app, s_current_app};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        rt_gui_app_t *app = candidates[i];
        if (app && rt_gui_app_uses_font(app, font)) {
            used = 1;
            rt_gui_retire_font(app, font);
        }
    }
    for (int i = 0; i < s_registered_app_count; i++) {
        rt_gui_app_t *app = s_registered_apps[i];
        if (app && rt_gui_app_uses_font(app, font)) {
            used = 1;
            rt_gui_retire_font(app, font);
        }
    }
    return used;
}

/// @brief Retire a font into every other app that still references it.
/// @return non-zero when another live app references @p font.
static int rt_gui_retire_font_in_other_apps(rt_gui_app_t *skip, vg_font_t *font) {
    if (!font)
        return 0;
    int used = 0;
    rt_gui_app_t *candidates[2] = {s_active_app, s_current_app};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        rt_gui_app_t *app = candidates[i];
        if (app && app != skip && rt_gui_app_uses_font(app, font)) {
            used = 1;
            rt_gui_retire_font(app, font);
        }
    }
    for (int i = 0; i < s_registered_app_count; i++) {
        rt_gui_app_t *app = s_registered_apps[i];
        if (app && app != skip && rt_gui_app_uses_font(app, font)) {
            used = 1;
            rt_gui_retire_font(app, font);
        }
    }
    return used;
}

/// @brief Recursively apply a font and size to a widget and all its descendants.
/// @details Different widget types have different font APIs (e.g., vg_label_set_font,
///          vg_button_set_font, etc.), so this function dispatches on widget->type
///          to call the correct setter. After updating the font, it marks the widget
///          as needing re-layout and re-paint, then recurses into children.
///          This is the mechanism behind App.SetFont propagating to every widget.
/// @param widget Root of the subtree to update.
/// @param font   Font to apply.
/// @param size   Font size in physical pixels.
static void rt_gui_apply_font_to_widget(vg_widget_t *widget, vg_font_t *font, float size) {
    if (!widget || !font)
        return;
    switch (widget->type) {
        case VG_WIDGET_LABEL:
            vg_label_set_font((vg_label_t *)widget, font, size);
            break;
        case VG_WIDGET_BUTTON:
            vg_button_set_font((vg_button_t *)widget, font, size);
            break;
        case VG_WIDGET_TEXTINPUT:
            vg_textinput_set_font((vg_textinput_t *)widget, font, size);
            break;
        case VG_WIDGET_CHECKBOX: {
            vg_checkbox_t *checkbox = (vg_checkbox_t *)widget;
            checkbox->font = font;
            checkbox->font_size = size;
            break;
        }
        case VG_WIDGET_LISTBOX:
            vg_listbox_set_font((vg_listbox_t *)widget, font, size);
            break;
        case VG_WIDGET_DROPDOWN:
            vg_dropdown_set_font((vg_dropdown_t *)widget, font, size);
            break;
        case VG_WIDGET_SLIDER: {
            vg_slider_t *slider = (vg_slider_t *)widget;
            slider->font = font;
            slider->font_size = size;
            break;
        }
        case VG_WIDGET_PROGRESS:
            vg_progressbar_set_font((vg_progressbar_t *)widget, font, size);
            break;
        case VG_WIDGET_SPINNER:
            vg_spinner_set_font((vg_spinner_t *)widget, font, size);
            break;
        case VG_WIDGET_COLORPICKER:
            vg_colorpicker_set_font((vg_colorpicker_t *)widget, font, size);
            break;
        case VG_WIDGET_TREEVIEW:
            vg_treeview_set_font((vg_treeview_t *)widget, font, size);
            break;
        case VG_WIDGET_TABBAR:
            vg_tabbar_set_font((vg_tabbar_t *)widget, font, size);
            break;
        case VG_WIDGET_MENUBAR:
            vg_menubar_set_font((vg_menubar_t *)widget, font, size);
            break;
        case VG_WIDGET_TOOLBAR:
            vg_toolbar_set_font((vg_toolbar_t *)widget, font, size);
            break;
        case VG_WIDGET_STATUSBAR:
            vg_statusbar_set_font((vg_statusbar_t *)widget, font, size);
            break;
        case VG_WIDGET_DIALOG:
            vg_dialog_set_font((vg_dialog_t *)widget, font, size);
            break;
        case VG_WIDGET_CODEEDITOR:
            vg_codeeditor_set_font((vg_codeeditor_t *)widget, font, size);
            break;
        case VG_WIDGET_RADIO: {
            vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
            radio->font = font;
            radio->font_size = size;
            break;
        }
        default:
            if (widget->vtable && widget->vtable->set_font)
                widget->vtable->set_font(widget, font, size);
            break;
    }
    widget->needs_layout = true;
    widget->needs_paint = true;
    for (vg_widget_t *child = widget->first_child; child; child = child->next_sibling) {
        rt_gui_apply_font_to_widget(child, font, size);
    }
}

/// @brief Return non-zero if the given single widget's font field equals `font`.
/// @details Dispatches on widget->type to access the correct font field for
///          each widget kind. Widgets whose type is not in the switch are
///          assumed not to track fonts (returns 0).
static int rt_gui_widget_uses_font(vg_widget_t *widget, vg_font_t *font) {
    if (!widget || !font)
        return 0;
    switch (widget->type) {
        case VG_WIDGET_LABEL:
            return ((vg_label_t *)widget)->font == font;
        case VG_WIDGET_BUTTON:
            return ((vg_button_t *)widget)->font == font;
        case VG_WIDGET_TEXTINPUT:
            return ((vg_textinput_t *)widget)->font == font;
        case VG_WIDGET_CHECKBOX:
            return ((vg_checkbox_t *)widget)->font == font;
        case VG_WIDGET_LISTBOX:
            return ((vg_listbox_t *)widget)->font == font;
        case VG_WIDGET_DROPDOWN:
            return ((vg_dropdown_t *)widget)->font == font;
        case VG_WIDGET_SLIDER:
            return ((vg_slider_t *)widget)->font == font;
        case VG_WIDGET_PROGRESS:
            return ((vg_progressbar_t *)widget)->font == font;
        case VG_WIDGET_SPINNER:
            return ((vg_spinner_t *)widget)->font == font;
        case VG_WIDGET_COLORPICKER:
            return ((vg_colorpicker_t *)widget)->font == font;
        case VG_WIDGET_TREEVIEW:
            return ((vg_treeview_t *)widget)->font == font;
        case VG_WIDGET_TABBAR:
            return ((vg_tabbar_t *)widget)->font == font;
        case VG_WIDGET_MENUBAR:
            return ((vg_menubar_t *)widget)->font == font;
        case VG_WIDGET_TOOLBAR:
            return ((vg_toolbar_t *)widget)->font == font;
        case VG_WIDGET_STATUSBAR:
            return ((vg_statusbar_t *)widget)->font == font;
        case VG_WIDGET_DIALOG:
            return ((vg_dialog_t *)widget)->font == font;
        case VG_WIDGET_CODEEDITOR:
            return ((vg_codeeditor_t *)widget)->font == font;
        case VG_WIDGET_RADIO:
            return ((vg_radiobutton_t *)widget)->font == font;
        default:
            return 0;
    }
}

/// @brief Iteratively check whether any widget in a subtree uses `font`.
/// @details Short-circuits at the first match so the full tree is not always
///          walked. Used by `rt_gui_app_uses_font` to scan dialogs and the root
///          widget tree.
static int rt_gui_widget_tree_uses_font(vg_widget_t *widget, vg_font_t *font) {
    if (!widget || !font)
        return 0;
    for (vg_widget_t *node = widget; node;) {
        if (rt_gui_widget_uses_font(node, font))
            return 1;
        if (node->first_child) {
            node = node->first_child;
            continue;
        }
        while (node && node != widget && !node->next_sibling)
            node = node->parent;
        if (!node || node == widget)
            break;
        node = node->next_sibling;
    }
    return 0;
}

/// @brief Return whether the font handle is safe to pass through metric APIs.
/// @details Runtime tests sometimes install opaque sentinel handles (for
///          example `(vg_font_t *)0x1`) to verify default-font propagation
///          without loading a real font. Those handles must be copied into
///          widgets without immediately dereferencing them. Real heap-backed
///          fonts always live well above the first memory page, so treat tiny
///          addresses as opaque placeholders and fall back to lazy assignment.
/// @param font Candidate font handle.
/// @return True when widget setters may safely query font metrics.
static bool rt_gui_font_handle_supports_metrics(vg_font_t *font) {
    return vg_font_is_live(font);
}

/// @brief Lazily copy a font handle into a widget subtree without measuring it.
/// @details Used when the runtime only needs construction-time inheritance of
///          an opaque font handle. This avoids synchronous metric lookups while
///          still updating the widget fields and invalidation flags so a later
///          real font assignment can lay out normally.
/// @param widget Root widget to update.
/// @param font Font handle to store.
/// @param size Font size in physical pixels.
static void rt_gui_inherit_font_to_widget(vg_widget_t *widget, vg_font_t *font, float size) {
    if (!widget || !font)
        return;

    vg_theme_t *theme = vg_theme_get_current();
    float scale = (theme && theme->ui_scale > 0.0f) ? theme->ui_scale : 1.0f;

    switch (widget->type) {
        case VG_WIDGET_LABEL: {
            vg_label_t *label = (vg_label_t *)widget;
            label->font = font;
            label->font_size = size > 0 ? size : 13.0f;
            break;
        }
        case VG_WIDGET_BUTTON: {
            vg_button_t *button = (vg_button_t *)widget;
            button->font = font;
            button->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 13.0f);
            break;
        }
        case VG_WIDGET_TEXTINPUT: {
            vg_textinput_t *input = (vg_textinput_t *)widget;
            input->font = font;
            input->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_CHECKBOX: {
            vg_checkbox_t *checkbox = (vg_checkbox_t *)widget;
            checkbox->font = font;
            checkbox->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_LISTBOX: {
            vg_listbox_t *listbox = (vg_listbox_t *)widget;
            listbox->font = font;
            listbox->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            float min_item_height =
                (theme ? theme->input.height : 28.0f * scale) > (listbox->font_size + 8.0f * scale)
                    ? (theme ? theme->input.height : 28.0f * scale)
                    : (listbox->font_size + 8.0f * scale);
            if (listbox->item_height < min_item_height)
                listbox->item_height = min_item_height;
            break;
        }
        case VG_WIDGET_DROPDOWN: {
            vg_dropdown_t *dropdown = (vg_dropdown_t *)widget;
            dropdown->font = font;
            dropdown->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_SLIDER: {
            vg_slider_t *slider = (vg_slider_t *)widget;
            slider->font = font;
            slider->font_size =
                size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
            break;
        }
        case VG_WIDGET_PROGRESS: {
            vg_progressbar_t *progress = (vg_progressbar_t *)widget;
            progress->font = font;
            progress->font_size =
                size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
            break;
        }
        case VG_WIDGET_SPINNER: {
            vg_spinner_t *spinner = (vg_spinner_t *)widget;
            spinner->font = font;
            spinner->font_size = size > 0 ? size : 14.0f;
            break;
        }
        case VG_WIDGET_COLORPICKER: {
            vg_colorpicker_t *picker = (vg_colorpicker_t *)widget;
            picker->font = font;
            picker->font_size = size > 0 ? size : 12.0f;
            break;
        }
        case VG_WIDGET_TREEVIEW: {
            vg_treeview_t *tree = (vg_treeview_t *)widget;
            tree->font = font;
            tree->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            {
                float row_height = 28.0f * scale;
                float text_height = tree->font_size + 8.0f * scale;
                if (text_height > row_height)
                    row_height = text_height;
                tree->row_height = row_height;
            }
            break;
        }
        case VG_WIDGET_TABBAR: {
            vg_tabbar_t *tabbar = (vg_tabbar_t *)widget;
            tabbar->font = font;
            tabbar->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_MENUBAR: {
            vg_menubar_t *menubar = (vg_menubar_t *)widget;
            menubar->font = font;
            menubar->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_TOOLBAR: {
            vg_toolbar_t *toolbar = (vg_toolbar_t *)widget;
            toolbar->font = font;
            toolbar->font_size =
                size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
            break;
        }
        case VG_WIDGET_STATUSBAR: {
            vg_statusbar_t *statusbar = (vg_statusbar_t *)widget;
            statusbar->font = font;
            statusbar->font_size =
                size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
            break;
        }
        case VG_WIDGET_DIALOG: {
            vg_dialog_t *dialog = (vg_dialog_t *)widget;
            dialog->font = font;
            dialog->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            dialog->title_font_size = dialog->font_size + scale;
            break;
        }
        case VG_WIDGET_CODEEDITOR: {
            vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;
            editor->font = font;
            editor->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            if (editor->char_width <= 0.0f)
                editor->char_width = editor->font_size * 0.6f;
            if (editor->line_height <= 0.0f)
                editor->line_height = editor->font_size * 1.35f;
            break;
        }
        case VG_WIDGET_RADIO: {
            vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
            radio->font = font;
            radio->font_size =
                size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        default:
            if (widget->vtable && widget->vtable->set_font)
                widget->vtable->set_font(widget, font, size);
            break;
    }

    widget->needs_layout = true;
    widget->needs_paint = true;
    for (vg_widget_t *child = widget->first_child; child; child = child->next_sibling) {
        rt_gui_inherit_font_to_widget(child, font, size);
    }
}

/// @brief Apply the app's default font to a newly-created widget.
/// @details Resolves the owning app from the widget's parent chain, ensures
///          the default font is loaded (lazy init), then calls
///          rt_gui_apply_font_to_widget to set the font on the widget and its
///          children. Called by every widget constructor so new widgets inherit
///          the app's font automatically.
/// @param widget Newly-created widget to apply the default font to.
void rt_gui_apply_default_font(vg_widget_t *widget) {
    if (!widget)
        return;
    rt_gui_app_t *app = rt_gui_app_from_widget(widget);
    if (!app)
        return;

    rt_gui_activate_app(app);
    if (!app->default_font)
        rt_gui_ensure_default_font();
    if (!app->default_font)
        return;
    if (rt_gui_font_handle_supports_metrics(app->default_font))
        rt_gui_apply_font_to_widget(widget, app->default_font, app->default_font_size);
    else
        rt_gui_inherit_font_to_widget(widget, app->default_font, app->default_font_size);
}

void rt_gui_reapply_default_font(rt_gui_app_t *app) {
    if (!app || !app->default_font)
        return;
    if (app->root) {
        if (rt_gui_font_handle_supports_metrics(app->default_font))
            rt_gui_apply_font_to_widget(app->root, app->default_font, app->default_font_size);
        else
            rt_gui_inherit_font_to_widget(app->root, app->default_font, app->default_font_size);
    }
    if (app->notification_manager) {
        vg_notification_manager_set_font(
            app->notification_manager, app->default_font, app->default_font_size);
    }
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i])
            vg_commandpalette_set_font(
                app->command_palettes[i], app->default_font, app->default_font_size);
    }
    if (app->manual_tooltip) {
        app->manual_tooltip->font = app->default_font;
        app->manual_tooltip->font_size = app->default_font_size;
    }
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i])
            vg_dialog_set_font(app->dialog_stack[i], app->default_font, app->default_font_size);
    }
}

/// @brief Create a new GUI application with a window and root widget container.
/// @details Allocates the app struct on the GC heap (rt_obj_new_i64), creates a
///          platform window via vgfx, sets up a root container widget, registers
///          the live-resize callback, applies dark theme by default, and activates
///          the app as current. The root widget is NOT given a fixed size — it is
///          resized dynamically every frame from the physical window dimensions.
/// @param title  Window title (runtime string).
/// @param width  Initial window width in logical pixels (clamped to [1, INT32_MAX]).
/// @param height Initial window height in logical pixels.
/// @return Pointer to the new app, or NULL on failure.
void *rt_gui_app_new(rt_string title, int64_t width, int64_t height) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = (rt_gui_app_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_gui_app_t));
    if (!app)
        return NULL;
    memset(app, 0, sizeof(rt_gui_app_t));
    app->magic = RT_GUI_APP_MAGIC;

    // Create window
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)(width < 1 ? 1 : width > INT32_MAX ? INT32_MAX : width);
    params.height = (int32_t)(height < 1 ? 1 : height > INT32_MAX ? INT32_MAX : height);
    char *ctitle = rt_string_to_gui_cstr(title);
    if (ctitle) {
        params.title = ctitle;
    }
    params.resizable = 1;

    app->window = vgfx_create_window(&params);
    app->title = ctitle ? strdup(ctitle) : NULL;
    free(ctitle);

    if (!app->window) {
        // app is GC-allocated (rt_obj_new_i64) so it will be reclaimed by the
        // collector. Zero the struct so the GC finalizer (if any) sees clean state.
        free(app->title);
        app->title = NULL;
        memset(app, 0, sizeof(rt_gui_app_t));
        return NULL;
    }

    // Register resize callback so the window repaints during macOS live-resize.
    // Without this, the Cocoa modal resize loop blocks our main thread and
    // the framebuffer stays black until the drag ends.
    vgfx_set_resize_callback(app->window, rt_gui_app_resize_render, app);

    // Create root container. The root is sized dynamically every frame by
    // vg_widget_layout(root, win_w, win_h) in rt_gui_app_render, which reads
    // the current physical window dimensions from vgfx_get_size. Do NOT pin it
    // with vg_widget_set_fixed_size — that creates hard min=max constraints that
    // prevent the layout engine from resizing the root on window resize.
    app->root = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!app->root) {
        vgfx_destroy_window(app->window);
        free(app->title);
        app->title = NULL;
        memset(app, 0, sizeof(rt_gui_app_t));
        return NULL;
    }

    app->theme_kind = RT_GUI_THEME_DARK;
    app->root->user_data = app;
    app->shortcuts_global_enabled = 1;
    app->manual_tooltip_delay_ms = 500;

    if (!rt_gui_register_app(app)) {
        vg_widget_destroy(app->root);
        vgfx_destroy_window(app->window);
        free(app->title);
        app->title = NULL;
        memset(app, 0, sizeof(rt_gui_app_t));
        return NULL;
    }

    rt_gui_activate_app(app);
    return app;
}

/// @brief Lazily load the default font on first use.
/// @details Tries the embedded JetBrains Mono Regular first (always available
///          because it's compiled into the binary). If that fails, falls back to
///          well-known system font paths on macOS, Linux, and Windows. The font
///          size is stored in logical points; the window/canvas backend owns
///          HiDPI coordinate scaling.
///          Once loaded, the font is marked as owned by the app and freed in
///          rt_gui_app_destroy. Subsequent calls are no-ops if the font is
///          already loaded.
void rt_gui_ensure_default_font(void) {
    RT_ASSERT_MAIN_THREAD();
    if (!s_current_app || s_current_app->default_font)
        return;

    // Try the embedded JetBrains Mono Regular first (always available).
    s_current_app->default_font =
        vg_font_load(vg_embedded_font_data, (size_t)vg_embedded_font_size);
    if (s_current_app->default_font) {
        s_current_app->default_font_owned = 1;
        s_current_app->default_font_size = 14.0f;
        return;
    }

    // Fall back to system fonts if the embedded data somehow fails.
    const char *font_paths[] = {"/System/Library/Fonts/Menlo.ttc",
                                "/System/Library/Fonts/SFNSMono.ttf",
                                "/System/Library/Fonts/Monaco.dfont",
                                "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                                "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                                "C:\\Windows\\Fonts\\consola.ttf",
                                "C:\\Windows\\Fonts\\cour.ttf",
                                NULL};
    for (int i = 0; font_paths[i]; i++) {
        s_current_app->default_font = vg_font_load_file(font_paths[i]);
        if (s_current_app->default_font) {
            s_current_app->default_font_owned = 1;
            s_current_app->default_font_size = 14.0f;
            break;
        }
    }
}

/// @brief Tear down the GUI application, releasing all owned resources.
/// @details Destruction order is critical to avoid use-after-free:
///          1. Activate the app so cleanup operates on the right global state.
///          2. Clean up feature resources (command palettes, notifications, etc.).
///          3. Destroy all dialogs on the stack and free the stack array.
///          4. Free keyboard shortcuts and their string data.
///          5. Clear global app pointers if this was the active/current app.
///          6. Destroy the theme, default font, retired fonts, root widget tree,
///             and finally the platform window.
///          The app struct itself is GC-allocated, so it will be reclaimed by the
///          collector — we just zero the magic so stale pointers are detected.
/// @param app_ptr Pointer to the app (opaque void* from the Zia layer).
void rt_gui_app_destroy(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = rt_gui_app_handle_checked(app_ptr);
    if (!app)
        return;
    rt_gui_app_t *previous_active = s_active_app;
    rt_gui_activate_app(app);
    if (app->window)
        vgfx_set_resize_callback(app->window, NULL, NULL);
    rt_gui_features_cleanup(app);

    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i]) {
            rt_messagebox_invalidate_dialog(app->dialog_stack[i]);
            rt_filedialog_invalidate_dialog(app->dialog_stack[i]);
            vg_widget_destroy(&app->dialog_stack[i]->base);
        }
    }
    free(app->dialog_stack);
    app->dialog_stack = NULL;
    app->dialog_count = 0;
    app->dialog_cap = 0;

    free(app->command_palettes);
    app->command_palettes = NULL;
    app->command_palette_count = 0;
    app->command_palette_cap = 0;

    for (int i = 0; i < app->shortcut_count; i++) {
        free(app->shortcuts[i].id);
        free(app->shortcuts[i].keys);
        free(app->shortcuts[i].description);
    }
    free(app->shortcuts);
    app->shortcuts = NULL;
    app->shortcut_count = 0;
    app->shortcut_cap = 0;
    free(app->triggered_shortcut_id);
    app->triggered_shortcut_id = NULL;

    if (s_current_app == app || s_active_app == app) {
        rt_gui_macos_menu_app_destroy(app);
        s_current_app = NULL;
        s_active_app = NULL;
        rt_gui_clear_widget_runtime_state();
    }

    free(app->title);
    app->title = NULL;
    if (app->root) {
        rt_widget_forget_runtime_refs(app, app->root);
        vg_widget_destroy(app->root);
        app->root = NULL;
    }
    if (app->theme) {
        if (vg_theme_get_current() == app->theme)
            vg_theme_set_current(vg_theme_dark());
        vg_theme_destroy(app->theme);
        app->theme = NULL;
    }
    if (app->default_font && app->default_font_owned) {
        int default_is_retired = 0;
        for (int i = 0; i < app->retired_font_count; i++) {
            if (app->retired_fonts[i] == app->default_font) {
                default_is_retired = 1;
                break;
            }
        }
        if (!default_is_retired && vg_font_is_live(app->default_font)) {
            if (!rt_gui_retire_font_in_other_apps(app, app->default_font))
                vg_font_destroy(app->default_font);
        }
        app->default_font = NULL;
    }
    for (int i = 0; i < app->retired_font_count; i++) {
        if (app->retired_fonts[i] && vg_font_is_live(app->retired_fonts[i]) &&
            !rt_gui_retire_font_in_other_apps(app, app->retired_fonts[i]))
            vg_font_destroy(app->retired_fonts[i]);
    }
    free(app->retired_fonts);
    app->retired_fonts = NULL;
    app->retired_font_count = 0;
    app->retired_font_cap = 0;
    if (app->window) {
        vgfx_destroy_window(app->window);
        app->window = NULL;
    }
    rt_gui_unregister_app(app);
    app->magic = 0;
    if (previous_active && previous_active != app && rt_gui_is_app_handle(previous_active)) {
        rt_gui_activate_app(previous_active);
    }
}

/// @brief Query whether the application's window has been closed.
/// @details Returns non-zero once the platform window receives a close event
///          (e.g., user clicked the X button, Alt+F4, or Cmd+Q). Zia code
///          polls this in the main loop to decide when to exit:
///          `while not app.ShouldClose() { ... }`
/// @param app_ptr Pointer to the app.
/// @return 1 if the window should close, 0 otherwise. Returns 1 for NULL.
int64_t rt_gui_app_should_close(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_handle_checked(app_ptr);
    if (!app)
        return 1;
    return app->should_close;
}

// Forward declarations for the recursive paint walker.
/// @brief Recursive widget-tree paint pass — emits draw calls for `widget` and its descendants.
static void render_widget_tree(vgfx_window_t window,
                               vg_widget_t *widget,
                               float parent_abs_x,
                               float parent_abs_y);
/// @brief Second pass that draws focus rings, drag previews, and tooltips above the main tree.
static void render_widget_overlay_tree(vgfx_window_t window,
                                       vg_widget_t *widget,
                                       float parent_abs_x,
                                       float parent_abs_y);
/// @brief Forward declaration; defined below.
static int rt_gui_widget_accepts_drop_type(vg_widget_t *widget, const char *type);
/// @brief Forward declaration; defined below.
static int rt_gui_send_event_to_widget(vg_widget_t *widget, vg_event_t *event);

/// @brief True if `widget` paints its own descendants (e.g. ScrollViews clip their own children).
///
/// Such widgets are skipped by the recursive painter — they are
/// expected to call into their custom paint vtable to handle
/// child rendering with whatever clipping / scrolling the widget needs.
static int rt_gui_widget_paints_children_internally(vg_widget_t *widget) {
    if (!widget)
        return 0;
    if (widget->type == VG_WIDGET_SCROLLVIEW)
        return 1;
    return widget->type == VG_WIDGET_CUSTOM && widget->vtable && widget->vtable->paint_overlay;
}

/// @brief Check if a widget accepts a given drag-and-drop data type.
/// @details Parses the widget's comma-separated accepted_drop_types string and
///          compares each entry (case-insensitive) against the given type. If
///          the widget has no type filter or the filter is empty, it accepts all
///          types. Non-drop-target widgets always return 0.
/// @param widget Widget to check.
/// @param type   MIME-like type string from the drag source.
/// @return Non-zero if the widget accepts this type.
static int rt_gui_widget_accepts_drop_type(vg_widget_t *widget, const char *type) {
    if (!widget || !widget->is_drop_target)
        return 0;
    if (!widget->accepted_drop_types || !widget->accepted_drop_types[0])
        return 1;
    if (!type || !type[0])
        return 0;

    const char *p = widget->accepted_drop_types;
    while (*p) {
        while (*p == ' ' || *p == ',')
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && end[-1] == ' ')
            end--;
        size_t len = (size_t)(end - start);
        if (len == strlen(type) && strncasecmp(start, type, len) == 0)
            return 1;
    }
    return 0;
}

/// @brief Find the drag-drop target for a drop of @p type at widget @p hit.
/// @details Walks from @p hit up through its ancestors and returns the first widget
///          (other than the drag @p source) that accepts the payload type, so a drop
///          on a child bubbles to an accepting container. NULL if none qualifies.
static vg_widget_t *rt_gui_find_drop_target(vg_widget_t *hit,
                                            vg_widget_t *source,
                                            const char *type) {
    for (vg_widget_t *candidate = hit; candidate; candidate = candidate->parent) {
        if (candidate == source)
            continue;
        if (rt_gui_widget_accepts_drop_type(candidate, type))
            return candidate;
    }
    return NULL;
}

/// @brief Send a GUI event directly to a specific widget (bypassing tree dispatch).
/// @details Sets the event target to the widget and, for mouse events, converts
///          screen-space coordinates to widget-local coordinates using the
///          widget's screen bounds. Returns whether the event was consumed.
///          Used for command palette and notification manager event routing.
/// @param widget Target widget.
/// @param event  GUI event to deliver.
/// @return 1 if the event was consumed, 0 otherwise.
static int rt_gui_send_event_to_widget(vg_widget_t *widget, vg_event_t *event) {
    if (!widget || !event)
        return 0;
    event->target = widget;
    // NOTE: VG_EVENT_MOUSE_WHEEL intentionally omitted. event.mouse and
    // event.wheel share a union, so writing mouse.x/y would overwrite
    // wheel.delta_x/y and silently destroy the scroll delta before the
    // widget's wheel handler could consume it.
    if (event->type == VG_EVENT_MOUSE_MOVE || event->type == VG_EVENT_MOUSE_DOWN ||
        event->type == VG_EVENT_MOUSE_UP || event->type == VG_EVENT_CLICK ||
        event->type == VG_EVENT_DOUBLE_CLICK) {
        float sx = 0.0f, sy = 0.0f;
        vg_widget_get_screen_bounds(widget, &sx, &sy, NULL, NULL);
        event->mouse.x = event->mouse.screen_x - sx;
        event->mouse.y = event->mouse.screen_y - sy;
    }
    return vg_event_send(widget, event) ? 1 : 0;
}

/// @brief Extract the screen-space X coordinate from any pointer event.
/// @details Mouse and wheel events store their screen coordinates in different
///          union members, so this helper centralizes the disambiguation. Wheel
///          events use `event->wheel.screen_x`; all other pointer events use
///          `event->mouse.screen_x`.
static float rt_gui_event_screen_x(const vg_event_t *event) {
    return event && event->type == VG_EVENT_MOUSE_WHEEL ? event->wheel.screen_x
                                                        : event->mouse.screen_x;
}

/// @brief Extract the screen-space Y coordinate from any pointer event.
/// @details Mirrors `rt_gui_event_screen_x`; dispatches on wheel vs. mouse
///          union to read the correct Y field.
static float rt_gui_event_screen_y(const vg_event_t *event) {
    return event && event->type == VG_EVENT_MOUSE_WHEEL ? event->wheel.screen_y
                                                        : event->mouse.screen_y;
}

/// @brief Snapshot the widget that was clicked this frame into `app->last_clicked`.
/// @details The vg widget system records the last-clicked widget and its timestamp
///          in a shared runtime state. After each dispatched event, we copy that
///          state to the app so Zia code can call `App.LastClicked()` without
///          needing direct access to the vg internals. The timestamp guard
///          prevents stale clicks from a previous frame from being re-captured.
static void rt_gui_capture_reported_click(rt_gui_app_t *app, const vg_event_t *event) {
    if (!app)
        return;
    vg_widget_runtime_state_t state = {0};
    vg_widget_get_runtime_state(&state);
    if (!state.reported_click_widget)
        return;
    if (event && state.reported_click_time_ms != 0 &&
        event->timestamp != state.reported_click_time_ms)
        return;
    app->last_clicked = state.reported_click_widget;
}

/// @brief Recompute which widget the in-progress drag is currently hovering over.
/// @details Hit-tests the current mouse position against the event root to find
///          the drop-target candidate; skips the drag source itself and widgets
///          that reject the source's data type via `rt_gui_widget_accepts_drop_type`.
///          The `_is_drag_over` flag is cleared on the previous target and set
///          on the new one so painters can highlight valid drop zones.
static void rt_gui_update_drag_over_target(rt_gui_app_t *app, vg_widget_t *event_root) {
    if (!app)
        return;

    vg_widget_t *next = NULL;
    if (app->drag_source && !vg_widget_is_live(app->drag_source))
        app->drag_source = NULL;
    if (app->drag_over_widget && !vg_widget_is_live(app->drag_over_widget))
        app->drag_over_widget = NULL;

    if (app->drag_source && event_root) {
        vg_widget_t *hit =
            vg_widget_hit_test(event_root, (float)app->mouse_x, (float)app->mouse_y);
        next = rt_gui_find_drop_target(hit, app->drag_source, app->drag_source->drag_type);
    }

    if (app->drag_over_widget && app->drag_over_widget != next) {
        app->drag_over_widget->_is_drag_over = false;
    }
    if (next) {
        next->_is_drag_over = true;
    }
    app->drag_over_widget = next;
}

/// @brief Clear the pending drag candidate and its recorded start position.
/// @details Called when a press is released without crossing the drag
///          threshold, or when the candidate widget is no longer live.
static void rt_gui_cancel_drag_candidate(rt_gui_app_t *app) {
    if (!app)
        return;
    app->drag_candidate = NULL;
    app->drag_start_x = 0;
    app->drag_start_y = 0;
}

/// @brief Promote a pending press to an active drag once the pointer moves far
///        enough from the press point.
/// @details Uses a squared-distance dead-zone (`dx*dx + dy*dy < 16`, i.e. a
///          4-pixel radius) so small jitters during a click do not start a
///          drag. If the candidate widget died meanwhile the candidate is
///          cancelled instead.
static void rt_gui_maybe_start_drag(rt_gui_app_t *app, vg_widget_t *event_root) {
    if (!app || app->drag_source || !app->drag_candidate)
        return;
    double dx = (double)app->mouse_x - (double)app->drag_start_x;
    double dy = (double)app->mouse_y - (double)app->drag_start_y;
    if (dx * dx + dy * dy < 16)
        return;
    if (!event_root || !vg_widget_is_live(app->drag_candidate)) {
        rt_gui_cancel_drag_candidate(app);
        return;
    }
    app->drag_source = app->drag_candidate;
    app->drag_candidate = NULL;
    app->drag_source->_is_being_dragged = true;
    rt_gui_update_drag_over_target(app, event_root);
}

/// @brief Finish an in-progress drag: hit-test the drop target and deliver it.
/// @details Always clears the drag candidate first. If a drag was active, the
///          widget under the pointer is hit-tested and, when it accepts the
///          source's drag type and is not the source itself, receives the drop.
static void rt_gui_complete_drag_drop(rt_gui_app_t *app, vg_widget_t *event_root) {
    if (!app)
        return;
    rt_gui_cancel_drag_candidate(app);
    if (!app->drag_source)
        return;
    if (!vg_widget_is_live(app->drag_source)) {
        app->drag_source = NULL;
        rt_gui_update_drag_over_target(app, event_root);
        return;
    }

    vg_widget_t *source = app->drag_source;
    source->_is_being_dragged = false;
    vg_widget_t *hit = event_root ? vg_widget_hit_test(
                                        event_root, (float)app->mouse_x, (float)app->mouse_y)
                                  : NULL;
    vg_widget_t *target = rt_gui_find_drop_target(hit, source, source->drag_type);
    if (target) {
        char *new_type = source->drag_type ? strdup(source->drag_type) : NULL;
        char *new_data = source->drag_data ? strdup(source->drag_data) : NULL;
        if ((source->drag_type && !new_type) || (source->drag_data && !new_data)) {
            free(new_type);
            free(new_data);
        } else {
            free(target->_drop_received_type);
            free(target->_drop_received_data);
            target->_drop_received_type = new_type;
            target->_drop_received_data = new_data;
            target->_was_dropped = true;
            target->_is_drag_over = false;
        }
    }
    app->drag_source = NULL;
    rt_gui_update_drag_over_target(app, event_root);
}

/// @brief Return the effective event-dispatch root for hit testing.
/// @details When a modal dialog is open, all hit testing and event dispatch is
///          scoped to the dialog's widget subtree (not the full app root). This
///          prevents clicks from "leaking through" to background widgets.
/// @param app Active app.
/// @return The topmost dialog widget, or the app root if no dialog is open.
static vg_widget_t *rt_gui_hit_root(rt_gui_app_t *app) {
    vg_dialog_t *top_dialog = rt_gui_top_dialog(app);
    return top_dialog ? &top_dialog->base : app->root;
}

/// @brief Measure and position a command palette centered near the top of the window.
/// @details Palettes are positioned at horizontal center and 15% down from the
///          top — mimicking VS Code's Ctrl+Shift+P behavior.
/// @param app     Active app.
/// @param palette Palette to layout.
/// @param win_w   Window width in physical pixels.
/// @param win_h   Window height in physical pixels.
static void rt_gui_layout_command_palette(rt_gui_app_t *app,
                                          vg_commandpalette_t *palette,
                                          int32_t win_w,
                                          int32_t win_h) {
    if (!app || !palette)
        return;
    vg_widget_measure(&palette->base, (float)win_w, (float)win_h);
    float pw = palette->base.measured_width;
    float ph = palette->base.measured_height;
    vg_widget_arrange(&palette->base, (win_w - pw) / 2.0f, win_h * 0.15f, pw, ph);
}

/// @brief Test if a screen-space point falls within the command palette bounds.
/// @details Used to determine whether mouse events should be routed to the
///          palette or dismissed (clicks outside close the palette).
/// @return Non-zero if (x, y) is inside the palette's layout rectangle.
static int rt_gui_palette_contains_point(vg_commandpalette_t *palette, float x, float y) {
    if (!palette)
        return 0;
    return x >= palette->base.x && x < palette->base.x + palette->base.width &&
           y >= palette->base.y && y < palette->base.y + palette->base.height;
}

/// @brief Process all pending platform events and dispatch them to the widget tree.
/// @details This is one half of the main loop (poll + render). It:
///          1. Clears per-frame state (last_clicked, drag-over, shortcut triggers).
///          2. Polls the platform event queue via vgfx_poll_event.
///          3. Handles close events, file drops, keyboard shortcuts.
///          4. Routes mouse/keyboard events through modal dialogs, command
///             palettes, notification manager, and finally the widget tree.
///          5. Manages drag-and-drop state transitions (start → over → drop).
///          Events are converted from platform format (vgfx_event_t) to GUI
///          format (vg_event_t) and dispatched via vg_event_dispatch. The
///          command palette intercepts all keyboard events when visible, and
///          mouse events inside its bounds. Clicks outside dismiss it.
/// @param app_ptr Pointer to the app.
void rt_gui_app_poll(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_handle_checked(app_ptr);
    if (!app)
        return;
    if (!app->window)
        return;
    rt_gui_activate_app(app);
    rt_gui_sync_modal_root(app);

    // Clear last clicked
    app->last_clicked = NULL;
    app->last_statusbar_clicked = NULL;
    app->last_toolbar_clicked = NULL;
    rt_gui_set_last_clicked(NULL);
    vg_widget_clear_reported_click();

    // Clear shortcut triggered flags from previous frame
    rt_shortcuts_clear_triggered(app);
    app->close_requested = 0;

    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();

    // Get mouse position
    vgfx_mouse_pos(app->window, &app->mouse_x, &app->mouse_y);
    rt_mouse_update_pos(app->mouse_x, app->mouse_y);

    // Poll events
    vgfx_event_t event;
    while (vgfx_poll_event(app->window, &event)) {
        app->last_event_time_ms = (uint64_t)event.time_ms;
        if (event.type == VGFX_EVENT_KEY_DOWN) {
            rt_keyboard_on_vgfx_key_down((int64_t)event.data.key.key);
        } else if (event.type == VGFX_EVENT_KEY_UP) {
            rt_keyboard_on_vgfx_key_up((int64_t)event.data.key.key);
        } else if (event.type == VGFX_EVENT_TEXT_INPUT) {
            rt_keyboard_text_input((int32_t)event.data.text.codepoint);
        } else if (event.type == VGFX_EVENT_MOUSE_MOVE) {
            rt_mouse_update_pos((int64_t)event.data.mouse_move.x, (int64_t)event.data.mouse_move.y);
        } else if (event.type == VGFX_EVENT_MOUSE_DOWN) {
            rt_mouse_update_pos(
                (int64_t)event.data.mouse_button.x, (int64_t)event.data.mouse_button.y);
            rt_mouse_button_down((int64_t)event.data.mouse_button.button);
        } else if (event.type == VGFX_EVENT_MOUSE_UP) {
            rt_mouse_update_pos(
                (int64_t)event.data.mouse_button.x, (int64_t)event.data.mouse_button.y);
            rt_mouse_button_up((int64_t)event.data.mouse_button.button);
        } else if (event.type == VGFX_EVENT_SCROLL) {
            rt_mouse_update_pos((int64_t)event.data.scroll.x, (int64_t)event.data.scroll.y);
            rt_mouse_update_wheel((double)event.data.scroll.delta_x, (double)event.data.scroll.delta_y);
        }

        if (event.type == VGFX_EVENT_CLOSE) {
            app->close_requested = 1;
            if (!app->prevent_close)
                app->should_close = 1;
            continue;
        }

        // File drop events — collect per-app state.
        if (event.type == VGFX_EVENT_FILE_DROP) {
            rt_gui_file_drop_add(app, event.data.file_drop.path);
            continue;
        }

        // Convert platform event to GUI event and dispatch to widget tree
        if (app->root) {
            vg_event_t gui_event = vg_event_from_platform(&event);
            if (gui_event.type == VG_EVENT_NONE)
                continue;
            vg_commandpalette_t *top_palette = rt_gui_top_visible_command_palette(app);
            vg_widget_t *event_root = rt_gui_hit_root(app);
            int32_t win_w = 0, win_h = 0;
            if (top_palette) {
                vgfx_get_size(app->window, &win_w, &win_h);
                rt_gui_layout_command_palette(app, top_palette, win_w, win_h);
            }

            // Track mouse position from events
            if (event.type == VGFX_EVENT_MOUSE_DOWN || event.type == VGFX_EVENT_MOUSE_UP) {
                app->mouse_x = event.data.mouse_button.x;
                app->mouse_y = event.data.mouse_button.y;
            }
            if (event.type == VGFX_EVENT_MOUSE_MOVE) {
                app->mouse_x = event.data.mouse_move.x;
                app->mouse_y = event.data.mouse_move.y;
            }
            rt_gui_update_drag_over_target(app, event_root);
            if (event.type == VGFX_EVENT_MOUSE_MOVE) {
                rt_gui_maybe_start_drag(app, event_root);

                // Drag-and-drop: update drag-over state during drag
                rt_gui_update_drag_over_target(app, event_root);

                if (top_palette) {
                    vg_tooltip_manager_on_leave(vg_tooltip_manager_get());
                } else {
                    vg_widget_t *hovered =
                        vg_widget_hit_test(event_root, (float)app->mouse_x, (float)app->mouse_y);
                    if (hovered) {
                        vg_tooltip_manager_on_hover(
                            vg_tooltip_manager_get(), hovered, app->mouse_x, app->mouse_y);
                    } else {
                        vg_tooltip_manager_on_leave(vg_tooltip_manager_get());
                    }
                }
            }

            if (top_palette && top_palette->is_visible) {
                int is_mouse_event =
                    gui_event.type == VG_EVENT_MOUSE_MOVE ||
                    gui_event.type == VG_EVENT_MOUSE_DOWN || gui_event.type == VG_EVENT_MOUSE_UP ||
                    gui_event.type == VG_EVENT_CLICK || gui_event.type == VG_EVENT_DOUBLE_CLICK ||
                    gui_event.type == VG_EVENT_MOUSE_WHEEL;
                int inside_palette = rt_gui_palette_contains_point(
                    top_palette, rt_gui_event_screen_x(&gui_event), rt_gui_event_screen_y(&gui_event));

                if (is_mouse_event && inside_palette) {
                    rt_gui_send_event_to_widget(&top_palette->base, &gui_event);
                    rt_gui_capture_reported_click(app, &gui_event);
                    if (event.type == VGFX_EVENT_MOUSE_UP)
                        rt_gui_complete_drag_drop(app, rt_gui_hit_root(app));
                    continue;
                }
                if (is_mouse_event && !inside_palette) {
                    if (gui_event.type == VG_EVENT_MOUSE_DOWN || gui_event.type == VG_EVENT_CLICK) {
                        vg_commandpalette_hide(top_palette);
                    }
                    if (event.type == VGFX_EVENT_MOUSE_UP)
                        rt_gui_complete_drag_drop(app, rt_gui_hit_root(app));
                    continue;
                }
                if (gui_event.type == VG_EVENT_KEY_DOWN || gui_event.type == VG_EVENT_KEY_UP ||
                    gui_event.type == VG_EVENT_KEY_CHAR) {
                    rt_gui_send_event_to_widget(&top_palette->base, &gui_event);
                    rt_gui_capture_reported_click(app, &gui_event);
                    continue;
                }
            }

            if (app->notification_manager &&
                rt_gui_send_event_to_widget(&app->notification_manager->base, &gui_event)) {
                rt_gui_capture_reported_click(app, &gui_event);
                if (event.type == VGFX_EVENT_MOUSE_UP)
                    rt_gui_complete_drag_drop(app, rt_gui_hit_root(app));
                continue;
            }

            // Drag-and-drop: start a drag candidate on mouse-down only after
            // overlays have had first chance to route or consume the event.
            if (event.type == VGFX_EVENT_MOUSE_DOWN && !app->drag_source &&
                event.data.mouse_button.button == VGFX_MOUSE_LEFT) {
                vg_widget_t *hit =
                    vg_widget_hit_test(event_root, (float)app->mouse_x, (float)app->mouse_y);
                if (hit && hit->draggable) {
                    app->drag_candidate = hit;
                    app->drag_start_x = app->mouse_x;
                    app->drag_start_y = app->mouse_y;
                }
            }

            if (gui_event.type == VG_EVENT_KEY_DOWN) {
                bool handled = vg_event_dispatch(app->root, &gui_event);
                rt_gui_capture_reported_click(app, &gui_event);
                rt_gui_sync_modal_root(app);
                if (handled || rt_gui_top_dialog(app))
                    continue;

                // Let the focused widget/root see the key first; only unhandled
                // non-modal key-downs become global shortcuts.
                if (rt_shortcuts_check_key(app, gui_event.key.key, (int)gui_event.modifiers))
                    continue;
                continue;
            }

            // Dispatch all events to widget tree (handles focus, keyboard, etc.)
            vg_event_dispatch(app->root, &gui_event);
            rt_gui_capture_reported_click(app, &gui_event);
            rt_gui_sync_modal_root(app);
            if (event.type == VGFX_EVENT_MOUSE_UP)
                rt_gui_complete_drag_drop(app, rt_gui_hit_root(app));
        }
    }
}

/// @brief Layout, paint, and present the entire UI to the window.
/// @details This is the render half of the main loop. It:
///          1. Ensures the default font is loaded and theme is up-to-date.
///          2. Runs the vg layout engine (vg_widget_layout) with current window
///             dimensions, computing sizes and positions for all widgets.
///          3. Clears the framebuffer with the theme's background color.
///          4. Walks the widget tree via render_widget_tree, converting relative
///             coordinates to absolute for painting, then restoring them.
///          5. Paints overlays: popup dropdowns, command palettes, dialogs,
///             notifications, and tooltips — each above the previous layer.
///          6. Presents the framebuffer via vgfx_update.
///          Widget coordinates remain relative after this call, so hit testing
///          during the next poll() uses parent-chain walks correctly.
/// @param app_ptr Pointer to the app.
void rt_gui_app_render(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_handle_checked(app_ptr);
    if (!app)
        return;
    if (!app->window)
        return;
    rt_gui_activate_app(app);

    // Ensure a default font is available for widget rendering.
    rt_gui_ensure_default_font();
    rt_gui_refresh_theme(app);

    if (app->notification_manager && app->default_font) {
        vg_notification_manager_set_font(
            app->notification_manager, app->default_font, app->default_font_size);
    }
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] && app->default_font) {
            vg_commandpalette_set_font(
                app->command_palettes[i], app->default_font, app->default_font_size);
        }
    }
    if (app->manual_tooltip && app->default_font) {
        app->manual_tooltip->font = app->default_font;
        app->manual_tooltip->font_size = app->default_font_size;
    }

    // Cache window dimensions once — reused for layout and dialog centering.
    int32_t win_w = 0, win_h = 0;
    vgfx_get_size(app->window, &win_w, &win_h);
    uint64_t now_ms = rt_gui_now_ms();
    float dt = 0.0f;
    if (app->last_render_time_ms > 0 && now_ms > app->last_render_time_ms) {
        dt = (float)(now_ms - app->last_render_time_ms) / 1000.0f;
        if (dt > 0.25f)
            dt = 0.25f;
    }
    app->last_render_time_ms = now_ms;

    bool did_layout = false;
    bool size_changed = false;
    if (app->root) {
        size_changed = app->last_layout_width != win_w || app->last_layout_height != win_h;
        if (size_changed || rt_gui_widget_tree_needs_layout(app->root)) {
            vg_widget_layout(app->root, (float)win_w, (float)win_h);
            app->last_layout_width = win_w;
            app->last_layout_height = win_h;
            did_layout = true;
        }
        rt_gui_tick_widget_tree(app->root, dt);
    }

    size_changed = app->last_layout_width != win_w || app->last_layout_height != win_h;
    for (int i = 0; i < app->command_palette_count; i++) {
        vg_commandpalette_t *palette = app->command_palettes[i];
        if (!palette || !palette->is_visible)
            continue;
        rt_gui_tick_widget_tree(&palette->base, dt);
        rt_gui_layout_command_palette(app, palette, win_w, win_h);
    }

    for (int i = 0; i < app->dialog_count; i++) {
        vg_dialog_t *dlg = app->dialog_stack[i];
        if (!dlg || !dlg->is_open)
            continue;
        rt_gui_tick_widget_tree(&dlg->base, dt);
        if (app->default_font) {
            vg_dialog_set_font(dlg, app->default_font, app->default_font_size);
        }
        if (dlg->base.measured_width < 1.0f || dlg->base.needs_layout) {
            vg_widget_measure(&dlg->base, (float)win_w, (float)win_h);
        }
        float ref_x = 0.0f;
        float ref_y = 0.0f;
        float ref_w = (float)win_w;
        float ref_h = (float)win_h;
        if (dlg->modal_parent) {
            vg_widget_get_screen_bounds(dlg->modal_parent, &ref_x, &ref_y, &ref_w, &ref_h);
        }
        float dw = dlg->base.measured_width;
        float dh = dlg->base.measured_height;
        vg_widget_arrange(
            &dlg->base, ref_x + (ref_w - dw) / 2.0f, ref_y + (ref_h - dh) / 2.0f, dw, dh);
    }

    if (app->notification_manager) {
        app->notification_manager->base.x = 0.0f;
        app->notification_manager->base.y = 0.0f;
        app->notification_manager->base.width = (float)win_w;
        app->notification_manager->base.height = (float)win_h;
        vg_notification_manager_update(app->notification_manager, now_ms);
    }

    vg_tooltip_manager_t *tooltip_mgr = vg_tooltip_manager_get();
    vg_tooltip_manager_update(tooltip_mgr, now_ms);
    if (tooltip_mgr->active_tooltip && tooltip_mgr->active_tooltip->is_visible && app->default_font) {
        tooltip_mgr->active_tooltip->font = app->default_font;
        tooltip_mgr->active_tooltip->font_size = app->default_font_size;
    }
    if (app->manual_tooltip && app->manual_tooltip->is_visible && app->default_font) {
        app->manual_tooltip->font = app->default_font;
        app->manual_tooltip->font_size = app->default_font_size;
    }

    bool root_needs_paint = rt_gui_widget_tree_needs_paint(app->root);
    bool overlays_need_paint = rt_gui_app_overlays_need_paint(app);
    if (!did_layout && !size_changed && !root_needs_paint && !overlays_need_paint) {
        vgfx_pump_events(app->window);
        rt_sleep_ms(16);
        return;
    }

    // Clear with theme background
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_cls(app->window, theme ? theme->colors.bg_secondary : 0xFF1E1E1E);

    // Render widget tree — absolute offsets are accumulated during traversal
    // so widget->x/y stay relative. This is critical: hit testing in poll()
    // uses vg_widget_get_screen_bounds() which walks the parent chain from
    // relative coords. If we converted to absolute here, hit testing would
    // double-count parent offsets and fail.
    if (app->root) {
        render_widget_tree(app->window, app->root, 0.0f, 0.0f);
    }

    // Paint widget overlays (dropdowns, menubar popups, floating panels) in a
    // second pass after the normal tree walk.
    if (app->root) {
        render_widget_overlay_tree(app->window, app->root, 0.0f, 0.0f);
    }

    for (int i = 0; i < app->command_palette_count; i++) {
        vg_commandpalette_t *palette = app->command_palettes[i];
        if (!palette || !palette->is_visible)
            continue;
        if (palette->base.vtable && palette->base.vtable->paint) {
            palette->base.vtable->paint(&palette->base, (void *)app->window);
        }
    }

    for (int i = 0; i < app->dialog_count; i++) {
        vg_dialog_t *dlg = app->dialog_stack[i];
        if (!dlg || !dlg->is_open)
            continue;
        if (dlg->base.vtable && dlg->base.vtable->paint) {
            dlg->base.vtable->paint(&dlg->base, (void *)app->window);
        }
    }

    if (app->notification_manager) {
        if (app->notification_manager->base.vtable &&
            app->notification_manager->base.vtable->paint) {
            app->notification_manager->base.vtable->paint(&app->notification_manager->base,
                                                          (void *)app->window);
        }
    }

    if (tooltip_mgr->active_tooltip && tooltip_mgr->active_tooltip->is_visible) {
        vg_widget_measure(&tooltip_mgr->active_tooltip->base, (float)win_w, (float)win_h);
        if (tooltip_mgr->active_tooltip->base.vtable &&
            tooltip_mgr->active_tooltip->base.vtable->paint) {
            tooltip_mgr->active_tooltip->base.vtable->paint(&tooltip_mgr->active_tooltip->base,
                                                            (void *)app->window);
        }
    }

    if (app->manual_tooltip && app->manual_tooltip->is_visible) {
        vg_widget_measure(&app->manual_tooltip->base, (float)win_w, (float)win_h);
        if (app->manual_tooltip->base.vtable && app->manual_tooltip->base.vtable->paint) {
            app->manual_tooltip->base.vtable->paint(&app->manual_tooltip->base,
                                                    (void *)app->window);
        }
    }

    // Present
    rt_gui_app_clear_paint_flags(app);
    vgfx_update(app->window);
}

/// @brief Return the root container widget of the app's widget tree.
/// @details The root is a plain VG_WIDGET_CONTAINER that fills the window. All
///          user-created widgets are added as children (or descendants) of this
///          root. Layout is driven by the window's physical dimensions.
/// @param app_ptr Pointer to the app.
/// @return Root vg_widget_t pointer, or NULL if the app is NULL.
void *rt_gui_app_get_root(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_handle_checked(app_ptr);
    if (!app)
        return NULL;
    return app->root;
}

/// @brief Replace the app's default font and propagate it to all existing widgets.
/// @details Sets the new font as the app's default (used by all future widget
///          constructors), then walks the entire widget tree, all dialogs,
///          command palettes, the notification manager, and the manual tooltip,
///          updating each to the new font. The old font is not freed immediately
///          — it is retained in the app's retired-fonts list and freed at
///          app_destroy, because widgets may still reference it during the
///          current frame.
/// @param app_ptr Pointer to the app.
/// @param font    New font to use (vg_font_t*).
/// @param size    Font size in logical points.
void rt_gui_app_set_font(void *app_ptr, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_handle_checked(app_ptr);
    if (!app)
        return;
    rt_gui_activate_app(app);
    vg_font_t *new_font = rt_gui_font_handle_checked(font);
    if (!new_font)
        return;
    vg_font_t *old_font = app->default_font;
    int old_owned = app->default_font_owned;
    app->default_font = new_font;
    app->default_font_size = (float)rt_gui_sanitize_font_size(size, 14.0);
    app->default_font_owned = 0;

    if (app->root && app->default_font) {
        rt_gui_apply_font_to_widget(app->root, app->default_font, app->default_font_size);
    }

    if (app->notification_manager && app->default_font) {
        vg_notification_manager_set_font(
            app->notification_manager, app->default_font, app->default_font_size);
    }
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] && app->default_font) {
            vg_commandpalette_set_font(
                app->command_palettes[i], app->default_font, app->default_font_size);
        }
    }
    if (app->manual_tooltip && app->default_font) {
        app->manual_tooltip->font = app->default_font;
        app->manual_tooltip->font_size = app->default_font_size;
    }
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] && app->default_font) {
            vg_dialog_set_font(app->dialog_stack[i], app->default_font, app->default_font_size);
        }
    }
    if (old_owned && old_font && old_font != app->default_font)
        rt_gui_retire_font(app, old_font);
}

/// @brief Recursively paint a widget subtree, accumulating absolute screen coordinates.
/// @details Adds parent_abs_x/y to widget->x/y so paint functions see absolute screen
///          positions, calls the vtable paint function, then immediately restores the
///          relative coordinates. Restoration is critical: hit testing in the next poll
///          calls vg_widget_get_screen_bounds which walks the parent chain — leaving
///          absolute coords would double-count every ancestor offset. Widgets that paint
///          their own children (e.g. ScrollView) are skipped by the child recursion.
/// @param window       Platform window handle passed to vtable paint.
/// @param widget       Root of the subtree to paint (skipped if NULL or invisible).
/// @param parent_abs_x Accumulated absolute X of the parent widget.
/// @param parent_abs_y Accumulated absolute Y of the parent widget.
typedef struct rt_gui_render_frame {
    vg_widget_t *widget;
    float parent_abs_x;
    float parent_abs_y;
} rt_gui_render_frame_t;

/// @brief Push a render frame (widget + accumulated parent absolute origin) onto the
///        explicit paint stack, growing it as needed.
/// @details Backs render_widget_tree's iterative (non-recursive) traversal so deeply
///          nested layouts cannot overflow the C stack. Capacity doubles from 64 via
///          an overflow-guarded realloc.
/// @return true on success — and on a NULL widget, treated as a no-op; false on
///         capacity overflow or realloc failure.
static bool rt_gui_render_stack_push(rt_gui_render_frame_t **frames,
                                     size_t *count,
                                     size_t *cap,
                                     vg_widget_t *widget,
                                     float parent_abs_x,
                                     float parent_abs_y) {
    if (!widget)
        return true;
    if (*count == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 64;
        if (new_cap < *cap || new_cap > SIZE_MAX / sizeof(**frames))
            return false;
        rt_gui_render_frame_t *new_frames =
            (rt_gui_render_frame_t *)realloc(*frames, new_cap * sizeof(*new_frames));
        if (!new_frames)
            return false;
        *frames = new_frames;
        *cap = new_cap;
    }
    (*frames)[(*count)++] = (rt_gui_render_frame_t){widget, parent_abs_x, parent_abs_y};
    return true;
}

/// @brief Paint a widget subtree into @p window, iteratively (no recursion).
/// @details Uses an explicit stack of frames so deep layouts can't overflow the C
///          stack. Each frame carries the parent's absolute origin; a widget's
///          stored relative (x,y) is temporarily promoted to absolute for painting
///          and its children pushed with the new origin. Invisible widgets are skipped.
static void render_widget_tree(vgfx_window_t window,
                               vg_widget_t *widget,
                               float parent_abs_x,
                               float parent_abs_y) {
    rt_gui_render_frame_t *frames = NULL;
    size_t count = 0;
    size_t cap = 0;
    if (!rt_gui_render_stack_push(&frames, &count, &cap, widget, parent_abs_x, parent_abs_y))
        return;

    while (count > 0) {
        rt_gui_render_frame_t frame = frames[--count];
        widget = frame.widget;
        if (!widget || !widget->visible)
            continue;

        float abs_x = widget->x + frame.parent_abs_x;
        float abs_y = widget->y + frame.parent_abs_y;
        float rel_x = widget->x;
        float rel_y = widget->y;
        widget->x = abs_x;
        widget->y = abs_y;

        if (widget->vtable && widget->vtable->paint) {
            bool was_screen_space = widget->_paint_screen_space;
            widget->_paint_screen_space = true;
            widget->vtable->paint(widget, (void *)window);
            widget->_paint_screen_space = was_screen_space;
        }

        widget->x = rel_x;
        widget->y = rel_y;

        if (rt_gui_widget_paints_children_internally(widget))
            continue;

        for (vg_widget_t *child = widget->last_child; child; child = child->prev_sibling) {
            if (!rt_gui_render_stack_push(&frames, &count, &cap, child, abs_x, abs_y)) {
                free(frames);
                return;
            }
        }
    }
    free(frames);
}

/// @brief Second-pass overlay paint: draws popup menus, focus rings, floating panels, and drag
///        previews above the main widget tree.
/// @details Same absolute-coordinate trick as render_widget_tree: temporarily converts
///          widget->x/y to screen space for paint_overlay, then restores relative. This
///          pass runs after the primary tree walk so overlays always appear on top. Widgets
///          that internally paint children (ScrollView) skip the recursive child walk.
/// @param window       Platform window handle.
/// @param widget       Root of the subtree (skipped if NULL or invisible).
/// @param parent_abs_x Accumulated absolute X of the parent widget.
/// @param parent_abs_y Accumulated absolute Y of the parent widget.
static void render_widget_overlay_tree(vgfx_window_t window,
                                       vg_widget_t *widget,
                                       float parent_abs_x,
                                       float parent_abs_y) {
    rt_gui_render_frame_t *frames = NULL;
    size_t count = 0;
    size_t cap = 0;
    if (!rt_gui_render_stack_push(&frames, &count, &cap, widget, parent_abs_x, parent_abs_y))
        return;

    while (count > 0) {
        rt_gui_render_frame_t frame = frames[--count];
        widget = frame.widget;
        if (!widget || !widget->visible)
            continue;

        float abs_x = widget->x + frame.parent_abs_x;
        float abs_y = widget->y + frame.parent_abs_y;
        float rel_x = widget->x;
        float rel_y = widget->y;
        widget->x = abs_x;
        widget->y = abs_y;

        if (widget->vtable && widget->vtable->paint_overlay) {
            bool was_screen_space = widget->_paint_screen_space;
            widget->_paint_screen_space = true;
            widget->vtable->paint_overlay(widget, (void *)window);
            widget->_paint_screen_space = was_screen_space;
        }

        widget->x = rel_x;
        widget->y = rel_y;

        if (rt_gui_widget_paints_children_internally(widget))
            continue;

        for (vg_widget_t *child = widget->last_child; child; child = child->prev_sibling) {
            if (!rt_gui_render_stack_push(&frames, &count, &cap, child, abs_x, abs_y)) {
                free(frames);
                return;
            }
        }
    }
    free(frames);
}

#else /* !VIPER_ENABLE_GRAPHICS */

// ===========================================================================
// Headless stubs — match the public-API prototypes above so that
// non-graphical builds (server / CLI / ViperDOS) link cleanly. Each
// stub safely no-ops or returns a sentinel (NULL, 0, 1 for "should
// close"). Doc comments inherit from the real implementations above.
// ===========================================================================

rt_gui_app_t *s_current_app = NULL;

/// @brief Stub: graphics disabled — no-op; modal dialog management requires a live app.
void rt_gui_set_active_dialog(void *dlg) {
    (void)dlg;
}

/// @brief Stub: graphics disabled — returns NULL; no window or widget tree is created.
void *rt_gui_app_new(rt_string title, int64_t width, int64_t height) {
    (void)title;
    (void)width;
    (void)height;
    return NULL;
}

/// @brief No-op stub: default font loading (graphics disabled).
void rt_gui_ensure_default_font(void) {}

/// @brief No-op stub: app destruction (graphics disabled).
void rt_gui_app_destroy(void *app_ptr) {
    (void)app_ptr;
}

/// @brief Stub: always returns 1 (close immediately when graphics disabled).
int64_t rt_gui_app_should_close(void *app_ptr) {
    (void)app_ptr;
    return 1;
}

/// @brief Poll the app.
void rt_gui_app_poll(void *app_ptr) {
    (void)app_ptr;
}

/// @brief Render the app.
void rt_gui_app_render(void *app_ptr) {
    (void)app_ptr;
}

/// @brief Stub: graphics disabled — returns NULL; no root widget exists without a live app.
void *rt_gui_app_get_root(void *app_ptr) {
    (void)app_ptr;
    return NULL;
}

/// @brief Set the font of the app.
void rt_gui_app_set_font(void *app_ptr, void *font, double size) {
    (void)app_ptr;
    (void)font;
    (void)size;
}

#endif /* VIPER_ENABLE_GRAPHICS */
