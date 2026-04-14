//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_findbar.c
// Purpose: Find/replace bar widget runtime bindings for ViperGUI. Provides
//   creation, editor binding, text/option configuration, and search/replace
//   operations for the vg_findreplacebar_t widget.
//
// Key invariants:
//   - The FindBar must be bound to a CodeEditor before search operations.
//   - rt_findbar_data_t (GC-managed) wraps the vg_findreplacebar_t pointer
//     and caches find/replace text and option state.
//   - Options (case_sensitive, whole_word, regex) are pushed to the vg layer
//     via rt_findbar_update_options() before each search operation.
//
// Ownership/Lifetime:
//   - rt_findbar_data_t is a GC heap object; the vg_findreplacebar_t is
//     manually freed on rt_findbar_destroy().
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/src/widgets/vg_findreplacebar.c (underlying widget)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// Phase 6: FindBar (Search & Replace)
//=============================================================================

// FindBar state tracking
typedef struct {
    vg_findreplacebar_t *bar;
    void *bound_editor;
    char *find_text;
    char *replace_text;
    int64_t case_sensitive;
    int64_t whole_word;
    int64_t regex;
    int64_t replace_mode;
} rt_findbar_data_t;

/// @brief Create a new find/replace bar widget.
/// @details Allocates a GC-managed rt_findbar_data_t wrapper around a
///          vg_findreplacebar_t. The bar must be bound to a CodeEditor via
///          rt_findbar_bind_editor before search operations will work. Options
///          (case-sensitive, whole-word, regex) are cached locally and pushed
///          to the vg layer before each search.
/// @param parent Parent container or app handle.
/// @return Opaque find bar handle, or NULL on failure.
void *rt_findbar_new(void *parent) {
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    vg_findreplacebar_t *bar = vg_findreplacebar_create();
    if (!bar)
        return NULL;

    rt_findbar_data_t *data =
        (rt_findbar_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_findbar_data_t));
    if (!data) {
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
    if (parent_widget) {
        vg_widget_add_child(parent_widget, &bar->base);
    }
    if (app && app->default_font) {
        vg_findreplacebar_set_font(bar, app->default_font, app->default_font_size);
    }
    return data;
}

/// @brief Destroy the find bar, freeing the vg widget and cached text.
/// @param bar Find bar handle (safe to pass NULL).
void rt_findbar_destroy(void *bar) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->bar) {
        vg_findreplacebar_destroy(data->bar);
    }
    if (data->find_text)
        free(data->find_text);
    if (data->replace_text)
        free(data->replace_text);
}

/// @brief Bind the find bar to a code editor for search/replace operations.
/// @details The bar needs a target editor to know which text to search. Without
///          binding, find/replace operations are no-ops.
/// @param bar    Find bar handle.
/// @param editor Code editor widget to bind to.
void rt_findbar_bind_editor(void *bar, void *editor) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = editor;
    vg_findreplacebar_set_target(data->bar, (vg_codeeditor_t *)editor);
}

/// @brief Unbind the find bar from its current code editor.
void rt_findbar_unbind_editor(void *bar) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = NULL;
    vg_findreplacebar_set_target(data->bar, NULL);
}

/// @brief Toggle between find-only mode and find+replace mode.
void rt_findbar_set_replace_mode(void *bar, int64_t replace) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->replace_mode = replace;
    vg_findreplacebar_set_show_replace(data->bar, replace != 0);
}

/// @brief Check whether the find bar is in replace mode.
int64_t rt_findbar_is_replace_mode(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->replace_mode;
}

/// @brief Set the search text for find operations.
/// @details Updates both the cached text and the underlying vg find input widget.
void rt_findbar_set_find_text(void *bar, rt_string text) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text)
        free(data->find_text);
    data->find_text = rt_string_to_cstr(text);
    vg_findreplacebar_set_find_text(data->bar, data->find_text);
}

/// @brief Get the current search text.
rt_string rt_findbar_get_find_text(void *bar) {
    if (!bar)
        return rt_str_empty();
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text) {
        return rt_string_from_bytes(data->find_text, strlen(data->find_text));
    }
    return rt_str_empty();
}

/// @brief Set the replacement text for replace operations.
/// @details Updates the cached text and pushes it to the underlying vg replace
///          input widget so replace/replace_all use the correct value.
void rt_findbar_set_replace_text(void *bar, rt_string text) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text)
        free(data->replace_text);
    data->replace_text = rt_string_to_cstr(text);
    // Apply to the underlying replace text input so replace/replace_all
    // operations use the correct replacement text.
    if (data->bar && data->bar->replace_input)
        vg_textinput_set_text((vg_textinput_t *)data->bar->replace_input, data->replace_text);
}

/// @brief Get the current replacement text.
rt_string rt_findbar_get_replace_text(void *bar) {
    if (!bar)
        return rt_str_empty();
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text) {
        return rt_string_from_bytes(data->replace_text, strlen(data->replace_text));
    }
    return rt_str_empty();
}

/// @brief Push cached search options to the vg find/replace bar widget.
/// @details Called internally after any option change (case-sensitive, whole-word,
///          regex) so the underlying search engine picks up the new settings.
static void rt_findbar_update_options(rt_findbar_data_t *data) {
    vg_search_options_t opts = {.case_sensitive = data->case_sensitive != 0,
                                .whole_word = data->whole_word != 0,
                                .use_regex = data->regex != 0,
                                .in_selection = false,
                                .wrap_around = true};
    vg_findreplacebar_set_options(data->bar, &opts);
}

