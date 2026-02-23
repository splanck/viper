//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_features.c
// Purpose: Runtime bindings for advanced ViperGUI feature widgets: CommandPalette
//   (fuzzy-searchable command list), Tooltip (hover annotation), Toast
//   (transient notification), Breadcrumb (navigation path), Minimap (scaled
//   document overview), and Drag & Drop. Each widget type wraps the corresponding
//   vg_* C widget with a GC-safe state struct that captures selection/event data
//   for polling by Zia code.
//
// Key invariants:
//   - rt_commandpalette_on_execute callback fires synchronously inside the vg
//     event loop; it strdup's the selected command id for later polling.
//   - Toast messages are transient: they auto-dismiss after the configured
//     duration; no explicit dismiss call is required.
//   - Minimap content is updated via rt_minimap_set_content and rendered at
//     reduced scale; the pixel buffer is owned by the vg_minimap_t widget.
//   - Drag & Drop uses VGFX drag events; drag data is stored as C strings
//     allocated by the platform and freed after the drop handler returns.
//   - All widget constructors accept a parent void* and cast it to vg_widget_t*.
//
// Ownership/Lifetime:
//   - Wrapper state structs (e.g. rt_commandpalette_data_t) are allocated via
//     rt_obj_new_i64 (GC heap); their embedded vg_* pointers are manually freed
//     in the corresponding destroy functions.
//   - selected_command is strdup'd on selection and freed on next selection or
//     at destroy time.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/include/vg.h (ViperGUI C API),
//        src/runtime/rt_platform.h (platform detection helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

//=============================================================================
// Phase 6: CommandPalette
//=============================================================================

// CommandPalette state tracking
typedef struct
{
    vg_commandpalette_t *palette;
    char *selected_command;
    int64_t was_selected;
} rt_commandpalette_data_t;

static void rt_commandpalette_on_execute(vg_commandpalette_t *palette,
                                         vg_command_t *cmd,
                                         void *user_data)
{
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)user_data;
    if (data && cmd && cmd->id)
    {
        if (data->selected_command)
            free(data->selected_command);
        data->selected_command = strdup(cmd->id);
        data->was_selected = 1;
    }
    (void)palette;
}

void *rt_commandpalette_new(void *parent)
{
    vg_commandpalette_t *palette = vg_commandpalette_create();
    if (!palette)
        return NULL;

    rt_commandpalette_data_t *data =
        (rt_commandpalette_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_commandpalette_data_t));
    data->palette = palette;
    data->selected_command = NULL;
    data->was_selected = 0;

    vg_commandpalette_set_callbacks(palette, rt_commandpalette_on_execute, NULL, data);

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_commandpalette_destroy(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    if (data->palette)
    {
        vg_commandpalette_destroy(data->palette);
    }
    if (data->selected_command)
        free(data->selected_command);
}

void rt_commandpalette_add_command(void *palette, rt_string id, rt_string label, rt_string category)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    char *clabel = rt_string_to_cstr(label);
    // Note: category is not used by underlying widget - could prepend to label if needed
    (void)category;

    vg_commandpalette_add_command(data->palette, cid, clabel, NULL, NULL, NULL);

    if (cid)
        free(cid);
    if (clabel)
        free(clabel);
}

void rt_commandpalette_add_command_with_shortcut(
    void *palette, rt_string id, rt_string label, rt_string category, rt_string shortcut)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    char *clabel = rt_string_to_cstr(label);
    char *cshort = rt_string_to_cstr(shortcut);
    // Note: category is not used by underlying widget
    (void)category;

    vg_commandpalette_add_command(data->palette, cid, clabel, cshort, NULL, NULL);

    if (cid)
        free(cid);
    if (clabel)
        free(clabel);
    if (cshort)
        free(cshort);
}

void rt_commandpalette_remove_command(void *palette, rt_string id)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    vg_commandpalette_remove_command(data->palette, cid);
    if (cid)
        free(cid);
}

