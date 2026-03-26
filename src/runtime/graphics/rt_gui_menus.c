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
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// MenuBar Widget (Phase 2)
//=============================================================================

void *rt_menubar_new(void *parent)
{
    RT_ASSERT_MAIN_THREAD();
    vg_menubar_t *mb = vg_menubar_create((vg_widget_t *)parent);
    if (mb)
    {
        rt_gui_ensure_default_font();
        if (s_current_app && s_current_app->default_font)
            vg_menubar_set_font(mb, s_current_app->default_font, s_current_app->default_font_size);
    }
    return mb;
}

/// @brief Perform menubar destroy operation.
/// @param menubar
void rt_menubar_destroy(void *menubar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menubar)
        return;
    vg_widget_destroy((vg_widget_t *)menubar);
}

void *rt_menubar_add_menu(void *menubar, rt_string title)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menubar)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_menu_t *menu = vg_menubar_add_menu((vg_menubar_t *)menubar, ctitle);
    free(ctitle);
    return menu;
}

/// @brief Perform menubar remove menu operation.
/// @param menubar
/// @param menu
void rt_menubar_remove_menu(void *menubar, void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menubar || !menu)
        return;
    vg_menubar_remove_menu((vg_menubar_t *)menubar, (vg_menu_t *)menu);
}

/// @brief Perform menubar get menu count operation.
/// @param menubar
/// @return Result value.
int64_t rt_menubar_get_menu_count(void *menubar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menubar)
        return 0;
    return ((vg_menubar_t *)menubar)->menu_count;
}

void *rt_menubar_get_menu(void *menubar, int64_t index)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menubar)
        return NULL;
    vg_menubar_t *mb = (vg_menubar_t *)menubar;
    if (index < 0 || index >= mb->menu_count)
        return NULL;

    vg_menu_t *menu = mb->first_menu;
    for (int64_t i = 0; i < index && menu; i++)
    {
        menu = menu->next;
    }
    return menu;
}

/// @brief Perform menubar set visible operation.
/// @param menubar
/// @param visible
void rt_menubar_set_visible(void *menubar, int64_t visible)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menubar)
        return;
    vg_widget_set_visible(&((vg_menubar_t *)menubar)->base, visible != 0);
}

/// @brief Perform menubar is visible operation.
/// @param menubar
/// @return Result value.
int64_t rt_menubar_is_visible(void *menubar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menubar)
        return 0;
    return ((vg_menubar_t *)menubar)->base.visible ? 1 : 0;
}

//=============================================================================
// Menu Widget (Phase 2)
//=============================================================================

void *rt_menu_add_item(void *menu, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_menu_item_t *item = vg_menu_add_item((vg_menu_t *)menu, ctext, NULL, NULL, NULL);
    free(ctext);
    return item;
}

void *rt_menu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    char *cshortcut = rt_string_to_cstr(shortcut);
    vg_menu_item_t *item = vg_menu_add_item((vg_menu_t *)menu, ctext, cshortcut, NULL, NULL);
    free(ctext);
    free(cshortcut);
    return item;
}

void *rt_menu_add_separator(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    return vg_menu_add_separator((vg_menu_t *)menu);
}

void *rt_menu_add_submenu(void *menu, rt_string title)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_menu_t *submenu = vg_menu_add_submenu((vg_menu_t *)menu, ctitle);
    free(ctitle);
    return submenu;
}

/// @brief Perform menu remove item operation.
/// @param menu
/// @param item
void rt_menu_remove_item(void *menu, void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu || !item)
        return;
    vg_menu_remove_item((vg_menu_t *)menu, (vg_menu_item_t *)item);
}

/// @brief Perform menu clear operation.
/// @param menu
void rt_menu_clear(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return;
    vg_menu_clear((vg_menu_t *)menu);
}

/// @brief Perform menu set title operation.
/// @param menu
/// @param title
void rt_menu_set_title(void *menu, rt_string title)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return;
    vg_menu_t *m = (vg_menu_t *)menu;
    free(m->title);
    m->title = rt_string_to_cstr(title);
}

/// @brief Perform menu get title operation.
/// @param menu
/// @return Result value.
rt_string rt_menu_get_title(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return rt_str_empty();
    const char *title = ((vg_menu_t *)menu)->title;
    if (!title)
        return rt_str_empty();
    return rt_string_from_bytes(title, strlen(title));
}

/// @brief Perform menu get item count operation.
/// @param menu
/// @return Result value.
int64_t rt_menu_get_item_count(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return 0;
    return ((vg_menu_t *)menu)->item_count;
}

void *rt_menu_get_item(void *menu, int64_t index)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    vg_menu_t *m = (vg_menu_t *)menu;
    if (index < 0 || index >= m->item_count)
        return NULL;

    vg_menu_item_t *item = m->first_item;
    for (int64_t i = 0; i < index && item; i++)
    {
        item = item->next;
    }
    return item;
}

/// @brief Perform menu set enabled operation.
/// @param menu
/// @param enabled
void rt_menu_set_enabled(void *menu, int64_t enabled)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return;
    ((vg_menu_t *)menu)->enabled = enabled != 0;
}

/// @brief Perform menu is enabled operation.
/// @param menu
/// @return Result value.
int64_t rt_menu_is_enabled(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return 0;
    return ((vg_menu_t *)menu)->enabled ? 1 : 0;
}

//=============================================================================
// MenuItem Widget (Phase 2)
//=============================================================================

/// @brief Perform menuitem set text operation.
/// @param item
/// @param text
void rt_menuitem_set_text(void *item, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    free(mi->text);
    mi->text = rt_string_to_cstr(text);
}

