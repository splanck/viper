//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_gui_menus.c
// Purpose: MenuBar, Menu, StatusBar, Toolbar, and ContextMenu widgets.
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"

//=============================================================================
// MenuBar Widget (Phase 2)
//=============================================================================

void *rt_menubar_new(void *parent)
{
    vg_menubar_t *mb = vg_menubar_create((vg_widget_t *)parent);
    if (mb)
    {
        rt_gui_ensure_default_font();
        if (s_current_app && s_current_app->default_font)
            vg_menubar_set_font(mb, s_current_app->default_font, s_current_app->default_font_size);
    }
    return mb;
}

void rt_menubar_destroy(void *menubar)
{
    if (!menubar)
        return;
    vg_widget_destroy((vg_widget_t *)menubar);
}

void *rt_menubar_add_menu(void *menubar, rt_string title)
{
    if (!menubar)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_menu_t *menu = vg_menubar_add_menu((vg_menubar_t *)menubar, ctitle);
    free(ctitle);
    return menu;
}

void rt_menubar_remove_menu(void *menubar, void *menu)
{
    if (!menubar || !menu)
        return;
    vg_menubar_remove_menu((vg_menubar_t *)menubar, (vg_menu_t *)menu);
}

int64_t rt_menubar_get_menu_count(void *menubar)
{
    if (!menubar)
        return 0;
    return ((vg_menubar_t *)menubar)->menu_count;
}

void *rt_menubar_get_menu(void *menubar, int64_t index)
{
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

void rt_menubar_set_visible(void *menubar, int64_t visible)
{
    if (!menubar)
        return;
    vg_widget_set_visible(&((vg_menubar_t *)menubar)->base, visible != 0);
}

int64_t rt_menubar_is_visible(void *menubar)
{
    if (!menubar)
        return 0;
    return ((vg_menubar_t *)menubar)->base.visible ? 1 : 0;
}

//=============================================================================
// Menu Widget (Phase 2)
//=============================================================================

void *rt_menu_add_item(void *menu, rt_string text)
{
    if (!menu)
        return NULL;
    char *ctext = rt_string_to_cstr(text);
    vg_menu_item_t *item = vg_menu_add_item((vg_menu_t *)menu, ctext, NULL, NULL, NULL);
    free(ctext);
    return item;
}

void *rt_menu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut)
{
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
    if (!menu)
        return NULL;
    return vg_menu_add_separator((vg_menu_t *)menu);
}

void *rt_menu_add_submenu(void *menu, rt_string title)
{
    if (!menu)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    vg_menu_t *submenu = vg_menu_add_submenu((vg_menu_t *)menu, ctitle);
    free(ctitle);
    return submenu;
}

void rt_menu_remove_item(void *menu, void *item)
{
    if (!menu || !item)
        return;
    vg_menu_remove_item((vg_menu_t *)menu, (vg_menu_item_t *)item);
}

void rt_menu_clear(void *menu)
{
    if (!menu)
        return;
    vg_menu_clear((vg_menu_t *)menu);
}

void rt_menu_set_title(void *menu, rt_string title)
{
    if (!menu)
        return;
    vg_menu_t *m = (vg_menu_t *)menu;
    free((void *)m->title);
    m->title = rt_string_to_cstr(title);
}

rt_string rt_menu_get_title(void *menu)
{
    if (!menu)
        return rt_str_empty();
    const char *title = ((vg_menu_t *)menu)->title;
    if (!title)
        return rt_str_empty();
    return rt_string_from_bytes(title, strlen(title));
}

int64_t rt_menu_get_item_count(void *menu)
{
    if (!menu)
        return 0;
    return ((vg_menu_t *)menu)->item_count;
}