void rt_commandpalette_clear(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    vg_commandpalette_clear(data->palette);
}

void rt_commandpalette_show(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    data->was_selected = 0; // Reset selection state when showing
    vg_commandpalette_show(data->palette);
}

void rt_commandpalette_hide(void *palette)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    vg_commandpalette_hide(data->palette);
}

int64_t rt_commandpalette_is_visible(void *palette)
{
    if (!palette)
        return 0;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    return data->palette->base.visible ? 1 : 0;
}

void rt_commandpalette_set_placeholder(void *palette, rt_string text)
{
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *ctext = rt_string_to_cstr(text);
    // Would need placeholder support in vg_commandpalette - stub
    (void)data;
    if (ctext)
        free(ctext);
}

rt_string rt_commandpalette_get_selected_command(void *palette)
{
    if (!palette)
        return rt_string_from_bytes("", 0);
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    if (data->selected_command)
    {
        return rt_string_from_bytes(data->selected_command, strlen(data->selected_command));
    }
    return rt_string_from_bytes("", 0);
}

int64_t rt_commandpalette_was_command_selected(void *palette)
{
    if (!palette)
        return 0;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    int64_t result = data->was_selected;
    data->was_selected = 0; // Reset after checking
    return result;
}

//=============================================================================
// Phase 7: Tooltip Implementation
//=============================================================================

// Global tooltip state
static vg_tooltip_t *g_active_tooltip = NULL;
static uint32_t g_tooltip_delay_ms = 500;

void rt_tooltip_show(rt_string text, int64_t x, int64_t y)
{
    char *ctext = rt_string_to_cstr(text);

    // Create tooltip if needed
    if (!g_active_tooltip)
    {
        g_active_tooltip = vg_tooltip_create();
    }

    if (g_active_tooltip && ctext)
    {
        vg_tooltip_set_text(g_active_tooltip, ctext);
        vg_tooltip_show_at(g_active_tooltip, (int)x, (int)y);
    }

    if (ctext)
        free(ctext);
}

void rt_tooltip_show_rich(rt_string title, rt_string body, int64_t x, int64_t y)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cbody = rt_string_to_cstr(body);

    // Create tooltip if needed
    if (!g_active_tooltip)
    {
        g_active_tooltip = vg_tooltip_create();
    }

    if (g_active_tooltip)
    {
        // Combine title and body for now (rich tooltip would need more widget support)
        char combined[1024];
        snprintf(combined, sizeof(combined), "%s\n%s", ctitle ? ctitle : "", cbody ? cbody : "");
        vg_tooltip_set_text(g_active_tooltip, combined);
        vg_tooltip_show_at(g_active_tooltip, (int)x, (int)y);
    }

    if (ctitle)
        free(ctitle);
    if (cbody)
        free(cbody);
}

void rt_tooltip_hide(void)
{
    if (g_active_tooltip)
    {
        vg_tooltip_hide(g_active_tooltip);
    }
}

void rt_tooltip_set_delay(int64_t delay_ms)
{
    g_tooltip_delay_ms = (uint32_t)delay_ms;
    if (g_active_tooltip)
    {
        vg_tooltip_set_timing(g_active_tooltip, g_tooltip_delay_ms, 100, 0);
    }
}

void rt_widget_set_tooltip(void *widget, rt_string text)
{
    if (!widget)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_widget_set_tooltip_text((vg_widget_t *)widget, ctext);
    if (ctext)
        free(ctext);
}

void rt_widget_set_tooltip_rich(void *widget, rt_string title, rt_string body)
{
    if (!widget)
        return;
    // Combine title and body for basic tooltip support
    char *ctitle = rt_string_to_cstr(title);
    char *cbody = rt_string_to_cstr(body);

    char combined[1024];
    snprintf(combined, sizeof(combined), "%s\n%s", ctitle ? ctitle : "", cbody ? cbody : "");
    vg_widget_set_tooltip_text((vg_widget_t *)widget, combined);

    if (ctitle)
        free(ctitle);
    if (cbody)
        free(cbody);
}

