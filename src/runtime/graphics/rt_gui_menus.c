//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_menus.c
// Purpose: Runtime bindings for ViperGUI menu-system widgets: MenuBar (top-level
//   menu strip), Menu (drop-down with items and separators), StatusBar (bottom
//   status strip with labeled sections), Toolbar (icon/label button strip), and
//   ContextMenu (right-click popup). Each widget wraps its vg_* counterpart and
//   exposes a Zia-callable API for item management, click detection, and styling.
//
// Key invariants:
//   - MenuBar and its child Menus share ownership: removing a Menu from the
//     MenuBar transfers ownership back to the caller.
//   - MenuItem click state is edge-triggered and cleared each frame by the vg
//     widget's internal update; callers must poll within the same frame.
//   - ContextMenu must be shown explicitly (rt_contextmenu_show) at a screen
//     coordinate; it auto-hides on any click outside its bounds.
//   - Toolbar buttons can carry both an icon (as text/glyph) and a label; either
//     may be NULL.
//   - StatusBar sections are addressed by zero-based index; count is fixed after
//     creation unless sections are explicitly added or removed.
//
// Ownership/Lifetime:
//   - All widget objects are vg_widget_t* subtrees owned by the vg widget tree;
//     vg_widget_destroy() recursively frees children.
//   - C strings passed to add_menu / add_item are copied by the vg layer; the
//     runtime frees temporary cstr allocations immediately after the call.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/include/vg.h (ViperGUI C API),
//        src/runtime/graphics/rt_gui_app.c (default font used at construction)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_pixels.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

void rt_gui_set_clicked_statusbar_item(void *item);

//=============================================================================
// MenuBar Widget (Phase 2)
//=============================================================================

/// @brief Walk up from a menu to its owning menubar (NULL for orphan / context menus).
static vg_menubar_t *rt_gui_menu_owner_from_menu(const vg_menu_t *menu) {
    return menu ? menu->owner_menubar : NULL;
}

/// @brief Walk from an item → parent menu → menubar; NULL if the chain isn't intact.
static vg_menubar_t *rt_gui_menu_owner_from_item(const vg_menu_item_t *item) {
    return item && item->parent_menu ? item->parent_menu->owner_menubar : NULL;
}

/// @brief Refresh runtime and native accelerator state after a menubar mutation.
///
/// The in-process accelerator table is used by non-native menu dispatch on every
/// platform; macOS also mirrors the same model into the native menu strip.
static void rt_gui_menu_sync_menubar(vg_menubar_t *menubar) {
    if (menubar)
        vg_menubar_rebuild_accelerators(menubar);
    rt_gui_macos_menu_sync_for_menubar(menubar);
}

/// @brief Validate a handle as a live MenuBar widget (NULL if not).
static vg_menubar_t *rt_menubar_checked(void *menubar) {
    return (vg_menubar_t *)rt_gui_widget_handle_checked_type(menubar, VG_WIDGET_MENUBAR);
}

/// @brief Validate a Menu handle.
static vg_menu_t *rt_menu_checked(void *menu) {
    return rt_gui_menu_from_handle(menu);
}

/// @brief Validate a ContextMenu handle.
static vg_contextmenu_t *rt_contextmenu_checked(void *menu) {
    return rt_gui_contextmenu_from_handle(menu);
}

/// @brief Validate a MenuItem handle.
static vg_menu_item_t *rt_menuitem_checked(void *item) {
    return rt_gui_menu_item_from_handle(item);
}

/// @brief Validate a StatusBarItem handle.
static vg_statusbar_item_t *rt_statusbaritem_checked(void *item) {
    return rt_gui_statusbar_item_from_handle(item);
}

/// @brief Validate a ToolbarItem handle.
static vg_toolbar_item_t *rt_toolbaritem_checked(void *item) {
    return rt_gui_toolbar_item_from_handle(item);
}

/// @brief Validate a handle as a live StatusBar widget (NULL if not).
static vg_statusbar_t *rt_statusbar_checked(void *bar) {
    return (vg_statusbar_t *)rt_gui_widget_handle_checked_type(bar, VG_WIDGET_STATUSBAR);
}

/// @brief Validate a handle as a live ToolBar widget (NULL if not).
static vg_toolbar_t *rt_toolbar_checked(void *toolbar) {
    return (vg_toolbar_t *)rt_gui_widget_handle_checked_type(toolbar, VG_WIDGET_TOOLBAR);
}

/// @brief Range-check a status-bar zone index and narrow it to the enum.
/// @param zone Caller-supplied zone index (LEFT..RIGHT).
/// @param out_zone Receives the validated enum value when in range.
/// @return 1 if @p zone is a valid zone, 0 otherwise.
static int rt_statusbar_zone_checked(int64_t zone, vg_statusbar_zone_t *out_zone) {
    if (zone < (int64_t)VG_STATUSBAR_ZONE_LEFT || zone > (int64_t)VG_STATUSBAR_ZONE_RIGHT)
        return 0;
    if (out_zone)
        *out_zone = (vg_statusbar_zone_t)zone;
    return 1;
}

/// @brief Status-bar item click callback: record the clicked item for the next poll.
/// @details Matches the GUI library's callback signature; @p user_data is unused
///          because the clicked item is surfaced through global poll state instead.
static void rt_statusbar_button_clicked(vg_statusbar_item_t *item, void *user_data) {
    (void)user_data;
    rt_gui_set_clicked_statusbar_item(item);
}

/// @brief Convert a runtime Pixels object (0xRRGGBBAA) into a `vg_icon_t` (RGBA).
/// @details Repacks the pixels' 0xRRGGBBAA uint32 buffer into the byte-order
///          RGBA layout that the GUI library's icon API expects (one byte
///          each, R-G-B-A in memory). Validates `width * height` for
///          integer overflow before allocating — guards against a
///          maliciously-sized Pixels object claiming dimensions whose
///          product overflows `size_t`.
///
///          Unlike `rt_codeeditor_icon_from_pixels`, this function returns
///          an empty `vg_icon_t{0}` on any failure path (NULL pixels, bad
///          dimensions, allocation failure) instead of trapping. Menu
///          icons are decorative — the menu still works without them — so
///          the runtime trades a missing glyph for a usable menu rather
///          than aborting the program.
/// @param pixels Source Pixels object; ownership not transferred.
/// @return Populated icon on success; `{0}` (`type == VG_ICON_NONE`) on
///         failure.
static vg_icon_t rt_gui_icon_from_pixels(void *pixels) {
    vg_icon_t icon = {0};
    if (!pixels)
        return icon;

    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    const uint32_t *raw = rt_pixels_raw_buffer(pixels);
    if (width <= 0 || height <= 0 || !raw)
        return icon;
    if ((uintmax_t)width > (uintmax_t)SIZE_MAX ||
        (uintmax_t)height > (uintmax_t)SIZE_MAX)
        return icon;

    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count / (size_t)width != (size_t)height)
        return icon;
    if (width > UINT32_MAX || height > UINT32_MAX || pixel_count > SIZE_MAX / 4)
        return icon;

    uint8_t *rgba = (uint8_t *)malloc(pixel_count * 4);
    if (!rgba)
        return icon;

    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t px = raw[i];
        rgba[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF);
        rgba[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);
        rgba[i * 4 + 3] = (uint8_t)(px & 0xFF);
    }

    icon = vg_icon_from_pixels(rgba, (uint32_t)width, (uint32_t)height);
    free(rgba);
    return icon;
}

