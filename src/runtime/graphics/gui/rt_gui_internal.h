//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_gui_internal.h
// Purpose: Shared internal header for the split rt_gui implementation modules, declaring the global
// application pointer, default font state, and common helper functions.
//
// Key invariants:
//   - s_current_app must be set before widget constructors run.
//   - Default font is lazily initialized on first use.
//   - This header is implementation-only; it is not part of the public runtime API.
//   - App state persists for the duration of the GUI event loop.
//
// Ownership/Lifetime:
//   - Internal module header; not included by user code or public headers.
//   - All state declared here is owned by the GUI subsystem.
//
// Links: src/runtime/graphics/rt_gui.h, src/runtime/core/rt_string.h, src/runtime/oop/rt_object.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_gui.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string.h"

#include "../lib/graphics/include/vgfx.h"
#include "../lib/gui/include/vg_event.h"
#include "../lib/gui/include/vg_font.h"
#include "../lib/gui/include/vg_ide_widgets.h"
#include "../lib/gui/include/vg_layout.h"
#include "../lib/gui/include/vg_theme.h"
#include "../lib/gui/include/vg_widget.h"
#include "../lib/gui/include/vg_widgets.h"

// Native file dialogs on macOS
#if RT_PLATFORM_MACOS
#include "../lib/gui/src/dialogs/vg_filedialog_native.h"
#endif

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Unix: strings.h provides strcasecmp.
#if !RT_PLATFORM_WINDOWS
#include <strings.h>
#endif