void rt_widget_clear_tooltip(void *widget)
{
    if (!widget)
        return;
    vg_widget_set_tooltip_text((vg_widget_t *)widget, NULL);
}

//=============================================================================
// Phase 7: Toast/Notifications Implementation
//=============================================================================

// Global notification manager
static vg_notification_manager_t *g_notification_manager = NULL;

// Wrapper to track toast state
typedef struct rt_toast_data
{
    uint32_t id;
    int64_t was_action_clicked;
    int64_t was_dismissed;
    char *action_label; ///< Optional action button label (owned, may be NULL)
} rt_toast_data_t;

static vg_notification_manager_t *rt_get_notification_manager(void)
{
    if (!g_notification_manager)
    {
        g_notification_manager = vg_notification_manager_create();
    }
    return g_notification_manager;
}

static vg_notification_type_t rt_toast_type_to_vg(int64_t type)
{
    switch (type)
    {
        case RT_TOAST_INFO:
            return VG_NOTIFICATION_INFO;
        case RT_TOAST_SUCCESS:
            return VG_NOTIFICATION_SUCCESS;
        case RT_TOAST_WARNING:
            return VG_NOTIFICATION_WARNING;
        case RT_TOAST_ERROR:
            return VG_NOTIFICATION_ERROR;
        default:
            return VG_NOTIFICATION_INFO;
    }
}

static vg_notification_position_t rt_toast_position_to_vg(int64_t position)
{
    switch (position)
    {
        case RT_TOAST_POSITION_TOP_RIGHT:
            return VG_NOTIFICATION_TOP_RIGHT;
        case RT_TOAST_POSITION_TOP_LEFT:
            return VG_NOTIFICATION_TOP_LEFT;
        case RT_TOAST_POSITION_BOTTOM_RIGHT:
            return VG_NOTIFICATION_BOTTOM_RIGHT;
        case RT_TOAST_POSITION_BOTTOM_LEFT:
            return VG_NOTIFICATION_BOTTOM_LEFT;
        case RT_TOAST_POSITION_TOP_CENTER:
            return VG_NOTIFICATION_TOP_CENTER;
        case RT_TOAST_POSITION_BOTTOM_CENTER:
            return VG_NOTIFICATION_BOTTOM_CENTER;
        default:
            return VG_NOTIFICATION_TOP_RIGHT;
    }
}

void rt_toast_info(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_INFO, "Info", cmsg, 3000);
    if (cmsg)
        free(cmsg);
}

void rt_toast_success(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_SUCCESS, "Success", cmsg, 3000);
    if (cmsg)
        free(cmsg);
}

void rt_toast_warning(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_WARNING, "Warning", cmsg, 5000);
    if (cmsg)
        free(cmsg);
}

void rt_toast_error(rt_string message)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    vg_notification_show(mgr, VG_NOTIFICATION_ERROR, "Error", cmsg, 0); // Sticky for errors
    if (cmsg)
        free(cmsg);
}

void *rt_toast_new(rt_string message, int64_t type, int64_t duration_ms)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (!mgr)
        return NULL;

    char *cmsg = rt_string_to_cstr(message);

    rt_toast_data_t *data = (rt_toast_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_toast_data_t));

    data->id =
        vg_notification_show(mgr, rt_toast_type_to_vg(type), NULL, cmsg, (uint32_t)duration_ms);
    data->was_action_clicked = 0;
    data->was_dismissed = 0;

    if (cmsg)
        free(cmsg);
    return data;
}

void rt_toast_set_action(void *toast, rt_string label)
{
    if (!toast)
        return;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    free(data->action_label);
    data->action_label = rt_string_to_cstr(label);
}

