//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_app_font.c
// Purpose: GUI font-lifetime management — retiring fonts until app teardown,
//          checking font usage across the widget tree and other apps, and
//          applying/inheriting/reapplying fonts down a widget subtree. Split
//          out of rt_gui_app.c.
//
// Key invariants:
//   - Graphics-only (VIPER_ENABLE_GRAPHICS); these helpers have no stub forms
//     because they are only reached from the graphics-enabled app paths.
//   - A font is only destroyed once no live app or widget still references it
//     (checked via the shared app registry).
//
// Ownership/Lifetime:
//   - Retired fonts are owned by the app and freed at app destroy.
//
// Links: src/runtime/graphics/gui/rt_gui_app.c (app core + registry),
//        src/runtime/graphics/gui/rt_gui_app_internal.h (shared registry/decls)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_app_internal.h"
#include "rt_gui_internal.h"
#include "rt_platform.h"

#include "fonts/embedded_font.h"

#ifdef VIPER_ENABLE_GRAPHICS

static int rt_gui_widget_tree_uses_font(vg_widget_t *widget, vg_font_t *font);

/// @brief Return non-zero if a context-menu tree references @p font.
/// @details Context menus are standalone overlays, not children of app->root, so
///          the generic widget-tree scan cannot discover their font references.
///          This helper walks the root popup and any submenu popups owned by
///          menu items.
/// @param menu Context menu root to inspect; may be NULL.
/// @param font Font handle to search for; may be NULL.
/// @return 1 when any menu in the tree uses @p font, otherwise 0.
static int rt_gui_contextmenu_tree_uses_font(vg_contextmenu_t *menu, vg_font_t *font) {
    if (!menu || !font)
        return 0;
    if (menu->font == font)
        return 1;
    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        if (item && item->submenu &&
            rt_gui_contextmenu_tree_uses_font((vg_contextmenu_t *)item->submenu, font))
            return 1;
    }
    return 0;
}

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
        if (app->dialog_stack[i] && rt_gui_widget_tree_uses_font(&app->dialog_stack[i]->base, font))
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
    if (rt_gui_contextmenu_tree_uses_font(app->active_context_menu, font))
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
int rt_gui_retire_font_in_other_apps(rt_gui_app_t *skip, vg_font_t *font) {
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
void rt_gui_apply_font_to_widget(vg_widget_t *widget, vg_font_t *font, float size) {
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
            // A code editor needs a monospace font: char_width forms a fixed grid
            // that must match the rendered glyph advances. Once a caller has set the
            // editor's font explicitly, do not overwrite it with the app-wide
            // (typically proportional) chrome font.
            if (!((vg_codeeditor_t *)widget)->font_pinned)
                vg_codeeditor_set_font((vg_codeeditor_t *)widget, font, size);
            break;
        case VG_WIDGET_OUTPUTPANE:
            vg_outputpane_set_font((vg_outputpane_t *)widget, font, size);
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
        case VG_WIDGET_OUTPUTPANE:
            return ((vg_outputpane_t *)widget)->font == font;
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
            button->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 13.0f);
            break;
        }
        case VG_WIDGET_TEXTINPUT: {
            vg_textinput_t *input = (vg_textinput_t *)widget;
            input->font = font;
            input->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_CHECKBOX: {
            vg_checkbox_t *checkbox = (vg_checkbox_t *)widget;
            checkbox->font = font;
            checkbox->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_LISTBOX: {
            vg_listbox_t *listbox = (vg_listbox_t *)widget;
            listbox->font = font;
            listbox->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
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
            dropdown->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_SLIDER: {
            vg_slider_t *slider = (vg_slider_t *)widget;
            slider->font = font;
            slider->font_size = size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
            break;
        }
        case VG_WIDGET_PROGRESS: {
            vg_progressbar_t *progress = (vg_progressbar_t *)widget;
            progress->font = font;
            progress->font_size = size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
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
            tree->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
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
            tabbar->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_MENUBAR: {
            vg_menubar_t *menubar = (vg_menubar_t *)widget;
            menubar->font = font;
            menubar->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            break;
        }
        case VG_WIDGET_TOOLBAR: {
            vg_toolbar_t *toolbar = (vg_toolbar_t *)widget;
            toolbar->font = font;
            toolbar->font_size = size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
            break;
        }
        case VG_WIDGET_STATUSBAR: {
            vg_statusbar_t *statusbar = (vg_statusbar_t *)widget;
            statusbar->font = font;
            statusbar->font_size = size > 0 ? size : (theme ? theme->typography.size_small : 12.0f);
            break;
        }
        case VG_WIDGET_DIALOG: {
            vg_dialog_t *dialog = (vg_dialog_t *)widget;
            dialog->font = font;
            dialog->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            dialog->title_font_size = dialog->font_size + scale;
            break;
        }
        case VG_WIDGET_CODEEDITOR: {
            vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;
            // Skip editors whose font was pinned by an explicit SetFont: the
            // monospace grid (char_width) must stay matched to the rendered font.
            if (!editor->font_pinned) {
                editor->font = font;
                editor->font_size =
                    size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
                if (editor->char_width <= 0.0f)
                    editor->char_width = editor->font_size * 0.6f;
                if (editor->line_height <= 0.0f)
                    editor->line_height = editor->font_size * 1.35f;
            }
            break;
        }
        case VG_WIDGET_OUTPUTPANE: {
            vg_outputpane_t *pane = (vg_outputpane_t *)widget;
            pane->font = font;
            pane->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
            if (pane->line_height <= 0.0f)
                pane->line_height = pane->font_size * 1.35f;
            break;
        }
        case VG_WIDGET_RADIO: {
            vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
            radio->font = font;
            radio->font_size = size > 0 ? size : (theme ? theme->typography.size_normal : 14.0f);
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
    if (app->active_context_menu) {
        if (rt_gui_font_handle_supports_metrics(app->default_font))
            vg_contextmenu_set_font(
                app->active_context_menu, app->default_font, app->default_font_size);
    }
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i])
            vg_dialog_set_font(app->dialog_stack[i], app->default_font, app->default_font_size);
    }
}

#endif // VIPER_ENABLE_GRAPHICS