/// @brief Compare two GUI ASCII tokens case-insensitively without exporting a macro.
/// @details Windows exposes the comparison as `_stricmp`; POSIX exposes `strcasecmp`. Keeping the
///          choice behind a local helper avoids changing the namespace of every translation unit
///          that includes this internal GUI header.
/// @param a First NUL-terminated token.
/// @param b Second NUL-terminated token.
/// @return Less than, equal to, or greater than zero using the platform comparison semantics.
static inline int rt_gui_ascii_casecmp(const char *a, const char *b) {
#if RT_PLATFORM_WINDOWS
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

//=============================================================================
// App state (defined in rt_gui_app.c)
//=============================================================================

/// @brief Internal application state for the GUI runtime.
/// @details Holds the graphics window, root widget, default font, mouse state,
///          and close flag. Defined in rt_gui_app.c and shared across split GUI modules.
typedef struct {
    char *id;
    char *keys;
    char *description;
    int enabled;
    int triggered;
    int parsed_ctrl;
    int parsed_shift;
    int parsed_alt;
    int parsed_super;
    int parsed_key;
} rt_gui_shortcut_t;

typedef struct {
    char **files;
    int64_t file_count;
    int64_t was_dropped;
} rt_gui_file_drop_data_t;

typedef enum {
    RT_GUI_THEME_DARK = 0,
    RT_GUI_THEME_LIGHT = 1,
    RT_GUI_THEME_SYSTEM = 2,
    RT_GUI_THEME_CUSTOM = 3,
} rt_gui_theme_kind_t;

typedef struct {
    uint64_t magic;
    vgfx_window_t window;      ///< Underlying graphics window handle.
    vg_widget_t *root;         ///< Root widget container for the UI hierarchy.
    vg_font_t *default_font;   ///< Regular proportional default font (lazily loaded).
    float default_font_size;   ///< Default font size in logical points.
    int default_font_owned;    ///< Non-zero when default_font is owned by the app.
    vg_font_t *role_font_bold; ///< App-owned bold role fallback; may alias default_font.
    vg_font_t *role_font_mono; ///< App-owned monospace role fallback; may alias default_font.
    int role_font_bold_owned;  ///< Non-zero when role_font_bold has independent ownership.
    int role_font_mono_owned;  ///< Non-zero when role_font_mono has independent ownership.
    vg_font_t **retired_fonts; ///< Fonts awaiting an unused safe presentation generation.
    uint64_t *retired_font_generations; ///< Generation at which each matching font was retired.
    int retired_font_count;             ///< Number of valid parallel retirement entries.
    int retired_font_cap;               ///< Allocated capacity of both retirement arrays.
    int64_t should_close;               ///< Non-zero when the application should exit.
    int64_t close_requested;            ///< Non-zero when a close request arrived this frame.
    int32_t prevent_close;     ///< Non-zero when close requests should not set should_close.
    vg_widget_t *last_clicked; ///< Widget clicked during the current frame.
    vg_statusbar_item_t *last_statusbar_clicked;
    vg_toolbar_item_t *last_toolbar_clicked;
    int32_t mouse_x; ///< Current mouse X coordinate in window space.
    int32_t mouse_y; ///< Current mouse Y coordinate in window space.
    uint64_t last_event_time_ms;
    uint64_t last_render_time_ms;
    int32_t animations_active; ///< Non-zero while the last scheduler pass found unsettled motion.
    int32_t frame_delta_override_pending; ///< One-shot deterministic delta consumed by Render.
    double frame_delta_override_ms;       ///< Pending deterministic frame delta in milliseconds.
    double scheduler_elapsed_ms;          ///< Full caller-visible elapsed scheduler time.
    double scheduler_clock_ms; ///< Timer clock advanced by real or deterministic frame deltas.
    uint64_t frame_generation; ///< Non-zero generation advanced once per render scheduler pass.
    int32_t last_layout_width;
    int32_t last_layout_height;
    vg_theme_t *theme;
    const vg_theme_t *theme_base;
    vg_theme_t *custom_theme_base; ///< Owned unscaled custom palette, or NULL when unset.
    float theme_scale;
    uint64_t theme_revision; ///< Monotonic revision incremented after each installed theme copy.
    uint64_t theme_reported_revision; ///< Revision consumed only by Theme.WasChanged.
    float user_scale; ///< User UI zoom multiplier applied atop the HiDPI scale (1.0 = default).
    rt_gui_theme_kind_t theme_kind;
    int32_t system_prefers_dark; ///< Last normalized platform appearance used by System mode.
    int32_t accessibility_high_contrast;  ///< Explicit per-app high-contrast preference.
    int32_t accessibility_reduced_motion; ///< Explicit per-app reduced-motion preference.
    uint64_t accessibility_revision;      ///< Monotonic preference/announcement revision.
    char *title;                          ///< Window title (owned, heap-allocated).
    vg_dialog_t **dialog_stack;
    int dialog_count;
    int dialog_cap;
    vg_widget_t *drag_candidate;
    int32_t drag_start_x;
    int32_t drag_start_y;
    vg_widget_t *drag_source;
    vg_widget_t *drag_over_widget;
    rt_gui_shortcut_t *shortcuts;
    int shortcut_count;
    int shortcut_cap;
    int shortcuts_global_enabled;
    char *triggered_shortcut_id;
    char **triggered_shortcut_ids;
    int triggered_shortcut_count;
    int triggered_shortcut_cap;
    rt_gui_file_drop_data_t file_drop;
    vg_commandpalette_t **command_palettes;
    int command_palette_count;
    int command_palette_cap;
    vg_contextmenu_t *active_context_menu; ///< Standalone right-click menu overlay (not parented
                                           ///< into app->root); painted and routed input directly.
    vg_notification_manager_t *notification_manager;
    vg_tooltip_manager_t tooltip_manager_state;
    vg_tooltip_t *manual_tooltip;
    uint32_t manual_tooltip_delay_ms;
    vg_widget_runtime_state_t widget_runtime_state;

    // Damage-region (partial) rendering — plan 07. When partial_paint_enabled is
    // 0 (kill switch via VIPER_GUI_FULL_REPAINT=1), every dirty frame full-clears
    // and repaints the whole tree, matching the pre-plan-07 behavior exactly.
    int32_t partial_paint_enabled; ///< 1 = damage-region path allowed; 0 = always full repaint.
    uint64_t frames_full;          ///< Dirty frames that took the full-window repaint path.
    uint64_t frames_partial;       ///< Dirty frames that took the damage-region repaint path.
    float last_damage_w;           ///< Width of the last partial-paint damage rect (0 if full).
    float last_damage_h;           ///< Height of the last partial-paint damage rect (0 if full).
    float overlay_last_x;          ///< Left edge of the last painted detached-overlay union.
    float overlay_last_y;          ///< Top edge of the last painted detached-overlay union.
    float overlay_last_w;          ///< Width of the last painted detached-overlay union.
    float overlay_last_h;          ///< Height of the last painted detached-overlay union.
    int32_t overlay_last_valid;    ///< Non-zero when the retained overlay union contains pixels.
} rt_gui_app_t;

#define RT_GUI_APP_MAGIC UINT64_C(0x5254475541505031)

/// @brief Global pointer to the current app for widget constructors to access the default font.
extern rt_gui_app_t *s_current_app;

/// @brief The currently active GUI app (the one receiving input/rendering).
rt_gui_app_t *rt_gui_get_active_app(void);
/// @brief Make @p app the active GUI app.
void rt_gui_activate_app(rt_gui_app_t *app);
/// @brief Resolve the owning GUI app for a widget (NULL if unparented).
rt_gui_app_t *rt_gui_app_from_widget(vg_widget_t *widget);

/// @brief Resolve a widget handle's owning app for isolated runtime modules.
/// @details The opaque return avoids exposing private app layout to modules that intentionally
///          include only the public GUI bridge. Invalid, destroyed, or unparented widgets return
///          NULL. The pointer is borrowed and must never be retained past app destruction.
/// @param handle Candidate live widget handle.
/// @return Borrowed owning app pointer, or NULL.
/// @internal
void *rt_gui_widget_owner_app(void *handle);

/// @brief Return one owning app's current scheduler frame generation.
/// @details The function validates the opaque app handle and is side-effect free. Generation zero
///          means the app has not rendered yet; overflow wraps directly to one so zero remains the
///          permanent pre-render sentinel.
/// @param app Candidate opaque app pointer.
/// @return Current generation, or zero for an invalid app.
/// @internal
uint64_t rt_gui_app_frame_generation_for_owner(void *app);
/// @brief Monotonic millisecond clock used for GUI timing/animation.
uint64_t rt_gui_now_ms(void);
/// @brief Return the app's effective logical-to-framebuffer scale.
/// @details Multiplies the platform window scale by the app's user UI zoom. Invalid, zero, or
///          non-finite factors are replaced by 1.0, and a non-finite product also falls back to
///          1.0. The function does not mutate or activate the app.
/// @param app App whose scale is requested; NULL represents the identity scale.
/// @return Positive finite effective scale, with 1.0 as the invalid-app fallback.
float rt_gui_app_effective_scale(const rt_gui_app_t *app);
/// @brief Convert the app's logical default font size to effective framebuffer pixels.
/// @details Uses @ref rt_gui_app_effective_scale exactly once. The stored font size remains in
///          logical points so subsequent monitor-scale or user-zoom changes can be reapplied
///          without cumulative rounding or multiplication.
/// @param app App whose default font size is requested; NULL uses 14 logical points at 1x.
/// @return Positive finite font size in framebuffer pixels.
float rt_gui_app_effective_font_size(const rt_gui_app_t *app);

/// @brief Atomically copy already-converted straight RGBA bytes into a GUI Image.
/// @details This internal bridge lets streaming media reuse a conversion buffer and bypass the
///          public Pixels-to-RGBA temporary allocation. The lower image owns its own reusable copy;
///          caller storage remains borrowed for the duration of the call only.
/// @param image Live GUI Image handle.
/// @param rgba Borrowed width*height*4 interleaved RGBA bytes.
/// @param width Positive pixel width fitting int32_t.
/// @param height Positive pixel height fitting int32_t.
/// @return 1 after a complete atomic upload, otherwise 0 with prior pixels preserved.
/// @internal
int rt_gui_image_try_set_rgba_bytes(void *image,
                                    const uint8_t *rgba,
                                    int64_t width,
                                    int64_t height);

/// @brief Synchronize every runtime Minimap owned by one GUI app.
/// @details Called before retained dirty-state inspection so editor content/layout/viewport changes
///          can invalidate the corresponding minimap in the same frame. Traversal is allocation-
///          free and detached/destroyed wrappers are ignored.
/// @param app Borrowed owning app pointer.
/// @internal
void rt_minimap_sync_app(void *app);
/// @brief Re-apply the current theme to @p app's widget tree after a change.
/// @details Rebuilds the per-app scaled theme only when its base theme or effective scale changed.
///          A successful replacement invalidates every app-owned layout/paint surface, reapplies
///          the logical default font at the new effective pixel size, and increments the app's
///          theme revision. Allocation failure leaves the previous theme and revision unchanged.
/// @param app App whose theme should be refreshed; NULL is a no-op.
void rt_gui_refresh_theme(rt_gui_app_t *app);
/// @brief Resolve the logical, unscaled base palette currently selected by one app.
/// @details Dark and light return immutable toolkit singletons. System resolves through the
///          app's last synchronized platform appearance, while custom returns the app-owned
///          palette when present and deterministically falls back to dark otherwise. The returned
///          pointer is borrowed and remains valid only while the app and its selected palette live.
/// @param app App whose logical palette is requested; NULL selects the built-in dark palette.
/// @return Borrowed non-NULL unscaled theme suitable for cloning; never destroy the result.
const vg_theme_t *rt_gui_app_theme_base(const rt_gui_app_t *app);
/// @brief Atomically replace one app's logical custom palette and select Custom mode.
/// @details Builds the DPI-scaled/accessibility-adjusted installed copy before publishing any
///          state. On success the app takes ownership of @p candidate, destroys its prior custom
///          base and installed copy, increments the theme revision, reapplies fonts, and
///          invalidates theme dependents. On failure every app field remains unchanged and the
///          caller retains ownership of @p candidate.
/// @param app Target app; must be non-NULL.
/// @param candidate Owned unscaled theme clone offered for transfer; must be non-NULL.
/// @return One when ownership transferred and Custom mode was installed, otherwise zero.
int rt_gui_install_custom_theme(rt_gui_app_t *app, vg_theme_t *candidate);
/// @brief Synchronize a System-mode app with the host desktop appearance.
/// @details Queries the platform adapter on demand. A changed light/dark preference rebuilds the
///          per-app scaled theme, increments its theme revision, and invalidates theme-dependent
///          surfaces. Non-System apps and NULL handles are unchanged.
/// @param app App to synchronize; may be NULL.
void rt_gui_sync_system_theme(rt_gui_app_t *app);
/// @brief Switch @p app's theme mode and refresh its installed theme.
/// @details Accepts dark, light, system, or custom. Custom is ignored until an app-owned custom
///          palette has been installed. Re-selecting the effective current mode is allocation-free
///          unless a scale or platform appearance changed.
/// @param app Target app; NULL is a no-op.
/// @param kind Requested validated theme mode.
void rt_gui_set_theme_kind(rt_gui_app_t *app, rt_gui_theme_kind_t kind);
/// @brief Resync the modal overlay root after modals open/close.
void rt_gui_sync_modal_root(rt_gui_app_t *app);
/// @brief Assign the app's default font to @p widget if it has none.
void rt_gui_apply_default_font(vg_widget_t *widget);
/// @brief Re-apply @p app's current default font/size to all app-owned GUI surfaces.
void rt_gui_reapply_default_font(rt_gui_app_t *app);
/// @brief Clear cached app-level runtime references that point into @p widget's subtree.
void rt_widget_forget_runtime_refs(rt_gui_app_t *app, vg_widget_t *widget);
/// @brief Return non-zero if @p root contains @p needle in its subtree.
int rt_gui_widget_tree_contains(vg_widget_t *root, const vg_widget_t *needle);
/// @brief Disconnect FindBar wrappers targeting a CodeEditor inside @p subtree.
void rt_findbar_forget_editor_subtree(vg_widget_t *subtree);
/// @brief Disconnect Minimap wrappers targeting a CodeEditor inside @p subtree.
void rt_minimap_forget_editor_subtree(vg_widget_t *subtree);
/// @brief Drop wrapper references to @p dialog before an app destroys it.
void rt_messagebox_invalidate_dialog(vg_dialog_t *dialog);
/// @brief Drop wrapper references to @p dialog before an app destroys it.
void rt_filedialog_invalidate_dialog(vg_dialog_t *dialog);
/// @brief Invalidate Viper-facing subobject handles owned by @p subtree.
void rt_gui_invalidate_widget_subhandles(vg_widget_t *subtree);
/// @brief Reclaim retired subobject payloads with no remaining managed wrapper.
/// @details Walks @p subtree's owner widgets and drains individual ListBox, TreeView, TabBar,
///          MenuBar, ContextMenu, StatusBar, and Toolbar retirement records only when the indexed
///          stable-wrapper registry proves the corresponding item or subtree group unreferenced.
///          The traversal is allocation-free and leaves every still-referenced stale wrapper and
///          its tombstone intact. Expected wrapper lookup is O(1); TreeView group checking is
///          linear only in the retired subtree being considered.
/// @param subtree Live widget subtree whose owners should be collected; NULL/retired roots are
///                ignored.
void rt_gui_collect_retired_subhandles(vg_widget_t *subtree);
/// @brief Return the process-global number of allocated GUI subhandle wrappers.
/// @details Intended for lifecycle regression tests and diagnostics; reading it has no side effects
///          and does not retain wrappers.
/// @return Number of wrappers not yet finalized.
size_t rt_gui_subhandle_debug_live_count(void);
/// @brief Return the allocated capacity of the expected-O(1) target index.
/// @return Power-of-two slot count, or zero when the registry is empty.
size_t rt_gui_subhandle_debug_index_capacity(void);
/// @brief Return the largest bounded target-index probe count observed since reset.
/// @return Maximum slots inspected by a target lookup; never exceeds current lookup capacity.
size_t rt_gui_subhandle_debug_max_probes(void);
/// @brief Reset subhandle target-index probe counters without altering registry state.
void rt_gui_subhandle_debug_reset_probes(void);
/// @brief Invalidate wrappers for retired nodes before @p tree reclaims their tombstones.
/// @details The tree's retired list still owns valid tombstone storage when this function is
///          called. It clears only wrappers whose node is already retired, preserving wrappers for
///          live nodes in the same tree. After return, `vg_treeview_prune_retired_nodes` may free
///          the retired storage without leaving a runtime wrapper that can dereference it.
/// @param tree Tree whose retired node wrappers should be invalidated; NULL is a no-op.
void rt_gui_invalidate_retired_tree_node_subhandles(vg_treeview_t *tree);
/// @brief Invalidate wrappers for retired tabs before @p tabbar reclaims their tombstones.
/// @details The tab bar's retired list still owns valid tombstone storage when this function is
///          called. It clears only wrappers whose tab is already retired, preserving wrappers for
///          live tabs. After return, `vg_tabbar_prune_retired_tabs` may safely free the tombstones.
/// @param tabbar Tab bar whose retired tab wrappers should be invalidated; NULL is a no-op.
void rt_gui_invalidate_retired_tab_subhandles(vg_tabbar_t *tabbar);
/// @brief Invalidate Viper-facing handles for @p context menu and its descendants.
void rt_gui_invalidate_contextmenu_tree(vg_contextmenu_t *menu);
/// @brief Invalidate Viper-facing item/submenu handles contained by @p context menu.
void rt_gui_invalidate_contextmenu_contents(vg_contextmenu_t *menu);
/// @brief Return a managed Viper handle for a tree node.
void *rt_gui_wrap_tree_node(vg_tree_node_t *node);
/// @brief Return a managed Viper handle for a tab.
void *rt_gui_wrap_tab(vg_tab_t *tab);
/// @brief Return a managed Viper handle for a listbox item.
void *rt_gui_wrap_listbox_item(vg_listbox_item_t *item);
/// @brief Return a managed Viper handle for a menu.
void *rt_gui_wrap_menu(vg_menu_t *menu);
/// @brief Return a managed Viper handle for a menu item.
void *rt_gui_wrap_menu_item(vg_menu_item_t *item);
/// @brief Return a managed Viper handle for a context menu.
void *rt_gui_wrap_contextmenu(vg_contextmenu_t *menu);
/// @brief Return a managed Viper handle for a status-bar item.
void *rt_gui_wrap_statusbar_item(vg_statusbar_item_t *item);
/// @brief Return a managed Viper handle for a toolbar item.
void *rt_gui_wrap_toolbar_item(vg_toolbar_item_t *item);
/// @brief Resolve a managed tree-node handle to its live VG node, or NULL.
vg_tree_node_t *rt_gui_tree_node_from_handle(void *handle);
/// @brief Resolve a managed tab handle to its live VG tab, or NULL.
vg_tab_t *rt_gui_tab_from_handle(void *handle);
/// @brief Resolve a managed listbox-item handle to its live VG item, or NULL.
vg_listbox_item_t *rt_gui_listbox_item_from_handle(void *handle);
/// @brief Resolve a managed menu handle to its live VG menu, or NULL.
vg_menu_t *rt_gui_menu_from_handle(void *handle);
/// @brief Resolve a managed menu-item handle to its live VG item, or NULL.
vg_menu_item_t *rt_gui_menu_item_from_handle(void *handle);
/// @brief Resolve a managed context-menu handle to its live VG menu, or NULL.
vg_contextmenu_t *rt_gui_contextmenu_from_handle(void *handle);
/// @brief Resolve a managed status-bar item handle to its live VG item, or NULL.
vg_statusbar_item_t *rt_gui_statusbar_item_from_handle(void *handle);
/// @brief Resolve a managed toolbar item handle to its live VG item, or NULL.
vg_toolbar_item_t *rt_gui_toolbar_item_from_handle(void *handle);
/// @brief Register a command palette so @p app routes its shortcut to it.
void rt_gui_register_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette);
/// @brief Unregister a previously registered command palette.
void rt_gui_unregister_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette);
/// @brief Queue a dropped file @p path for @p app's drag-and-drop handlers.
void rt_gui_file_drop_add(rt_gui_app_t *app, const char *path);
/// @brief Whether @p handle is a currently-live GUI app handle.
int rt_gui_is_app_handle_known(const void *handle);
/// @brief Whether @p handle refers to a destroyed (stale) GUI app handle.
int rt_gui_is_destroyed_app_handle(const void *handle);
/// @brief Retire @p font from @p app at its current render generation.
/// @details The app reclaims it only after a later safe boundary and after every app/widget/theme
///          reference disappears. Queue growth failure leaves prior entries intact.
/// @param app App that will schedule reclamation.
/// @param font Font being replaced or explicitly destroyed.
/// @return Non-zero if the font was queued/refreshed.
int rt_gui_retire_font(rt_gui_app_t *app, vg_font_t *font);
/// @brief Queue @p font in each app that still has a render or theme reference.
/// @param font Font whose public owner is releasing it.
/// @return Non-zero if at least one app queued the font; zero means immediate destruction is safe.
int rt_gui_retire_font_if_in_use(vg_font_t *font);