/// @brief Load a Pixels image from a file path and convert it to a `vg_icon_t`.
/// @details Loads the file via `rt_pixels_load`, delegates conversion to
///          `rt_gui_icon_from_pixels`, then immediately releases the Pixels
///          object (the icon owns its own RGBA copy). An empty or NULL path,
///          a missing file, or a zero-dimension image all produce a
///          `VG_ICON_NONE` sentinel — callers treat a missing icon as
///          decorative fallback.
/// @param path Filesystem path to the image file (C string).
/// @return Populated icon on success; `{0}` on any failure.
static vg_icon_t rt_gui_icon_from_path_cstr(const char *path) {
    vg_icon_t icon = {0};
    if (!path || path[0] == '\0')
        return icon;

    void *pixels = rt_pixels_load(rt_const_cstr(path));
    if (!pixels)
        return icon;

    icon = rt_gui_icon_from_pixels(pixels);
    if (rt_obj_release_check0(pixels))
        rt_obj_free(pixels);
    return icon;
}

/// @brief Create a new menu bar widget at the top of the window.
/// @details Creates a vg_menubar_t and applies the app's default font. On
///          macOS, also registers with the native app menu bar so menus appear
///          in the system menu strip rather than inside the window.
/// @param parent Parent container or app handle.
/// @return Opaque menu bar widget handle, or NULL on failure.
void *rt_menubar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    vg_widget_t *parent_widget = rt_gui_widget_parent_container_from_handle(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_menubar_t *mb = vg_menubar_create(parent_widget);
    if (mb) {
        if (app)
            rt_gui_activate_app(app);
        rt_gui_ensure_default_font();
        if (app && app->default_font)
            vg_menubar_set_font(mb, app->default_font, app->default_font_size);
        mb->native_main_menu = rt_gui_macos_menu_register_menubar(mb);
        if (mb->native_main_menu) {
            mb->base.constraints.min_height = 0.0f;
            mb->base.constraints.preferred_height = 0.0f;
            mb->base.measured_height = 0.0f;
        }
        rt_gui_menu_sync_menubar(mb);
    }
    return mb;
}

/// @brief Release resources and destroy the menubar.
void rt_menubar_destroy(void *menubar) {
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = rt_menubar_checked(menubar);
    if (!mb)
        return;
    rt_gui_macos_menu_unregister_menubar(mb);
    rt_widget_destroy(mb);
}

/// @brief Add a top-level menu (e.g., "File", "Edit") to the menu bar.
/// @details The title text is copied by the vg layer. Syncs with the native
///          macOS menu bar if registered. Returns the menu handle for adding items.
/// @param menubar Menu bar widget handle.
/// @param title   Menu title text (runtime string).
/// @return Opaque menu handle, or NULL on failure.
void *rt_menubar_add_menu(void *menubar, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = rt_menubar_checked(menubar);
    if (!mb)
        return NULL;
    char *ctitle = rt_string_to_gui_cstr(title);
    vg_menu_t *menu = vg_menubar_add_menu(mb, ctitle);
    free(ctitle);
    rt_gui_menu_sync_menubar(mb);
    return rt_gui_wrap_menu(menu);
}

/// @brief Remove a menu from the menu bar.
void rt_menubar_remove_menu(void *menubar, void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = rt_menubar_checked(menubar);
    vg_menu_t *m = rt_menu_checked(menu);
    if (!mb || !m || m->owner_menubar != mb)
        return;
    vg_menubar_remove_menu(mb, m);
    rt_gui_menu_sync_menubar(mb);
}

/// @brief Get the number of menus in the menu bar.
int64_t rt_menubar_get_menu_count(void *menubar) {
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = rt_menubar_checked(menubar);
    return mb ? mb->menu_count : 0;
}

/// @brief Return the `index`-th top-level menu in `menubar`, or NULL if out of range.
///
/// Linear walk through the singly-linked menu list — fine because
/// the typical menubar has under 10 entries (File/Edit/View/Help/…).
void *rt_menubar_get_menu(void *menubar, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = rt_menubar_checked(menubar);
    if (!mb)
        return NULL;
    if (index < 0 || index >= mb->menu_count)
        return NULL;

    vg_menu_t *menu = mb->first_menu;
    for (int64_t i = 0; i < index && menu; i++) {
        menu = menu->next;
    }
    return rt_gui_wrap_menu(menu);
}

/// @brief Show or hide the menu bar widget.
void rt_menubar_set_visible(void *menubar, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = rt_menubar_checked(menubar);
    if (!mb)
        return;
    vg_widget_set_visible(&mb->base, visible != 0);
    rt_gui_menu_sync_menubar(mb);
}

/// @brief Check whether the menu bar is currently visible.
int64_t rt_menubar_is_visible(void *menubar) {
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = rt_menubar_checked(menubar);
    return mb && mb->base.visible ? 1 : 0;
}

//=============================================================================
// Menu Widget (Phase 2)
//=============================================================================

/// @brief Add a clickable item to a menu.
/// @details The text is copied by the vg layer. Returns the item handle for
///          checking clicks, enabling/disabling, or setting check state. Syncs
///          with the macOS native menu bar if the menu is part of a registered menubar.
/// @param menu Menu handle.
/// @param text Item label text (runtime string).
/// @return Opaque menu item handle, or NULL on failure.
void *rt_menu_add_item(void *menu, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return NULL;
    char *ctext = rt_string_to_gui_cstr(text);
    vg_menu_item_t *item = vg_menu_add_item(m, ctext, NULL, NULL, NULL);
    free(ctext);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
    return rt_gui_wrap_menu_item(item);
}

/// @brief Add an item with a keyboard shortcut (e.g. `"Ctrl+S"`).
/// The shortcut string is parsed by the platform layer at sync time.
void *rt_menu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return NULL;
    char *ctext = rt_string_to_gui_cstr(text);
    char *cshortcut = rt_string_to_gui_cstr(shortcut);
    vg_menu_item_t *item = vg_menu_add_item(m, ctext, cshortcut, NULL, NULL);
    free(ctext);
    free(cshortcut);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
    return rt_gui_wrap_menu_item(item);
}

/// @brief Append a horizontal separator line between menu items.
void *rt_menu_add_separator(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return NULL;
    vg_menu_item_t *item = vg_menu_add_separator(m);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
    return rt_gui_wrap_menu_item(item);
}

/// @brief Add a nested submenu with the given title; returns the new submenu handle.
void *rt_menu_add_submenu(void *menu, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return NULL;
    char *ctitle = rt_string_to_gui_cstr(title);
    vg_menu_t *submenu = vg_menu_add_submenu(m, ctitle);
    free(ctitle);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
    return rt_gui_wrap_menu(submenu);
}

/// @brief Remove a menu item and free its resources.
void rt_menu_remove_item(void *menu, void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!m || !mi)
        return;
    vg_menu_remove_item(m, mi);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
}

/// @brief Remove all items from a menu, leaving it empty.
void rt_menu_clear(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return;
    vg_menu_clear(m);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
}

/// @brief Set the title of the menu.
void rt_menu_set_title(void *menu, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return;
    char *new_title = title ? rt_string_to_gui_cstr(title) : NULL;
    if (title && !new_title)
        return;
    free(m->title);
    m->title = new_title;
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
}

/// @brief Get the title of the menu.
rt_string rt_menu_get_title(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return rt_str_empty();
    const char *title = m->title;
    if (!title)
        return rt_str_empty();
    return rt_string_from_bytes(title, strlen(title));
}

/// @brief Get the number of items in a menu.
int64_t rt_menu_get_item_count(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    return m ? m->item_count : 0;
}