/// @brief Perform menuitem get text operation.
/// @param item
/// @return Result value.
rt_string rt_menuitem_get_text(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    const char *text = ((vg_menu_item_t *)item)->text;
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

/// @brief Perform menuitem set shortcut operation.
/// @param item
/// @param shortcut
void rt_menuitem_set_shortcut(void *item, rt_string shortcut)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    free(mi->shortcut);
    mi->shortcut = rt_string_to_cstr(shortcut);
}

/// @brief Perform menuitem get shortcut operation.
/// @param item
/// @return Result value.
rt_string rt_menuitem_get_shortcut(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    const char *shortcut = ((vg_menu_item_t *)item)->shortcut;
    if (!shortcut)
        return rt_str_empty();
    return rt_string_from_bytes(shortcut, strlen(shortcut));
}

/// @brief Perform menuitem set icon operation.
/// @param item
/// @param pixels
void rt_menuitem_set_icon(void *item, void *pixels)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    if (pixels)
    {
        // Store as a glyph icon (pixels pointer used as codepoint for icon text)
        mi->icon.type = VG_ICON_GLYPH;
        mi->icon.data.glyph = (uint32_t)(uintptr_t)pixels;
    }
    else
    {
        mi->icon.type = VG_ICON_NONE;
    }
}

/// @brief Perform menuitem set checkable operation.
/// @param item
/// @param checkable
void rt_menuitem_set_checkable(void *item, int64_t checkable)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    /* Mark the item as a toggle type by setting its checked state.
       When checkable is disabled, also clear the checked state. */
    if (!checkable)
        vg_menu_item_set_checked((vg_menu_item_t *)item, false);
}

/// @brief Perform menuitem is checkable operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_checkable(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    /* An item is considered checkable if it has ever had a checked state set. */
    (void)item;
    return 1; /* All menu items support the checked field */
}

/// @brief Perform menuitem set checked operation.
/// @param item
/// @param checked
void rt_menuitem_set_checked(void *item, int64_t checked)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    ((vg_menu_item_t *)item)->checked = checked != 0;
}

/// @brief Perform menuitem is checked operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_checked(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->checked ? 1 : 0;
}

/// @brief Perform menuitem set enabled operation.
/// @param item
/// @param enabled
void rt_menuitem_set_enabled(void *item, int64_t enabled)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    ((vg_menu_item_t *)item)->enabled = enabled != 0;
}

/// @brief Perform menuitem is enabled operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_enabled(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->enabled ? 1 : 0;
}

/// @brief Perform menuitem is separator operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_separator(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->separator ? 1 : 0;
}

/// @brief Perform menuitem was clicked operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_was_clicked(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    if (mi->was_clicked)
    {
        mi->was_clicked = false;
        return 1;
    }
    return 0;
}

//=============================================================================
// ContextMenu Widget (Phase 2)
//=============================================================================

void *rt_contextmenu_new(void)
{
    RT_ASSERT_MAIN_THREAD();
    return vg_contextmenu_create();
}

/// @brief Perform contextmenu destroy operation.
/// @param menu
void rt_contextmenu_destroy(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (menu)
    {
        vg_contextmenu_destroy((vg_contextmenu_t *)menu);
    }
}

void *rt_contextmenu_add_item(void *menu, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_menu_item_t *item =
        vg_contextmenu_add_item((vg_contextmenu_t *)menu, ctext, NULL, NULL, NULL);
    free(ctext);
    return item;
}

void *rt_contextmenu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    char *cshortcut = rt_string_to_cstr(shortcut);
    vg_menu_item_t *item =
        vg_contextmenu_add_item((vg_contextmenu_t *)menu, ctext, cshortcut, NULL, NULL);
    free(ctext);
    free(cshortcut);
    return item;
}

void *rt_contextmenu_add_separator(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    vg_contextmenu_add_separator((vg_contextmenu_t *)menu);
    return NULL; // vg_contextmenu_add_separator returns void
}

void *rt_contextmenu_add_submenu(void *menu, rt_string title)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);

    // Create a child context menu and attach it as a submenu
    vg_contextmenu_t *submenu = vg_contextmenu_create();
    if (!submenu)
    {
        free(ctitle);
        return NULL;
    }
    vg_contextmenu_add_submenu((vg_contextmenu_t *)menu, ctitle, submenu);
    free(ctitle);
    return submenu;
}

/// @brief Perform contextmenu clear operation.
/// @param menu
void rt_contextmenu_clear(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (menu)
    {
        vg_contextmenu_clear((vg_contextmenu_t *)menu);
    }
}

/// @brief Perform contextmenu show operation.
/// @param menu
/// @param x
/// @param y
void rt_contextmenu_show(void *menu, int64_t x, int64_t y)
{
    RT_ASSERT_MAIN_THREAD();
    if (menu)
    {
        vg_contextmenu_show_at((vg_contextmenu_t *)menu, (int)x, (int)y);
    }
}

/// @brief Perform contextmenu hide operation.
/// @param menu
void rt_contextmenu_hide(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (menu)
    {
        vg_contextmenu_dismiss((vg_contextmenu_t *)menu);
    }
}

/// @brief Perform contextmenu is visible operation.
/// @param menu
/// @return Result value.
int64_t rt_contextmenu_is_visible(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return 0;
    return ((vg_contextmenu_t *)menu)->is_visible ? 1 : 0;
}

void *rt_contextmenu_get_clicked_item(void *menu)
{
    RT_ASSERT_MAIN_THREAD();
    if (!menu)
        return NULL;
    vg_contextmenu_t *cm = (vg_contextmenu_t *)menu;
    if (cm->clicked_index >= 0 && cm->clicked_index < (int)cm->item_count)
    {
        vg_menu_item_t *item = cm->items[cm->clicked_index];
        cm->clicked_index = -1; // Edge-triggered: clear after read
        return item;
    }
    return NULL;
}

