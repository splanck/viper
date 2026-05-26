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

#define RT_MESSAGEBOX_DATA_MAGIC UINT64_C(0x52544D5347424F58)

/// @brief Return the active GUI app for message box hosting (falls back to `s_current_app`).
static rt_gui_app_t *rt_messagebox_app(void) {
    rt_gui_app_t *app = rt_gui_get_active_app();
    return app ? app : s_current_app;
}

/// @brief Return non-zero if the button label should be treated as a "cancel / close / no" action.
static int rt_messagebox_label_is_cancel(const char *label) {
    if (!label)
        return 0;
    return strcasecmp(label, "cancel") == 0 || strcasecmp(label, "close") == 0 ||
           strcasecmp(label, "no") == 0;
}

/// @brief Configure a dialog for modal presentation: enforce minimum width, apply font,
///        set modal root, center-show, and push onto the app's dialog stack.
static int rt_messagebox_prepare_modal(rt_gui_app_t *app, vg_dialog_t *dlg) {
    if (!app || !app->window || !app->root || !dlg)
        return 0;
    rt_gui_activate_app(app);
    rt_gui_ensure_default_font();
    rt_gui_apply_default_font((vg_widget_t *)dlg);
    if (dlg->min_width < 360)
        vg_dialog_set_size_constraints(dlg, 360, dlg->min_height, 720, dlg->max_height);
    vg_dialog_set_modal(dlg, true, app->root);
    vg_dialog_show_centered(dlg, app->root);
    rt_gui_push_dialog(app, dlg);
    return 1;
}

/// @brief Run the event loop until the dialog closes or the app signals shutdown.
/// @details Pops the dialog from the stack on exit and returns the VG dialog result code.
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
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_messagebox_app();
    char *ctitle = rt_string_to_gui_cstr(title);
    char *cmsg = rt_string_to_gui_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_INFO, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    if (!rt_messagebox_prepare_modal(app, dlg)) {
        vg_widget_destroy(&dlg->base);
        return 0;
    }
    rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return 0;
}

/// @brief One-shot warning message box (single OK button, warning/exclamation icon). Always
/// returns 0; for accept/decline use `_confirm` or `_question`.
int64_t rt_messagebox_warning(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_messagebox_app();
    char *ctitle = rt_string_to_gui_cstr(title);
    char *cmsg = rt_string_to_gui_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_WARNING, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    if (!rt_messagebox_prepare_modal(app, dlg)) {
        vg_widget_destroy(&dlg->base);
        return 0;
    }
    rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return 0;
}

/// @brief One-shot error message box (single OK, red/error icon). Returns 0 always.
int64_t rt_messagebox_error(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_messagebox_app();
    char *ctitle = rt_string_to_gui_cstr(title);
    char *cmsg = rt_string_to_gui_cstr(message);
    vg_dialog_t *dlg = vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_ERROR, VG_DIALOG_BUTTONS_OK);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    if (!rt_messagebox_prepare_modal(app, dlg)) {
        vg_widget_destroy(&dlg->base);
        return 0;
    }
    rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return 0;
}

/// @brief Yes/No question box (question icon). Returns 1 if user chose Yes, 0 for No or any
/// other dismissal (Esc, window close).
int64_t rt_messagebox_question(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_messagebox_app();
    char *ctitle = rt_string_to_gui_cstr(title);
    char *cmsg = rt_string_to_gui_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_QUESTION, VG_DIALOG_BUTTONS_YES_NO);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    if (!rt_messagebox_prepare_modal(app, dlg)) {
        vg_widget_destroy(&dlg->base);
        return 0;
    }
    vg_dialog_result_t result = rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return (result == VG_DIALOG_RESULT_YES) ? 1 : 0;
}

/// @brief OK/Cancel confirmation box (question icon). Returns 1 for OK, 0 for Cancel.
int64_t rt_messagebox_confirm(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_messagebox_app();
    char *ctitle = rt_string_to_gui_cstr(title);
    char *cmsg = rt_string_to_gui_cstr(message);
    vg_dialog_t *dlg =
        vg_dialog_message(ctitle, cmsg, VG_DIALOG_ICON_QUESTION, VG_DIALOG_BUTTONS_OK_CANCEL);
    if (ctitle)
        free(ctitle);
    if (cmsg)
        free(cmsg);
    if (!dlg)
        return 0;
    if (!rt_messagebox_prepare_modal(app, dlg)) {
        vg_widget_destroy(&dlg->base);
        return 0;
    }
    vg_dialog_result_t result = rt_messagebox_run_modal(app, dlg);
    vg_widget_destroy(&dlg->base);
    return (result == VG_DIALOG_RESULT_OK) ? 1 : 0;
}

