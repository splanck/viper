//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_gui_codeeditor.c
// Purpose: CodeEditor enhancements, MessageBox, FileDialog, and FindBar.
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"

//=============================================================================
// CodeEditor Enhancements - Syntax Highlighting (Phase 4)
//=============================================================================

void rt_codeeditor_set_language(void *editor, rt_string language)
{
    if (!editor)
        return;
    char *clang = rt_string_to_cstr(language);
    // Store language in editor state (would need vg_codeeditor_set_language)
    // Stub for now - the actual implementation would set up syntax rules
    free(clang);
}

void rt_codeeditor_set_token_color(void *editor, int64_t token_type, int64_t color)
{
    if (!editor)
        return;
    // Would store token colors in editor state
    // Stub for now
    (void)token_type;
    (void)color;
}

void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords)
{
    if (!editor)
        return;
    char *ckw = rt_string_to_cstr(keywords);
    // Would parse and store custom keywords
    // Stub for now
    free(ckw);
}

void rt_codeeditor_clear_highlights(void *editor)
{
    if (!editor)
        return;
    // Would clear all syntax highlight spans
    // Stub for now
}

void rt_codeeditor_add_highlight(void *editor,
                                 int64_t start_line,
                                 int64_t start_col,
                                 int64_t end_line,
                                 int64_t end_col,
                                 int64_t token_type)
{
    if (!editor)
        return;
    // Would add a highlight span to the editor
    // Stub for now
    (void)start_line;
    (void)start_col;
    (void)end_line;
    (void)end_col;
    (void)token_type;
}

void rt_codeeditor_refresh_highlights(void *editor)
{
    if (!editor)
        return;
    // Would trigger a re-render with updated highlights
    // Stub for now
}

//=============================================================================
// CodeEditor Enhancements - Gutter & Line Numbers (Phase 4)
//=============================================================================

void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->show_line_numbers = show != 0;
}

int64_t rt_codeeditor_get_show_line_numbers(void *editor)
{
    if (!editor)
        return 1; // Default to showing
    return ((vg_codeeditor_t *)editor)->show_line_numbers ? 1 : 0;
}

void rt_codeeditor_set_line_number_width(void *editor, int64_t width)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->gutter_width = (int)width * 8; // Approximate char width
}

void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot)
{
    if (!editor)
        return;
    // Would store gutter icon for the line/slot
    // Stub for now
    (void)line;
    (void)pixels;
    (void)slot;
}

void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot)
{
    if (!editor)
        return;
    // Would clear gutter icon for the line/slot
    // Stub for now
    (void)line;
    (void)slot;
}

void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot)
{
    if (!editor)
        return;
    // Would clear all gutter icons for the slot
    // Stub for now
    (void)slot;
}

// Gutter click tracking
static int g_gutter_clicked = 0;
static int64_t g_gutter_clicked_line = -1;
static int64_t g_gutter_clicked_slot = -1;

void rt_gui_set_gutter_click(int64_t line, int64_t slot)
{
    g_gutter_clicked = 1;
    g_gutter_clicked_line = line;
    g_gutter_clicked_slot = slot;
}

void rt_gui_clear_gutter_click(void)
{
    g_gutter_clicked = 0;
    g_gutter_clicked_line = -1;
    g_gutter_clicked_slot = -1;
}

int64_t rt_codeeditor_was_gutter_clicked(void *editor)
{
    if (!editor)
        return 0;
    return g_gutter_clicked ? 1 : 0;
}

int64_t rt_codeeditor_get_gutter_clicked_line(void *editor)
{
    if (!editor)
        return -1;
    return g_gutter_clicked_line;
}

int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor)
{
    if (!editor)
        return -1;
    return g_gutter_clicked_slot;
}

void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show)
{
    if (!editor)
        return;
    // Would enable/disable fold gutter column
    // Stub for now
    (void)show;
}

//=============================================================================
// CodeEditor Enhancements - Code Folding (Phase 4)
//=============================================================================

void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line)
{
    if (!editor)
        return;
    // Would add a foldable region
    // Stub for now
    (void)start_line;
    (void)end_line;
}

void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line)
{
    if (!editor)
        return;
    // Would remove a foldable region
    // Stub for now
    (void)start_line;
}

void rt_codeeditor_clear_fold_regions(void *editor)
{
    if (!editor)
        return;
    // Would clear all fold regions
    // Stub for now
}

void rt_codeeditor_fold(void *editor, int64_t line)
{
    if (!editor)
        return;
    // Would fold the region at line
    // Stub for now
    (void)line;
}