/// @brief Enable or disable case-sensitive matching.
void rt_findbar_set_case_sensitive(void *bar, int64_t sensitive) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->case_sensitive = sensitive;
    rt_findbar_update_options(data);
}

/// @brief Check whether case-sensitive matching is enabled.
int64_t rt_findbar_is_case_sensitive(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->case_sensitive;
}

/// @brief Enable or disable whole-word matching.
void rt_findbar_set_whole_word(void *bar, int64_t whole) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->whole_word = whole;
    rt_findbar_update_options(data);
}

/// @brief Check whether whole-word matching is enabled.
int64_t rt_findbar_is_whole_word(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->whole_word;
}

/// @brief Enable or disable regex-based pattern matching.
void rt_findbar_set_regex(void *bar, int64_t regex) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->regex = regex;
    rt_findbar_update_options(data);
}

/// @brief Check whether regex matching is enabled.
int64_t rt_findbar_is_regex(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->regex;
}

/// @brief Advance to the next match in the bound editor.
/// @return 1 if at least one match exists, 0 otherwise.
int64_t rt_findbar_find_next(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_next(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

/// @brief Move to the previous match in the bound editor.
/// @return 1 if at least one match exists, 0 otherwise.
int64_t rt_findbar_find_previous(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_prev(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

/// @brief Replace the current match with the replacement text.
/// @return 1 on success.
int64_t rt_findbar_replace(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_replace_current(data->bar);
    return 1; // Assume success
}

/// @brief Replace all matches in the bound editor with the replacement text.
/// @return The number of matches that existed before replacement.
int64_t rt_findbar_replace_all(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    size_t count_before = vg_findreplacebar_get_match_count(data->bar);
    vg_findreplacebar_replace_all(data->bar);
    return (int64_t)count_before;
}

/// @brief Get the total number of matches for the current search text.
int64_t rt_findbar_get_match_count(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_match_count(data->bar);
}

/// @brief Get the 1-based index of the currently highlighted match.
int64_t rt_findbar_get_current_match(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_current_match(data->bar);
}

/// @brief Show or hide the find bar widget.
void rt_findbar_set_visible(void *bar, int64_t visible) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->bar)
        data->bar->base.visible = visible != 0;
}

/// @brief Check whether the find bar is currently visible.
int64_t rt_findbar_is_visible(void *bar) {
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (data->bar && data->bar->base.visible) ? 1 : 0;
}

/// @brief Give keyboard focus to the find bar's search input field.
void rt_findbar_focus(void *bar) {
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_focus(data->bar);
}

#else /* !VIPER_ENABLE_GRAPHICS */

// Graphics-disabled stubs — every public entry-point returns a benign zero/no-op so callers
// link cleanly without graphics. Behavioral docs live on the real implementations above.

/// @brief Stub — graphics disabled at build time. Returns NULL.
void *rt_findbar_new(void *parent) {
    (void)parent;
    return NULL;
}

void rt_findbar_destroy(void *bar) {
    (void)bar;
}

void rt_findbar_bind_editor(void *bar, void *editor) {
    (void)bar;
    (void)editor;
}

void rt_findbar_unbind_editor(void *bar) {
    (void)bar;
}

void rt_findbar_set_replace_mode(void *bar, int64_t replace) {
    (void)bar;
    (void)replace;
}

int64_t rt_findbar_is_replace_mode(void *bar) {
    (void)bar;
    return 0;
}

void rt_findbar_set_find_text(void *bar, rt_string text) {
    (void)bar;
    (void)text;
}

rt_string rt_findbar_get_find_text(void *bar) {
    (void)bar;
    return rt_str_empty();
}

void rt_findbar_set_replace_text(void *bar, rt_string text) {
    (void)bar;
    (void)text;
}

rt_string rt_findbar_get_replace_text(void *bar) {
    (void)bar;
    return rt_str_empty();
}

void rt_findbar_set_case_sensitive(void *bar, int64_t sensitive) {
    (void)bar;
    (void)sensitive;
}

int64_t rt_findbar_is_case_sensitive(void *bar) {
    (void)bar;
    return 0;
}

void rt_findbar_set_whole_word(void *bar, int64_t whole) {
    (void)bar;
    (void)whole;
}

int64_t rt_findbar_is_whole_word(void *bar) {
    (void)bar;
    return 0;
}

void rt_findbar_set_regex(void *bar, int64_t regex) {
    (void)bar;
    (void)regex;
}

int64_t rt_findbar_is_regex(void *bar) {
    (void)bar;
    return 0;
}

int64_t rt_findbar_find_next(void *bar) {
    (void)bar;
    return 0;
}

int64_t rt_findbar_find_previous(void *bar) {
    (void)bar;
    return 0;
}

int64_t rt_findbar_replace(void *bar) {
    (void)bar;
    return 0;
}

int64_t rt_findbar_replace_all(void *bar) {
    (void)bar;
    return 0;
}

int64_t rt_findbar_get_match_count(void *bar) {
    (void)bar;
    return 0;
}

int64_t rt_findbar_get_current_match(void *bar) {
    (void)bar;
    return 0;
}

void rt_findbar_set_visible(void *bar, int64_t visible) {
    (void)bar;
    (void)visible;
}

int64_t rt_findbar_is_visible(void *bar) {
    (void)bar;
    return 0;
}

void rt_findbar_focus(void *bar) {
    (void)bar;
}

#endif /* VIPER_ENABLE_GRAPHICS */