//=============================================================================
// StatusBar Widget (Phase 3)
//=============================================================================

void *rt_statusbar_new(void *parent)
{
    RT_ASSERT_MAIN_THREAD();
    vg_statusbar_t *sb = vg_statusbar_create((vg_widget_t *)parent);
    if (sb)
    {
        rt_gui_ensure_default_font();
        if (s_current_app && s_current_app->default_font)
            vg_statusbar_set_font(
                sb, s_current_app->default_font, s_current_app->default_font_size);
    }
    return sb;
}

/// @brief Perform statusbar destroy operation.
/// @param bar
void rt_statusbar_destroy(void *bar)
{
    RT_ASSERT_MAIN_THREAD();
    if (bar)
    {
        vg_widget_destroy((vg_widget_t *)bar);
    }
}

// Internal: helper to get first text item in a zone
static vg_statusbar_item_t *get_zone_text_item(vg_statusbar_t *sb, vg_statusbar_zone_t zone)
{
    vg_statusbar_item_t **items = NULL;
    size_t count = 0;
    switch (zone)
    {
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
    for (size_t i = 0; i < count; i++)
    {
        if (items[i] && items[i]->type == VG_STATUSBAR_ITEM_TEXT)
        {
            return items[i];
        }
    }
    return NULL;
}

/// @brief Perform statusbar set left text operation.
/// @param bar
/// @param text
void rt_statusbar_set_left_text(void *bar, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_LEFT);
    if (item)
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_item_set_text(item, ctext);
        free(ctext);
    }
    else
    {
        // Create a new text item
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_LEFT, ctext);
        free(ctext);
    }
}

/// @brief Perform statusbar set center text operation.
/// @param bar
/// @param text
void rt_statusbar_set_center_text(void *bar, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_CENTER);
    if (item)
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_item_set_text(item, ctext);
        free(ctext);
    }
    else
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_CENTER, ctext);
        free(ctext);
    }
}

/// @brief Perform statusbar set right text operation.
/// @param bar
/// @param text
void rt_statusbar_set_right_text(void *bar, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_item_t *item = get_zone_text_item(sb, VG_STATUSBAR_ZONE_RIGHT);
    if (item)
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_item_set_text(item, ctext);
        free(ctext);
    }
    else
    {
        char *ctext = rt_string_to_cstr(text);
        vg_statusbar_add_text(sb, VG_STATUSBAR_ZONE_RIGHT, ctext);
        free(ctext);
    }
}

/// @brief Perform statusbar get left text operation.
/// @param bar
/// @return Result value.
rt_string rt_statusbar_get_left_text(void *bar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_LEFT);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

/// @brief Perform statusbar get center text operation.
/// @param bar
/// @return Result value.
rt_string rt_statusbar_get_center_text(void *bar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_CENTER);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

/// @brief Perform statusbar get right text operation.
/// @param bar
/// @return Result value.
rt_string rt_statusbar_get_right_text(void *bar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_RIGHT);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

void *rt_statusbar_add_text(void *bar, rt_string text, int64_t zone)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_statusbar_item_t *item =
        vg_statusbar_add_text((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone, ctext);
    free(ctext);
    return item;
}

void *rt_statusbar_add_button(void *bar, rt_string text, int64_t zone)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_statusbar_item_t *item = vg_statusbar_add_button(
        (vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone, ctext, NULL, NULL);
    free(ctext);
    return item;
}

void *rt_statusbar_add_progress(void *bar, int64_t zone)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return NULL;
    return vg_statusbar_add_progress((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void *rt_statusbar_add_separator(void *bar, int64_t zone)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return NULL;
    return vg_statusbar_add_separator((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void *rt_statusbar_add_spacer(void *bar, int64_t zone)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return NULL;
    return vg_statusbar_add_spacer((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

/// @brief Perform statusbar remove item operation.
/// @param bar
/// @param item
void rt_statusbar_remove_item(void *bar, void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar || !item)
        return;
    vg_statusbar_remove_item((vg_statusbar_t *)bar, (vg_statusbar_item_t *)item);
}

/// @brief Perform statusbar clear operation.
/// @param bar
void rt_statusbar_clear(void *bar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_LEFT);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_CENTER);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_RIGHT);
}

/// @brief Perform statusbar set visible operation.
/// @param bar
/// @param visible
void rt_statusbar_set_visible(void *bar, int64_t visible)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return;
    vg_widget_set_visible(&((vg_statusbar_t *)bar)->base, visible != 0);
}

/// @brief Perform statusbar is visible operation.
/// @param bar
/// @return Result value.
int64_t rt_statusbar_is_visible(void *bar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!bar)
        return 0;
    return ((vg_statusbar_t *)bar)->base.visible ? 1 : 0;
}

//=============================================================================
// StatusBarItem Widget (Phase 3)
//=============================================================================

/// @brief Perform statusbaritem set text operation.
/// @param item
/// @param text
void rt_statusbaritem_set_text(void *item, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_statusbar_item_set_text((vg_statusbar_item_t *)item, ctext);
    free(ctext);
}

/// @brief Perform statusbaritem get text operation.
/// @param item
/// @return Result value.
rt_string rt_statusbaritem_get_text(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    vg_statusbar_item_t *sbi = (vg_statusbar_item_t *)item;
    if (sbi->text)
    {
        return rt_string_from_bytes(sbi->text, strlen(sbi->text));
    }
    return rt_str_empty();
}

/// @brief Perform statusbaritem set tooltip operation.
/// @param item
/// @param tooltip
void rt_statusbaritem_set_tooltip(void *item, rt_string tooltip)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    char *ctext = rt_string_to_cstr(tooltip);
    vg_statusbar_item_set_tooltip((vg_statusbar_item_t *)item, ctext);
    free(ctext);
}

