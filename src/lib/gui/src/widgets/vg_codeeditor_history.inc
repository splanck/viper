//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file vg_codeeditor_history.inc
/// @brief CodeEditor undo and redo history snapshot management.
///
/// @details This include fragment is consumed only by vg_codeeditor.c and
/// isolates the history ring helpers that record, replay, clear, and clone
/// editor text and cursor state.
///
// File: src/lib/gui/src/widgets/vg_codeeditor_history.inc
// Purpose: Undo and redo history storage, edit operation allocation, grouping, and stack navigation
// helpers.
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
// Undo/Redo History Management
//=============================================================================

#define HISTORY_INITIAL_CAPACITY 64
#define HISTORY_MAX_OPERATIONS 4096

/// @brief Allocates and zero-initializes a new edit history with HISTORY_INITIAL_CAPACITY operation
/// slots.
static vg_edit_history_t *edit_history_create(void) {
    vg_edit_history_t *history = calloc(1, sizeof(vg_edit_history_t));
    if (!history)
        return NULL;

    history->operations = calloc(HISTORY_INITIAL_CAPACITY, sizeof(vg_edit_op_t *));
    if (!history->operations) {
        free(history);
        return NULL;
    }

    history->capacity = HISTORY_INITIAL_CAPACITY;
    history->count = 0;
    history->current_index = 0;
    history->next_group_id = 1;
    history->is_grouping = false;
    history->current_group = 0;

    return history;
}

/// @brief Frees the old_text and new_text strings owned by @p op, then frees the op itself.
static void edit_op_destroy(vg_edit_op_t *op) {
    if (!op)
        return;
    free(op->old_text);
    free(op->new_text);
    free(op);
}

/// @brief Destroys all operations in @p history and frees the history struct itself.
static void edit_history_destroy(vg_edit_history_t *history) {
    if (!history)
        return;

    for (size_t i = 0; i < history->count; i++) {
        edit_op_destroy(history->operations[i]);
    }
    free(history->operations);
    free(history);
}

/// @brief Destroys all operations in @p history and resets count and current_index to 0 without
/// freeing the struct.
VG_UNUSED
static void edit_history_clear(vg_edit_history_t *history) {
    if (!history)
        return;

    for (size_t i = 0; i < history->count; i++) {
        edit_op_destroy(history->operations[i]);
        history->operations[i] = NULL;
    }
    history->count = 0;
    history->current_index = 0;
    history->is_grouping = false;
    history->current_group = 0;
}

/// @brief Destroy the oldest operation and compact the remaining history.
/// @details Used to enforce HISTORY_MAX_OPERATIONS while preserving the newest
///          edits. The current undo cursor is shifted with the compacted array.
static void edit_history_evict_oldest(vg_edit_history_t *history) {
    if (!history || history->count == 0)
        return;

    edit_op_destroy(history->operations[0]);
    if (history->count > 1) {
        memmove(history->operations,
                history->operations + 1,
                (history->count - 1) * sizeof(vg_edit_op_t *));
    }
    history->count--;
    history->operations[history->count] = NULL;
    if (history->current_index > 0)
        history->current_index--;
}

/// @brief Discards any redo tail, grows the operation array if needed, and appends @p op; sets its
/// group_id if grouping.
static void edit_history_push(vg_edit_history_t *history, vg_edit_op_t *op) {
    if (!history || !op)
        return;

    // Discard any redo operations
    for (size_t i = history->current_index; i < history->count; i++) {
        edit_op_destroy(history->operations[i]);
        history->operations[i] = NULL;
    }
    history->count = history->current_index;

    while (history->count >= HISTORY_MAX_OPERATIONS)
        edit_history_evict_oldest(history);

    // Grow capacity if needed
    if (history->count >= history->capacity) {
        size_t new_capacity = history->capacity * 2;
        if (new_capacity > HISTORY_MAX_OPERATIONS)
            new_capacity = HISTORY_MAX_OPERATIONS;
        vg_edit_op_t **new_ops =
            realloc(history->operations, new_capacity * sizeof(vg_edit_op_t *));
        if (!new_ops) {
            edit_op_destroy(op);
            return;
        }
        history->operations = new_ops;
        history->capacity = new_capacity;
    }

    // Set group ID if grouping
    if (history->is_grouping) {
        op->group_id = history->current_group;
    }

    history->operations[history->count++] = op;
    history->current_index = history->count;
}

/// @brief Decrements current_index and returns the operation to be undone, or NULL if at the
/// beginning.
static vg_edit_op_t *edit_history_pop_undo(vg_edit_history_t *history) {
    if (!history || history->current_index == 0)
        return NULL;
    history->current_index--;
    return history->operations[history->current_index];
}

/// @brief Returns the operation at current_index-1 without advancing the index, or NULL if empty.
static vg_edit_op_t *edit_history_peek_undo(vg_edit_history_t *history) {
    if (!history || history->current_index == 0)
        return NULL;
    return history->operations[history->current_index - 1];
}

/// @brief Returns the operation at current_index and advances it, enabling a redo step; NULL if at
/// end.
static vg_edit_op_t *edit_history_pop_redo(vg_edit_history_t *history) {
    if (!history || history->current_index >= history->count)
        return NULL;
    vg_edit_op_t *op = history->operations[history->current_index];
    history->current_index++;
    return op;
}

/// @brief Begins an edit group so that subsequent pushed operations share a group_id for atomic
/// undo.
VG_UNUSED
static void edit_history_begin_group(vg_edit_history_t *history) {
    if (!history)
        return;
    history->is_grouping = true;
    history->current_group = history->next_group_id++;
}

/// @brief Ends the current edit group, clearing is_grouping and current_group on @p history.
VG_UNUSED
static void edit_history_end_group(vg_edit_history_t *history) {
    if (!history)
        return;
    history->is_grouping = false;
    history->current_group = 0;
}

/// @brief Rewrite the cursor-after position of the latest undo operation.
/// @details Used by paired-character insertion: the edit still owns the full
///          inserted range, but redo should leave the cursor between the pair.
static void edit_history_update_latest_cursor_after(vg_edit_history_t *history, int line, int col) {
    if (!history || history->current_index == 0 || history->current_index != history->count)
        return;
    vg_edit_op_t *op = history->operations[history->current_index - 1];
    if (!op || op->group_id != 0)
        return;
    op->cursor_line_after = line;
    op->cursor_col_after = col;
}

/// @brief Allocates a new vg_edit_op_t, strdup'ing old_text and new_text, with group_id initialised
/// to 0.
static vg_edit_op_t *create_edit_op(vg_edit_op_type_t type,
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
    vg_edit_op_t *op = calloc(1, sizeof(vg_edit_op_t));
    if (!op)
        return NULL;

    op->type = type;
    op->cursor_id = cursor_id;
    op->start_line = start_line;
    op->start_col = start_col;
    op->end_line = end_line;
    op->end_col = end_col;
    op->old_text = old_text ? vg_strdup(old_text) : NULL;
    op->new_text = new_text ? vg_strdup(new_text) : NULL;
    if ((old_text && !op->old_text) || (new_text && !op->new_text)) {
        edit_op_destroy(op);
        return NULL;
    }
    op->cursor_line_before = cursor_line_before;
    op->cursor_col_before = cursor_col_before;
    op->cursor_line_after = cursor_line_after;
    op->cursor_col_after = cursor_col_after;
    op->group_id = 0;

    return op;
}