/// @brief True if @p handle is a known (live) GUI App handle.
static inline int rt_gui_is_app_handle(const void *handle) {
    return rt_gui_is_app_handle_known(handle);
}

/// @brief True if @p handle is a live widget handle.
static inline int rt_gui_is_widget_handle(const void *handle) {
    return handle && vg_widget_is_live((const vg_widget_t *)handle);
}

/// @brief Safe-cast @p handle to a widget, rejecting App/destroyed handles.
/// @return The widget, or NULL if @p handle is not a live widget handle.
static inline vg_widget_t *rt_gui_widget_handle_checked(void *handle) {
    if (!handle || rt_gui_is_app_handle(handle) || rt_gui_is_destroyed_app_handle(handle))
        return NULL;
    return rt_gui_is_widget_handle(handle) ? (vg_widget_t *)handle : NULL;
}

/// @brief Safe-cast @p handle to a widget of a specific @p type.
/// @return The widget if it is live and matches @p type, else NULL.
static inline vg_widget_t *rt_gui_widget_handle_checked_type(void *handle, vg_widget_type_t type) {
    vg_widget_t *widget = rt_gui_widget_handle_checked(handle);
    return widget && widget->type == type ? widget : NULL;
}

/// @brief Private runtime-object class tag used by managed Viper.GUI.Font wrappers.
/// @details This negative internal identifier cannot collide with generated public class IDs and
///          lets opaque-handle validation reject arbitrary heap objects before reading a wrapper.
#define RT_GUI_FONT_HANDLE_CLASS_ID INT64_C(-0x47554601)