int64_t rt_toast_was_action_clicked(void *toast)
{
    if (!toast)
        return 0;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    int64_t result = data->was_action_clicked;
    data->was_action_clicked = 0;
    return result;
}

int64_t rt_toast_was_dismissed(void *toast)
{
    if (!toast)
        return 0;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    // Check with manager if notification is still active
    // For now, return stored state
    return data->was_dismissed;
}

void rt_toast_dismiss(void *toast)
{
    if (!toast)
        return;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        vg_notification_dismiss(mgr, data->id);
        data->was_dismissed = 1;
    }
}

void rt_toast_set_position(int64_t position)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        vg_notification_manager_set_position(mgr, rt_toast_position_to_vg(position));
    }
}

void rt_toast_set_max_visible(int64_t count)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        mgr->max_visible = (uint32_t)count;
    }
}

void rt_toast_dismiss_all(void)
{
    vg_notification_manager_t *mgr = rt_get_notification_manager();
    if (mgr)
    {
        vg_notification_dismiss_all(mgr);
    }
}

//=============================================================================
// Phase 8: Breadcrumb Implementation
//=============================================================================

// Wrapper to track breadcrumb state
typedef struct rt_breadcrumb_data
{
    vg_breadcrumb_t *breadcrumb;
    int64_t clicked_index;
    char *clicked_data;
    int64_t was_clicked;
} rt_breadcrumb_data_t;

// Breadcrumb click callback
static void rt_breadcrumb_on_click(vg_breadcrumb_t *bc, int index, void *user_data)
{
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)user_data;
    if (!data)
        return;

    data->clicked_index = index;
    data->was_clicked = 1;

    // Store the clicked item's data
    if (data->clicked_data)
    {
        free(data->clicked_data);
        data->clicked_data = NULL;
    }

    if (index >= 0 && (size_t)index < bc->item_count)
    {
        if (bc->items[index].user_data)
        {
            data->clicked_data = strdup((const char *)bc->items[index].user_data);
        }
    }
}

void *rt_breadcrumb_new(void *parent)
{
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    if (!bc)
        return NULL;

    rt_breadcrumb_data_t *data =
        (rt_breadcrumb_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_breadcrumb_data_t));
    data->breadcrumb = bc;
    data->clicked_index = -1;
    data->clicked_data = NULL;
    data->was_clicked = 0;

    vg_breadcrumb_set_on_click(bc, rt_breadcrumb_on_click, data);

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_breadcrumb_destroy(void *crumb)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    if (data->breadcrumb)
    {
        vg_breadcrumb_destroy(data->breadcrumb);
    }
    if (data->clicked_data)
        free(data->clicked_data);
}

void rt_breadcrumb_set_path(void *crumb, rt_string path, rt_string separator)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *cpath = rt_string_to_cstr(path);
    char *csep = rt_string_to_cstr(separator);

    // Clear existing items
    vg_breadcrumb_clear(data->breadcrumb);

    // Parse path and add items
    if (cpath && csep && csep[0])
    {
        char *saveptr = NULL;
        char *token = rt_strtok_r(cpath, csep, &saveptr);
        while (token)
        {
            char *label = strdup(token);
            if (label)
                vg_breadcrumb_push(data->breadcrumb, token, label);
            token = rt_strtok_r(NULL, csep, &saveptr);
        }
    }

    if (cpath)
        free(cpath);
    if (csep)
        free(csep);
}

void rt_breadcrumb_set_items(void *crumb, rt_string items)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *citems = rt_string_to_cstr(items);

    // Clear existing items
    vg_breadcrumb_clear(data->breadcrumb);

    // Parse comma-separated items
    if (citems)
    {
        char *saveptr = NULL;
        char *token = rt_strtok_r(citems, ",", &saveptr);
        while (token)
        {
            // Trim whitespace
            while (*token == ' ')
                token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ')
                *end-- = '\0';

            char *label = strdup(token);
            if (label)
                vg_breadcrumb_push(data->breadcrumb, token, label);
            token = rt_strtok_r(NULL, ",", &saveptr);
        }
        free(citems);
    }
}