void rt_codeeditor_unfold(void *editor, int64_t line)
{
    if (!editor)
        return;
    // Would unfold the region at line
    // Stub for now
    (void)line;
}

void rt_codeeditor_toggle_fold(void *editor, int64_t line)
{
    if (!editor)
        return;
    // Would toggle fold state at line
    // Stub for now
    (void)line;
}

int64_t rt_codeeditor_is_folded(void *editor, int64_t line)
{
    if (!editor)
        return 0;
    // Would check if line is in a folded region
    // Stub for now
    (void)line;
    return 0;
}

void rt_codeeditor_fold_all(void *editor)
{
    if (!editor)
        return;
    // Would fold all regions
    // Stub for now
}

void rt_codeeditor_unfold_all(void *editor)
{
    if (!editor)
        return;
    // Would unfold all regions
    // Stub for now
}

void rt_codeeditor_set_auto_fold_detection(void *editor, int64_t enable)
{
    if (!editor)
        return;
    // Would enable/disable automatic fold detection
    // Stub for now
    (void)enable;
}

//=============================================================================
// CodeEditor Enhancements - Multiple Cursors (Phase 4)
//=============================================================================

int64_t rt_codeeditor_get_cursor_count(void *editor)
{
    if (!editor)
        return 1;
    // Currently only support single cursor, return 1
    return 1;
}

void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col)
{
    if (!editor)
        return;
    // Would add additional cursor
    // Stub for now - single cursor only
    (void)line;
    (void)col;
}

void rt_codeeditor_remove_cursor(void *editor, int64_t index)
{
    if (!editor)
        return;
    // Would remove cursor at index (except primary)
    // Stub for now
    (void)index;
}

void rt_codeeditor_clear_extra_cursors(void *editor)
{
    if (!editor)
        return;
    // Would clear all cursors except primary
    // Stub for now - only single cursor supported
}

int64_t rt_codeeditor_get_cursor_line_at(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    return ((vg_codeeditor_t *)editor)->cursor_line;
}

int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    return ((vg_codeeditor_t *)editor)->cursor_col;
}

int64_t rt_codeeditor_get_cursor_line(void *editor)
{
    return rt_codeeditor_get_cursor_line_at(editor, 0);
}

int64_t rt_codeeditor_get_cursor_col(void *editor)
{
    return rt_codeeditor_get_cursor_col_at(editor, 0);
}

void rt_codeeditor_set_cursor_position_at(void *editor, int64_t index, int64_t line, int64_t col)
{
    if (!editor)
        return;
    if (index != 0)
        return; // Only primary cursor supported
    vg_codeeditor_set_cursor((vg_codeeditor_t *)editor, (int)line, (int)col);
}

void rt_codeeditor_set_cursor_selection(void *editor,
                                        int64_t index,
                                        int64_t start_line,
                                        int64_t start_col,
                                        int64_t end_line,
                                        int64_t end_col)
{
    if (!editor)
        return;
    if (index != 0)
        return; // Only primary cursor supported
    // Would set selection for cursor
    // Stub for now
    (void)start_line;
    (void)start_col;
    (void)end_line;
    (void)end_col;
}

int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return ce->has_selection ? 1 : 0;
}

void rt_codeeditor_undo(void *editor)
{
    if (editor)
        vg_codeeditor_undo((vg_codeeditor_t *)editor);
}

void rt_codeeditor_redo(void *editor)
{
    if (editor)
        vg_codeeditor_redo((vg_codeeditor_t *)editor);
}

int64_t rt_codeeditor_copy(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_copy((vg_codeeditor_t *)editor) ? 1 : 0;
}

int64_t rt_codeeditor_cut(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_cut((vg_codeeditor_t *)editor) ? 1 : 0;
}

int64_t rt_codeeditor_paste(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_paste((vg_codeeditor_t *)editor) ? 1 : 0;
}

void rt_codeeditor_select_all(void *editor)
{
    if (editor)
        vg_codeeditor_select_all((vg_codeeditor_t *)editor);
}

//=============================================================================
// Phase 5: MessageBox Dialog
//=============================================================================

int64_t rt_messagebox_info(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_INFO, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    // In a real implementation, we'd need to run a modal loop
    // For now, just return 0 (OK button)
    return 0;
}

int64_t rt_messagebox_warning(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_WARNING, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    return 0;
}

int64_t rt_messagebox_error(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_ERROR, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    return 0;
}