/// @brief Perform statusbaritem set progress operation.
/// @param item
/// @param value
void rt_statusbaritem_set_progress(void *item, double value)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_statusbar_item_set_progress((vg_statusbar_item_t *)item, (float)value);
}

/// @brief Perform statusbaritem get progress operation.
/// @param item
/// @return Result value.
double rt_statusbaritem_get_progress(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0.0;
    return (double)((vg_statusbar_item_t *)item)->progress;
}

/// @brief Perform statusbaritem set visible operation.
/// @param item
/// @param visible
void rt_statusbaritem_set_visible(void *item, int64_t visible)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_statusbar_item_set_visible((vg_statusbar_item_t *)item, visible != 0);
}

// Track clicked status bar item
static vg_statusbar_item_t *g_clicked_statusbar_item = NULL;

/// @brief Set the clicked statusbar item value.
/// @param item
void rt_gui_set_clicked_statusbar_item(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    g_clicked_statusbar_item = (vg_statusbar_item_t *)item;
}

/// @brief Perform statusbaritem was clicked operation.
/// @param item
/// @return Result value.
int64_t rt_statusbaritem_was_clicked(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    return (g_clicked_statusbar_item == item) ? 1 : 0;
}

//=============================================================================
// Toolbar Widget (Phase 3)
//=============================================================================

void *rt_toolbar_new(void *parent)
{
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = vg_toolbar_create((vg_widget_t *)parent, VG_TOOLBAR_HORIZONTAL);
    if (tb)
    {
        rt_gui_ensure_default_font();
        if (s_current_app && s_current_app->default_font)
            vg_toolbar_set_font(tb, s_current_app->default_font, s_current_app->default_font_size);
    }
    return tb;
}

void *rt_toolbar_new_vertical(void *parent)
{
    RT_ASSERT_MAIN_THREAD();
    vg_toolbar_t *tb = vg_toolbar_create((vg_widget_t *)parent, VG_TOOLBAR_VERTICAL);
    if (tb)
    {
        rt_gui_ensure_default_font();
        if (s_current_app && s_current_app->default_font)
            vg_toolbar_set_font(tb, s_current_app->default_font, s_current_app->default_font_size);
    }
    return tb;
}

/// @brief Perform toolbar destroy operation.
/// @param toolbar
void rt_toolbar_destroy(void *toolbar)
{
    RT_ASSERT_MAIN_THREAD();
    if (toolbar)
    {
        vg_widget_destroy((vg_widget_t *)toolbar);
    }
}

void *rt_toolbar_add_button(void *toolbar, rt_string icon_path, rt_string tooltip)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    // Only set path icon if there's actually a path
    if (cicon && cicon[0] != '\0')
    {
        icon.type = VG_ICON_PATH;
        icon.data.path = cicon; // Ownership transferred to item on success
    }
    else
    {
        icon.type = VG_ICON_NONE;
        free(cicon); // Empty string, free it
        cicon = NULL;
    }

    vg_toolbar_item_t *item =
        vg_toolbar_add_button((vg_toolbar_t *)toolbar, NULL, NULL, icon, NULL, NULL);
    if (item)
    {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }
    else if (cicon)
    {
        // Item creation failed, we still own cicon
        free(cicon);
    }

    free(ctooltip);
    return item;
}

void *rt_toolbar_add_button_with_text(void *toolbar,
                                      rt_string icon_path,
                                      rt_string text,
                                      rt_string tooltip)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctext = rt_string_to_cstr(text);
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    // Only set path icon if there's actually a path
    if (cicon && cicon[0] != '\0')
    {
        icon.type = VG_ICON_PATH;
        icon.data.path = cicon; // Ownership transferred to item on success
    }
    else
    {
        icon.type = VG_ICON_NONE;
        free(cicon); // Empty string, free it
        cicon = NULL;
    }

    vg_toolbar_item_t *item =
        vg_toolbar_add_button((vg_toolbar_t *)toolbar, NULL, ctext, icon, NULL, NULL);
    if (item)
    {
        // Force label visible — tb->show_labels defaults to false, so items
        // created via AddButtonWithText would otherwise never show their label.
        item->show_label = true;
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }
    else if (cicon)
    {
        // Item creation failed, we still own cicon
        free(cicon);
    }

    free(ctext);
    free(ctooltip);
    return item;
}

void *rt_toolbar_add_toggle(void *toolbar, rt_string icon_path, rt_string tooltip)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return NULL;
    char *cicon = rt_string_to_cstr(icon_path);
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    // Only set path icon if there's actually a path
    if (cicon && cicon[0] != '\0')
    {
        icon.type = VG_ICON_PATH;
        icon.data.path = cicon; // Ownership transferred to item on success
    }
    else
    {
        icon.type = VG_ICON_NONE;
        free(cicon); // Empty string, free it
        cicon = NULL;
    }

    vg_toolbar_item_t *item =
        vg_toolbar_add_toggle((vg_toolbar_t *)toolbar, NULL, NULL, icon, false, NULL, NULL);
    if (item)
    {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }
    else if (cicon)
    {
        // Item creation failed, we still own cicon
        free(cicon);
    }

    free(ctooltip);
    return item;
}

void *rt_toolbar_add_separator(void *toolbar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return NULL;
    return vg_toolbar_add_separator((vg_toolbar_t *)toolbar);
}

void *rt_toolbar_add_spacer(void *toolbar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return NULL;
    return vg_toolbar_add_spacer((vg_toolbar_t *)toolbar);
}