/// @brief Resolve a public GUI font handle to its live lower-toolkit font.
/// @details Accepts both modern managed Font wrappers and legacy raw vg_font_t pointers. Wrapper
///          validation uses the runtime heap metadata before reading private fields; raw pointers
///          are authenticated against the lower live-font registry without dereferencing unknown
///          memory. Explicitly destroyed and stale handles return NULL.
/// @param handle Managed Font wrapper, legacy raw font, or unrelated/null value.
/// @return Borrowed live vg_font_t, or NULL when validation fails.
vg_font_t *rt_gui_font_handle_checked(void *handle);

/// @brief Determine whether a handle is a live managed Viper.GUI.Font wrapper.
/// @details The underlying font may already have been explicitly destroyed; this operation checks
///          wrapper identity/liveness only and is used to balance runtime retains in palettes.
/// @param handle Candidate opaque runtime value.
/// @return Non-zero for a live managed wrapper, otherwise zero.
int rt_gui_font_handle_is_managed(void *handle);

/// @brief Safe-cast @p handle to an App, or NULL if it is not an App handle.
static inline rt_gui_app_t *rt_gui_app_handle_checked(void *handle) {
    return rt_gui_is_app_handle(handle) ? (rt_gui_app_t *)handle : NULL;
}

/// @brief Resolve any handle (NULL/App/widget) to its owning App.
/// @details NULL yields the active app; an App handle returns itself; a live
///          widget returns its owning app; a destroyed App handle yields NULL.
static inline rt_gui_app_t *rt_gui_app_from_handle(void *handle) {
    if (!handle)
        return rt_gui_get_active_app();
    if (rt_gui_is_app_handle(handle))
        return (rt_gui_app_t *)handle;
    if (rt_gui_is_destroyed_app_handle(handle))
        return NULL;
    return rt_gui_is_widget_handle(handle) ? rt_gui_app_from_widget((vg_widget_t *)handle) : NULL;
}

