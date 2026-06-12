//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file vg_codeeditor_editing.inc
/// @brief CodeEditor text mutation, cursor movement, selection, line-editing,
/// and indentation helpers.
///
/// @details This include fragment is consumed only by vg_codeeditor.c and keeps
/// editing operations separate from persistence, paint, input-dispatch, and
/// public API wrappers.
///
// File: src/lib/gui/src/widgets/vg_codeeditor_editing.inc
// Purpose: Selection normalization, cursor targeting, multi-cursor edit target application, and
// history recording glue.
//
// Key invariants:
//   - Included only by src/lib/gui/src/widgets/vg_codeeditor.c.
//   - Fragment order preserves the original static helper visibility and
//     vtable wiring from the monolithic source file.
//   - Public CodeEditor behavior and exported symbols remain unchanged.
//
// Ownership/Lifetime:
//   - Ownership follows vg_codeeditor_t rules from vg_ide_widgets_editor.h;
//     text lines, history, highlights, fold regions, inlay hints, and extra
//     cursor storage are freed by the lifecycle fragment.
//
// Links: src/lib/gui/src/widgets/vg_codeeditor.c,
//        src/lib/gui/include/vg_ide_widgets_editor.h
//
//===----------------------------------------------------------------------===//

//=============================================================================
// Selection Helper Functions
//=============================================================================

/// @brief Reads editor->selection and writes a canonically ordered start/end into the four
/// out-params.
static void normalize_selection(
    vg_codeeditor_t *editor, int *start_line, int *start_col, int *end_line, int *end_col) {
    *start_line = editor->selection.start_line;
    *start_col = editor->selection.start_col;
    *end_line = editor->selection.end_line;
    *end_col = editor->selection.end_col;

    // Normalize: start should be before end
    if (*start_line > *end_line || (*start_line == *end_line && *start_col > *end_col)) {
        int tmp = *start_line;
        *start_line = *end_line;
        *end_line = tmp;
        tmp = *start_col;
        *start_col = *end_col;
        *end_col = tmp;
    }
}

/// @brief Swaps the start/end pair in-place so that start always precedes end in document order.
static void normalize_selection_range(int *start_line,
                                      int *start_col,
                                      int *end_line,
                                      int *end_col) {
    if (*start_line > *end_line || (*start_line == *end_line && *start_col > *end_col)) {
        int tmp = *start_line;
        *start_line = *end_line;
        *end_line = tmp;
        tmp = *start_col;
        *start_col = *end_col;
        *end_col = tmp;
    }
}

/// @brief Extracts and returns a heap-allocated copy of the text in [start_line:start_col,
/// end_line:end_col).
static char *copy_text_range_len(vg_codeeditor_t *editor,
                                 int start_line,
                                 int start_col,
                                 int end_line,
                                 int end_col,
                                 size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if (!editor)
        return NULL;

    normalize_selection_range(&start_line, &start_col, &end_line, &end_col);

    size_t total_len = 0;
    for (int line = start_line; line <= end_line; line++) {
        int col_start = (line == start_line) ? start_col : 0;
        int col_end = (line == end_line) ? end_col : (int)editor->lines[line].length;
        total_len += (size_t)(col_end - col_start);
        if (line < end_line)
            total_len++;
    }

    char *result = malloc(total_len + 1);
    if (!result)
        return NULL;

    char *ptr = result;
    for (int line = start_line; line <= end_line; line++) {
        int col_start = (line == start_line) ? start_col : 0;
        int col_end = (line == end_line) ? end_col : (int)editor->lines[line].length;
        size_t len = (size_t)(col_end - col_start);
        if (len > 0) {
            memcpy(ptr, editor->lines[line].text + col_start, len);
            ptr += len;
        }
        if (line < end_line)
            *ptr++ = '\n';
    }
    *ptr = '\0';
    if (out_len)
        *out_len = total_len;
    return result;
}

/// @brief Extracts and returns a heap-allocated copy of the text in [start_line:start_col,
/// end_line:end_col).
static char *copy_text_range(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col) {
    return copy_text_range_len(editor, start_line, start_col, end_line, end_col, NULL);
}

/// @brief Clears the has_selection flag on all extra (non-primary) cursors.
static void clear_extra_cursor_selections(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    for (int i = 0; i < editor->extra_cursor_count; i++)
        editor->extra_cursors[i].has_selection = false;
}