void *rt_toolbar_add_dropdown(void *toolbar, rt_string tooltip)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return NULL;
    char *ctooltip = rt_string_to_cstr(tooltip);

    vg_icon_t icon = {0};
    icon.type = VG_ICON_NONE;

    vg_toolbar_item_t *item =
        vg_toolbar_add_dropdown((vg_toolbar_t *)toolbar, NULL, NULL, icon, NULL);
    if (item)
    {
        vg_toolbar_item_set_tooltip(item, ctooltip);
    }

    free(ctooltip);
    return item;
}

/// @brief Perform toolbar remove item operation.
/// @param toolbar
/// @param item
void rt_toolbar_remove_item(void *toolbar, void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar || !item)
        return;
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    if (ti->id)
    {
        vg_toolbar_remove_item((vg_toolbar_t *)toolbar, ti->id);
    }
}

/// @brief Perform toolbar set icon size operation.
/// @param toolbar
/// @param size
void rt_toolbar_set_icon_size(void *toolbar, int64_t size)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return;
    vg_toolbar_set_icon_size((vg_toolbar_t *)toolbar, (vg_toolbar_icon_size_t)size);
}

/// @brief Perform toolbar get icon size operation.
/// @param toolbar
/// @return Result value.
int64_t rt_toolbar_get_icon_size(void *toolbar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return RT_TOOLBAR_ICON_MEDIUM;
    return ((vg_toolbar_t *)toolbar)->icon_size;
}

/// @brief Perform toolbar set style operation.
/// @param toolbar
/// @param style
void rt_toolbar_set_style(void *toolbar, int64_t style)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return;
    vg_toolbar_set_show_labels((vg_toolbar_t *)toolbar, style != RT_TOOLBAR_STYLE_ICON_ONLY);
}

/// @brief Perform toolbar get item count operation.
/// @param toolbar
/// @return Result value.
int64_t rt_toolbar_get_item_count(void *toolbar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return 0;
    return ((vg_toolbar_t *)toolbar)->item_count;
}

void *rt_toolbar_get_item(void *toolbar, int64_t index)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return NULL;
    vg_toolbar_t *tb = (vg_toolbar_t *)toolbar;
    if (index < 0 || index >= (int64_t)tb->item_count)
        return NULL;
    return tb->items[index];
}

/// @brief Perform toolbar set visible operation.
/// @param toolbar
/// @param visible
void rt_toolbar_set_visible(void *toolbar, int64_t visible)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return;
    ((vg_toolbar_t *)toolbar)->base.visible = visible != 0;
}

/// @brief Perform toolbar is visible operation.
/// @param toolbar
/// @return Result value.
int64_t rt_toolbar_is_visible(void *toolbar)
{
    RT_ASSERT_MAIN_THREAD();
    if (!toolbar)
        return 0;
    return ((vg_toolbar_t *)toolbar)->base.visible ? 1 : 0;
}

//=============================================================================
// ToolbarItem Widget (Phase 3)
//=============================================================================

/// @brief Perform toolbaritem set icon operation.
/// @param item
/// @param icon_path
void rt_toolbaritem_set_icon(void *item, rt_string icon_path)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    char *cicon = rt_string_to_cstr(icon_path);
    vg_icon_t icon = {0};
    // Only set path icon if there's actually a path
    if (cicon && cicon[0] != '\0')
    {
        icon.type = VG_ICON_PATH;
        icon.data.path = cicon; // Ownership transferred to vg layer — do NOT free
    }
    else
    {
        icon.type = VG_ICON_NONE;
        free(cicon);
    }
    vg_toolbar_item_set_icon((vg_toolbar_item_t *)item, icon);
}

/// @brief Perform toolbaritem set icon pixels operation.
/// @param item
/// @param pixels
void rt_toolbaritem_set_icon_pixels(void *item, void *pixels)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item || !pixels)
        return;
    // Treat the pixels pointer as an opaque icon handle — store as glyph codepoint.
    // Full pixel-buffer icon support would require width/height parameters.
    vg_icon_t icon = {0};
    icon.type = VG_ICON_GLYPH;
    icon.data.glyph = (uint32_t)(uintptr_t)pixels;
    vg_toolbar_item_set_icon((vg_toolbar_item_t *)item, icon);
}

/// @brief Perform toolbaritem set text operation.
/// @param item
/// @param text
void rt_toolbaritem_set_text(void *item, rt_string text)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    free(ti->label);
    ti->label = rt_string_to_cstr(text);
}

/// @brief Perform toolbaritem set tooltip operation.
/// @param item
/// @param tooltip
void rt_toolbaritem_set_tooltip(void *item, rt_string tooltip)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    char *ctooltip = rt_string_to_cstr(tooltip);
    vg_toolbar_item_set_tooltip((vg_toolbar_item_t *)item, ctooltip);
    free(ctooltip);
}

/// @brief Perform toolbaritem set enabled operation.
/// @param item
/// @param enabled
void rt_toolbaritem_set_enabled(void *item, int64_t enabled)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_toolbar_item_set_enabled((vg_toolbar_item_t *)item, enabled != 0);
}

/// @brief Perform toolbaritem is enabled operation.
/// @param item
/// @return Result value.
int64_t rt_toolbaritem_is_enabled(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    return ((vg_toolbar_item_t *)item)->enabled ? 1 : 0;
}

/// @brief Perform toolbaritem set toggled operation.
/// @param item
/// @param toggled
void rt_toolbaritem_set_toggled(void *item, int64_t toggled)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_toolbar_item_set_checked((vg_toolbar_item_t *)item, toggled != 0);
}

/// @brief Perform toolbaritem is toggled operation.
/// @param item
/// @return Result value.
int64_t rt_toolbaritem_is_toggled(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    return ((vg_toolbar_item_t *)item)->checked ? 1 : 0;
}