/// @brief Return the `index`-th item in `menu` (linear walk; safe on out-of-range).
void *rt_menu_get_item(void *menu, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return NULL;
    if (index < 0 || index >= m->item_count)
        return NULL;

    vg_menu_item_t *item = m->first_item;
    for (int64_t i = 0; i < index && item; i++) {
        item = item->next;
    }
    return rt_gui_wrap_menu_item(item);
}

/// @brief Enable or disable an entire menu (greys out all items when disabled).
void rt_menu_set_enabled(void *menu, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    if (!m)
        return;
    m->enabled = enabled != 0;
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_menu(m));
}

/// @brief Check whether a menu is currently enabled.
int64_t rt_menu_is_enabled(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_t *m = rt_menu_checked(menu);
    return m && m->enabled ? 1 : 0;
}

//=============================================================================
// MenuItem Widget (Phase 2)
//=============================================================================

/// @brief Set the text of the menuitem.
void rt_menuitem_set_text(void *item, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return;
    char *new_text = text ? rt_string_to_gui_cstr(text) : NULL;
    if (text && !new_text)
        return;
    free(mi->text);
    mi->text = new_text;
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_item(mi));
    if (mi->owner_contextmenu)
        vg_contextmenu_item_set_enabled(mi, mi->enabled);
}

/// @brief Get the text of the menuitem.
rt_string rt_menuitem_get_text(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return rt_str_empty();
    const char *text = mi->text;
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

/// @brief Set the shortcut of the menuitem.
void rt_menuitem_set_shortcut(void *item, rt_string shortcut) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return;
    char *new_shortcut = shortcut ? rt_string_to_gui_cstr(shortcut) : NULL;
    if (shortcut && !new_shortcut)
        return;
    free(mi->shortcut);
    mi->shortcut = new_shortcut;
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_item(mi));
    if (mi->owner_contextmenu)
        vg_contextmenu_item_set_enabled(mi, mi->enabled);
}

/// @brief Get the shortcut of the menuitem.
rt_string rt_menuitem_get_shortcut(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return rt_str_empty();
    const char *shortcut = mi->shortcut;
    if (!shortcut)
        return rt_str_empty();
    return rt_string_from_bytes(shortcut, strlen(shortcut));
}

/// @brief Set the icon of the menuitem.
void rt_menuitem_set_icon(void *item, void *pixels) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return;
    vg_icon_t icon = rt_gui_icon_from_pixels(pixels);
    if (mi->owner_contextmenu) {
        vg_contextmenu_item_set_icon(mi, icon);
        return;
    }

    vg_icon_destroy(&mi->icon);
    mi->icon = icon;
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_item(mi));
}

/// @brief Enable or disable the checkable toggle behavior for a menu item.
void rt_menuitem_set_checkable(void *item, int64_t checkable) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return;
    mi->checkable = checkable != 0;
    if (!mi->checkable)
        mi->checked = false;
    if (mi->owner_contextmenu) {
        if (mi->checkable)
            vg_contextmenu_item_set_checked(mi, mi->checked);
        else {
            vg_contextmenu_item_set_checked(mi, false);
            mi->checkable = false;
        }
    } else if (mi->parent_menu) {
        if (mi->checkable)
            vg_menu_item_set_checked(mi, mi->checked);
        rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_item(mi));
    }
}

/// @brief Check whether a menu item supports the checkable toggle state.
int64_t rt_menuitem_is_checkable(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return 0;
    return mi->checkable ? 1 : 0;
}

/// @brief Set the checked of the menuitem.
void rt_menuitem_set_checked(void *item, int64_t checked) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return;
    if (mi->owner_contextmenu) {
        vg_contextmenu_item_set_checked(mi, checked != 0);
        return;
    }
    vg_menu_item_set_checked(mi, checked != 0);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_item(mi));
}

/// @brief Check whether a menu item is currently in the checked state.
int64_t rt_menuitem_is_checked(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return 0;
    return mi->checked ? 1 : 0;
}

/// @brief Enable or disable a menu item (disabled items are greyed out).
void rt_menuitem_set_enabled(void *item, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return;
    if (mi->owner_contextmenu) {
        vg_contextmenu_item_set_enabled(mi, enabled != 0);
        return;
    }
    vg_menu_item_set_enabled(mi, enabled != 0);
    rt_gui_menu_sync_menubar(rt_gui_menu_owner_from_item(mi));
}

/// @brief Check whether a menu item is currently enabled.
int64_t rt_menuitem_is_enabled(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return 0;
    return mi->enabled ? 1 : 0;
}

/// @brief Check whether a menu item is a separator (horizontal dividing line).
int64_t rt_menuitem_is_separator(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return 0;
    return mi->separator ? 1 : 0;
}

/// @brief Check whether a menu item was clicked this frame (edge-triggered, clears after read).
int64_t rt_menuitem_was_clicked(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_menu_item_t *mi = rt_menuitem_checked(item);
    if (!mi)
        return 0;
    if (mi->was_clicked) {
        mi->was_clicked = false;
        return 1;
    }
    return 0;
}

//=============================================================================
// ContextMenu Widget (Phase 2)
//=============================================================================

/// @brief Create a new context (right-click) menu.
/// @details Context menus are standalone popup menus not attached to a menu bar.
///          Use rt_contextmenu_show to display at specific screen coordinates.
///          The menu auto-dismisses when the user clicks outside its bounds.
/// @return Opaque context menu handle, or NULL on failure.
void *rt_contextmenu_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return rt_gui_wrap_contextmenu(vg_contextmenu_create());
}

/// @brief Release resources and destroy the contextmenu.
void rt_contextmenu_destroy(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (cm) {
        rt_gui_invalidate_contextmenu_tree(cm);
        vg_contextmenu_destroy(cm);
    }
}

/// @brief Add a clickable item to a context (right-click) popup menu.
void *rt_contextmenu_add_item(void *menu, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (!cm)
        return NULL;
    char *ctext = rt_string_to_gui_cstr(text);
    vg_menu_item_t *item = vg_contextmenu_add_item(cm, ctext, NULL, NULL, NULL);
    free(ctext);
    return rt_gui_wrap_menu_item(item);
}

/// @brief Add a context-menu item with an associated keyboard shortcut.
void *rt_contextmenu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (!cm)
        return NULL;
    char *ctext = rt_string_to_gui_cstr(text);
    char *cshortcut = rt_string_to_gui_cstr(shortcut);
    vg_menu_item_t *item = vg_contextmenu_add_item(cm, ctext, cshortcut, NULL, NULL);
    free(ctext);
    free(cshortcut);
    return rt_gui_wrap_menu_item(item);
}

/// @brief Append a separator line to a context menu.
/// Returns the separator item handle so callers can configure it consistently
/// with other menu item handles.
void *rt_contextmenu_add_separator(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    return cm ? rt_gui_wrap_menu_item(vg_contextmenu_add_separator(cm)) : NULL;
}

/// @brief Add a nested submenu to a context menu; returns the new submenu handle.
void *rt_contextmenu_add_submenu(void *menu, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (!cm)
        return NULL;
    char *ctitle = rt_string_to_gui_cstr(title);

    // Create a child context menu and attach it as a submenu
    vg_contextmenu_t *submenu = vg_contextmenu_create();
    if (!submenu) {
        free(ctitle);
        return NULL;
    }
    vg_menu_item_t *item = vg_contextmenu_add_submenu(cm, ctitle, submenu);
    free(ctitle);
    if (!item) {
        vg_contextmenu_destroy(submenu);
        return NULL;
    }
    return rt_gui_wrap_contextmenu(submenu);
}

