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

static rt_gui_app_t *rt_messagebox_app(void) {
    rt_gui_app_t *app = rt_gui_get_active_app();
    return app ? app : s_current_app;
}

static int rt_messagebox_label_is_cancel(const char *label) {
    if (!label)
        return 0;
    return strcasecmp(label, "cancel") == 0 || strcasecmp(label, "close") == 0 ||
           strcasecmp(label, "no") == 0;
}

static void rt_messagebox_prepare_modal(rt_gui_app_t *app, vg_dialog_t *dlg) {
    rt_gui_activate_app(app);
    rt_gui_ensure_default_font();
    if (app && app->default_font)
        vg_dialog_set_font(dlg, app->default_font, app->default_font_size);
    vg_dialog_set_modal(dlg, true, app ? app->root : NULL);
    vg_dialog_show_centered(dlg, app ? app->root : NULL);
    rt_gui_push_dialog(app, dlg);
}

static vg_dialog_result_t rt_messagebox_run_modal(rt_gui_app_t *app, vg_dialog_t *dlg) {
    while (dlg && dlg->is_open && app && !app->should_close) {
        rt_gui_app_poll(app);
        rt_gui_app_render(app);
    }
    if (app)
        rt_gui_remove_dialog(app, dlg);
    return dlg ? vg_dialog_get_result(dlg) : VG_DIALOG_RESULT_NONE;
}

/// @brief One-shot informational message box (single OK button, info icon). Blocks until user
/// dismisses. Returns 0 (no meaningful selection — the OK is the only choice).
int64_t rt_messagebox_info(rt_string title, rt_string message) {
    rt_gui_app_t *app = rt_messagebox_app();
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_INFO, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    rt_messagebox_prepare_modal(app, dlg);
    rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return 0;
}

/// @brief One-shot warning message box (single OK button, warning/exclamation icon). Always
/// returns 0; for accept/decline use `_confirm` or `_question`.
int64_t rt_messagebox_warning(rt_string title, rt_string message) {
    rt_gui_app_t *app = rt_messagebox_app();
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
    rt_messagebox_prepare_modal(app, dlg);
    rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return 0;
}

/// @brief One-shot error message box (single OK, red/error icon). Returns 0 always.
int64_t rt_messagebox_error(rt_string title, rt_string message) {
    rt_gui_app_t *app = rt_messagebox_app();
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg = rt_string_to_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_ERROR, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    rt_messagebox_prepare_modal(app, dlg);
    rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return 0;
}

/// @brief Yes/No question box (question icon). Returns 1 if user chose Yes, 0 for No or any
/// other dismissal (Esc, window close).
int64_t rt_messagebox_question(rt_string title, rt_string message) {
    rt_gui_app_t *app = rt_messagebox_app();
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
    rt_messagebox_prepare_modal(app, dlg);
    vg_dialog_result_t result = rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return (result == VG_DIALOG_RESULT_YES) ? 1 : 0;
}