// Track clicked toolbar item
static vg_toolbar_item_t *g_clicked_toolbar_item = NULL;

/// @brief Set the clicked toolbar item value.
/// @param item
void rt_gui_set_clicked_toolbar_item(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    g_clicked_toolbar_item = (vg_toolbar_item_t *)item;
}

/// @brief Perform toolbaritem was clicked operation.
/// @param item
/// @return Result value.
int64_t rt_toolbaritem_was_clicked(void *item)
{
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return 0;
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    if (ti->was_clicked)
    {
        ti->was_clicked = false;
        return 1;
    }
    return 0;
}

#else /* !VIPER_ENABLE_GRAPHICS */

void *rt_menubar_new(void *parent)
{
    (void)parent;
    return NULL;
}

/// @brief Perform menubar destroy operation.
/// @param menubar
void rt_menubar_destroy(void *menubar)
{
    (void)menubar;
}

void *rt_menubar_add_menu(void *menubar, rt_string title)
{
    (void)menubar;
    (void)title;
    return NULL;
}

/// @brief Perform menubar remove menu operation.
/// @param menubar
/// @param menu
void rt_menubar_remove_menu(void *menubar, void *menu)
{
    (void)menubar;
    (void)menu;
}

/// @brief Perform menubar get menu count operation.
/// @param menubar
/// @return Result value.
int64_t rt_menubar_get_menu_count(void *menubar)
{
    (void)menubar;
    return 0;
}

void *rt_menubar_get_menu(void *menubar, int64_t index)
{
    (void)menubar;
    (void)index;
    return NULL;
}

/// @brief Perform menubar set visible operation.
/// @param menubar
/// @param visible
void rt_menubar_set_visible(void *menubar, int64_t visible)
{
    (void)menubar;
    (void)visible;
}

/// @brief Perform menubar is visible operation.
/// @param menubar
/// @return Result value.
int64_t rt_menubar_is_visible(void *menubar)
{
    (void)menubar;
    return 0;
}

void *rt_menu_add_item(void *menu, rt_string text)
{
    (void)menu;
    (void)text;
    return NULL;
}

void *rt_menu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut)
{
    (void)menu;
    (void)text;
    (void)shortcut;
    return NULL;
}

void *rt_menu_add_separator(void *menu)
{
    (void)menu;
    return NULL;
}

void *rt_menu_add_submenu(void *menu, rt_string title)
{
    (void)menu;
    (void)title;
    return NULL;
}

/// @brief Perform menu remove item operation.
/// @param menu
/// @param item
void rt_menu_remove_item(void *menu, void *item)
{
    (void)menu;
    (void)item;
}

/// @brief Perform menu clear operation.
/// @param menu
void rt_menu_clear(void *menu)
{
    (void)menu;
}

/// @brief Perform menu set title operation.
/// @param menu
/// @param title
void rt_menu_set_title(void *menu, rt_string title)
{
    (void)menu;
    (void)title;
}

/// @brief Perform menu get title operation.
/// @param menu
/// @return Result value.
rt_string rt_menu_get_title(void *menu)
{
    (void)menu;
    return rt_str_empty();
}

/// @brief Perform menu get item count operation.
/// @param menu
/// @return Result value.
int64_t rt_menu_get_item_count(void *menu)
{
    (void)menu;
    return 0;
}

void *rt_menu_get_item(void *menu, int64_t index)
{
    (void)menu;
    (void)index;
    return NULL;
}

/// @brief Perform menu set enabled operation.
/// @param menu
/// @param enabled
void rt_menu_set_enabled(void *menu, int64_t enabled)
{
    (void)menu;
    (void)enabled;
}

/// @brief Perform menu is enabled operation.
/// @param menu
/// @return Result value.
int64_t rt_menu_is_enabled(void *menu)
{
    (void)menu;
    return 0;
}

/// @brief Perform menuitem set text operation.
/// @param item
/// @param text
void rt_menuitem_set_text(void *item, rt_string text)
{
    (void)item;
    (void)text;
}

/// @brief Perform menuitem get text operation.
/// @param item
/// @return Result value.
rt_string rt_menuitem_get_text(void *item)
{
    (void)item;
    return rt_str_empty();
}

/// @brief Perform menuitem set shortcut operation.
/// @param item
/// @param shortcut
void rt_menuitem_set_shortcut(void *item, rt_string shortcut)
{
    (void)item;
    (void)shortcut;
}

/// @brief Perform menuitem get shortcut operation.
/// @param item
/// @return Result value.
rt_string rt_menuitem_get_shortcut(void *item)
{
    (void)item;
    return rt_str_empty();
}

/// @brief Perform menuitem set icon operation.
/// @param item
/// @param pixels
void rt_menuitem_set_icon(void *item, void *pixels)
{
    (void)item;
    (void)pixels;
}

/// @brief Perform menuitem set checkable operation.
/// @param item
/// @param checkable
void rt_menuitem_set_checkable(void *item, int64_t checkable)
{
    (void)item;
    (void)checkable;
}

/// @brief Perform menuitem is checkable operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_checkable(void *item)
{
    (void)item;
    return 0;
}

/// @brief Perform menuitem set checked operation.
/// @param item
/// @param checked
void rt_menuitem_set_checked(void *item, int64_t checked)
{
    (void)item;
    (void)checked;
}

/// @brief Perform menuitem is checked operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_checked(void *item)
{
    (void)item;
    return 0;
}

/// @brief Perform menuitem set enabled operation.
/// @param item
/// @param enabled
void rt_menuitem_set_enabled(void *item, int64_t enabled)
{
    (void)item;
    (void)enabled;
}