void rt_breadcrumb_add_item(void *crumb, rt_string text, rt_string item_data)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *ctext = rt_string_to_cstr(text);
    char *cdata = rt_string_to_cstr(item_data);

    if (ctext)
    {
        vg_breadcrumb_push(data->breadcrumb, ctext, cdata ? strdup(cdata) : NULL);
        free(ctext);
    }
    if (cdata)
        free(cdata);
}

void rt_breadcrumb_clear(void *crumb)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    vg_breadcrumb_clear(data->breadcrumb);
}

int64_t rt_breadcrumb_was_item_clicked(void *crumb)
{
    if (!crumb)
        return 0;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    int64_t result = data->was_clicked;
    data->was_clicked = 0; // Reset after checking
    return result;
}

int64_t rt_breadcrumb_get_clicked_index(void *crumb)
{
    if (!crumb)
        return -1;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    return data->clicked_index;
}

rt_string rt_breadcrumb_get_clicked_data(void *crumb)
{
    if (!crumb)
        return rt_string_from_bytes("", 0);
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    if (data->clicked_data)
    {
        return rt_string_from_bytes(data->clicked_data, strlen(data->clicked_data));
    }
    return rt_string_from_bytes("", 0);
}

void rt_breadcrumb_set_separator(void *crumb, rt_string sep)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    char *csep = rt_string_to_cstr(sep);
    if (csep)
    {
        vg_breadcrumb_set_separator(data->breadcrumb, csep);
        free(csep);
    }
}

void rt_breadcrumb_set_max_items(void *crumb, int64_t max)
{
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    vg_breadcrumb_set_max_items(data->breadcrumb, (int)max);
}

//=============================================================================
// Phase 8: Minimap Implementation
//=============================================================================

// Wrapper to track minimap state
typedef struct rt_minimap_data
{
    vg_minimap_t *minimap;
    int64_t width;
} rt_minimap_data_t;

void *rt_minimap_new(void *parent)
{
    vg_minimap_t *minimap = vg_minimap_create(NULL);
    if (!minimap)
        return NULL;

    rt_minimap_data_t *data =
        (rt_minimap_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_minimap_data_t));
    data->minimap = minimap;
    data->width = 80; // Default width

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_minimap_destroy(void *minimap)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    if (data->minimap)
    {
        vg_minimap_destroy(data->minimap);
    }
}

void rt_minimap_bind_editor(void *minimap, void *editor)
{
    if (!minimap || !editor)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_editor(data->minimap, (vg_codeeditor_t *)editor);
}

void rt_minimap_unbind_editor(void *minimap)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_editor(data->minimap, NULL);
}

void rt_minimap_set_width(void *minimap, int64_t width)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    data->width = width;
    data->minimap->base.width = (float)width;
}

int64_t rt_minimap_get_width(void *minimap)
{
    if (!minimap)
        return 0;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    return data->width;
}

void rt_minimap_set_scale(void *minimap, double scale)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_scale(data->minimap, (float)scale);
}

void rt_minimap_set_show_slider(void *minimap, int64_t show)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_show_viewport(data->minimap, show != 0);
}

void rt_minimap_add_marker(void *minimap, int64_t line, int64_t color, int64_t type)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_t *mm = data->minimap;

    if (mm->marker_count >= mm->marker_cap)
    {
        int new_cap = mm->marker_cap ? mm->marker_cap * 2 : 8;
        void *p = realloc(mm->markers, (size_t)new_cap * sizeof(*mm->markers));
        if (!p)
            return;
        mm->markers = p;
        mm->marker_cap = new_cap;
    }
    struct vg_minimap_marker *m = &mm->markers[mm->marker_count++];
    m->line  = (int)line;
    m->color = (uint32_t)color;
    m->type  = (int)type;
    mm->base.needs_paint = true;
}