int64_t rt_messagebox_question(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_QUESTION, VG_DIALOG_BUTTONS_YES_NO);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    // Return 1 for Yes, 0 for No - would need modal loop for real result
    return 1;
}

int64_t rt_messagebox_confirm(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_QUESTION, VG_DIALOG_BUTTONS_OK_CANCEL);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    vg_dialog_show(dlg);
    // Return 1 for OK, 0 for Cancel - would need modal loop for real result
    return 1;
}

// Custom MessageBox structure for tracking state
typedef struct
{
    vg_dialog_t *dialog;
    int64_t result;
    int64_t default_button;
} rt_messagebox_data_t;

void *rt_messagebox_new(rt_string title, rt_string message, int64_t type)
{
    char *ctitle = rt_string_to_cstr(title);
    vg_dialog_t *dlg = vg_dialog_create(ctitle);
    if (ctitle)
        free(ctitle);
    if (!dlg)
        return NULL;

    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_set_message(dlg, cmsg);
    if (cmsg)
        free(cmsg);

    vg_dialog_icon_t icon = VG_DIALOG_ICON_INFO;
    switch (type)
    {
        case RT_MESSAGEBOX_INFO:
            icon = VG_DIALOG_ICON_INFO;
            break;
        case RT_MESSAGEBOX_WARNING:
            icon = VG_DIALOG_ICON_WARNING;
            break;
        case RT_MESSAGEBOX_ERROR:
            icon = VG_DIALOG_ICON_ERROR;
            break;
        case RT_MESSAGEBOX_QUESTION:
            icon = VG_DIALOG_ICON_QUESTION;
            break;
    }
    vg_dialog_set_icon(dlg, icon);
    vg_dialog_set_buttons(dlg, VG_DIALOG_BUTTONS_NONE);

    rt_messagebox_data_t *data = (rt_messagebox_data_t *)malloc(sizeof(rt_messagebox_data_t));
    if (!data)
        return NULL;
    data->dialog = dlg;
    data->result = -1;
    data->default_button = 0;

    return data;
}

void rt_messagebox_add_button(void *box, rt_string text, int64_t id)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    // In a full implementation, we'd track custom buttons
    // For now, stub - the dialog system uses presets
    (void)data;
    (void)text;
    (void)id;
}

void rt_messagebox_set_default_button(void *box, int64_t id)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    data->default_button = id;
}

int64_t rt_messagebox_show(void *box)
{
    if (!box)
        return -1;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    vg_dialog_show(data->dialog);
    // Would need modal loop to get actual result
    return data->default_button;
}

void rt_messagebox_destroy(void *box)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    if (data->dialog)
    {
        vg_widget_destroy((vg_widget_t *)data->dialog);
    }
    free(data);
}

//=============================================================================
// Phase 5: FileDialog
//=============================================================================

rt_string rt_filedialog_open(rt_string title, rt_string filter, rt_string default_path)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cfilter = rt_string_to_cstr(filter);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_open_file(ctitle, cpath, "Files", cfilter);
#else
    // vg_filedialog_open_file expects: title, path, filter_name, filter_pattern
    char *result = vg_filedialog_open_file(ctitle, cpath, "Files", cfilter);
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);
    char *cfilter = rt_string_to_cstr(filter);

    vg_filedialog_t *dlg = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    if (!dlg)
    {
        if (ctitle)
            free(ctitle);
        if (cpath)
            free(cpath);
        if (cfilter)
            free(cfilter);
        return rt_string_from_bytes("", 0);
    }

    vg_filedialog_set_title(dlg, ctitle);
    vg_filedialog_set_initial_path(dlg, cpath);
    vg_filedialog_set_multi_select(dlg, true);
    if (cfilter && cfilter[0])
    {
        vg_filedialog_add_filter(dlg, "Files", cfilter);
    }

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    vg_filedialog_show(dlg);

    size_t count = 0;
    char **paths = vg_filedialog_get_selected_paths(dlg, &count);

    rt_string result = rt_string_from_bytes("", 0);
    if (paths && count > 0)
    {
        // Join paths with semicolon
        size_t total_len = 0;
        for (size_t i = 0; i < count; i++)
        {
            total_len += strlen(paths[i]) + 1;
        }
        char *joined = (char *)malloc(total_len);
        if (joined)
        {
            size_t off = 0;
            for (size_t i = 0; i < count; i++)
            {
                if (i > 0)
                    joined[off++] = ';';
                size_t len = strlen(paths[i]);
                memcpy(joined + off, paths[i], len);
                off += len;
            }
            joined[off] = '\0';
            result = rt_string_from_bytes(joined, off);
            free(joined);
        }
        for (size_t i = 0; i < count; i++)
        {
            free(paths[i]);
        }
        free(paths);
    }

    vg_filedialog_destroy(dlg);
    return result;
}