/// @brief Remove all items from a context menu.
void rt_contextmenu_clear(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (cm) {
        rt_gui_invalidate_contextmenu_contents(cm);
        vg_contextmenu_clear(cm);
    }
}

/// @brief Show the contextmenu.
void rt_contextmenu_show(void *menu, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (cm) {
        vg_contextmenu_show_at(cm,
                               rt_gui_clamp_i64_to_i32(x, INT32_MIN, INT32_MAX),
                               rt_gui_clamp_i64_to_i32(y, INT32_MIN, INT32_MAX));
    }
}

/// @brief Hide the contextmenu.
void rt_contextmenu_hide(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (cm)
        vg_contextmenu_dismiss(cm);
}

/// @brief Check whether a context menu is currently visible on screen.
int64_t rt_contextmenu_is_visible(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    return cm && cm->is_visible ? 1 : 0;
}

/// @brief Return (and consume) the most recently clicked context-menu item.
///
/// Edge-triggered: each click is reported exactly once. Subsequent
/// calls without a fresh click return NULL. The expected polling
/// pattern is `if (item := get_clicked()) handle(item);` once per frame.
void *rt_contextmenu_get_clicked_item(void *menu) {
    RT_ASSERT_MAIN_THREAD();
    vg_contextmenu_t *cm = rt_contextmenu_checked(menu);
    if (!cm)
        return NULL;
    if (cm->clicked_index >= 0 && cm->clicked_index < (int)cm->item_count) {
        vg_menu_item_t *item = cm->items[cm->clicked_index];
        cm->clicked_index = -1; // Edge-triggered: clear after read
        return rt_gui_wrap_menu_item(item);
    }
    return NULL;
}

//=============================================================================
// StatusBar Widget (Phase 3)
//=============================================================================

/// @brief Create a new status bar widget (typically placed at the bottom of a window).
/// @details Creates a vg_statusbar_t with three zones (left, center, right) for
///          displaying status text, buttons, progress indicators, and separators.
///          Items are added to specific zones and rendered in the status strip.
/// @param parent Parent container or app handle.
/// @return Opaque status bar widget handle, or NULL on failure.
void *rt_statusbar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    vg_widget_t *parent_widget = rt_gui_widget_parent_container_from_handle(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_statusbar_t *sb = vg_statusbar_create(parent_widget);
    if (sb) {
        if (app)
            rt_gui_activate_app(app);
        rt_gui_ensure_default_font();
        if (app && app->default_font)
            vg_statusbar_set_font(sb, app->default_font, app->default_font_size);
    }
    return sb;
}

/// @brief Release resources and destroy the statusbar.
void rt_statusbar_destroy(void *bar) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (sb)
        rt_widget_destroy(sb);
}

/// @brief Return the first text-type item in the given status-bar zone (or NULL).
///
/// Used by the convenience setters (`set_left_text` etc.) so they
/// can update an existing label rather than appending a new one
/// each time. Walking the zone is fine — zones rarely hold more
/// than a handful of items.
static vg_statusbar_item_t *get_zone_text_item(vg_statusbar_t *sb, vg_statusbar_zone_t zone) {
    vg_statusbar_item_t **items = NULL;
    size_t count = 0;
    switch (zone) {
        case VG_STATUSBAR_ZONE_LEFT:
            items = sb->left_items;
            count = sb->left_count;
            break;
        case VG_STATUSBAR_ZONE_CENTER:
            items = sb->center_items;
            count = sb->center_count;
            break;
        case VG_STATUSBAR_ZONE_RIGHT:
            items = sb->right_items;
            count = sb->right_count;
            break;
    }
    for (size_t i = 0; i < count; i++) {
        if (items[i] && items[i]->type == VG_STATUSBAR_ITEM_TEXT) {
            return items[i];
        }
    }
    return NULL;
}

/// @brief Set the left text of the statusbar.
void rt_statusbar_set_left_text(void *bar, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_LEFT);
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return;
    if (item) {
        vg_statusbar_item_set_text(item, ctext);
    } else {
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_LEFT, ctext);
    }
    free(ctext);
}

/// @brief Set the center text of the statusbar.
void rt_statusbar_set_center_text(void *bar, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_CENTER);
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return;
    if (item) {
        vg_statusbar_item_set_text(item, ctext);
    } else {
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_CENTER, ctext);
    }
    free(ctext);
}

/// @brief Set the right text of the statusbar.
void rt_statusbar_set_right_text(void *bar, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_RIGHT);
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return;
    if (item) {
        vg_statusbar_item_set_text(item, ctext);
    } else {
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_RIGHT, ctext);
    }
    free(ctext);
}

/// @brief Get the left text of the statusbar.
rt_string rt_statusbar_get_left_text(void *bar) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_LEFT);
    if (item && item->text) {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

/// @brief Get the center text of the statusbar.
rt_string rt_statusbar_get_center_text(void *bar) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_CENTER);
    if (item && item->text) {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

/// @brief Get the right text of the statusbar.
rt_string rt_statusbar_get_right_text(void *bar) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_RIGHT);
    if (item && item->text) {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

// ===========================================================================
// Status-bar zone-targeted item builders. `zone` is one of
// `VG_STATUSBAR_ZONE_LEFT/CENTER/RIGHT` (passed in as int64 from
// the language layer). Each returns the new item handle so callers
// can capture it for later updates (e.g. progress value).
// ===========================================================================

/// @brief Append a text label to the given status-bar zone.
void *rt_statusbar_add_text(void *bar, rt_string text, int64_t zone) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    vg_statusbar_zone_t checked_zone = VG_STATUSBAR_ZONE_LEFT;
    if (!sb || !rt_statusbar_zone_checked(zone, &checked_zone))
        return NULL;
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return NULL;
    vg_statusbar_item_t *item = vg_statusbar_add_text(sb, checked_zone, ctext);
    free(ctext);
    return rt_gui_wrap_statusbar_item(item);
}

/// @brief Append a clickable button to a status-bar zone.
void *rt_statusbar_add_button(void *bar, rt_string text, int64_t zone) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    vg_statusbar_zone_t checked_zone = VG_STATUSBAR_ZONE_LEFT;
    if (!sb || !rt_statusbar_zone_checked(zone, &checked_zone))
        return NULL;
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return NULL;
    vg_statusbar_item_t *item =
        vg_statusbar_add_button(sb, checked_zone, ctext, rt_statusbar_button_clicked, NULL);
    free(ctext);
    return rt_gui_wrap_statusbar_item(item);
}

/// @brief Append a progress bar to a status-bar zone (drive via `rt_statusbaritem_set_progress`).
void *rt_statusbar_add_progress(void *bar, int64_t zone) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    vg_statusbar_zone_t checked_zone = VG_STATUSBAR_ZONE_LEFT;
    return sb && rt_statusbar_zone_checked(zone, &checked_zone)
               ? rt_gui_wrap_statusbar_item(vg_statusbar_add_progress(sb, checked_zone))
               : NULL;
}

/// @brief Append a vertical separator line to a status-bar zone.
void *rt_statusbar_add_separator(void *bar, int64_t zone) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    vg_statusbar_zone_t checked_zone = VG_STATUSBAR_ZONE_LEFT;
    return sb && rt_statusbar_zone_checked(zone, &checked_zone)
               ? rt_gui_wrap_statusbar_item(vg_statusbar_add_separator(sb, checked_zone))
               : NULL;
}