// Prompt commit callback data
typedef struct {
    uint64_t magic;
    vg_dialog_t *dialog;
} rt_prompt_commit_data_t;

/// @brief Text-input `on_commit` callback — closes the prompt dialog as OK when Enter is pressed.
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
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_messagebox_app();
    if (!app)
        return rt_str_empty();
    rt_gui_activate_app(app);
    rt_gui_ensure_default_font();

    char *ctitle = rt_string_to_gui_cstr(title);
    char *cmsg = rt_string_to_gui_cstr(message);

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
    rt_gui_apply_default_font((vg_widget_t *)dlg);

    // Create the text input and attach it as dialog content.
    vg_textinput_t *input = vg_textinput_create(NULL);
    if (!input) {
        vg_widget_destroy((vg_widget_t *)dlg);
        return rt_str_empty();
    }

    rt_gui_apply_default_font((vg_widget_t *)input);
    input->base.constraints.min_width = 240.0f;
    input->base.constraints.preferred_width = 360.0f;
    vg_textinput_set_placeholder(input, "Enter a value");

    // When Enter is pressed inside the input, dismiss as OK
    rt_prompt_commit_data_t commit_data = {.dialog = dlg};
    vg_textinput_set_on_commit(input, prompt_on_commit, &commit_data);

    // Place the input as the dialog's content widget
    vg_dialog_set_content(dlg, (vg_widget_t *)input);
    vg_dialog_set_buttons(dlg, VG_DIALOG_BUTTONS_OK_CANCEL);
    vg_dialog_set_size_constraints(dlg, 420, 190, 760, 420);

    // Show and focus the input so the user can type immediately.
    if (!rt_messagebox_prepare_modal(app, dlg)) {
        vg_widget_destroy((vg_widget_t *)dlg);
        return rt_str_empty();
    }
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
    uint64_t magic;
    vg_dialog_t *dialog;
    int64_t result;
    int64_t default_button;
    int has_default_button;
    rt_gui_app_t *owner_app;
    // Custom button tracking for rt_messagebox_add_button
    vg_dialog_button_def_t *custom_buttons;
    int64_t *custom_button_ids;
    size_t custom_button_count;
    size_t custom_button_cap;
} rt_messagebox_data_t;

static rt_messagebox_data_t **s_messagebox_wrappers = NULL;
static size_t s_messagebox_wrapper_count = 0;
static size_t s_messagebox_wrapper_cap = 0;

/// @brief Record a wrapper in the global message-box registry (idempotent).
/// @details The registry is the source of truth for handle validation: a checked
///          cast only trusts an opaque `void*` once it is found here (then verifies
///          the magic tag), guarding against forged/freed handles. Capacity doubles from 8.
/// @return 1 on success or if already present; 0 on overflow or realloc failure.
static int rt_messagebox_register_wrapper(rt_messagebox_data_t *data) {
    if (!data)
        return 0;
    for (size_t i = 0; i < s_messagebox_wrapper_count; i++) {
        if (s_messagebox_wrappers[i] == data)
            return 1;
    }
    if (s_messagebox_wrapper_count >= s_messagebox_wrapper_cap) {
        size_t new_cap = s_messagebox_wrapper_cap ? s_messagebox_wrapper_cap * 2 : 8;
        if (new_cap < s_messagebox_wrapper_cap ||
            new_cap > SIZE_MAX / sizeof(*s_messagebox_wrappers))
            return 0;
        void *p = realloc(s_messagebox_wrappers, new_cap * sizeof(*s_messagebox_wrappers));
        if (!p)
            return 0;
        s_messagebox_wrappers = (rt_messagebox_data_t **)p;
        s_messagebox_wrapper_cap = new_cap;
    }
    s_messagebox_wrappers[s_messagebox_wrapper_count++] = data;
    return 1;
}