rt_string rt_filedialog_save(rt_string title,
                             rt_string filter,
                             rt_string default_name,
                             rt_string default_path)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cfilter = rt_string_to_cstr(filter);
    char *cname = rt_string_to_cstr(default_name);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_save_file(ctitle, cpath, cname, "Files", cfilter);
#else
    // vg_filedialog_save_file expects: title, path, default_name, filter_name, filter_pattern
    char *result = vg_filedialog_save_file(ctitle, cpath, cname, "Files", cfilter);
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);
    if (cname)
        free(cname);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_select_folder(ctitle, cpath);
#else
    char *result = vg_filedialog_select_folder(ctitle, cpath);
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

// Custom FileDialog structure
typedef struct
{
    vg_filedialog_t *dialog;
    char **selected_paths;
    size_t selected_count;
    int64_t result;
} rt_filedialog_data_t;

void *rt_filedialog_new(int64_t type)
{
    vg_filedialog_mode_t mode;
    switch (type)
    {
        case RT_FILEDIALOG_OPEN:
            mode = VG_FILEDIALOG_OPEN;
            break;
        case RT_FILEDIALOG_SAVE:
            mode = VG_FILEDIALOG_SAVE;
            break;
        case RT_FILEDIALOG_FOLDER:
            mode = VG_FILEDIALOG_SELECT_FOLDER;
            break;
        default:
            mode = VG_FILEDIALOG_OPEN;
            break;
    }

    vg_filedialog_t *dlg = vg_filedialog_create(mode);
    if (!dlg)
        return NULL;

    rt_filedialog_data_t *data = (rt_filedialog_data_t *)malloc(sizeof(rt_filedialog_data_t));
    if (!data)
    {
        vg_filedialog_destroy(dlg);
        return NULL;
    }
    data->dialog = dlg;
    data->selected_paths = NULL;
    data->selected_count = 0;
    data->result = 0;

    return data;
}

void rt_filedialog_set_title(void *dialog, rt_string title)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *ctitle = rt_string_to_cstr(title);
    vg_filedialog_set_title(data->dialog, ctitle);
    if (ctitle)
        free(ctitle);
}

void rt_filedialog_set_path(void *dialog, rt_string path)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cpath = rt_string_to_cstr(path);
    vg_filedialog_set_initial_path(data->dialog, cpath);
    if (cpath)
        free(cpath);
}

void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_clear_filters(data->dialog);
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_set_default_name(void *dialog, rt_string name)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    vg_filedialog_set_filename(data->dialog, cname);
    if (cname)
        free(cname);
}

void rt_filedialog_set_multiple(void *dialog, int64_t multiple)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_set_multi_select(data->dialog, multiple != 0);
}

int64_t rt_filedialog_show(void *dialog)
{
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_show(data->dialog);

    // Get selected paths
    if (data->selected_paths)
    {
        for (size_t i = 0; i < data->selected_count; i++)
        {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
    }
    data->selected_paths = vg_filedialog_get_selected_paths(data->dialog, &data->selected_count);
    data->result = (data->selected_count > 0) ? 1 : 0;

    return data->result;
}

rt_string rt_filedialog_get_path(void *dialog)
{
    if (!dialog)
        return rt_string_from_bytes("", 0);
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && data->selected_count > 0)
    {
        return rt_string_from_bytes(data->selected_paths[0], strlen(data->selected_paths[0]));
    }
    return rt_string_from_bytes("", 0);
}

int64_t rt_filedialog_get_path_count(void *dialog)
{
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    return (int64_t)data->selected_count;
}

rt_string rt_filedialog_get_path_at(void *dialog, int64_t index)
{
    if (!dialog)
        return rt_string_from_bytes("", 0);
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && index >= 0 && (size_t)index < data->selected_count)
    {
        return rt_string_from_bytes(data->selected_paths[index],
                                    strlen(data->selected_paths[index]));
    }
    return rt_string_from_bytes("", 0);
}

void rt_filedialog_destroy(void *dialog)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths)
    {
        for (size_t i = 0; i < data->selected_count; i++)
        {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
    }
    if (data->dialog)
    {
        vg_filedialog_destroy(data->dialog);
    }
    free(data);
}

//=============================================================================
// Phase 6: FindBar (Search & Replace)
//=============================================================================