/// @brief Append a flexible spacer (consumes free space within the zone).
void *rt_statusbar_add_spacer(void *bar, int64_t zone) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    vg_statusbar_zone_t checked_zone = VG_STATUSBAR_ZONE_LEFT;
    return sb && rt_statusbar_zone_checked(zone, &checked_zone)
               ? rt_gui_wrap_statusbar_item(vg_statusbar_add_spacer(sb, checked_zone))
               : NULL;
}

/// @brief Remove an item from the status bar.
void rt_statusbar_remove_item(void *bar, void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!bar || !item)
        return;
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sb || !sbi || sbi->owner != sb)
        return;
    rt_gui_app_t *app = rt_gui_app_from_widget(&sb->base);
    if (app && app->last_statusbar_clicked == sbi)
        app->last_statusbar_clicked = NULL;
    vg_statusbar_remove_item(sb, sbi);
}

/// @brief Remove all items from all status bar zones.
void rt_statusbar_clear(void *bar) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return;
    rt_gui_app_t *app = rt_gui_app_from_widget(&sb->base);
    if (app)
        app->last_statusbar_clicked = NULL;
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_LEFT);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_CENTER);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_RIGHT);
}

/// @brief Show or hide the status bar.
void rt_statusbar_set_visible(void *bar, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    if (!sb)
        return;
    vg_widget_set_visible(&sb->base, visible != 0);
}

/// @brief Check whether the status bar is currently visible.
int64_t rt_statusbar_is_visible(void *bar) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = rt_statusbar_checked(bar);
    return sb && sb->base.visible ? 1 : 0;
}

//=============================================================================
// StatusBarItem Widget (Phase 3)
//=============================================================================

/// @brief Set the text of the statusbaritem.
void rt_statusbaritem_set_text(void *item, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return;
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return;
    vg_statusbar_item_set_text(sbi, ctext);
    free(ctext);
}

/// @brief Set the text color of the statusbaritem.
void rt_statusbaritem_set_text_color(void *item, int64_t color) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return;
    vg_statusbar_item_set_text_color(sbi, (uint32_t)color);
}

/// @brief Get the text of the statusbaritem.
rt_string rt_statusbaritem_get_text(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return rt_str_empty();
    if (sbi->text) {
        return rt_string_from_bytes(sbi->text, strlen(sbi->text));
    }
    return rt_str_empty();
}

/// @brief Set the tooltip of the statusbaritem.
void rt_statusbaritem_set_tooltip(void *item, rt_string tooltip) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return;
    char *ctext = rt_string_to_gui_cstr(tooltip);
    if (!ctext)
        return;
    vg_statusbar_item_set_tooltip(sbi, ctext);
    free(ctext);
}

/// @brief Set the progress of the statusbaritem.
void rt_statusbaritem_set_progress(void *item, double value) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return;
    double sanitized = rt_gui_double_is_finite(value) ? value : 0.0;
    vg_statusbar_item_set_progress(sbi, (float)rt_gui_clamp_f64(sanitized, 0.0, 1.0));
}

/// @brief Get the progress of the statusbaritem.
double rt_statusbaritem_get_progress(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return 0.0;
    return (double)sbi->progress;
}

/// @brief Show or hide a status bar item.
void rt_statusbaritem_set_visible(void *item, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return;
    vg_statusbar_item_set_visible(sbi, visible != 0);
}

/// @brief Record which status bar item was clicked (for frame-based polling).
/// @param item
void rt_gui_set_clicked_statusbar_item(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = (vg_statusbar_item_t *)item;
    if (!vg_statusbar_item_is_live(sbi) || !sbi->owner)
        return;
    rt_gui_app_t *app = rt_gui_app_from_widget(&sbi->owner->base);
    if (app)
        app->last_statusbar_clicked = sbi;
}

/// @brief Check if a status bar item was clicked this frame (edge-triggered).
int64_t rt_statusbaritem_was_clicked(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_item_t *sbi = rt_statusbaritem_checked(item);
    if (!sbi)
        return 0;
    if (!sbi->owner)
        return 0;
    rt_gui_app_t *app = rt_gui_app_from_widget(&sbi->owner->base);
    if (!app || app->last_statusbar_clicked != sbi)
        return 0;
    app->last_statusbar_clicked = NULL;
    return 1;
}

//=============================================================================
// Toolbar Widget (Phase 3)
//=============================================================================

/// @brief Create a new horizontal toolbar widget.
/// @details Creates a vg_toolbar_t for displaying icon buttons, toggles,
///          separators, and dropdown items in a horizontal strip. Typically
///          placed below the menu bar. Buttons can carry both icons and labels.
/// @param parent Parent container or app handle.
/// @return Opaque toolbar widget handle, or NULL on failure.
void *rt_toolbar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    vg_widget_t *parent_widget = rt_gui_widget_parent_container_from_handle(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_toolbar_t *tb = vg_toolbar_create(parent_widget, VG_TOOLBAR_HORIZONTAL);
    if (tb) {
        if (app)
            rt_gui_activate_app(app);
        rt_gui_ensure_default_font();
        if (app && app->default_font)
            vg_toolbar_set_font(tb, app->default_font, app->default_font_size);
    }
    return tb;
}

/// @brief Create a new vertical toolbar widget.
/// @details Like rt_toolbar_new but arranged vertically (e.g., sidebar tool palette).
/// @param parent Parent container or app handle.
/// @return Opaque toolbar widget handle, or NULL on failure.
void *rt_toolbar_new_vertical(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    vg_widget_t *parent_widget = rt_gui_widget_parent_container_from_handle(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_toolbar_t *tb = vg_toolbar_create(parent_widget, VG_TOOLBAR_VERTICAL);
    if (tb) {
        if (app)
            rt_gui_activate_app(app);
        rt_gui_ensure_default_font();
        if (app && app->default_font)
            vg_toolbar_set_font(tb, app->default_font, app->default_font_size);
    }
    return tb;
}

/// @brief Release resources and destroy the toolbar.
void rt_toolbar_destroy(void *toolbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (tb)
        rt_widget_destroy(tb);
}

/// @brief Append an icon-only toolbar button.
///
/// Loads the icon from `icon_path` and uses `tooltip` for hover text.
void *rt_toolbar_add_button(void *toolbar, rt_string icon_path, rt_string tooltip) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctooltip = rt_string_to_gui_cstr(tooltip);
    if ((icon_path && !cicon) || !ctooltip) {
        free(cicon);
        free(ctooltip);
        return NULL;
    }

    vg_icon_t icon = rt_gui_icon_from_path_cstr(cicon);
    free(cicon);
    cicon = NULL;

    vg_toolbar_item_t *item = vg_toolbar_add_button(tb, NULL, NULL, icon, NULL, NULL);
    if (item) {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(ctooltip);
    return rt_gui_wrap_toolbar_item(item);
}

/// @brief Append a toolbar button that shows both an icon and a text label.
///
/// Forces `show_label = true` on the resulting item — toolbars
/// default to icon-only display, so this is required for the
/// label to actually render.
void *rt_toolbar_add_button_with_text(void *toolbar,
                                      rt_string icon_path,
                                      rt_string text,
                                      rt_string tooltip) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctext = rt_string_to_gui_cstr(text);
    char *ctooltip = rt_string_to_gui_cstr(tooltip);
    if ((icon_path && !cicon) || !ctext || !ctooltip) {
        free(cicon);
        free(ctext);
        free(ctooltip);
        return NULL;
    }

    vg_icon_t icon = rt_gui_icon_from_path_cstr(cicon);
    free(cicon);
    cicon = NULL;

    vg_toolbar_item_t *item = vg_toolbar_add_button(tb, NULL, ctext, icon, NULL, NULL);
    if (item) {
        // Force label visible — tb->show_labels defaults to false, so items
        // created via AddButtonWithText would otherwise never show their label.
        item->show_label = true;
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(ctext);
    free(ctooltip);
    return rt_gui_wrap_toolbar_item(item);
}

/// @brief Append a sticky toggle button (radio/checkbox-style press state).
void *rt_toolbar_add_toggle(void *toolbar, rt_string icon_path, rt_string tooltip) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctooltip = rt_string_to_gui_cstr(tooltip);
    if ((icon_path && !cicon) || !ctooltip) {
        free(cicon);
        free(ctooltip);
        return NULL;
    }

    vg_icon_t icon = rt_gui_icon_from_path_cstr(cicon);
    free(cicon);
    cicon = NULL;

    vg_toolbar_item_t *item = vg_toolbar_add_toggle(tb, NULL, NULL, icon, false, NULL, NULL);
    if (item) {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(ctooltip);
    return rt_gui_wrap_toolbar_item(item);
}

/// @brief Append a vertical (or horizontal, for vertical toolbars) separator line.
void *rt_toolbar_add_separator(void *toolbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    return tb ? rt_gui_wrap_toolbar_item(vg_toolbar_add_separator(tb)) : NULL;
}

/// @brief Append a flexible spacer (consumes free space, useful for right-aligning items).
void *rt_toolbar_add_spacer(void *toolbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    return tb ? rt_gui_wrap_toolbar_item(vg_toolbar_add_spacer(tb)) : NULL;
}

/// @brief Append a dropdown button — clicking opens an attached menu of choices.
void *rt_toolbar_add_dropdown(void *toolbar, rt_string tooltip) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return NULL;
    char *ctooltip = rt_string_to_gui_cstr(tooltip);
    if (!ctooltip)
        return NULL;

    vg_icon_t icon = {0};
    icon.type = VG_ICON_NONE;

    vg_toolbar_item_t *item = vg_toolbar_add_dropdown(tb, NULL, NULL, icon, NULL);
    if (item) {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(ctooltip);
    return rt_gui_wrap_toolbar_item(item);
}

/// @brief Remove an item from the toolbar.
void rt_toolbar_remove_item(void *toolbar, void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar || !item)
        return;
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return;
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti || ti->owner != tb)
        return;
    rt_gui_app_t *app = rt_gui_app_from_widget(&tb->base);
    if (app && app->last_toolbar_clicked == ti)
        app->last_toolbar_clicked = NULL;
    vg_toolbar_remove_item_ptr(tb, ti);
}