/// @brief Clamps @p *line to [0, line_count-1] without touching the column (used for selection
/// anchors).
static void clamp_editor_line(vg_codeeditor_t *editor, int *line) {
    if (!editor || !line || editor->line_count <= 0)
        return;
    if (*line < 0)
        *line = 0;
    if (*line >= editor->line_count)
        *line = editor->line_count - 1;
}

/// @brief Clamps @p *col to [0, lines[line].length] for an already-validated @p line index.
static void clamp_editor_col(vg_codeeditor_t *editor, int line, int *col) {
    if (!editor || !col || editor->line_count <= 0)
        return;
    if (line < 0 || line >= editor->line_count)
        return;
    if (*col < 0)
        *col = 0;
    if (*col > (int)editor->lines[line].length)
        *col = (int)editor->lines[line].length;
}

/// @brief Clamps both line and col atomically; note that clamping the line may implicitly constrain
/// the col.
static void clamp_editor_position(vg_codeeditor_t *editor, int *line, int *col) {
    if (!editor || !line || !col || editor->line_count <= 0)
        return;
    clamp_editor_line(editor, line);
    clamp_editor_col(editor, *line, col);
}

/// @brief Add a secondary cursor unless one already exists at the same position.
static void codeeditor_add_extra_cursor_at(vg_codeeditor_t *editor, int line, int col) {
    if (!editor || editor->line_count <= 0)
        return;
    clamp_editor_position(editor, &line, &col);
    if (editor->cursor_line == line && editor->cursor_col == col)
        return;
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        if (editor->extra_cursors[i].line == line && editor->extra_cursors[i].col == col)
            return;
    }
    if (editor->extra_cursor_count >= editor->extra_cursor_cap) {
        if (editor->extra_cursor_cap > INT_MAX / 2)
            return;
        int new_cap = editor->extra_cursor_cap ? editor->extra_cursor_cap * 2 : 4;
        void *p = realloc(editor->extra_cursors, (size_t)new_cap * sizeof(*editor->extra_cursors));
        if (!p)
            return;
        editor->extra_cursors = p;
        editor->extra_cursor_cap = new_cap;
    }
    struct vg_extra_cursor *cursor = &editor->extra_cursors[editor->extra_cursor_count++];
    cursor->line = line;
    cursor->col = col;
    memset(&cursor->selection, 0, sizeof(cursor->selection));
    cursor->has_selection = false;
}

/// @brief Clamps (line, col) to valid bounds and then snaps the line to its fold's visible anchor.
static void codeeditor_clamp_cursor_to_visible(vg_codeeditor_t *editor, int *line, int *col) {
    if (!editor || !line || !col || editor->line_count <= 0)
        return;
    clamp_editor_position(editor, line, col);
    *line = codeeditor_visible_anchor_line(editor, *line);
    clamp_editor_col(editor, *line, col);
}

/// @brief Snaps all cursors (primary and extra) off hidden lines after a fold toggle or line
/// deletion.
static void codeeditor_adjust_hidden_cursors(vg_codeeditor_t *editor) {
    if (!editor || editor->line_count <= 0)
        return;

    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        codeeditor_clamp_cursor_to_visible(
            editor, &editor->extra_cursors[i].line, &editor->extra_cursors[i].col);
    }
}

/// @brief Returns -1, 0, or 1 for lhs < rhs, lhs == rhs, or lhs > rhs in document order.
static int compare_positions(int lhs_line, int lhs_col, int rhs_line, int rhs_col) {
    if (lhs_line != rhs_line)
        return (lhs_line < rhs_line) ? -1 : 1;
    if (lhs_col != rhs_col)
        return (lhs_col < rhs_col) ? -1 : 1;
    return 0;
}

typedef struct vg_edit_target {
    int cursor_id;
    int cursor_line_before;
    int cursor_col_before;
    int start_line;
    int start_col;
    int end_line;
    int end_col;
} vg_edit_target_t;

/// @brief qsort comparator that orders edit targets in descending document position (last target
/// first).
static int edit_target_compare_desc(const void *lhs, const void *rhs) {
    const vg_edit_target_t *a = (const vg_edit_target_t *)lhs;
    const vg_edit_target_t *b = (const vg_edit_target_t *)rhs;
    int cmp = compare_positions(b->start_line, b->start_col, a->start_line, a->start_col);
    if (cmp != 0)
        return cmp;
    cmp = compare_positions(b->end_line, b->end_col, a->end_line, a->end_col);
    if (cmp != 0)
        return cmp;
    return a->cursor_id - b->cursor_id;
}

