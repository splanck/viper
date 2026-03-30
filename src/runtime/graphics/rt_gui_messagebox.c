//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_messagebox.c
// Purpose: MessageBox dialog runtime bindings for ViperGUI. Provides both
//   simple convenience dialogs (info, warning, error, question, confirm,
//   prompt) and an object-oriented builder API (new, add_button, show, destroy).
//
// Key invariants:
//   - Simple dialogs block until the user dismisses them and return the button ID.
//   - The builder API uses rt_messagebox_data_t (GC-managed) to track custom
//     buttons and the underlying vg_dialog_t pointer.
//   - The vg_dialog_t must be destroyed before the wrapper is GC'd.
//
// Ownership/Lifetime:
//   - rt_messagebox_data_t is a GC heap object; the embedded vg_dialog_t is
//     manually freed on rt_messagebox_destroy().
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/src/widgets/vg_dialog.c (underlying widget)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// Phase 5: MessageBox Dialog
//=============================================================================

int64_t rt_messagebox_info(rt_string title, rt_string message) {
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_INFO, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    rt_gui_ensure_default_font();
    if (s_current_app)
        vg_dialog_set_font(dlg, s_current_app->default_font, s_current_app->default_font_size);
    vg_dialog_show(dlg);
    rt_gui_set_active_dialog(dlg);
    return 0;
}

int64_t rt_messagebox_warning(rt_string title, rt_string message) {
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
    rt_gui_ensure_default_font();
    if (s_current_app)
        vg_dialog_set_font(dlg, s_current_app->default_font, s_current_app->default_font_size);
    vg_dialog_show(dlg);
    rt_gui_set_active_dialog(dlg);
    return 0;
}

int64_t rt_messagebox_error(rt_string title, rt_string message) {
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_ERROR, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    rt_gui_ensure_default_font();
    if (s_current_app)
        vg_dialog_set_font(dlg, s_current_app->default_font, s_current_app->default_font_size);
    vg_dialog_show(dlg);
    rt_gui_set_active_dialog(dlg);
    return 0;
}

int64_t rt_messagebox_question(rt_string title, rt_string message) {
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

    rt_gui_ensure_default_font();
    if (s_current_app)
        vg_dialog_set_font(dlg, s_current_app->default_font, s_current_app->default_font_size);
    vg_dialog_show(dlg);
    rt_gui_set_active_dialog(dlg);

    // Blocking modal loop — runs until user clicks Yes or No
    while (dlg->is_open && s_current_app && !s_current_app->should_close) {
        rt_gui_app_poll(s_current_app);
        rt_gui_app_render(s_current_app);
    }

    vg_dialog_result_t result = vg_dialog_get_result(dlg);
    rt_gui_set_active_dialog(NULL);
    vg_widget_destroy(&dlg->base);
    return (result == VG_DIALOG_RESULT_YES) ? 1 : 0;
}

int64_t rt_messagebox_confirm(rt_string title, rt_string message) {
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

    rt_gui_ensure_default_font();
    if (s_current_app)
        vg_dialog_set_font(dlg, s_current_app->default_font, s_current_app->default_font_size);
    vg_dialog_show(dlg);
    rt_gui_set_active_dialog(dlg);

    // Blocking modal loop — runs until user clicks OK or Cancel
    while (dlg->is_open && s_current_app && !s_current_app->should_close) {
        rt_gui_app_poll(s_current_app);
        rt_gui_app_render(s_current_app);
    }

    vg_dialog_result_t result = vg_dialog_get_result(dlg);
    rt_gui_set_active_dialog(NULL);
    vg_widget_destroy(&dlg->base);
    return (result == VG_DIALOG_RESULT_OK) ? 1 : 0;
}

// Prompt commit callback data
typedef struct {
    vg_dialog_t *dialog;
} rt_prompt_commit_data_t;

static void prompt_on_commit(vg_widget_t *w, const char *text, void *user_data) {
    (void)w;
    (void)text;
    rt_prompt_commit_data_t *d = (rt_prompt_commit_data_t *)user_data;
    if (d && d->dialog)
        vg_dialog_close(d->dialog, VG_DIALOG_RESULT_OK);
}

rt_string rt_messagebox_prompt(rt_string title, rt_string message) {
    if (!s_current_app)
        return rt_str_empty();

    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);

    vg_dialog_t *dlg = vg_dialog_create(ctitle);
    if (ctitle)
        free(ctitle);
    if (!dlg) {
        if (cmsg)
            free(cmsg);
        return rt_str_empty();
    }

    // Show the prompt message above the text input
    if (cmsg) {
        vg_dialog_set_message(dlg, cmsg);
        free(cmsg);
    }

    // Apply app font to dialog
    if (s_current_app->default_font)
        vg_dialog_set_font(dlg, s_current_app->default_font, s_current_app->default_font_size);

    // Create the text input (no parent — set as dialog content, not widget-tree child)
    vg_textinput_t *input = vg_textinput_create(NULL);
    if (!input) {
        vg_widget_destroy((vg_widget_t *)dlg);
        return rt_str_empty();
    }

    if (s_current_app->default_font)
        vg_textinput_set_font(input, s_current_app->default_font, s_current_app->default_font_size);

    // When Enter is pressed inside the input, dismiss as OK
    rt_prompt_commit_data_t commit_data = {.dialog = dlg};
    vg_textinput_set_on_commit(input, prompt_on_commit, &commit_data);

    // Place the input as the dialog's content widget
    vg_dialog_set_content(dlg, (vg_widget_t *)input);
    vg_dialog_set_buttons(dlg, VG_DIALOG_BUTTONS_OK_CANCEL);
    vg_dialog_set_modal(dlg, true, s_current_app->root);

    // Show and focus the input so the user can type immediately
    vg_dialog_show_centered(dlg, s_current_app->root);
    vg_widget_set_focus((vg_widget_t *)input);

    // Modal event loop: pump events and render until dialog is dismissed
    while (vg_dialog_is_open(dlg)) {
        rt_gui_app_poll(s_current_app);
        rt_gui_app_render(s_current_app);
    }

    // Collect result before destroying
    rt_string result = rt_str_empty();
    if (vg_dialog_get_result(dlg) == VG_DIALOG_RESULT_OK) {
        const char *text = vg_textinput_get_text(input);
        if (text && text[0])
            result = rt_string_from_bytes(text, strlen(text));
    }

    // The dialog does not own the input (created with NULL parent); destroy both.
    // Clear content pointer first so dialog_destroy doesn't see a stale pointer.
    dlg->content = NULL;
    vg_widget_destroy((vg_widget_t *)dlg);
    vg_widget_destroy((vg_widget_t *)input);
    return result;
}