/// @brief Perform menuitem is enabled operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_enabled(void *item)
{
    (void)item;
    return 0;
}

/// @brief Perform menuitem is separator operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_is_separator(void *item)
{
    (void)item;
    return 0;
}

/// @brief Perform menuitem was clicked operation.
/// @param item
/// @return Result value.
int64_t rt_menuitem_was_clicked(void *item)
{
    (void)item;
    return 0;
}

void *rt_contextmenu_new(void)
{
    return NULL;
}

/// @brief Perform contextmenu destroy operation.
/// @param menu
void rt_contextmenu_destroy(void *menu)
{
    (void)menu;
}

void *rt_contextmenu_add_item(void *menu, rt_string text)
{
    (void)menu;
    (void)text;
    return NULL;
}

void *rt_contextmenu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut)
{
    (void)menu;
    (void)text;
    (void)shortcut;
    return NULL;
}

void *rt_contextmenu_add_separator(void *menu)
{
    (void)menu;
    return NULL;
}

void *rt_contextmenu_add_submenu(void *menu, rt_string title)
{
    (void)menu;
    (void)title;
    return NULL;
}

/// @brief Perform contextmenu clear operation.
/// @param menu
void rt_contextmenu_clear(void *menu)
{
    (void)menu;
}

/// @brief Perform contextmenu show operation.
/// @param menu
/// @param x
/// @param y
void rt_contextmenu_show(void *menu, int64_t x, int64_t y)
{
    (void)menu;
    (void)x;
    (void)y;
}

/// @brief Perform contextmenu hide operation.
/// @param menu
void rt_contextmenu_hide(void *menu)
{
    (void)menu;
}

/// @brief Perform contextmenu is visible operation.
/// @param menu
/// @return Result value.
int64_t rt_contextmenu_is_visible(void *menu)
{
    (void)menu;
    return 0;
}

void *rt_contextmenu_get_clicked_item(void *menu)
{
    (void)menu;
    return NULL;
}

void *rt_statusbar_new(void *parent)
{
    (void)parent;
    return NULL;
}

/// @brief Perform statusbar destroy operation.
/// @param bar
void rt_statusbar_destroy(void *bar)
{
    (void)bar;
}

/// @brief Perform statusbar set left text operation.
/// @param bar
/// @param text
void rt_statusbar_set_left_text(void *bar, rt_string text)
{
    (void)bar;
    (void)text;
}

/// @brief Perform statusbar set center text operation.
/// @param bar
/// @param text
void rt_statusbar_set_center_text(void *bar, rt_string text)
{
    (void)bar;
    (void)text;
}

/// @brief Perform statusbar set right text operation.
/// @param bar
/// @param text
void rt_statusbar_set_right_text(void *bar, rt_string text)
{
    (void)bar;
    (void)text;
}

/// @brief Perform statusbar get left text operation.
/// @param bar
/// @return Result value.
rt_string rt_statusbar_get_left_text(void *bar)
{
    (void)bar;
    return rt_str_empty();
}

/// @brief Perform statusbar get center text operation.
/// @param bar
/// @return Result value.
rt_string rt_statusbar_get_center_text(void *bar)
{
    (void)bar;
    return rt_str_empty();
}

/// @brief Perform statusbar get right text operation.
/// @param bar
/// @return Result value.
rt_string rt_statusbar_get_right_text(void *bar)
{
    (void)bar;
    return rt_str_empty();
}

void *rt_statusbar_add_text(void *bar, rt_string text, int64_t zone)
{
    (void)bar;
    (void)text;
    (void)zone;
    return NULL;
}

void *rt_statusbar_add_button(void *bar, rt_string text, int64_t zone)
{
    (void)bar;
    (void)text;
    (void)zone;
    return NULL;
}

void *rt_statusbar_add_progress(void *bar, int64_t zone)
{
    (void)bar;
    (void)zone;
    return NULL;
}

void *rt_statusbar_add_separator(void *bar, int64_t zone)
{
    (void)bar;
    (void)zone;
    return NULL;
}

void *rt_statusbar_add_spacer(void *bar, int64_t zone)
{
    (void)bar;
    (void)zone;
    return NULL;
}

/// @brief Perform statusbar remove item operation.
/// @param bar
/// @param item
void rt_statusbar_remove_item(void *bar, void *item)
{
    (void)bar;
    (void)item;
}

/// @brief Perform statusbar clear operation.
/// @param bar
void rt_statusbar_clear(void *bar)
{
    (void)bar;
}

/// @brief Perform statusbar set visible operation.
/// @param bar
/// @param visible
void rt_statusbar_set_visible(void *bar, int64_t visible)
{
    (void)bar;
    (void)visible;
}

/// @brief Perform statusbar is visible operation.
/// @param bar
/// @return Result value.
int64_t rt_statusbar_is_visible(void *bar)
{
    (void)bar;
    return 0;
}

/// @brief Perform statusbaritem set text operation.
/// @param item
/// @param text
void rt_statusbaritem_set_text(void *item, rt_string text)
{
    (void)item;
    (void)text;
}

/// @brief Perform statusbaritem get text operation.
/// @param item
/// @return Result value.
rt_string rt_statusbaritem_get_text(void *item)
{
    (void)item;
    return rt_str_empty();
}

/// @brief Perform statusbaritem set tooltip operation.
/// @param item
/// @param tooltip
void rt_statusbaritem_set_tooltip(void *item, rt_string tooltip)
{
    (void)item;
    (void)tooltip;
}

/// @brief Perform statusbaritem set progress operation.
/// @param item
/// @param value
void rt_statusbaritem_set_progress(void *item, double value)
{
    (void)item;
    (void)value;
}