/// @brief OK/Cancel confirmation box (question icon). Returns 1 for OK, 0 for Cancel.
int64_t rt_messagebox_confirm(rt_string title, rt_string message) {
    rt_gui_app_t *app = rt_messagebox_app();
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
    rt_messagebox_prepare_modal(app, dlg);
    vg_dialog_result_t result = rt_messagebox_run_modal(app, dlg);
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

/// @brief Single-line text-input prompt: shows `message`, an editable text field, and OK/Cancel.
/// Pressing Enter inside the input dismisses as OK. Returns the entered text on OK, or empty
/// on Cancel/empty submission. The text input receives focus immediately for fast typing.
rt_string rt_messagebox_prompt(rt_string title, rt_string message) {
    rt_gui_app_t *app = rt_messagebox_app();
    if (!app)
        return rt_str_empty();
    rt_gui_activate_app(app);
    rt_gui_ensure_default_font();

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
    if (app->default_font)
        vg_dialog_set_font(dlg, app->default_font, app->default_font_size);

    // Create the text input and attach it as dialog content.
    vg_textinput_t *input = vg_textinput_create(NULL);
    if (!input) {
        vg_widget_destroy((vg_widget_t *)dlg);
        return rt_str_empty();
    }

    if (app->default_font)
        vg_textinput_set_font(input, app->default_font, app->default_font_size);

    // When Enter is pressed inside the input, dismiss as OK
    rt_prompt_commit_data_t commit_data = {.dialog = dlg};
    vg_textinput_set_on_commit(input, prompt_on_commit, &commit_data);

    // Place the input as the dialog's content widget
    vg_dialog_set_content(dlg, (vg_widget_t *)input);
    vg_dialog_set_buttons(dlg, VG_DIALOG_BUTTONS_OK_CANCEL);

    // Show and focus the input so the user can type immediately.
    rt_messagebox_prepare_modal(app, dlg);
    vg_widget_set_focus((vg_widget_t *)input);

    vg_dialog_result_t result_code = rt_messagebox_run_modal(app, dlg);

    // Collect result before destroying
    rt_string result = rt_str_empty();
    if (result_code == VG_DIALOG_RESULT_OK) {
        const char *text = vg_textinput_get_text(input);
        if (text && text[0])
            result = rt_string_from_bytes(text, strlen(text));
    }

    vg_widget_destroy((vg_widget_t *)dlg);
    return result;
}

// Custom MessageBox structure for tracking state
typedef struct {
    vg_dialog_t *dialog;
    int64_t result;
    int64_t default_button;
    rt_gui_app_t *owner_app;
    // Custom button tracking for rt_messagebox_add_button
    vg_dialog_button_def_t *custom_buttons;
    size_t custom_button_count;
    size_t custom_button_cap;
} rt_messagebox_data_t;

static void rt_messagebox_dispose(rt_messagebox_data_t *data) {
    if (!data)
        return;
    rt_gui_app_t *app = data->owner_app ? data->owner_app : rt_messagebox_app();
    for (size_t i = 0; i < data->custom_button_count; i++)
        free(data->custom_buttons[i].label);
    free(data->custom_buttons);
    data->custom_buttons = NULL;
    data->custom_button_count = 0;
    data->custom_button_cap = 0;
    if (data->dialog) {
        rt_gui_remove_dialog(app, data->dialog);
        vg_widget_destroy((vg_widget_t *)data->dialog);
        data->dialog = NULL;
    }
    data->result = -1;
}

static void rt_messagebox_finalize(void *box) {
    rt_messagebox_dispose((rt_messagebox_data_t *)box);
}

/// @brief Build a stateful MessageBox (icon by `type`: INFO/WARNING/ERROR/QUESTION). Buttons
/// start empty — call `_add_button` to register them, then `_show` to display modally. Returns
/// the GC-managed handle, or NULL on failure.
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
    data->owner_app = rt_messagebox_app();
    data->custom_buttons = NULL;
    data->custom_button_count = 0;
    data->custom_button_cap = 0;
    rt_obj_set_finalizer(data, rt_messagebox_finalize);

    return data;
}

/// @brief Convenience: stateful MessageBox with INFO icon.
void *rt_messagebox_new_info(rt_string title, rt_string message) {
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_INFO);
}

/// @brief Convenience: stateful MessageBox with WARNING icon.
void *rt_messagebox_new_warning(rt_string title, rt_string message) {
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_WARNING);
}

/// @brief Convenience: stateful MessageBox with ERROR icon.
void *rt_messagebox_new_error(rt_string title, rt_string message) {
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_ERROR);
}

/// @brief Convenience: stateful MessageBox with QUESTION icon.
void *rt_messagebox_new_question(rt_string title, rt_string message) {
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_QUESTION);
}

/// @brief Append a custom button (`text` label, `id` returned on click). Multiple calls extend
/// the button row; auto-detects "Cancel"/"Close"/"No" labels for Esc-binding behavior.
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
    btn->is_cancel = rt_messagebox_label_is_cancel(btn->label);
}

/// @brief Mark the button with `id` as the default (Enter-key activated). Updates any matching
/// button in the buttons-list. Stored separately so it works even if added later.
void rt_messagebox_set_default_button(void *box, int64_t id) {
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    data->default_button = id;
    for (size_t i = 0; i < data->custom_button_count; i++) {
        data->custom_buttons[i].is_default = (data->custom_buttons[i].result == id);
    }
}

/// @brief Display the dialog modally and return the chosen button's id (or for preset
/// OK/Yes/No/Cancel: 0/0/1/2 respectively). Returns -1 if no app context, the dialog wasn't
/// constructed, or the user closed the window without picking. Custom buttons (added via
/// `_add_button`) take precedence over preset mappings.
int64_t rt_messagebox_show(void *box) {
    if (!box)
        return -1;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    rt_gui_app_t *app = data->owner_app ? data->owner_app : rt_messagebox_app();
    if (!app)
        return -1;

    // Apply custom buttons if any were added via rt_messagebox_add_button
    if (data->custom_button_count > 0) {
        vg_dialog_set_custom_buttons(data->dialog, data->custom_buttons, data->custom_button_count);
    }

    rt_messagebox_prepare_modal(app, data->dialog);
    vg_dialog_result_t result = rt_messagebox_run_modal(app, data->dialog);

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

/// @brief Manually free dialog resources (custom buttons, backend handle). The GC finalizer
/// also calls this — explicit destruction is optional but useful for early cleanup.
void rt_messagebox_destroy(void *box) {
    if (!box)
        return;
    rt_messagebox_dispose((rt_messagebox_data_t *)box);
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

void *rt_messagebox_new_info(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return NULL;
}

void *rt_messagebox_new_warning(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return NULL;
}

void *rt_messagebox_new_error(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return NULL;
}

void *rt_messagebox_new_question(rt_string title, rt_string message) {
    (void)title;
    (void)message;
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