// Custom MessageBox structure for tracking state
typedef struct {
    vg_dialog_t *dialog;
    int64_t result;
    int64_t default_button;
    // Custom button tracking for rt_messagebox_add_button
    vg_dialog_button_def_t *custom_buttons;
    size_t custom_button_count;
    size_t custom_button_cap;
} rt_messagebox_data_t;

void *rt_messagebox_new(rt_string title, rt_string message, int64_t type) {
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
    switch (type) {
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

    rt_messagebox_data_t *data =
        (rt_messagebox_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_messagebox_data_t));
    if (!data) {
        vg_widget_destroy((vg_widget_t *)dlg);
        return NULL;
    }
    data->dialog = dlg;
    data->result = -1;
    data->default_button = 0;

    return data;
}

void rt_messagebox_add_button(void *box, rt_string text, int64_t id) {
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;

    // Grow the custom buttons array if needed
    if (data->custom_button_count >= data->custom_button_cap) {
        size_t new_cap = data->custom_button_cap ? data->custom_button_cap * 2 : 4;
        vg_dialog_button_def_t *p = (vg_dialog_button_def_t *)realloc(
            data->custom_buttons, new_cap * sizeof(vg_dialog_button_def_t));
        if (!p)
            return;
        data->custom_buttons = p;
        data->custom_button_cap = new_cap;
    }

    char *clabel = rt_string_to_cstr(text);
    vg_dialog_button_def_t *btn = &data->custom_buttons[data->custom_button_count++];
    btn->label = clabel ? clabel : strdup("OK");
    btn->result = (vg_dialog_result_t)id;
    btn->is_default = (id == data->default_button);
    btn->is_cancel = false;
}

void rt_messagebox_set_default_button(void *box, int64_t id) {
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    data->default_button = id;
}

int64_t rt_messagebox_show(void *box) {
    if (!box)
        return -1;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;

    // Apply custom buttons if any were added via rt_messagebox_add_button
    if (data->custom_button_count > 0) {
        vg_dialog_set_custom_buttons(data->dialog, data->custom_buttons, data->custom_button_count);
    }

    vg_dialog_show(data->dialog);
    rt_gui_set_active_dialog(data->dialog);

    // Blocking modal loop — same pattern as rt_messagebox_confirm/question.
    while (data->dialog->is_open && s_current_app && !s_current_app->should_close) {
        rt_gui_app_poll(s_current_app);
        rt_gui_app_render(s_current_app);
    }

    vg_dialog_result_t result = vg_dialog_get_result(data->dialog);
    rt_gui_set_active_dialog(NULL);

    // For custom buttons, the result code maps directly to the id passed
    // to rt_messagebox_add_button. For preset buttons, use standard mapping.
    if (data->custom_button_count > 0)
        return (int64_t)result;

    if (result == VG_DIALOG_RESULT_OK || result == VG_DIALOG_RESULT_YES)
        return 0;
    if (result == VG_DIALOG_RESULT_NO)
        return 1;
    if (result == VG_DIALOG_RESULT_CANCEL)
        return 2;
    return data->default_button;
}

void rt_messagebox_destroy(void *box) {
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    // Free custom button labels
    for (size_t i = 0; i < data->custom_button_count; i++)
        free(data->custom_buttons[i].label);
    free(data->custom_buttons);
    if (data->dialog) {
        vg_widget_destroy((vg_widget_t *)data->dialog);
    }
}

#else /* !VIPER_ENABLE_GRAPHICS */

int64_t rt_messagebox_info(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

int64_t rt_messagebox_warning(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

int64_t rt_messagebox_error(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

int64_t rt_messagebox_question(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

int64_t rt_messagebox_confirm(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

rt_string rt_messagebox_prompt(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return rt_str_empty();
}

void *rt_messagebox_new(rt_string title, rt_string message, int64_t type) {
    (void)title;
    (void)message;
    (void)type;
    return NULL;
}

void rt_messagebox_add_button(void *box, rt_string text, int64_t id) {
    (void)box;
    (void)text;
    (void)id;
}

void rt_messagebox_set_default_button(void *box, int64_t id) {
    (void)box;
    (void)id;
}

int64_t rt_messagebox_show(void *box) {
    (void)box;
    return -1;
}

void rt_messagebox_destroy(void *box) {
    (void)box;
}

#endif /* VIPER_ENABLE_GRAPHICS */