/// @brief Remove a wrapper from the message-box registry, compacting the array. No-op if absent.
static void rt_messagebox_unregister_wrapper(rt_messagebox_data_t *data) {
    if (!data)
        return;
    for (size_t i = 0; i < s_messagebox_wrapper_count; i++) {
        if (s_messagebox_wrappers[i] != data)
            continue;
        memmove(&s_messagebox_wrappers[i],
                &s_messagebox_wrappers[i + 1],
                (s_messagebox_wrapper_count - i - 1) * sizeof(*s_messagebox_wrappers));
        s_messagebox_wrapper_count--;
        return;
    }
}

/// @brief True if @p data is a currently-registered wrapper; backs handle validation.
static int rt_messagebox_wrapper_is_registered(const rt_messagebox_data_t *data) {
    if (!data)
        return 0;
    for (size_t i = 0; i < s_messagebox_wrapper_count; i++) {
        if (s_messagebox_wrappers[i] == data)
            return 1;
    }
    return 0;
}

void rt_messagebox_invalidate_dialog(vg_dialog_t *dialog) {
    if (!dialog)
        return;
    for (size_t i = 0; i < s_messagebox_wrapper_count; i++) {
        rt_messagebox_data_t *data = s_messagebox_wrappers[i];
        if (data && data->dialog == dialog) {
            data->dialog = NULL;
            data->owner_app = NULL;
            data->result = -1;
        }
    }
}

/// @brief Authenticate a MessageBox handle via its magic tag (NULL if not).
static rt_messagebox_data_t *rt_messagebox_checked(void *box) {
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    return rt_messagebox_wrapper_is_registered(data) && data->magic == RT_MESSAGEBOX_DATA_MAGIC
               ? data
               : NULL;
}

/// @brief Release all resources: free custom button labels, destroy the VG dialog,
///        and remove it from the app's dialog stack.
static void rt_messagebox_dispose(rt_messagebox_data_t *data) {
    if (!data)
        return;
    rt_gui_app_t *app = rt_gui_is_app_handle(data->owner_app) ? data->owner_app
                                                              : rt_messagebox_app();
    for (size_t i = 0; i < data->custom_button_count; i++)
        free(data->custom_buttons[i].label);
    free(data->custom_buttons);
    free(data->custom_button_ids);
    data->custom_buttons = NULL;
    data->custom_button_ids = NULL;
    data->custom_button_count = 0;
    data->custom_button_cap = 0;
    if (data->dialog) {
        rt_gui_remove_dialog(app, data->dialog);
        vg_widget_destroy((vg_widget_t *)data->dialog);
        data->dialog = NULL;
    }
    data->result = -1;
    data->magic = 0;
    rt_messagebox_unregister_wrapper(data);
}

/// @brief GC finalizer — delegates to `rt_messagebox_dispose` to free custom button labels
///        and destroy the underlying VG dialog handle before the GC reclaims the object.
static void rt_messagebox_finalize(void *box) {
    rt_messagebox_dispose((rt_messagebox_data_t *)box);
}

/// @brief Build a stateful MessageBox (icon by `type`: INFO/WARNING/ERROR/QUESTION). Buttons
/// start empty — call `_add_button` to register them, then `_show` to display modally. Returns
/// the GC-managed handle, or NULL on failure.
void *rt_messagebox_new(rt_string title, rt_string message, int64_t type) {
    RT_ASSERT_MAIN_THREAD();
    char *ctitle = rt_string_to_gui_cstr(title);
    vg_dialog_t *dlg = vg_dialog_create(ctitle);
    if (ctitle)
        free(ctitle);
    if (!dlg)
        return NULL;

    char *cmsg = rt_string_to_gui_cstr(message);
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
    data->magic = RT_MESSAGEBOX_DATA_MAGIC;
    data->result = -1;
    data->default_button = 0;
    data->has_default_button = 0;
    data->owner_app = rt_messagebox_app();
    data->custom_buttons = NULL;
    data->custom_button_ids = NULL;
    data->custom_button_count = 0;
    data->custom_button_cap = 0;
    if (!rt_messagebox_register_wrapper(data)) {
        rt_messagebox_dispose(data);
        return NULL;
    }
    rt_obj_set_finalizer(data, rt_messagebox_finalize);

    return data;
}