/// @brief Resolve a handle to the widget that should act as a parent.
/// @details An App handle resolves to its root widget; a live widget resolves
///          to itself; NULL or a destroyed App handle yields NULL.
static inline vg_widget_t *rt_gui_widget_parent_from_handle(void *handle) {
    if (!handle)
        return NULL;
    if (rt_gui_is_app_handle(handle)) {
        rt_gui_app_t *app = (rt_gui_app_t *)handle;
        return app->root;
    }
    if (rt_gui_is_destroyed_app_handle(handle))
        return NULL;
    return rt_gui_is_widget_handle(handle) ? (vg_widget_t *)handle : NULL;
}

/// @brief True if widgets of @p type can safely own arbitrary runtime children.
static inline int rt_gui_widget_type_accepts_runtime_children(vg_widget_type_t type) {
    switch (type) {
        case VG_WIDGET_CONTAINER:
        case VG_WIDGET_SCROLLVIEW:
        case VG_WIDGET_SPLITPANE:
        case VG_WIDGET_DIALOG:
        case VG_WIDGET_LISTVIEW:
        case VG_WIDGET_GROUPBOX:
        case VG_WIDGET_CUSTOM:
            return 1;
        case VG_WIDGET_LABEL:
        case VG_WIDGET_BUTTON:
        case VG_WIDGET_TEXTINPUT:
        case VG_WIDGET_CHECKBOX:
        case VG_WIDGET_RADIO:
        case VG_WIDGET_SLIDER:
        case VG_WIDGET_PROGRESS:
        case VG_WIDGET_LISTBOX:
        case VG_WIDGET_DROPDOWN:
        case VG_WIDGET_TREEVIEW:
        case VG_WIDGET_TABBAR:
        case VG_WIDGET_MENUBAR:
        case VG_WIDGET_MENU:
        case VG_WIDGET_MENUITEM:
        case VG_WIDGET_TOOLBAR:
        case VG_WIDGET_STATUSBAR:
        case VG_WIDGET_CODEEDITOR:
        case VG_WIDGET_OUTPUTPANE:
        case VG_WIDGET_IMAGE:
        case VG_WIDGET_SPINNER:
        case VG_WIDGET_COLORSWATCH:
        case VG_WIDGET_COLORPALETTE:
        case VG_WIDGET_COLORPICKER:
        default:
            return 0;
    }
}