/// @brief Get a size property of the toolbar (button width or height).
void rt_toolbar_set_icon_size(void *toolbar, int64_t size) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return;
    vg_toolbar_set_icon_size(tb,
                             (vg_toolbar_icon_size_t)rt_gui_clamp_i64_to_i32(size, 0, 2));
}

/// @brief Get a size property of the toolbar (button width or height).
int64_t rt_toolbar_get_icon_size(void *toolbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    return tb ? tb->icon_size : RT_TOOLBAR_ICON_MEDIUM;
}

/// @brief Set the style of the toolbar.
void rt_toolbar_set_style(void *toolbar, int64_t style) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return;
    if (style < RT_TOOLBAR_STYLE_ICON_ONLY || style > RT_TOOLBAR_STYLE_ICON_TEXT)
        return;
    vg_toolbar_set_show_labels(tb, style != RT_TOOLBAR_STYLE_ICON_ONLY);
}

/// @brief Get the number of items in the toolbar.
int64_t rt_toolbar_get_item_count(void *toolbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    return tb ? (int64_t)tb->item_count : 0;
}

/// @brief Return the `index`-th item in the toolbar (NULL on out-of-range).
void *rt_toolbar_get_item(void *toolbar, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return NULL;
    if (index < 0 || index >= (int64_t)tb->item_count)
        return NULL;
    return rt_gui_wrap_toolbar_item(tb->items[index]);
}

/// @brief Show or hide the toolbar.
void rt_toolbar_set_visible(void *toolbar, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    if (!tb)
        return;
    vg_widget_set_visible(&tb->base, visible != 0);
}

/// @brief Check whether the toolbar is currently visible.
int64_t rt_toolbar_is_visible(void *toolbar) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = rt_toolbar_checked(toolbar);
    return tb && tb->base.visible ? 1 : 0;
}

//=============================================================================
// ToolbarItem Widget (Phase 3)
//=============================================================================

/// @brief Set the icon of the toolbaritem.
void rt_toolbaritem_set_icon(void *item, rt_string icon_path) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return;
    char *cicon = rt_string_to_cstr(icon_path);
    vg_icon_t icon = rt_gui_icon_from_path_cstr(cicon);
    free(cicon);
    vg_toolbar_item_set_icon(ti, icon);
}

/// @brief Set the icon pixels of the toolbaritem.
void rt_toolbaritem_set_icon_pixels(void *item, void *pixels) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return;
    vg_toolbar_item_set_icon(ti, rt_gui_icon_from_pixels(pixels));
}

/// @brief Set the text of the toolbaritem.
void rt_toolbaritem_set_text(void *item, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return;
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return;
    vg_toolbar_item_set_text(ti, ctext);
    free(ctext);
}

/// @brief Set the tooltip of the toolbaritem.
void rt_toolbaritem_set_tooltip(void *item, rt_string tooltip) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return;
    char *ctooltip = rt_string_to_gui_cstr(tooltip);
    if (!ctooltip)
        return;
    vg_toolbar_item_set_tooltip(ti, ctooltip);
    free(ctooltip);
}

/// @brief Enable or disable a toolbar item.
void rt_toolbaritem_set_enabled(void *item, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return;
    vg_toolbar_item_set_enabled(ti, enabled != 0);
}

/// @brief Check whether a toolbar item is currently enabled.
int64_t rt_toolbaritem_is_enabled(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return 0;
    return ti->enabled ? 1 : 0;
}

/// @brief Set the toggled of the toolbaritem.
void rt_toolbaritem_set_toggled(void *item, int64_t toggled) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return;
    vg_toolbar_item_set_checked(ti, toggled != 0);
}

/// @brief Check whether a toolbar toggle button is currently in the toggled state.
int64_t rt_toolbaritem_is_toggled(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return 0;
    return ti->checked ? 1 : 0;
}

/// @brief Record which toolbar item was clicked (for frame-based polling).
/// @param item
void rt_gui_set_clicked_toolbar_item(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    if (!vg_toolbar_item_is_live(ti) || !ti->owner)
        return;
    rt_gui_app_t *app = rt_gui_app_from_widget(&ti->owner->base);
    if (app)
        app->last_toolbar_clicked = ti;
}

/// @brief Check if a toolbar button was clicked this frame (edge-triggered).
int64_t rt_toolbaritem_was_clicked(void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_item_t *ti = rt_toolbaritem_checked(item);
    if (!ti)
        return 0;
    if (ti->was_clicked) {
        ti->was_clicked = false;
        rt_gui_app_t *app = ti->owner ? rt_gui_app_from_widget(&ti->owner->base) : NULL;
        if (app && app->last_toolbar_clicked == ti)
            app->last_toolbar_clicked = NULL;
        return 1;
    }
    if (!ti->owner)
        return 0;
    rt_gui_app_t *app = rt_gui_app_from_widget(&ti->owner->base);
    if (app && app->last_toolbar_clicked == ti) {
        app->last_toolbar_clicked = NULL;
        return 1;
    }
    return 0;
}

#else /* !VIPER_ENABLE_GRAPHICS */