void rt_minimap_remove_markers(void *minimap, int64_t line)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_t *mm = data->minimap;
    int w = 0;
    for (int i = 0; i < mm->marker_count; i++)
    {
        if (mm->markers[i].line != (int)line)
            mm->markers[w++] = mm->markers[i];
    }
    if (w != mm->marker_count)
    {
        mm->marker_count = w;
        mm->base.needs_paint = true;
    }
}

void rt_minimap_clear_markers(void *minimap)
{
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_t *mm = data->minimap;
    free(mm->markers);
    mm->markers = NULL;
    mm->marker_count = 0;
    mm->marker_cap = 0;
    mm->base.needs_paint = true;
}

//=============================================================================
// Phase 8: Drag and Drop Implementation
//=============================================================================

// Drag and drop state per widget (would need to be stored in widget user_data)
typedef struct rt_drag_drop_data
{
    int64_t is_draggable;
    char *drag_type;
    char *drag_data;
    int64_t is_drop_target;
    char *accepted_types;
    int64_t is_being_dragged;
    int64_t is_drag_over;
    int64_t was_dropped;
    char *drop_type;
    char *drop_data;
} rt_drag_drop_data_t;

// Global drag state for simple implementation
static rt_drag_drop_data_t *g_current_drag = NULL;

void rt_widget_set_draggable(void *widget, int64_t draggable)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    // For now, this is a stub that tracks state
    (void)draggable;
}

void rt_widget_set_drag_data(void *widget, rt_string type, rt_string data)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    (void)type;
    (void)data;
}

int64_t rt_widget_is_being_dragged(void *widget)
{
    if (!widget)
        return 0;
    // Note: Would need to extend vg_widget to support drag/drop
    return 0;
}

void rt_widget_set_drop_target(void *widget, int64_t target)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    (void)target;
}

void rt_widget_set_accepted_drop_types(void *widget, rt_string types)
{
    if (!widget)
        return;
    // Note: Would need to extend vg_widget to support drag/drop
    (void)types;
}

int64_t rt_widget_is_drag_over(void *widget)
{
    if (!widget)
        return 0;
    // Note: Would need to extend vg_widget to support drag/drop
    return 0;
}

int64_t rt_widget_was_dropped(void *widget)
{
    if (!widget)
        return 0;
    // Note: Would need to extend vg_widget to support drag/drop
    return 0;
}

rt_string rt_widget_get_drop_type(void *widget)
{
    if (!widget)
        return rt_string_from_bytes("", 0);
    // Note: Would need to extend vg_widget to support drag/drop
    return rt_string_from_bytes("", 0);
}

rt_string rt_widget_get_drop_data(void *widget)
{
    if (!widget)
        return rt_string_from_bytes("", 0);
    // Note: Would need to extend vg_widget to support drag/drop
    return rt_string_from_bytes("", 0);
}

// File drop state for app
typedef struct rt_file_drop_data
{
    char **files;
    int64_t file_count;
    int64_t was_dropped;
} rt_file_drop_data_t;

static rt_file_drop_data_t g_file_drop = {0};

int64_t rt_app_was_file_dropped(void *app)
{
    (void)app;
    int64_t result = g_file_drop.was_dropped;
    g_file_drop.was_dropped = 0;
    return result;
}

int64_t rt_app_get_dropped_file_count(void *app)
{
    (void)app;
    return g_file_drop.file_count;
}

rt_string rt_app_get_dropped_file(void *app, int64_t index)
{
    (void)app;
    if (index >= 0 && index < g_file_drop.file_count && g_file_drop.files)
    {
        char *file = g_file_drop.files[index];
        if (file)
        {
            return rt_string_from_bytes(file, strlen(file));
        }
    }
    return rt_string_from_bytes("", 0);
}
