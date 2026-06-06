//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_bars.c
// Purpose: Status-bar and tool-bar GUI widgets (and their items) for the Viper
//          runtime. Split out of rt_gui_menus.c; shares widget types and the
//          status-bar/tool-bar cast + icon helpers via rt_gui_internal.h.
//
// Key invariants:
//   - Widget handles are validated through the rt_*_checked cast helpers before
//     use, so a wrong-typed or stale handle is rejected rather than misread.
//   - Item helpers operate relative to their owning bar; a detached item is a
//     no-op rather than a crash.
//   - Mirrors rt_gui_menus.c's VIPER_ENABLE_GRAPHICS guard: real widgets when
//     graphics is enabled, no-op stubs otherwise.
//
// Ownership/Lifetime:
//   - Widgets are owned by the GUI widget tree; this layer borrows them.
//
// Links: src/runtime/graphics/gui/rt_gui_menus.c (menu widgets + shared helpers),
//        src/runtime/graphics/gui/rt_gui_internal.h (shared GUI types + API)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_pixels.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

void rt_gui_set_clicked_statusbar_item(void *item);

/// @brief Status-bar item click callback: record the clicked item for the next poll.
/// @details Matches the GUI library's callback signature; @p user_data is unused
///          because the clicked item is surfaced through global poll state instead.
static void rt_statusbar_button_clicked(vg_statusbar_item_t *item, void *user_data) {
    (void)user_data;
    rt_gui_set_clicked_statusbar_item(item);
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
    char *cicon = rt_string_to_cstr_no_nul(icon_path);
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
    char *cicon = rt_string_to_cstr_no_nul(icon_path);
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
    char *cicon = rt_string_to_cstr_no_nul(icon_path);
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
    vg_toolbar_set_icon_size(tb, (vg_toolbar_icon_size_t)rt_gui_clamp_i64_to_i32(size, 0, 2));
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
    char *cicon = rt_string_to_cstr_no_nul(icon_path);
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