/// @brief qsort comparator that orders edit targets in ascending document position (first target
/// first).
static int edit_target_compare_asc(const void *lhs, const void *rhs) {
    const vg_edit_target_t *a = (const vg_edit_target_t *)lhs;
    const vg_edit_target_t *b = (const vg_edit_target_t *)rhs;
    int cmp = compare_positions(a->start_line, a->start_col, b->start_line, b->start_col);
    if (cmp != 0)
        return cmp;
    cmp = compare_positions(a->end_line, a->end_col, b->end_line, b->end_col);
    if (cmp != 0)
        return cmp;
    return a->cursor_id - b->cursor_id;
}

/// @brief Updates the cursor at @p cursor_id (0 = primary, 1+ = extra) to (line, col).
static void set_cursor_position_by_id(vg_codeeditor_t *editor, int cursor_id, int line, int col) {
    if (cursor_id == 0) {
        editor->cursor_line = line;
        editor->cursor_col = col;
        return;
    }
    int extra_idx = cursor_id - 1;
    if (extra_idx < 0 || extra_idx >= editor->extra_cursor_count)
        return;
    editor->extra_cursors[extra_idx].line = line;
    editor->extra_cursors[extra_idx].col = col;
}

/// @brief Clears the selection flag for the cursor identified by @p cursor_id.
static void clear_cursor_selection_by_id(vg_codeeditor_t *editor, int cursor_id) {
    if (cursor_id == 0) {
        editor->has_selection = false;
        return;
    }
    int extra_idx = cursor_id - 1;
    if (extra_idx < 0 || extra_idx >= editor->extra_cursor_count)
        return;
    editor->extra_cursors[extra_idx].has_selection = false;
}

/// @brief Appends a normalized, de-duplicated edit target to @p targets[]; no-op if an identical
/// range exists.
static void add_edit_target(vg_edit_target_t *targets,
                            int *count,
                            int cursor_id,
                            int cursor_line_before,
                            int cursor_col_before,
                            int start_line,
                            int start_col,
                            int end_line,
                            int end_col) {
    normalize_selection_range(&start_line, &start_col, &end_line, &end_col);
    for (int i = 0; i < *count; i++) {
        if (targets[i].start_line == start_line && targets[i].start_col == start_col &&
            targets[i].end_line == end_line && targets[i].end_col == end_col)
            return;
    }

    targets[*count].cursor_id = cursor_id;
    targets[*count].cursor_line_before = cursor_line_before;
    targets[*count].cursor_col_before = cursor_col_before;
    targets[*count].start_line = start_line;
    targets[*count].start_col = start_col;
    targets[*count].end_line = end_line;
    targets[*count].end_col = end_col;
    (*count)++;
}

/// @brief Creates an edit operation and pushes it onto the undo history; no-op if history is NULL.
static void record_edit_history(vg_codeeditor_t *editor,
                                vg_edit_op_type_t type,
                                int cursor_id,
                                int start_line,
                                int start_col,
                                int end_line,
                                int end_col,
                                const char *old_text,
                                const char *new_text,
                                int cursor_line_before,
                                int cursor_col_before,
                                int cursor_line_after,
                                int cursor_col_after) {
    if (!editor || !editor->history)
        return;
    vg_edit_op_t *op = create_edit_op(type,
                                      cursor_id,
                                      start_line,
                                      start_col,
                                      end_line,
                                      end_col,
                                      old_text,
                                      new_text,
                                      cursor_line_before,
                                      cursor_col_before,
                                      cursor_line_after,
                                      cursor_col_after);
    if (op)
        edit_history_push(editor->history, op);
}