/// @brief Resolve a parent handle only when it can own arbitrary runtime children.
/// @details NULL is valid for detached widget creation and returns NULL. Non-NULL
///          invalid handles return NULL. Only explicit runtime-container widget
///          types are accepted; leaf widgets are rejected even if their vtable
///          lacks a custom arrange callback.
static inline vg_widget_t *rt_gui_widget_parent_container_from_handle(void *handle) {
    vg_widget_t *parent = rt_gui_widget_parent_from_handle(handle);
    if (!parent)
        return NULL;
    return rt_gui_widget_type_accepts_runtime_children(parent->type) ? parent : NULL;
}

#define RT_GUI_MAX_LAYOUT_VALUE 1000000.0
#define RT_GUI_STRING_DATA_MAGIC UINT64_C(0x5254475544535452)

#ifdef _MSC_VER
#define RT_GUI_FLEX_ARRAY_SIZE 1
#else
#define RT_GUI_FLEX_ARRAY_SIZE
#endif

typedef struct {
    uint64_t magic;
    size_t len;
    char bytes[RT_GUI_FLEX_ARRAY_SIZE];
} rt_gui_string_data_t;

/// @brief 1 if @p value is finite (not NaN/Inf), else 0.
static inline int rt_gui_double_is_finite(double value) {
    return isfinite(value) ? 1 : 0;
}