// FindBar state tracking
typedef struct
{
    vg_findreplacebar_t *bar;
    void *bound_editor;
    char *find_text;
    char *replace_text;
    int64_t case_sensitive;
    int64_t whole_word;
    int64_t regex;
    int64_t replace_mode;
} rt_findbar_data_t;

void *rt_findbar_new(void *parent)
{
    vg_findreplacebar_t *bar = vg_findreplacebar_create();
    if (!bar)
        return NULL;

    rt_findbar_data_t *data = (rt_findbar_data_t *)malloc(sizeof(rt_findbar_data_t));
    if (!data)
    {
        vg_findreplacebar_destroy(bar);
        return NULL;
    }
    data->bar = bar;
    data->bound_editor = NULL;
    data->find_text = NULL;
    data->replace_text = NULL;
    data->case_sensitive = 0;
    data->whole_word = 0;
    data->regex = 0;
    data->replace_mode = 0;

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_findbar_destroy(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->bar)
    {
        vg_findreplacebar_destroy(data->bar);
    }
    if (data->find_text)
        free(data->find_text);
    if (data->replace_text)
        free(data->replace_text);
    free(data);
}

void rt_findbar_bind_editor(void *bar, void *editor)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = editor;
    vg_findreplacebar_set_target(data->bar, (vg_codeeditor_t *)editor);
}

void rt_findbar_unbind_editor(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = NULL;
    vg_findreplacebar_set_target(data->bar, NULL);
}

void rt_findbar_set_replace_mode(void *bar, int64_t replace)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->replace_mode = replace;
    vg_findreplacebar_set_show_replace(data->bar, replace != 0);
}

int64_t rt_findbar_is_replace_mode(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->replace_mode;
}

void rt_findbar_set_find_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text)
        free(data->find_text);
    data->find_text = rt_string_to_cstr(text);
    vg_findreplacebar_set_find_text(data->bar, data->find_text);
}

rt_string rt_findbar_get_find_text(void *bar)
{
    if (!bar)
        return rt_string_from_bytes("", 0);
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text)
    {
        return rt_string_from_bytes(data->find_text, strlen(data->find_text));
    }
    return rt_string_from_bytes("", 0);
}

void rt_findbar_set_replace_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text)
        free(data->replace_text);
    data->replace_text = rt_string_to_cstr(text);
    // vg_findreplacebar doesn't have a set_replace_text - would need to track locally
}

rt_string rt_findbar_get_replace_text(void *bar)
{
    if (!bar)
        return rt_string_from_bytes("", 0);
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text)
    {
        return rt_string_from_bytes(data->replace_text, strlen(data->replace_text));
    }
    return rt_string_from_bytes("", 0);
}

// Helper to update find options
static void rt_findbar_update_options(rt_findbar_data_t *data)
{
    vg_search_options_t opts = {.case_sensitive = data->case_sensitive != 0,
                                .whole_word = data->whole_word != 0,
                                .use_regex = data->regex != 0,
                                .in_selection = false,
                                .wrap_around = true};
    vg_findreplacebar_set_options(data->bar, &opts);
}

void rt_findbar_set_case_sensitive(void *bar, int64_t sensitive)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->case_sensitive = sensitive;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_case_sensitive(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->case_sensitive;
}

void rt_findbar_set_whole_word(void *bar, int64_t whole)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->whole_word = whole;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_whole_word(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->whole_word;
}

void rt_findbar_set_regex(void *bar, int64_t regex)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->regex = regex;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_regex(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->regex;
}

int64_t rt_findbar_find_next(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_next(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

int64_t rt_findbar_find_previous(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_prev(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

int64_t rt_findbar_replace(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_replace_current(data->bar);
    return 1; // Assume success
}

int64_t rt_findbar_replace_all(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    size_t count_before = vg_findreplacebar_get_match_count(data->bar);
    vg_findreplacebar_replace_all(data->bar);
    return (int64_t)count_before;
}

int64_t rt_findbar_get_match_count(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_match_count(data->bar);
}

int64_t rt_findbar_get_current_match(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_current_match(data->bar);
}

void rt_findbar_set_visible(void *bar, int64_t visible)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    // The widget would need visibility control - stub for now
    (void)data;
    (void)visible;
}

int64_t rt_findbar_is_visible(void *bar)
{
    if (!bar)
        return 0;
    // Stub - would need widget visibility query
    (void)bar;
    return 0;
}

void rt_findbar_focus(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_focus(data->bar);
}