// ===========================================================================
// Headless stubs — same prototypes as the real implementations above so
// non-graphical builds (server / CLI / ViperDOS) can link without pulling
// in the GUI subsystem. Each stub safely no-ops or returns a sentinel
// (NULL pointer, 0, or empty string) without referencing any GUI state.
// Doxygen comments are inherited from the real implementations above by
// virtue of identical names — these stubs intentionally have no extra docs.
// ===========================================================================

/// @brief Stub: graphics disabled — returns NULL; no menu bar widget is created.
void *rt_menubar_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Release resources and destroy the menubar.
void rt_menubar_destroy(void *menubar) {
    (void)menubar;
}

/// @brief Stub: graphics disabled — returns NULL; no menu is added to the menu bar.
void *rt_menubar_add_menu(void *menubar, rt_string title) {
    (void)menubar;
    (void)title;
    return NULL;
}

/// @brief Remove a menu from the menu bar.
void rt_menubar_remove_menu(void *menubar, void *menu) {
    (void)menubar;
    (void)menu;
}

/// @brief Get the number of menus in the menu bar.
int64_t rt_menubar_get_menu_count(void *menubar) {
    (void)menubar;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no menu bar exists to retrieve from.
void *rt_menubar_get_menu(void *menubar, int64_t index) {
    (void)menubar;
    (void)index;
    return NULL;
}

/// @brief Show or hide the menu bar widget.
void rt_menubar_set_visible(void *menubar, int64_t visible) {
    (void)menubar;
    (void)visible;
}

/// @brief Check whether the menu bar is currently visible.
int64_t rt_menubar_is_visible(void *menubar) {
    (void)menubar;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no menu item is created.
void *rt_menu_add_item(void *menu, rt_string text) {
    (void)menu;
    (void)text;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no menu item with shortcut is created.
void *rt_menu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut) {
    (void)menu;
    (void)text;
    (void)shortcut;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no separator item is created.
void *rt_menu_add_separator(void *menu) {
    (void)menu;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no submenu is created.
void *rt_menu_add_submenu(void *menu, rt_string title) {
    (void)menu;
    (void)title;
    return NULL;
}

/// @brief Remove a menu item and free its resources.
void rt_menu_remove_item(void *menu, void *item) {
    (void)menu;
    (void)item;
}

/// @brief Remove all items from a menu, leaving it empty.
void rt_menu_clear(void *menu) {
    (void)menu;
}

/// @brief Set the title of the menu.
void rt_menu_set_title(void *menu, rt_string title) {
    (void)menu;
    (void)title;
}

/// @brief Get the title of the menu.
rt_string rt_menu_get_title(void *menu) {
    (void)menu;
    return rt_str_empty();
}

/// @brief Get the number of items in a menu.
int64_t rt_menu_get_item_count(void *menu) {
    (void)menu;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no menu exists to retrieve items from.
void *rt_menu_get_item(void *menu, int64_t index) {
    (void)menu;
    (void)index;
    return NULL;
}

/// @brief Enable or disable an entire menu (greys out all items when disabled).
void rt_menu_set_enabled(void *menu, int64_t enabled) {
    (void)menu;
    (void)enabled;
}

/// @brief Check whether a menu is currently enabled.
int64_t rt_menu_is_enabled(void *menu) {
    (void)menu;
    return 0;
}

/// @brief Set the text of the menuitem.
void rt_menuitem_set_text(void *item, rt_string text) {
    (void)item;
    (void)text;
}

/// @brief Get the text of the menuitem.
rt_string rt_menuitem_get_text(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Set the shortcut of the menuitem.
void rt_menuitem_set_shortcut(void *item, rt_string shortcut) {
    (void)item;
    (void)shortcut;
}

/// @brief Get the shortcut of the menuitem.
rt_string rt_menuitem_get_shortcut(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Set the icon of the menuitem.
void rt_menuitem_set_icon(void *item, void *pixels) {
    (void)item;
    (void)pixels;
}

/// @brief Enable or disable the checkable toggle behavior for a menu item.
void rt_menuitem_set_checkable(void *item, int64_t checkable) {
    (void)item;
    (void)checkable;
}

/// @brief Check whether a menu item supports the checkable toggle state.
int64_t rt_menuitem_is_checkable(void *item) {
    (void)item;
    return 0;
}

/// @brief Set the checked of the menuitem.
void rt_menuitem_set_checked(void *item, int64_t checked) {
    (void)item;
    (void)checked;
}

/// @brief Check whether a menu item is currently in the checked state.
int64_t rt_menuitem_is_checked(void *item) {
    (void)item;
    return 0;
}

/// @brief Enable or disable a menu item (disabled items are greyed out).
void rt_menuitem_set_enabled(void *item, int64_t enabled) {
    (void)item;
    (void)enabled;
}

/// @brief Check whether a menu item is currently enabled.
int64_t rt_menuitem_is_enabled(void *item) {
    (void)item;
    return 0;
}

/// @brief Check whether a menu item is a separator (horizontal dividing line).
int64_t rt_menuitem_is_separator(void *item) {
    (void)item;
    return 0;
}

/// @brief Check whether a menu item was clicked this frame (edge-triggered, clears after read).
int64_t rt_menuitem_was_clicked(void *item) {
    (void)item;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no context menu is created.
void *rt_contextmenu_new(void) {
    return NULL;
}

/// @brief Release resources and destroy the contextmenu.
void rt_contextmenu_destroy(void *menu) {
    (void)menu;
}

/// @brief Stub: graphics disabled — returns NULL; no context menu item is created.
void *rt_contextmenu_add_item(void *menu, rt_string text) {
    (void)menu;
    (void)text;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no context menu item with shortcut is created.
void *rt_contextmenu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut) {
    (void)menu;
    (void)text;
    (void)shortcut;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no context menu separator is created.
void *rt_contextmenu_add_separator(void *menu) {
    (void)menu;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no context menu submenu is created.
void *rt_contextmenu_add_submenu(void *menu, rt_string title) {
    (void)menu;
    (void)title;
    return NULL;
}

/// @brief Remove all items from a context menu.
void rt_contextmenu_clear(void *menu) {
    (void)menu;
}

/// @brief Show the contextmenu.
void rt_contextmenu_show(void *menu, int64_t x, int64_t y) {
    (void)menu;
    (void)x;
    (void)y;
}

/// @brief Hide the contextmenu.
void rt_contextmenu_hide(void *menu) {
    (void)menu;
}

/// @brief Check whether a context menu is currently visible on screen.
int64_t rt_contextmenu_is_visible(void *menu) {
    (void)menu;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no context menu item was clicked.
void *rt_contextmenu_get_clicked_item(void *menu) {
    (void)menu;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no status bar widget is created.
void *rt_statusbar_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Release resources and destroy the statusbar.
void rt_statusbar_destroy(void *bar) {
    (void)bar;
}

/// @brief Set the left text of the statusbar.
void rt_statusbar_set_left_text(void *bar, rt_string text) {
    (void)bar;
    (void)text;
}

/// @brief Set the center text of the statusbar.
void rt_statusbar_set_center_text(void *bar, rt_string text) {
    (void)bar;
    (void)text;
}

/// @brief Set the right text of the statusbar.
void rt_statusbar_set_right_text(void *bar, rt_string text) {
    (void)bar;
    (void)text;
}

/// @brief Get the left text of the statusbar.
rt_string rt_statusbar_get_left_text(void *bar) {
    (void)bar;
    return rt_str_empty();
}

/// @brief Get the center text of the statusbar.
rt_string rt_statusbar_get_center_text(void *bar) {
    (void)bar;
    return rt_str_empty();
}

/// @brief Get the right text of the statusbar.
rt_string rt_statusbar_get_right_text(void *bar) {
    (void)bar;
    return rt_str_empty();
}

/// @brief Stub: graphics disabled — returns NULL; no status bar text item is created.
void *rt_statusbar_add_text(void *bar, rt_string text, int64_t zone) {
    (void)bar;
    (void)text;
    (void)zone;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no status bar button item is created.
void *rt_statusbar_add_button(void *bar, rt_string text, int64_t zone) {
    (void)bar;
    (void)text;
    (void)zone;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no status bar progress item is created.
void *rt_statusbar_add_progress(void *bar, int64_t zone) {
    (void)bar;
    (void)zone;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no status bar separator item is created.
void *rt_statusbar_add_separator(void *bar, int64_t zone) {
    (void)bar;
    (void)zone;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no status bar spacer item is created.
void *rt_statusbar_add_spacer(void *bar, int64_t zone) {
    (void)bar;
    (void)zone;
    return NULL;
}

/// @brief Remove an item from the status bar.
void rt_statusbar_remove_item(void *bar, void *item) {
    (void)bar;
    (void)item;
}

/// @brief Remove all items from all status bar zones.
void rt_statusbar_clear(void *bar) {
    (void)bar;
}

/// @brief Show or hide the status bar.
void rt_statusbar_set_visible(void *bar, int64_t visible) {
    (void)bar;
    (void)visible;
}

/// @brief Check whether the status bar is currently visible.
int64_t rt_statusbar_is_visible(void *bar) {
    (void)bar;
    return 0;
}

/// @brief Set the text of the statusbaritem.
void rt_statusbaritem_set_text(void *item, rt_string text) {
    (void)item;
    (void)text;
}

/// @brief Set the text color of the statusbaritem.
void rt_statusbaritem_set_text_color(void *item, int64_t color) {
    (void)item;
    (void)color;
}

/// @brief Get the text of the statusbaritem.
rt_string rt_statusbaritem_get_text(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Set the tooltip of the statusbaritem.
void rt_statusbaritem_set_tooltip(void *item, rt_string tooltip) {
    (void)item;
    (void)tooltip;
}

/// @brief Set the progress of the statusbaritem.
void rt_statusbaritem_set_progress(void *item, double value) {
    (void)item;
    (void)value;
}

/// @brief Get the progress of the statusbaritem.
double rt_statusbaritem_get_progress(void *item) {
    (void)item;
    return 0.0;
}

/// @brief Show or hide a status bar item.
void rt_statusbaritem_set_visible(void *item, int64_t visible) {
    (void)item;
    (void)visible;
}

/// @brief Record which status bar item was clicked (for frame-based polling).
/// @param item
void rt_gui_set_clicked_statusbar_item(void *item) {
    (void)item;
}

/// @brief Check if a status bar item was clicked this frame (edge-triggered).
int64_t rt_statusbaritem_was_clicked(void *item) {
    (void)item;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no horizontal toolbar widget is created.
void *rt_toolbar_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no vertical toolbar widget is created.
void *rt_toolbar_new_vertical(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Release resources and destroy the toolbar.
void rt_toolbar_destroy(void *toolbar) {
    (void)toolbar;
}

/// @brief Stub: graphics disabled — returns NULL; no toolbar button is created.
void *rt_toolbar_add_button(void *toolbar, rt_string icon_path, rt_string tooltip) {
    (void)toolbar;
    (void)icon_path;
    (void)tooltip;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no toolbar button with text label is created.
void *rt_toolbar_add_button_with_text(void *toolbar,
                                      rt_string icon_path,
                                      rt_string text,
                                      rt_string tooltip) {
    (void)toolbar;
    (void)icon_path;
    (void)text;
    (void)tooltip;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no toolbar toggle button is created.
void *rt_toolbar_add_toggle(void *toolbar, rt_string icon_path, rt_string tooltip) {
    (void)toolbar;
    (void)icon_path;
    (void)tooltip;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no toolbar separator item is created.
void *rt_toolbar_add_separator(void *toolbar) {
    (void)toolbar;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no toolbar spacer item is created.
void *rt_toolbar_add_spacer(void *toolbar) {
    (void)toolbar;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no toolbar dropdown item is created.
void *rt_toolbar_add_dropdown(void *toolbar, rt_string tooltip) {
    (void)toolbar;
    (void)tooltip;
    return NULL;
}

/// @brief Remove an item from the toolbar.
void rt_toolbar_remove_item(void *toolbar, void *item) {
    (void)toolbar;
    (void)item;
}

/// @brief Get a size property of the toolbar (button width or height).
void rt_toolbar_set_icon_size(void *toolbar, int64_t size) {
    (void)toolbar;
    (void)size;
}

/// @brief Get a size property of the toolbar (button width or height).
int64_t rt_toolbar_get_icon_size(void *toolbar) {
    (void)toolbar;
    return 0;
}

/// @brief Set the style of the toolbar.
void rt_toolbar_set_style(void *toolbar, int64_t style) {
    (void)toolbar;
    (void)style;
}

/// @brief Get the number of items in the toolbar.
int64_t rt_toolbar_get_item_count(void *toolbar) {
    (void)toolbar;
    return 0;
}

/// @brief Stub: graphics disabled — returns NULL; no toolbar exists to retrieve items from.
void *rt_toolbar_get_item(void *toolbar, int64_t index) {
    (void)toolbar;
    (void)index;
    return NULL;
}

/// @brief Show or hide the toolbar.
void rt_toolbar_set_visible(void *toolbar, int64_t visible) {
    (void)toolbar;
    (void)visible;
}

/// @brief Check whether the toolbar is currently visible.
int64_t rt_toolbar_is_visible(void *toolbar) {
    (void)toolbar;
    return 0;
}

/// @brief Set the icon of the toolbaritem.
void rt_toolbaritem_set_icon(void *item, rt_string icon_path) {
    (void)item;
    (void)icon_path;
}

/// @brief Set the icon pixels of the toolbaritem.
void rt_toolbaritem_set_icon_pixels(void *item, void *pixels) {
    (void)item;
    (void)pixels;
}

/// @brief Set the text of the toolbaritem.
void rt_toolbaritem_set_text(void *item, rt_string text) {
    (void)item;
    (void)text;
}

/// @brief Set the tooltip of the toolbaritem.
void rt_toolbaritem_set_tooltip(void *item, rt_string tooltip) {
    (void)item;
    (void)tooltip;
}

/// @brief Enable or disable a toolbar item.
void rt_toolbaritem_set_enabled(void *item, int64_t enabled) {
    (void)item;
    (void)enabled;
}

/// @brief Check whether a toolbar item is currently enabled.
int64_t rt_toolbaritem_is_enabled(void *item) {
    (void)item;
    return 0;
}

/// @brief Set the toggled of the toolbaritem.
void rt_toolbaritem_set_toggled(void *item, int64_t toggled) {
    (void)item;
    (void)toggled;
}

/// @brief Check whether a toolbar toggle button is currently in the toggled state.
int64_t rt_toolbaritem_is_toggled(void *item) {
    (void)item;
    return 0;
}

/// @brief Record which toolbar item was clicked (for frame-based polling).
/// @param item
void rt_gui_set_clicked_toolbar_item(void *item) {
    (void)item;
}

/// @brief Check if a toolbar button was clicked this frame (edge-triggered).
int64_t rt_toolbaritem_was_clicked(void *item) {
    (void)item;
    return 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