/// @brief Clamp @p value to the inclusive [min_value, max_value] range.
static inline double rt_gui_clamp_f64(double value, double min_value, double max_value) {
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

/// @brief Clamp a double to [0, max_value] as a float; non-finite -> 0.
static inline float rt_gui_sanitize_nonnegative_float(double value, double max_value) {
    if (!rt_gui_double_is_finite(value))
        return 0.0f;
    return (float)rt_gui_clamp_f64(value, 0.0, max_value);
}

/// @brief Clamp a double to [-max_abs_value, max_abs_value] as a float;
///        non-finite -> 0.
static inline float rt_gui_sanitize_signed_float(double value, double max_abs_value) {
    if (!rt_gui_double_is_finite(value))
        return 0.0f;
    return (float)rt_gui_clamp_f64(value, -max_abs_value, max_abs_value);
}

/// @brief Return the logical-to-framebuffer scale associated with a widget.
/// @details Attached widgets inherit the effective scale of their owning app. Detached widgets
///          deliberately use the identity scale so public geometry is deterministic before
///          attachment. @ref rt_gui_app_effective_scale guarantees a positive finite result.
/// @param widget Live widget whose coordinate boundary is being evaluated; may be NULL.
/// @return Positive finite effective scale, or 1 for a detached/NULL widget.
static inline float rt_gui_widget_effective_scale(vg_widget_t *widget) {
    return rt_gui_app_effective_scale(rt_gui_app_from_widget(widget));
}

/// @brief Convert a public non-negative logical metric to toolkit framebuffer units.
/// @details Invalid values become zero. Multiplication is performed in double precision and the
///          final result is clamped to @ref RT_GUI_MAX_LAYOUT_VALUE, preventing a large logical
///          value and scale from overflowing float storage.
/// @param widget Widget supplying the effective app scale.
/// @param value Public logical length.
/// @return Sanitized non-negative framebuffer length.
static inline float rt_gui_logical_length_to_physical(vg_widget_t *widget, double value) {
    if (!rt_gui_double_is_finite(value))
        return 0.0f;
    double physical = value * (double)rt_gui_widget_effective_scale(widget);
    return rt_gui_sanitize_nonnegative_float(physical, RT_GUI_MAX_LAYOUT_VALUE);
}

/// @brief Convert a signed public logical coordinate to toolkit framebuffer units.
/// @details Invalid values become zero and the scaled result is clamped symmetrically to the
///          supported layout range. Legitimate negative overlay positions remain negative.
/// @param widget Widget supplying the effective app scale.
/// @param value Public logical coordinate.
/// @return Sanitized signed framebuffer coordinate.
static inline float rt_gui_logical_coordinate_to_physical(vg_widget_t *widget, double value) {
    if (!rt_gui_double_is_finite(value))
        return 0.0f;
    double physical = value * (double)rt_gui_widget_effective_scale(widget);
    return rt_gui_sanitize_signed_float(physical, RT_GUI_MAX_LAYOUT_VALUE);
}

/// @brief Convert one toolkit framebuffer metric to a public logical value.
/// @param widget Widget supplying the effective app scale.
/// @param value Stored framebuffer coordinate or length.
/// @return Logical value after exactly one division by the positive effective scale.
static inline double rt_gui_physical_to_logical(vg_widget_t *widget, float value) {
    return (double)value / (double)rt_gui_widget_effective_scale(widget);
}

/// @brief Clamp an int64 to the inclusive [min_value, max_value] int32 range.
static inline int32_t rt_gui_clamp_i64_to_i32(int64_t value, int32_t min_value, int32_t max_value) {
    if (value < (int64_t)min_value)
        return min_value;
    if (value > (int64_t)max_value)
        return max_value;
    return (int32_t)value;
}

/// @brief Validate a font size, returning @p fallback for non-finite input and
///        clamping otherwise to the supported [1, 256] point range.
static inline double rt_gui_sanitize_font_size(double size, double fallback) {
    if (!rt_gui_double_is_finite(size))
        return fallback;
    return rt_gui_clamp_f64(size, 1.0, 256.0);
}

/// @brief Allocate an owned, magic-tagged copy of a runtime string's bytes.
/// @details Used to give widgets a stable NUL-terminated C buffer they own.
///          The leading magic is an internal validation guard for fields whose
///          owns_user_data flag already says they contain this wrapper type.
/// @return New rt_gui_string_data_t (caller frees via free_if_owned), or NULL.
static inline rt_gui_string_data_t *rt_gui_string_data_new(rt_string value) {
    int64_t len64 = rt_str_len(value);
    if (len64 < 0)
        return NULL;
    size_t len = (size_t)len64;
    const size_t header_size = offsetof(rt_gui_string_data_t, bytes);
    if (len > SIZE_MAX - header_size - 1)
        return NULL;
    const char *bytes = len ? rt_string_cstr(value) : "";
    if (len && !bytes)
        return NULL;
    rt_gui_string_data_t *data = (rt_gui_string_data_t *)malloc(header_size + len + 1);
    if (!data)
        return NULL;
    data->magic = RT_GUI_STRING_DATA_MAGIC;
    data->len = len;
    if (len)
        memcpy(data->bytes, bytes, len);
    data->bytes[len] = '\0';
    return data;
}

/// @brief Allocate an owned runtime string-data block from raw bytes.
static inline rt_gui_string_data_t *rt_gui_string_data_new_bytes(const char *bytes, size_t len) {
    if (len > 0 && !bytes)
        return NULL;
    const size_t header_size = offsetof(rt_gui_string_data_t, bytes);
    if (len > SIZE_MAX - header_size - 1)
        return NULL;
    rt_gui_string_data_t *data = (rt_gui_string_data_t *)malloc(header_size + len + 1);
    if (!data)
        return NULL;
    data->magic = RT_GUI_STRING_DATA_MAGIC;
    data->len = len;
    if (len)
        memcpy(data->bytes, bytes, len);
    data->bytes[len] = '\0';
    return data;
}

/// @brief True if @p ptr is an rt_gui_string_data_t block (magic matches).
/// @details Only call this for pointers that an out-of-band ownership flag says
///          may be an rt_gui_string_data_t. It is not a safe generic borrowed
///          C-string discriminator.
static inline int rt_gui_string_data_is_owned(const void *ptr) {
    if (!ptr)
        return 0;
    const rt_gui_string_data_t *data = (const rt_gui_string_data_t *)ptr;
    return data->magic == RT_GUI_STRING_DATA_MAGIC;
}

/// @brief free() @p ptr only if it is a GUI-owned string-data block.
/// @details Only use this when an ownership flag says @p ptr may hold runtime
///          string data; plain borrowed strings are not self-describing.
static inline void rt_gui_string_data_free_if_owned(void *ptr) {
    if (rt_gui_string_data_is_owned(ptr))
        free(ptr);
}

/// @brief Convert a GUI string handle back to a runtime string.
/// @details Handles owned string-data blocks using their stored byte length.
///          NULL or an unexpected block yields the empty string.
static inline rt_string rt_gui_string_data_to_rt_string(const void *ptr) {
    if (!ptr)
        return rt_str_empty();
    if (rt_gui_string_data_is_owned(ptr)) {
        const rt_gui_string_data_t *data = (const rt_gui_string_data_t *)ptr;
        return rt_string_from_bytes(data->bytes, data->len);
    }
    return rt_str_empty();
}

//=============================================================================
// Shared helpers
//=============================================================================

/// @brief Convert a runtime string to a heap-allocated NUL-terminated C string.
/// @details Allocates a new buffer via malloc, copies the string contents, and
///          appends a NUL terminator. The caller is responsible for freeing the
///          returned buffer.
/// @param str Runtime string to convert (may be NULL).
/// @return Heap-allocated C string, or NULL if str is NULL or allocation fails.
static inline char *rt_string_to_cstr(rt_string str) {
    if (!str)
        return NULL;
    int64_t len64 = rt_str_len(str);
    if (len64 < 0)
        return NULL;
    if ((uint64_t)len64 > (uint64_t)SIZE_MAX)
        return NULL;
    size_t len = (size_t)len64;
    if (len > SIZE_MAX - 1)
        return NULL;
    const char *bytes = len ? rt_string_cstr(str) : "";
    if (len && !bytes)
        return NULL;
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    if (len)
        memcpy(result, bytes, len);
    result[len] = '\0';
    return result;
}

/// @brief Return non-zero if a runtime string contains an embedded NUL byte.
static inline int rt_string_contains_nul(rt_string str) {
    if (!str)
        return 0;
    int64_t len64 = rt_str_len(str);
    if (len64 <= 0)
        return 0;
    const char *bytes = rt_string_cstr(str);
    if (!bytes)
        return 0;
    return memchr(bytes, '\0', (size_t)len64) != NULL;
}

/// @brief Convert a runtime string to a C string only when it contains no embedded NUL.
/// @details Use for identifiers, filesystem paths, filters, shortcuts, and other
///          non-display values where silent C-string truncation would be incorrect.
static inline char *rt_string_to_cstr_no_nul(rt_string str) {
    if (rt_string_contains_nul(str))
        return NULL;
    return rt_string_to_cstr(str);
}

/// @brief Convert a runtime string to GUI-visible UTF-8 text.
/// @details GUI widgets store and render NUL-terminated text, so embedded NUL
///          bytes are replaced with U+FFFD instead of truncating the suffix.
///          NULL runtime strings become an allocated empty string.
static inline char *rt_string_to_gui_cstr(rt_string str) {
    if (!str) {
        char *empty = malloc(1);
        if (empty)
            empty[0] = '\0';
        return empty;
    }

    int64_t len64 = rt_str_len(str);
    if (len64 < 0)
        return NULL;
    size_t len = (size_t)len64;
    const char *bytes = len ? rt_string_cstr(str) : "";
    if (len && !bytes)
        return NULL;

    size_t nul_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] == '\0')
            nul_count++;
    }
    if (len > SIZE_MAX - 1 || nul_count > (SIZE_MAX - len - 1) / 2)
        return NULL;

    char *result = malloc(len + nul_count * 2 + 1);
    if (!result)
        return NULL;

    char *out = result;
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] == '\0') {
            memcpy(out, "\xEF\xBF\xBD", 3);
            out += 3;
        } else {
            *out++ = bytes[i];
        }
    }
    *out = '\0';
    return result;
}