void *rt_menu_get_item(void *menu, int64_t index)
{
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

void rt_menu_set_enabled(void *menu, int64_t enabled)
{
    if (!menu)
        return;
    // Menu enabled state not currently tracked in vg_menu struct
    // Stub for future implementation
    (void)enabled;
}

int64_t rt_menu_is_enabled(void *menu)
{
    if (!menu)
        return 0;
    // Menu enabled state not currently tracked in vg_menu struct
    return 1; // Default to enabled
}

//=============================================================================
// MenuItem Widget (Phase 2)
//=============================================================================

void rt_menuitem_set_text(void *item, rt_string text)
{
    if (!item)
        return;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    free((void *)mi->text);
    mi->text = rt_string_to_cstr(text);
}

rt_string rt_menuitem_get_text(void *item)
{
    if (!item)
        return rt_str_empty();
    const char *text = ((vg_menu_item_t *)item)->text;
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

void rt_menuitem_set_shortcut(void *item, rt_string shortcut)
{
    if (!item)
        return;
    vg_menu_item_t *mi = (vg_menu_item_t *)item;
    free((void *)mi->shortcut);
    mi->shortcut = rt_string_to_cstr(shortcut);
}

rt_string rt_menuitem_get_shortcut(void *item)
{
    if (!item)
        return rt_str_empty();
    const char *shortcut = ((vg_menu_item_t *)item)->shortcut;
    if (!shortcut)
        return rt_str_empty();
    return rt_string_from_bytes(shortcut, strlen(shortcut));
}

void rt_menuitem_set_icon(void *item, void *pixels)
{
    if (!item)
        return;
    // Icon support would require extending vg_menu_item_t
    (void)pixels;
}

void rt_menuitem_set_checkable(void *item, int64_t checkable)
{
    if (!item)
        return;
    /* Mark the item as a toggle type by setting its checked state.
       When checkable is disabled, also clear the checked state. */
    if (!checkable)
        vg_menu_item_set_checked((vg_menu_item_t *)item, false);
}

int64_t rt_menuitem_is_checkable(void *item)
{
    /* An item is considered checkable if it has ever had a checked state set. */
    (void)item;
    return 1; /* All menu items support the checked field */
}

void rt_menuitem_set_checked(void *item, int64_t checked)
{
    if (!item)
        return;
    ((vg_menu_item_t *)item)->checked = checked != 0;
}

int64_t rt_menuitem_is_checked(void *item)
{
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->checked ? 1 : 0;
}

void rt_menuitem_set_enabled(void *item, int64_t enabled)
{
    if (!item)
        return;
    ((vg_menu_item_t *)item)->enabled = enabled != 0;
}

int64_t rt_menuitem_is_enabled(void *item)
{
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->enabled ? 1 : 0;
}

int64_t rt_menuitem_is_separator(void *item)
{
    if (!item)
        return 0;
    return ((vg_menu_item_t *)item)->separator ? 1 : 0;
}

// Track clicked menu items per frame
static vg_menu_item_t *g_clicked_menuitem = NULL;

void rt_gui_set_clicked_menuitem(void *item)
{
    g_clicked_menuitem = (vg_menu_item_t *)item;
}

int64_t rt_menuitem_was_clicked(void *item)
{
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
    return vg_contextmenu_create();
}

void rt_contextmenu_destroy(void *menu)
{
    if (menu)
    {
        vg_contextmenu_destroy((vg_contextmenu_t *)menu);
    }
}

void *rt_contextmenu_add_item(void *menu, rt_string text)
{
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
    if (!menu)
        return NULL;
    vg_contextmenu_add_separator((vg_contextmenu_t *)menu);
    return NULL; // vg_contextmenu_add_separator returns void
}

void *rt_contextmenu_add_submenu(void *menu, rt_string title)
{
    if (!menu)
        return NULL;
    char *ctitle = rt_string_to_cstr(title);
    // Context menu submenu support would need vg_contextmenu_add_submenu
    // For now return NULL as placeholder
    free(ctitle);
    return NULL;
}

void rt_contextmenu_clear(void *menu)
{
    if (menu)
    {
        vg_contextmenu_clear((vg_contextmenu_t *)menu);
    }
}

void rt_contextmenu_show(void *menu, int64_t x, int64_t y)
{
    if (menu)
    {
        vg_contextmenu_show_at((vg_contextmenu_t *)menu, (int)x, (int)y);
    }
}

void rt_contextmenu_hide(void *menu)
{
    if (menu)
    {
        vg_contextmenu_dismiss((vg_contextmenu_t *)menu);
    }
}

int64_t rt_contextmenu_is_visible(void *menu)
{
    if (!menu)
        return 0;
    return ((vg_contextmenu_t *)menu)->is_visible ? 1 : 0;
}

void *rt_contextmenu_get_clicked_item(void *menu)
{
    if (!menu)
        return NULL;
    vg_contextmenu_t *cm = (vg_contextmenu_t *)menu;
    if (cm->hovered_index >= 0 && cm->hovered_index < (int)cm->item_count)
    {
        return cm->items[cm->hovered_index];
    }
    return NULL;
}

//=============================================================================
// StatusBar Widget (Phase 3)
//=============================================================================

void *rt_statusbar_new(void *parent)
{
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

void rt_statusbar_destroy(void *bar)
{
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

void rt_statusbar_set_left_text(void *bar, rt_string text)
{
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

void rt_statusbar_set_center_text(void *bar, rt_string text)
{
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

void rt_statusbar_set_right_text(void *bar, rt_string text)
{
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

rt_string rt_statusbar_get_left_text(void *bar)
{
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_LEFT);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

rt_string rt_statusbar_get_center_text(void *bar)
{
    if (!bar)
        return rt_str_empty();
    vg_statusbar_item_t *item = get_zone_text_item((vg_statusbar_t *)bar, VG_STATUSBAR_ZONE_CENTER);
    if (item && item->text)
    {
        return rt_string_from_bytes(item->text, strlen(item->text));
    }
    return rt_str_empty();
}

rt_string rt_statusbar_get_right_text(void *bar)
{
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
    if (!bar)
        return NULL;
    return vg_statusbar_add_progress((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void *rt_statusbar_add_separator(void *bar, int64_t zone)
{
    if (!bar)
        return NULL;
    return vg_statusbar_add_separator((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void *rt_statusbar_add_spacer(void *bar, int64_t zone)
{
    if (!bar)
        return NULL;
    return vg_statusbar_add_spacer((vg_statusbar_t *)bar, (vg_statusbar_zone_t)zone);
}

void rt_statusbar_remove_item(void *bar, void *item)
{
    if (!bar || !item)
        return;
    vg_statusbar_remove_item((vg_statusbar_t *)bar, (vg_statusbar_item_t *)item);
}

void rt_statusbar_clear(void *bar)
{
    if (!bar)
        return;
    vg_statusbar_t *sb = (vg_statusbar_t *)bar;
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_LEFT);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_CENTER);
    vg_statusbar_clear_zone(sb, VG_STATUSBAR_ZONE_RIGHT);
}

void rt_statusbar_set_visible(void *bar, int64_t visible)
{
    if (!bar)
        return;
    vg_widget_set_visible(&((vg_statusbar_t *)bar)->base, visible != 0);
}

int64_t rt_statusbar_is_visible(void *bar)
{
    if (!bar)
        return 0;
    return ((vg_statusbar_t *)bar)->base.visible ? 1 : 0;
}

//=============================================================================
// StatusBarItem Widget (Phase 3)
//=============================================================================

void rt_statusbaritem_set_text(void *item, rt_string text)
{
    if (!item)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_statusbar_item_set_text((vg_statusbar_item_t *)item, ctext);
    free(ctext);
}

rt_string rt_statusbaritem_get_text(void *item)
{
    if (!item)
        return rt_str_empty();
    vg_statusbar_item_t *sbi = (vg_statusbar_item_t *)item;
    if (sbi->text)
    {
        return rt_string_from_bytes(sbi->text, strlen(sbi->text));
    }
    return rt_str_empty();
}

void rt_statusbaritem_set_tooltip(void *item, rt_string tooltip)
{
    if (!item)
        return;
    char *ctext = rt_string_to_cstr(tooltip);
    vg_statusbar_item_set_tooltip((vg_statusbar_item_t *)item, ctext);
    free(ctext);
}

void rt_statusbaritem_set_progress(void *item, double value)
{
    if (!item)
        return;
    vg_statusbar_item_set_progress((vg_statusbar_item_t *)item, (float)value);
}

double rt_statusbaritem_get_progress(void *item)
{
    if (!item)
        return 0.0;
    return (double)((vg_statusbar_item_t *)item)->progress;
}

void rt_statusbaritem_set_visible(void *item, int64_t visible)
{
    if (!item)
        return;
    vg_statusbar_item_set_visible((vg_statusbar_item_t *)item, visible != 0);
}

// Track clicked status bar item
static vg_statusbar_item_t *g_clicked_statusbar_item = NULL;

void rt_gui_set_clicked_statusbar_item(void *item)
{
    g_clicked_statusbar_item = (vg_statusbar_item_t *)item;
}

int64_t rt_statusbaritem_was_clicked(void *item)
{
    if (!item)
        return 0;
    return (g_clicked_statusbar_item == item) ? 1 : 0;
}

//=============================================================================
// Toolbar Widget (Phase 3)
//=============================================================================

void *rt_toolbar_new(void *parent)
{
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
    return vg_toolbar_create((vg_widget_t *)parent, VG_TOOLBAR_VERTICAL);
}

void rt_toolbar_destroy(void *toolbar)
{
    if (toolbar)
    {
        vg_widget_destroy((vg_widget_t *)toolbar);
    }
}

void *rt_toolbar_add_button(void *toolbar, rt_string icon_path, rt_string tooltip)
{
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
    if (!toolbar)
        return NULL;
    return vg_toolbar_add_separator((vg_toolbar_t *)toolbar);
}

void *rt_toolbar_add_spacer(void *toolbar)
{
    if (!toolbar)
        return NULL;
    return vg_toolbar_add_spacer((vg_toolbar_t *)toolbar);
}

void *rt_toolbar_add_dropdown(void *toolbar, rt_string tooltip)
{
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

void rt_toolbar_remove_item(void *toolbar, void *item)
{
    if (!toolbar || !item)
        return;
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    if (ti->id)
    {
        vg_toolbar_remove_item((vg_toolbar_t *)toolbar, ti->id);
    }
}

void rt_toolbar_set_icon_size(void *toolbar, int64_t size)
{
    if (!toolbar)
        return;
    vg_toolbar_set_icon_size((vg_toolbar_t *)toolbar, (vg_toolbar_icon_size_t)size);
}

int64_t rt_toolbar_get_icon_size(void *toolbar)
{
    if (!toolbar)
        return RT_TOOLBAR_ICON_MEDIUM;
    return ((vg_toolbar_t *)toolbar)->icon_size;
}

void rt_toolbar_set_style(void *toolbar, int64_t style)
{
    if (!toolbar)
        return;
    vg_toolbar_set_show_labels((vg_toolbar_t *)toolbar, style != RT_TOOLBAR_STYLE_ICON_ONLY);
}

int64_t rt_toolbar_get_item_count(void *toolbar)
{
    if (!toolbar)
        return 0;
    return ((vg_toolbar_t *)toolbar)->item_count;
}

void *rt_toolbar_get_item(void *toolbar, int64_t index)
{
    if (!toolbar)
        return NULL;
    vg_toolbar_t *tb = (vg_toolbar_t *)toolbar;
    if (index < 0 || index >= (int64_t)tb->item_count)
        return NULL;
    return tb->items[index];
}

void rt_toolbar_set_visible(void *toolbar, int64_t visible)
{
    if (!toolbar)
        return;
    ((vg_toolbar_t *)toolbar)->base.visible = visible != 0;
}

int64_t rt_toolbar_is_visible(void *toolbar)
{
    if (!toolbar)
        return 0;
    return ((vg_toolbar_t *)toolbar)->base.visible ? 1 : 0;
}

//=============================================================================
// ToolbarItem Widget (Phase 3)
//=============================================================================

void rt_toolbaritem_set_icon(void *item, rt_string icon_path)
{
    if (!item)
        return;
    char *cicon = rt_string_to_cstr(icon_path);
    vg_icon_t icon = {0};
    icon.type = VG_ICON_PATH;
    icon.data.path = cicon;
    vg_toolbar_item_set_icon((vg_toolbar_item_t *)item, icon);
    free(cicon);
}

void rt_toolbaritem_set_icon_pixels(void *item, void *pixels)
{
    if (!item || !pixels)
        return;
    // Would need to convert pixels to vg_icon_t
    // Stub for now
    (void)pixels;
}

void rt_toolbaritem_set_text(void *item, rt_string text)
{
    if (!item)
        return;
    vg_toolbar_item_t *ti = (vg_toolbar_item_t *)item;
    free(ti->label);
    ti->label = rt_string_to_cstr(text);
}

void rt_toolbaritem_set_tooltip(void *item, rt_string tooltip)
{
    if (!item)
        return;
    char *ctooltip = rt_string_to_cstr(tooltip);
    vg_toolbar_item_set_tooltip((vg_toolbar_item_t *)item, ctooltip);
    free(ctooltip);
}

void rt_toolbaritem_set_enabled(void *item, int64_t enabled)
{
    if (!item)
        return;
    vg_toolbar_item_set_enabled((vg_toolbar_item_t *)item, enabled != 0);
}

int64_t rt_toolbaritem_is_enabled(void *item)
{
    if (!item)
        return 0;
    return ((vg_toolbar_item_t *)item)->enabled ? 1 : 0;
}

void rt_toolbaritem_set_toggled(void *item, int64_t toggled)
{
    if (!item)
        return;
    vg_toolbar_item_set_checked((vg_toolbar_item_t *)item, toggled != 0);
}

int64_t rt_toolbaritem_is_toggled(void *item)
{
    if (!item)
        return 0;
    return ((vg_toolbar_item_t *)item)->checked ? 1 : 0;
}

// Track clicked toolbar item
static vg_toolbar_item_t *g_clicked_toolbar_item = NULL;

void rt_gui_set_clicked_toolbar_item(void *item)
{
    g_clicked_toolbar_item = (vg_toolbar_item_t *)item;
}

int64_t rt_toolbaritem_was_clicked(void *item)
{
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