/// @brief Perform statusbaritem get progress operation.
/// @param item
/// @return Result value.
double rt_statusbaritem_get_progress(void *item)
{
    (void)item;
    return 0.0;
}

/// @brief Perform statusbaritem set visible operation.
/// @param item
/// @param visible
void rt_statusbaritem_set_visible(void *item, int64_t visible)
{
    (void)item;
    (void)visible;
}

/// @brief Set the clicked statusbar item value.
/// @param item
void rt_gui_set_clicked_statusbar_item(void *item)
{
    (void)item;
}

/// @brief Perform statusbaritem was clicked operation.
/// @param item
/// @return Result value.
int64_t rt_statusbaritem_was_clicked(void *item)
{
    (void)item;
    return 0;
}

void *rt_toolbar_new(void *parent)
{
    (void)parent;
    return NULL;
}

void *rt_toolbar_new_vertical(void *parent)
{
    (void)parent;
    return NULL;
}

/// @brief Perform toolbar destroy operation.
/// @param toolbar
void rt_toolbar_destroy(void *toolbar)
{
    (void)toolbar;
}

void *rt_toolbar_add_button(void *toolbar, rt_string icon_path, rt_string tooltip)
{
    (void)toolbar;
    (void)icon_path;
    (void)tooltip;
    return NULL;
}

void *rt_toolbar_add_button_with_text(void *toolbar,
                                      rt_string icon_path,
                                      rt_string text,
                                      rt_string tooltip)
{
    (void)toolbar;
    (void)icon_path;
    (void)text;
    (void)tooltip;
    return NULL;
}

void *rt_toolbar_add_toggle(void *toolbar, rt_string icon_path, rt_string tooltip)
{
    (void)toolbar;
    (void)icon_path;
    (void)tooltip;
    return NULL;
}

void *rt_toolbar_add_separator(void *toolbar)
{
    (void)toolbar;
    return NULL;
}

void *rt_toolbar_add_spacer(void *toolbar)
{
    (void)toolbar;
    return NULL;
}

void *rt_toolbar_add_dropdown(void *toolbar, rt_string tooltip)
{
    (void)toolbar;
    (void)tooltip;
    return NULL;
}

/// @brief Perform toolbar remove item operation.
/// @param toolbar
/// @param item
void rt_toolbar_remove_item(void *toolbar, void *item)
{
    (void)toolbar;
    (void)item;
}

/// @brief Perform toolbar set icon size operation.
/// @param toolbar
/// @param size
void rt_toolbar_set_icon_size(void *toolbar, int64_t size)
{
    (void)toolbar;
    (void)size;
}

/// @brief Perform toolbar get icon size operation.
/// @param toolbar
/// @return Result value.
int64_t rt_toolbar_get_icon_size(void *toolbar)
{
    (void)toolbar;
    return 0;
}

/// @brief Perform toolbar set style operation.
/// @param toolbar
/// @param style
void rt_toolbar_set_style(void *toolbar, int64_t style)
{
    (void)toolbar;
    (void)style;
}

/// @brief Perform toolbar get item count operation.
/// @param toolbar
/// @return Result value.
int64_t rt_toolbar_get_item_count(void *toolbar)
{
    (void)toolbar;
    return 0;
}

void *rt_toolbar_get_item(void *toolbar, int64_t index)
{
    (void)toolbar;
    (void)index;
    return NULL;
}

/// @brief Perform toolbar set visible operation.
/// @param toolbar
/// @param visible
void rt_toolbar_set_visible(void *toolbar, int64_t visible)
{
    (void)toolbar;
    (void)visible;
}

/// @brief Perform toolbar is visible operation.
/// @param toolbar
/// @return Result value.
int64_t rt_toolbar_is_visible(void *toolbar)
{
    (void)toolbar;
    return 0;
}

/// @brief Perform toolbaritem set icon operation.
/// @param item
/// @param icon_path
void rt_toolbaritem_set_icon(void *item, rt_string icon_path)
{
    (void)item;
    (void)icon_path;
}

/// @brief Perform toolbaritem set icon pixels operation.
/// @param item
/// @param pixels
void rt_toolbaritem_set_icon_pixels(void *item, void *pixels)
{
    (void)item;
    (void)pixels;
}

/// @brief Perform toolbaritem set text operation.
/// @param item
/// @param text
void rt_toolbaritem_set_text(void *item, rt_string text)
{
    (void)item;
    (void)text;
}

/// @brief Perform toolbaritem set tooltip operation.
/// @param item
/// @param tooltip
void rt_toolbaritem_set_tooltip(void *item, rt_string tooltip)
{
    (void)item;
    (void)tooltip;
}

/// @brief Perform toolbaritem set enabled operation.
/// @param item
/// @param enabled
void rt_toolbaritem_set_enabled(void *item, int64_t enabled)
{
    (void)item;
    (void)enabled;
}

/// @brief Perform toolbaritem is enabled operation.
/// @param item
/// @return Result value.
int64_t rt_toolbaritem_is_enabled(void *item)
{
    (void)item;
    return 0;
}

/// @brief Perform toolbaritem set toggled operation.
/// @param item
/// @param toggled
void rt_toolbaritem_set_toggled(void *item, int64_t toggled)
{
    (void)item;
    (void)toggled;
}

/// @brief Perform toolbaritem is toggled operation.
/// @param item
/// @return Result value.
int64_t rt_toolbaritem_is_toggled(void *item)
{
    (void)item;
    return 0;
}

/// @brief Set the clicked toolbar item value.
/// @param item
void rt_gui_set_clicked_toolbar_item(void *item)
{
    (void)item;
}

/// @brief Perform toolbaritem was clicked operation.
/// @param item
/// @return Result value.
int64_t rt_toolbaritem_was_clicked(void *item)
{
    (void)item;
    return 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