/// @brief Return @p text when non-NULL, otherwise the immutable empty C string.
/// @details GUI wrapper functions often receive converted runtime strings that can
///          be NULL only after allocation or validation failure. Passing this helper's
///          result to VG labels/placeholders preserves the established empty-string
///          fallback without forcing every caller to duplicate the same ternary.
static inline const char *rt_gui_cstr_or_empty(const char *text) {
    return text ? text : "";
}

/// @brief Ensure the default font is loaded (lazy init on first use).
/// @details Loads the default font from the embedded font data if it has not
///          been loaded yet. Defined in rt_gui_app.c.
void rt_gui_ensure_default_font(void);

/// @brief Track the last clicked widget (set by GUI.App.Poll).
/// @details Records the widget that was clicked during the current event poll
///          cycle so that click handlers can query it. Defined in
///          rt_gui_widgets_complex.c.
/// @param widget Pointer to the clicked widget (may be NULL to clear).
void rt_gui_set_last_clicked(void *widget);

/// @brief Push @p dlg onto @p app's modal-dialog stack (it becomes topmost).
void rt_gui_push_dialog(rt_gui_app_t *app, vg_dialog_t *dlg);
/// @brief Remove @p dlg from @p app's dialog stack (no-op if not present).
void rt_gui_remove_dialog(rt_gui_app_t *app, vg_dialog_t *dlg);
/// @brief The topmost dialog on @p app's stack, or NULL if none is open.
vg_dialog_t *rt_gui_top_dialog(rt_gui_app_t *app);

/// @brief Free per-app feature resources owned by rt_gui_features.c.
/// @details Called from rt_gui_app_destroy. Defined in rt_gui_features.c.
void rt_gui_features_cleanup(rt_gui_app_t *app);

/// @brief Re-apply HiDPI scale to the current theme. Called after theme switch.
void rt_theme_apply_hidpi_scale(void);

//=============================================================================
// macOS native app menubar bridge
//=============================================================================

#if RT_PLATFORM_MACOS
/// @brief Register @p menubar as the app's native macOS menubar.
/// @return true if it became the active native menubar. Defined in
///         rt_gui_macos_menu.m.
bool rt_gui_macos_menu_register_menubar(vg_menubar_t *menubar);
/// @brief Unregister a previously registered native macOS menubar.
void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar);
/// @brief Rebuild the native macOS menu from @p menubar's current contents.
void rt_gui_macos_menu_sync_for_menubar(vg_menubar_t *menubar);
/// @brief Rebuild the native macOS menu for @p app's current menubar.
void rt_gui_macos_menu_sync_app(rt_gui_app_t *app);
/// @brief Tear down native macOS menu state owned by @p app.
void rt_gui_macos_menu_app_destroy(rt_gui_app_t *app);
#else
/// @brief Non-Apple no-op stub for the native macOS menubar bridge.
/// @return Always false (no native menubar on this platform).
static inline bool rt_gui_macos_menu_register_menubar(vg_menubar_t *menubar) {
    (void)menubar;
    return false;
}

/// @brief Non-Apple no-op stub: unregister a native macOS menubar.
static inline void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar) {
    (void)menubar;
}

/// @brief Non-Apple no-op stub: sync a menubar to the native macOS menu.
static inline void rt_gui_macos_menu_sync_for_menubar(vg_menubar_t *menubar) {
    (void)menubar;
}

/// @brief Non-Apple no-op stub: sync an app's menus to the native macOS menu.
static inline void rt_gui_macos_menu_sync_app(rt_gui_app_t *app) {
    (void)app;
}

/// @brief Non-Apple no-op stub: tear down native macOS menus for an app.
static inline void rt_gui_macos_menu_app_destroy(rt_gui_app_t *app) {
    (void)app;
}
#endif

/// @brief Clear all triggered shortcut flags for the current frame.
/// @details Called at the start of each poll cycle to reset shortcut state.
///          Defined in rt_gui_system.c.
void rt_shortcuts_clear_triggered(rt_gui_app_t *app);

/// @brief Check whether a key/modifier combination matches any registered shortcut.
/// @details Called during the poll loop to dispatch keyboard shortcuts.
///          Defined in rt_gui_system.c.
/// @param key  The translated VG_KEY_* code that was pressed.
/// @param mods Translated VG_MOD_* flags active for the event.
/// @return Non-zero if a matching shortcut was triggered; 0 otherwise.
int8_t rt_shortcuts_check_key(rt_gui_app_t *app, int key, int mods);

//=============================================================================
// Shared status-bar / tool-bar helpers (defined in rt_gui_menus.c, consumed by
// the status-bar/tool-bar widgets in rt_gui_bars.c). Icon helpers are also used
// by the menu widgets that remain in rt_gui_menus.c.
//=============================================================================

vg_statusbar_item_t *rt_statusbaritem_checked(void *item);
vg_toolbar_item_t *rt_toolbaritem_checked(void *item);
vg_statusbar_t *rt_statusbar_checked(void *bar);
vg_toolbar_t *rt_toolbar_checked(void *toolbar);
int rt_statusbar_zone_checked(int64_t zone, vg_statusbar_zone_t *out_zone);
vg_icon_t rt_gui_icon_from_pixels(void *pixels);
vg_icon_t rt_gui_icon_from_path_cstr(const char *path);