/// @brief Applies @p replacement_text at each edit target in descending order, recording undo ops,
/// then updates cursor positions.
static void apply_edit_targets(vg_codeeditor_t *editor,
                               vg_edit_target_t *targets,
                               int target_count,
                               const char *replacement_text) {
    if (!editor || target_count <= 0)
        return;

    const int cursor_count = 1 + editor->extra_cursor_count;
    int *new_lines = malloc((size_t)cursor_count * sizeof(int));
    int *new_cols = malloc((size_t)cursor_count * sizeof(int));
    vg_edit_target_t *clamped_targets = calloc((size_t)target_count, sizeof(vg_edit_target_t));
    char **old_texts = calloc((size_t)target_count, sizeof(char *));
    bool *has_old_texts = calloc((size_t)target_count, sizeof(bool));
    if (!new_lines || !new_cols || !clamped_targets || !old_texts || !has_old_texts) {
        free(new_lines);
        free(new_cols);
        free(clamped_targets);
        free(old_texts);
        free(has_old_texts);
        return;
    }

    new_lines[0] = editor->cursor_line;
    new_cols[0] = editor->cursor_col;
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        new_lines[i + 1] = editor->extra_cursors[i].line;
        new_cols[i + 1] = editor->extra_cursors[i].col;
    }

    for (int i = 0; i < target_count; i++) {
        clamped_targets[i] = targets[i];
        clamp_editor_position(
            editor, &clamped_targets[i].start_line, &clamped_targets[i].start_col);
        clamp_editor_position(editor, &clamped_targets[i].end_line, &clamped_targets[i].end_col);
        normalize_selection_range(&clamped_targets[i].start_line,
                                  &clamped_targets[i].start_col,
                                  &clamped_targets[i].end_line,
                                  &clamped_targets[i].end_col);
        has_old_texts[i] = compare_positions(clamped_targets[i].start_line,
                                             clamped_targets[i].start_col,
                                             clamped_targets[i].end_line,
                                             clamped_targets[i].end_col) != 0;
        if (has_old_texts[i]) {
            old_texts[i] = copy_text_range(editor,
                                           clamped_targets[i].start_line,
                                           clamped_targets[i].start_col,
                                           clamped_targets[i].end_line,
                                           clamped_targets[i].end_col);
            if (!old_texts[i])
                goto cleanup;
        }
    }

    if (editor->history && target_count > 1)
        edit_history_begin_group(editor->history);

    bool changed = false;
    bool syntax_global_dirty = false;
    for (int i = 0; i < target_count; i++) {
        const vg_edit_target_t *target = &clamped_targets[i];
        char *old_text = old_texts[i];
        int history_end_line = target->end_line;
        int history_end_col = target->end_col;
        const bool has_old_text = has_old_texts[i];
        syntax_global_dirty = syntax_global_dirty ||
                              codeeditor_text_may_affect_multiline_syntax(old_text) ||
                              codeeditor_text_may_affect_multiline_syntax(replacement_text);

        if (has_old_text) {
            delete_text_range_internal(
                editor, target->start_line, target->start_col, target->end_line, target->end_col);
            changed = true;
        } else {
            editor->cursor_line = target->start_line;
            editor->cursor_col = target->start_col;
        }

        if (replacement_text && replacement_text[0] != '\0') {
            insert_text_at_internal(
                editor, target->start_line, target->start_col, replacement_text);
            changed = true;
        }

        new_lines[target->cursor_id] = editor->cursor_line;
        new_cols[target->cursor_id] = editor->cursor_col;
        clear_cursor_selection_by_id(editor, target->cursor_id);
        history_end_line = editor->cursor_line;
        history_end_col = editor->cursor_col;

        if (replacement_text && replacement_text[0] != '\0') {
            vg_edit_op_type_t type = has_old_text ? VG_EDIT_REPLACE : VG_EDIT_INSERT;
            record_edit_history(editor,
                                type,
                                target->cursor_id,
                                target->start_line,
                                target->start_col,
                                history_end_line,
                                history_end_col,
                                old_text,
                                replacement_text,
                                target->cursor_line_before,
                                target->cursor_col_before,
                                editor->cursor_line,
                                editor->cursor_col);
        } else if (has_old_text) {
            record_edit_history(editor,
                                VG_EDIT_DELETE,
                                target->cursor_id,
                                target->start_line,
                                target->start_col,
                                target->end_line,
                                target->end_col,
                                old_text,
                                NULL,
                                target->cursor_line_before,
                                target->cursor_col_before,
                                target->start_line,
                                target->start_col);
        }
    }

cleanup:
    if (editor->history && target_count > 1)
        edit_history_end_group(editor->history);

    for (int i = 0; i < cursor_count; i++)
        set_cursor_position_by_id(editor, i, new_lines[i], new_cols[i]);

    free(new_lines);
    free(new_cols);
    for (int i = 0; i < target_count; i++)
        free(old_texts[i]);
    free(clamped_targets);
    free(old_texts);
    free(has_old_texts);
    if (!changed)
        return;
    codeeditor_bump_revision(editor);
    if (syntax_global_dirty)
        codeeditor_invalidate_all_highlights(editor);
    editor->modified = true;
    vg_codeeditor_refresh_layout_state(editor);
    ensure_cursor_visible(editor);
    editor->base.needs_paint = true;
}