/// @brief Convenience: stateful MessageBox with INFO icon.
void *rt_messagebox_new_info(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_INFO);
}

/// @brief Convenience: stateful MessageBox with WARNING icon.
void *rt_messagebox_new_warning(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_WARNING);
}

/// @brief Convenience: stateful MessageBox with ERROR icon.
void *rt_messagebox_new_error(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_ERROR);
}

/// @brief Convenience: stateful MessageBox with QUESTION icon.
void *rt_messagebox_new_question(rt_string title, rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    return rt_messagebox_new(title, message, RT_MESSAGEBOX_QUESTION);
}

/// @brief Append a custom button (`text` label, `id` returned on click). Multiple calls extend
/// the button row; auto-detects "Cancel"/"Close"/"No" labels for Esc-binding behavior.
void rt_messagebox_add_button(void *box, rt_string text, int64_t id) {
    RT_ASSERT_MAIN_THREAD();
    rt_messagebox_data_t *data = rt_messagebox_checked(box);
    if (!data)
        return;
    if (!data->dialog)
        return;

    // Grow the custom buttons array if needed
    if (data->custom_button_count >= data->custom_button_cap) {
        if (data->custom_button_cap > SIZE_MAX / 2) {
            return;
        }
        size_t new_cap = data->custom_button_cap ? data->custom_button_cap * 2 : 4;
        if (new_cap > SIZE_MAX / sizeof(vg_dialog_button_def_t) ||
            new_cap > SIZE_MAX / sizeof(int64_t)) {
            return;
        }
        vg_dialog_button_def_t *new_buttons =
            (vg_dialog_button_def_t *)calloc(new_cap, sizeof(vg_dialog_button_def_t));
        int64_t *new_ids = (int64_t *)calloc(new_cap, sizeof(int64_t));
        if (!new_buttons || !new_ids) {
            free(new_buttons);
            free(new_ids);
            return;
        }
        if (data->custom_button_count > 0) {
            memcpy(new_buttons,
                   data->custom_buttons,
                   data->custom_button_count * sizeof(vg_dialog_button_def_t));
            memcpy(new_ids, data->custom_button_ids, data->custom_button_count * sizeof(int64_t));
        }
        free(data->custom_buttons);
        free(data->custom_button_ids);
        data->custom_buttons = new_buttons;
        data->custom_button_ids = new_ids;
        data->custom_button_cap = new_cap;
    }

    char *clabel = rt_string_to_gui_cstr(text);
    if (!clabel)
        clabel = strdup("OK");
    if (!clabel)
        return;
    size_t index = data->custom_button_count++;
    vg_dialog_button_def_t *btn = &data->custom_buttons[index];
    btn->label = clabel;
    btn->result = (vg_dialog_result_t)(index + 1);
    btn->is_default = data->has_default_button && id == data->default_button;
    btn->is_cancel = rt_messagebox_label_is_cancel(btn->label);
    data->custom_button_ids[index] = id;
}

/// @brief Mark the button with `id` as the default (Enter-key activated). Updates any matching
/// button in the buttons-list. Stored separately so it works even if added later.
void rt_messagebox_set_default_button(void *box, int64_t id) {
    RT_ASSERT_MAIN_THREAD();
    rt_messagebox_data_t *data = rt_messagebox_checked(box);
    if (!data)
        return;
    if (!data->dialog)
        return;
    data->default_button = id;
    data->has_default_button = 1;
    for (size_t i = 0; i < data->custom_button_count; i++) {
        data->custom_buttons[i].is_default = (data->custom_button_ids[i] == id);
    }
}

/// @brief Display the dialog modally and return the chosen button's id (or for preset
/// OK/Yes/No/Cancel: 0/0/1/2 respectively). Returns -1 if no app context, the dialog wasn't
/// constructed, or the user closed the window without picking. Custom buttons (added via
/// `_add_button`) take precedence over preset mappings.
int64_t rt_messagebox_show(void *box) {
    RT_ASSERT_MAIN_THREAD();
    rt_messagebox_data_t *data = rt_messagebox_checked(box);
    if (!data)
        return -1;
    rt_gui_app_t *app = rt_gui_is_app_handle(data->owner_app) ? data->owner_app
                                                              : rt_messagebox_app();
    if (!app || !data->dialog)
        return -1;

    // Apply custom buttons if any were added via rt_messagebox_add_button
    if (data->custom_button_count > 0) {
        for (size_t i = 0; i < data->custom_button_count; i++) {
            data->custom_buttons[i].result = (vg_dialog_result_t)(i + 1);
            data->custom_buttons[i].is_default =
                data->has_default_button && data->custom_button_ids &&
                data->custom_button_ids[i] == data->default_button;
        }
        vg_dialog_set_custom_buttons(data->dialog, data->custom_buttons, data->custom_button_count);
    }

    if (!rt_messagebox_prepare_modal(app, data->dialog))
        return -1;
    vg_dialog_result_t result = rt_messagebox_run_modal(app, data->dialog);

    // For custom buttons, the result code maps directly to the id passed
    // to rt_messagebox_add_button. For preset buttons, use standard mapping.
    if (data->custom_button_count > 0) {
        if (result == VG_DIALOG_RESULT_NONE)
            return -1;
        int64_t index = (int64_t)result - 1;
        if (index < 0 || (size_t)index >= data->custom_button_count || !data->custom_button_ids)
            return -1;
        return data->custom_button_ids[index];
    }

    if (result == VG_DIALOG_RESULT_OK || result == VG_DIALOG_RESULT_YES)
        return 0;
    if (result == VG_DIALOG_RESULT_NO)
        return 1;
    if (result == VG_DIALOG_RESULT_CANCEL)
        return 2;
    return -1;
}

/// @brief Manually free dialog resources (custom buttons, backend handle). The GC finalizer
/// also calls this — explicit destruction is optional but useful for early cleanup.
void rt_messagebox_destroy(void *box) {
    RT_ASSERT_MAIN_THREAD();
    rt_messagebox_data_t *data = rt_messagebox_checked(box);
    if (!data)
        return;
    rt_messagebox_dispose(data);
}

#else /* !VIPER_ENABLE_GRAPHICS */

/// @brief Stub: graphics disabled — no dialog shown; returns 0 immediately.
int64_t rt_messagebox_info(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

/// @brief Stub: graphics disabled — no dialog shown; returns 0 immediately.
int64_t rt_messagebox_warning(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

/// @brief Stub: graphics disabled — no dialog shown; returns 0 immediately.
int64_t rt_messagebox_error(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

/// @brief Stub: graphics disabled — no Yes/No dialog; returns 0 (treated as "No").
int64_t rt_messagebox_question(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

/// @brief Stub: graphics disabled — no OK/Cancel dialog; returns 0 (treated as "Cancel").
int64_t rt_messagebox_confirm(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return 0;
}

/// @brief Stub: graphics disabled — no text-input prompt; returns empty string immediately.
rt_string rt_messagebox_prompt(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return rt_str_empty();
}

/// @brief Stub: graphics disabled — returns NULL; no stateful dialog object is created.
void *rt_messagebox_new(rt_string title, rt_string message, int64_t type) {
    (void)title;
    (void)message;
    (void)type;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no info dialog object is created.
void *rt_messagebox_new_info(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no warning dialog object is created.
void *rt_messagebox_new_warning(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no error dialog object is created.
void *rt_messagebox_new_error(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no question dialog object is created.
void *rt_messagebox_new_question(rt_string title, rt_string message) {
    (void)title;
    (void)message;
    return NULL;
}

/// @brief Stub: graphics disabled — no-op; button registration is silently ignored.
void rt_messagebox_add_button(void *box, rt_string text, int64_t id) {
    (void)box;
    (void)text;
    (void)id;
}

/// @brief Stub: graphics disabled — no-op; default button selection is silently ignored.
void rt_messagebox_set_default_button(void *box, int64_t id) {
    (void)box;
    (void)id;
}

/// @brief Stub: graphics disabled — returns -1 (no dialog to show, no button chosen).
int64_t rt_messagebox_show(void *box) {
    (void)box;
    return -1;
}

/// @brief Stub: graphics disabled — no-op; nothing to destroy when graphics are absent.
void rt_messagebox_destroy(void *box) {
    (void)box;
}

#endif /* VIPER_ENABLE_GRAPHICS */
