//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file vg_codeeditor_input.inc
/// @brief CodeEditor keyboard, mouse, wheel, selection gesture, popup, and
/// clipboard event handling.
///
/// @details This include fragment is consumed only by vg_codeeditor.c and keeps
/// event dispatch separate from text mutation primitives and the public API
/// layer that calls them.
///
// File: src/lib/gui/src/widgets/vg_codeeditor_input.inc
// Purpose: UTF-8 input encoding, indentation helpers, bracket-pair editing, backspace/delete
// handling, event dispatch, and focus hooks.
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

/// @brief Encodes @p cp as UTF-8 into @p buf (≥4 bytes); returns bytes written, or 0 for invalid
/// codepoints.
static int encode_utf8(uint32_t cp, char *buf) {
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        // Reject UTF-16 surrogates (U+D800–U+DFFF)
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return 0;
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0; // Out-of-range
}

/// @brief True if a character event should insert literal text instead of being treated as a
/// command shortcut.
static bool codeeditor_key_char_allows_text(const vg_event_t *event) {
    if (!event)
        return false;

    uint32_t mods = event->modifiers;
    bool has_super = (mods & VG_MOD_SUPER) != 0;
    bool has_ctrl = (mods & VG_MOD_CTRL) != 0;
    bool has_alt = (mods & VG_MOD_ALT) != 0;

    if (has_super)
        return false;
    if (has_ctrl && !has_alt)
        return false;
    if (has_alt && !has_ctrl)
        return false;
    return true;
}

static bool codeeditor_codepoint_is_text(uint32_t cp) {
    if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF)
        return false;
    if (cp >= 0x80 && cp <= 0x9F)
        return false;
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return false;
    if ((cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xF0000 && cp <= 0xFFFFD) ||
        (cp >= 0x100000 && cp <= 0x10FFFD))
        return false;
    return true;
}

static int codeeditor_leading_indent_len(const char *text, size_t len) {
    int i = 0;
    while ((size_t)i < len && (text[i] == ' ' || text[i] == '\t'))
        i++;
    return i;
}

static int codeeditor_last_nonspace_before(const char *text, int col) {
    int i = col - 1;
    while (i >= 0 && (text[i] == ' ' || text[i] == '\t'))
        i--;
    return i;
}

static bool codeeditor_opener_adds_indent(char ch) {
    return ch == '{' || ch == '(' || ch == '[';
}

static char codeeditor_closing_for_opener(char opener);

/// @brief Return true when @p ch is an ASCII identifier character.
/// @details Used by pair insertion heuristics to avoid inserting quotes in the
///          middle of words and to avoid adding paired punctuation immediately
///          before identifiers.
static bool codeeditor_is_identifier_byte(char ch) {
    unsigned char c = (unsigned char)ch;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

/// @brief Return the indentation text that should be added for one nesting level.
/// @details The returned text is stored in @p out and is always NUL-terminated.
///          It respects the editor's soft-tab setting and clamps extreme tab
///          widths to the same range as the rest of the editor input code.
static void codeeditor_make_extra_indent(const vg_codeeditor_t *editor,
                                         char *out,
                                         size_t out_cap,
                                         int *out_len) {
    if (out_len)
        *out_len = 0;
    if (!out || out_cap == 0)
        return;
    out[0] = '\0';
    if (!editor)
        return;

    int extra_len = editor->use_spaces ? editor->tab_width : 1;
    if (extra_len < 1)
        extra_len = 1;
    if (extra_len > 16)
        extra_len = 16;

    if (editor->use_spaces) {
        size_t count = (size_t)extra_len;
        if (count >= out_cap)
            count = out_cap - 1;
        memset(out, ' ', count);
        out[count] = '\0';
        if (out_len)
            *out_len = (int)count;
    } else {
        out[0] = '\t';
        out[1 < out_cap ? 1 : 0] = '\0';
        if (out_len)
            *out_len = 1;
    }
}

/// @brief Return true when @p opener and @p closer form one of the editor's
///        automatic insertion pairs.
static bool codeeditor_chars_are_pair(char opener, char closer) {
    return codeeditor_closing_for_opener(opener) == closer;
}

/// @brief Return true when the cursor is directly between an automatic pair.
/// @details This is the condition needed to split `{|}` into a three-line block
///          on Enter. The helper is intentionally byte-based because all pair
///          characters are single-byte ASCII.
static bool codeeditor_cursor_between_pair(vg_codeeditor_t *editor, char *out_opener) {
    if (out_opener)
        *out_opener = '\0';
    if (!editor || editor->cursor_line < 0 || editor->cursor_line >= editor->line_count)
        return false;
    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    int col = editor->cursor_col;
    if (col <= 0 || col >= (int)line->length)
        return false;
    char opener = line->text[col - 1];
    char closer = line->text[col];
    if (!codeeditor_chars_are_pair(opener, closer))
        return false;
    if (out_opener)
        *out_opener = opener;
    return true;
}

/// @brief Insert a split block when Enter is pressed between an automatic pair.
/// @details For `{|}` this inserts "\n<indent+level>\n<indent>" and restores the
///          cursor to the indented middle line. The underlying insertion remains
///          a single undo step; the latest history cursor is adjusted to the
///          user-visible final cursor position.
static bool codeeditor_try_split_pair_on_enter(vg_codeeditor_t *editor) {
    char opener = '\0';
    if (!codeeditor_cursor_between_pair(editor, &opener))
        return false;
    if (!codeeditor_opener_adds_indent(opener))
        return false;

    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    int base_indent_len = codeeditor_leading_indent_len(line->text, line->length);
    char extra_indent[32];
    int extra_indent_len = 0;
    codeeditor_make_extra_indent(editor, extra_indent, sizeof(extra_indent), &extra_indent_len);

    size_t insert_len = 2u + (size_t)base_indent_len * 2u + (size_t)extra_indent_len;
    char *insert = malloc(insert_len + 1u);
    if (!insert)
        return false;

    char *p = insert;
    *p++ = '\n';
    if (base_indent_len > 0) {
        memcpy(p, line->text, (size_t)base_indent_len);
        p += base_indent_len;
    }
    if (extra_indent_len > 0) {
        memcpy(p, extra_indent, (size_t)extra_indent_len);
        p += extra_indent_len;
    }
    *p++ = '\n';
    if (base_indent_len > 0) {
        memcpy(p, line->text, (size_t)base_indent_len);
        p += base_indent_len;
    }
    *p = '\0';

    int cursor_line_after = editor->cursor_line + 1;
    int cursor_col_after = base_indent_len + extra_indent_len;
    vg_codeeditor_insert_text(editor, insert);
    editor->cursor_line = cursor_line_after;
    editor->cursor_col = cursor_col_after;
    edit_history_update_latest_cursor_after(
        editor->history, editor->cursor_line, editor->cursor_col);
    ensure_cursor_visible(editor);
    editor->cursor_visible = true;
    editor->base.needs_paint = true;
    free(insert);
    return true;
}

static void codeeditor_insert_newline_with_indent(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    if (!editor->auto_indent || editor->has_selection || editor->extra_cursor_count > 0 ||
        editor->cursor_line < 0 || editor->cursor_line >= editor->line_count) {
        vg_codeeditor_insert_text(editor, "\n");
        return;
    }

    if (codeeditor_try_split_pair_on_enter(editor))
        return;

    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    int line_len = line->length > (size_t)INT_MAX ? INT_MAX : (int)line->length;
    int col = editor->cursor_col;
    if (col < 0)
        col = 0;
    if (col > line_len)
        col = line_len;

    int indent_len = codeeditor_leading_indent_len(line->text, line->length);
    int extra_indent_len = 0;
    int last_nonspace = codeeditor_last_nonspace_before(line->text, col);
    char extra_indent[32];
    codeeditor_make_extra_indent(editor, extra_indent, sizeof(extra_indent), &extra_indent_len);
    if (last_nonspace < 0 || !codeeditor_opener_adds_indent(line->text[last_nonspace]))
        extra_indent_len = 0;

    size_t insert_len = 1u + (size_t)indent_len + (size_t)extra_indent_len;
    char *insert = malloc(insert_len + 1u);
    if (!insert) {
        vg_codeeditor_insert_text(editor, "\n");
        return;
    }

    char *p = insert;
    *p++ = '\n';
    if (indent_len > 0) {
        memcpy(p, line->text, (size_t)indent_len);
        p += indent_len;
    }
    if (extra_indent_len > 0) {
        memcpy(p, extra_indent, (size_t)extra_indent_len);
        p += extra_indent_len;
    }
    *p = '\0';

    vg_codeeditor_insert_text(editor, insert);
    free(insert);
}

static char codeeditor_closing_for_opener(char opener) {
    switch (opener) {
        case '(':
            return ')';
        case '[':
            return ']';
        case '{':
            return '}';
        case '"':
            return '"';
        case '\'':
            return '\'';
        default:
            return '\0';
    }
}

static bool codeeditor_is_pair_closer(char ch) {
    return ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'';
}

static bool codeeditor_is_whitespace_span(const char *text, int start, int end) {
    if (!text || start < 0 || end < start)
        return false;
    for (int i = start; i < end; i++) {
        if (text[i] != ' ' && text[i] != '\t')
            return false;
    }
    return true;
}

static bool codeeditor_try_format_closing_brace(vg_codeeditor_t *editor) {
    if (!editor || !editor->auto_indent || editor->has_selection || editor->extra_cursor_count > 0)
        return false;
    if (editor->cursor_line < 0 || editor->cursor_line >= editor->line_count)
        return false;

    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    int line_len = line->length > (size_t)INT_MAX ? INT_MAX : (int)line->length;
    int col = editor->cursor_col;
    if (col < 0 || col > line_len)
        return false;
    if (col == 0)
        return false;
    if (!codeeditor_is_whitespace_span(line->text, 0, col))
        return false;

    int replace_end = col;
    if (replace_end < line_len && line->text[replace_end] == '}')
        replace_end++;
    if (!codeeditor_is_whitespace_span(line->text, replace_end, line_len))
        return false;
    replace_end = line_len;

    int opener_line = -1;
    int opener_col = -1;
    int target_indent_len = 0;
    if (codeeditor_find_unmatched_opener_before(
            editor, editor->cursor_line, editor->cursor_col, '{', '}', &opener_line, &opener_col) &&
        opener_line >= 0 && opener_line < editor->line_count) {
        target_indent_len =
            codeeditor_leading_indent_len(editor->lines[opener_line].text,
                                          editor->lines[opener_line].length);
    } else {
        target_indent_len = col;
        if (target_indent_len > 0 && line->text[target_indent_len - 1] == '\t') {
            target_indent_len--;
        } else {
            int spaces_to_remove = editor->tab_width;
            if (spaces_to_remove < 1)
                spaces_to_remove = 1;
            if (spaces_to_remove > 16)
                spaces_to_remove = 16;
            int removed = 0;
            while (target_indent_len > 0 && removed < spaces_to_remove &&
                   line->text[target_indent_len - 1] == ' ') {
                target_indent_len--;
                removed++;
            }
        }
    }
    if (target_indent_len < 0)
        target_indent_len = 0;
    if (target_indent_len > line_len)
        target_indent_len = line_len;

    size_t replacement_len = (size_t)target_indent_len + 1u;
    char *replacement = malloc(replacement_len + 1u);
    if (!replacement)
        return false;
    if (target_indent_len > 0)
        memcpy(replacement, line->text, (size_t)target_indent_len);
    replacement[target_indent_len] = '}';
    replacement[target_indent_len + 1] = '\0';

    vg_edit_target_t target = {0};
    int target_count = 0;
    add_edit_target(&target,
                    &target_count,
                    0,
                    editor->cursor_line,
                    editor->cursor_col,
                    editor->cursor_line,
                    0,
                    editor->cursor_line,
                    replace_end);
    apply_edit_targets(editor, &target, target_count, replacement);
    free(replacement);
    return true;
}

static bool codeeditor_try_skip_pair_closer(vg_codeeditor_t *editor, char ch) {
    if (!editor || editor->has_selection || editor->extra_cursor_count > 0)
        return false;
    if (!codeeditor_is_pair_closer(ch))
        return false;
    if (editor->cursor_line < 0 || editor->cursor_line >= editor->line_count)
        return false;
    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    if (editor->cursor_col < 0 || editor->cursor_col >= (int)line->length)
        return false;
    if (line->text[editor->cursor_col] != ch)
        return false;
    editor->cursor_col++;
    editor->cursor_visible = true;
    editor->base.needs_paint = true;
    return true;
}

/// @brief Decide whether auto-pair insertion is appropriate at one caret.
/// @details The check intentionally stays conservative: it refuses to insert a
///          pair inside comments/strings, before identifiers, or for quotes in
///          the middle of identifier text. Returning false lets the caller fall
///          back to inserting the literal opener.
static bool codeeditor_should_insert_pair_at(vg_codeeditor_t *editor,
                                             int line_index,
                                             int col,
                                             char opener) {
    if (!editor || line_index < 0 || line_index >= editor->line_count)
        return false;
    char closer = codeeditor_closing_for_opener(opener);
    if (closer == '\0')
        return false;
    if (!codeeditor_position_is_code_context(editor, line_index, col))
        return false;

    vg_code_line_t *line = &editor->lines[line_index];
    char prev = col > 0 && col <= (int)line->length ? line->text[col - 1] : '\0';
    char next = col >= 0 && col < (int)line->length ? line->text[col] : '\0';
    if (next != '\0' && codeeditor_is_identifier_byte(next))
        return false;
    if ((opener == '"' || opener == '\'') && codeeditor_is_identifier_byte(prev))
        return false;
    return true;
}

/// @brief Return whether an edit target covers selected text rather than a caret.
static bool codeeditor_edit_target_has_range(const vg_edit_target_t *target) {
    return target && compare_positions(
                         target->start_line, target->start_col, target->end_line, target->end_col) != 0;
}

/// @brief Build the replacement text for one pair operation.
/// @details Selection targets are wrapped as `<opener><selected><closer>`.
///          Caret targets insert either the opener/closer pair or the literal
///          opener depending on @p use_pair. The returned string is heap-owned.
static char *codeeditor_pair_replacement_for_target(vg_codeeditor_t *editor,
                                                    const vg_edit_target_t *target,
                                                    char opener,
                                                    char closer,
                                                    bool use_pair) {
    if (!target)
        return NULL;
    if (codeeditor_edit_target_has_range(target)) {
        size_t selected_len = 0;
        char *selected = copy_text_range_len(editor,
                                             target->start_line,
                                             target->start_col,
                                             target->end_line,
                                             target->end_col,
                                             &selected_len);
        if (!selected)
            return NULL;
        if (selected_len > SIZE_MAX - 3u) {
            free(selected);
            return NULL;
        }
        char *replacement = malloc(selected_len + 3u);
        if (!replacement) {
            free(selected);
            return NULL;
        }
        replacement[0] = opener;
        memcpy(replacement + 1, selected, selected_len);
        replacement[selected_len + 1u] = closer;
        replacement[selected_len + 2u] = '\0';
        free(selected);
        return replacement;
    }

    char *replacement = malloc(use_pair ? 3u : 2u);
    if (!replacement)
        return NULL;
    replacement[0] = opener;
    if (use_pair) {
        replacement[1] = closer;
        replacement[2] = '\0';
    } else {
        replacement[1] = '\0';
    }
    return replacement;
}

/// @brief Insert or wrap an automatic pair for every active cursor.
/// @details Supports mixed selections and carets. At carets that pass the
///          context check, the cursor is restored between the inserted pair.
///          At selection targets, the selected text is wrapped and the cursor
///          lands after the closing delimiter.
static bool codeeditor_try_insert_pair(vg_codeeditor_t *editor, char opener) {
    if (!editor)
        return false;
    char closer = codeeditor_closing_for_opener(opener);
    if (closer == '\0')
        return false;

    const int max_targets = 1 + editor->extra_cursor_count;
    vg_edit_target_t *targets = calloc((size_t)max_targets, sizeof(vg_edit_target_t));
    if (!targets) {
        free(targets);
        return false;
    }

    int target_count = 0;
    bool should_handle = false;
    if (editor->has_selection) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->selection.start_line,
                        editor->selection.start_col,
                        editor->selection.end_line,
                        editor->selection.end_col);
        should_handle = true;
    } else {
        bool use_pair =
            codeeditor_should_insert_pair_at(editor, editor->cursor_line, editor->cursor_col, opener);
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line,
                        editor->cursor_col);
        should_handle = should_handle || use_pair;
    }

    for (int i = 0; i < editor->extra_cursor_count; i++) {
        struct vg_extra_cursor *cursor = &editor->extra_cursors[i];
        if (cursor->has_selection) {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->selection.start_line,
                            cursor->selection.start_col,
                            cursor->selection.end_line,
                            cursor->selection.end_col);
            should_handle = true;
        } else {
            bool use_pair = codeeditor_should_insert_pair_at(editor, cursor->line, cursor->col, opener);
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->line,
                            cursor->col,
                            cursor->line,
                            cursor->col);
            should_handle = should_handle || use_pair;
        }
    }

    if (!should_handle || target_count <= 0) {
        free(targets);
        return false;
    }

    qsort(targets, (size_t)target_count, sizeof(*targets), edit_target_compare_desc);
    bool history_group_open = false;
    if (editor->history && target_count > 1) {
        edit_history_begin_group(editor->history);
        history_group_open = true;
    }
    for (int i = 0; i < target_count; i++) {
        bool has_range = codeeditor_edit_target_has_range(&targets[i]);
        bool use_pair = has_range || codeeditor_should_insert_pair_at(
                                       editor, targets[i].start_line, targets[i].start_col, opener);
        char *replacement =
            codeeditor_pair_replacement_for_target(editor, &targets[i], opener, closer, use_pair);
        if (!replacement)
            continue;
        apply_edit_targets(editor, &targets[i], 1, replacement);
        if (!has_range && use_pair) {
            set_cursor_position_by_id(
                editor, targets[i].cursor_id, targets[i].start_line, targets[i].start_col + 1);
            edit_history_update_latest_cursor_after(
                editor->history, targets[i].start_line, targets[i].start_col + 1);
        }
        free(replacement);
    }
    if (history_group_open)
        edit_history_end_group(editor->history);

    ensure_cursor_visible(editor);
    editor->cursor_visible = true;
    editor->base.needs_paint = true;
    free(targets);
    return true;
}

// Insert n raw bytes at the current cursor position and advance cursor_col by n.
VG_UNUSED static void insert_bytes(vg_codeeditor_t *editor, const char *bytes, size_t n) {
    if (!editor || !bytes || !n)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    vg_code_line_t *line = &editor->lines[editor->cursor_line];

    if (line->length > SIZE_MAX - 1 || n > SIZE_MAX - line->length - 1 ||
        line->length > (size_t)INT_MAX || n > (size_t)INT_MAX - line->length)
        return;
    if (!ensure_text_capacity(line, line->length + n + 1))
        return;

    memmove(line->text + editor->cursor_col + n,
            line->text + editor->cursor_col,
            line->length - editor->cursor_col + 1);

    memcpy(line->text + editor->cursor_col, bytes, n);
    line->length += n;
    editor->cursor_col += (int)n;
    editor->modified = true;
    line->modified = true;
    codeeditor_invalidate_line_highlight(editor, editor->cursor_line);
}

VG_UNUSED static void insert_char(vg_codeeditor_t *editor, char c) {
    if (!editor)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    vg_code_line_t *line = &editor->lines[editor->cursor_line];

    if (line->length > SIZE_MAX - 2 || line->length >= (size_t)INT_MAX)
        return;
    if (!ensure_text_capacity(line, line->length + 2))
        return;

    // Make room for new char
    memmove(line->text + editor->cursor_col + 1,
            line->text + editor->cursor_col,
            line->length - editor->cursor_col + 1);

    line->text[editor->cursor_col] = c;
    line->length++;
    editor->cursor_col++;
    editor->modified = true;
    line->modified = true;
    codeeditor_invalidate_line_highlight(editor, editor->cursor_line);
}

/// @brief Inserts @p n bytes from @p bytes into @p line_idx at *col, advancing *col past the
/// inserted content.
static bool codeeditor_insert_bytes_at(
    vg_codeeditor_t *editor, int line_idx, int *col, const char *bytes, size_t n) {
    if (!editor || !bytes || !col || n == 0 || line_idx < 0 || line_idx >= editor->line_count)
        return false;

    vg_code_line_t *line = &editor->lines[line_idx];
    if (!ensure_line_text_exists(line))
        return false;
    if (*col < 0)
        *col = 0;
    int line_len = line->length > (size_t)INT_MAX ? INT_MAX : (int)line->length;
    if (*col > line_len)
        *col = line_len;

    if (line->length > SIZE_MAX - 1 || n > SIZE_MAX - line->length - 1 ||
        line->length > (size_t)INT_MAX || n > (size_t)INT_MAX - line->length)
        return false;
    if (n > (size_t)(INT_MAX - *col))
        return false;
    if (!ensure_text_capacity(line, line->length + n + 1))
        return false;

    memmove(line->text + *col + n, line->text + *col, line->length - (size_t)*col + 1);
    memcpy(line->text + *col, bytes, n);
    line->length += n;
    *col += (int)n;
    line->modified = true;
    codeeditor_invalidate_line_highlight(editor, line_idx);
    return true;
}

/// @brief Splits @p line_idx at @p col by inserting a new empty line and moving trailing text to
/// it.
static bool codeeditor_split_line_at(vg_codeeditor_t *editor, int line_idx, int col) {
    if (!editor || line_idx < 0 || line_idx >= editor->line_count)
        return false;
    if (editor->line_count == INT_MAX)
        return false;
    if (!ensure_line_capacity(editor, editor->line_count + 1))
        return false;

    vg_code_line_t *current = &editor->lines[line_idx];
    if (!ensure_line_text_exists(current))
        return false;
    if (col < 0)
        col = 0;
    int current_len = current->length > (size_t)INT_MAX ? INT_MAX : (int)current->length;
    if (col > current_len)
        col = current_len;

    size_t split_col = (size_t)col;
    size_t remaining = current->length - split_col;
    vg_code_line_t new_line;
    if (!init_line_text(&new_line, current->text + split_col, remaining))
        return false;

    if (line_idx + 1 < editor->line_count) {
        memmove(&editor->lines[line_idx + 2],
                &editor->lines[line_idx + 1],
                (size_t)(editor->line_count - line_idx - 1) * sizeof(vg_code_line_t));
    }
    editor->lines[line_idx + 1] = new_line;
    current = &editor->lines[line_idx];
    current->text[split_col] = '\0';
    current->length = split_col;
    current->modified = true;
    editor->lines[line_idx + 1].modified = true;
    codeeditor_invalidate_line_highlight(editor, line_idx);
    codeeditor_invalidate_line_highlight(editor, line_idx + 1);
    editor->line_count++;
    return true;
}

VG_UNUSED static void insert_newline(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    if (!codeeditor_split_line_at(editor, editor->cursor_line, editor->cursor_col))
        return;

    editor->cursor_line++;
    editor->cursor_col = 0;
    editor->modified = true;

    vg_codeeditor_refresh_layout_state(editor);
}

VG_UNUSED static void delete_char_backward(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    if (editor->cursor_col > 0) {
        vg_code_line_t *line = &editor->lines[editor->cursor_line];
        memmove(line->text + editor->cursor_col - 1,
                line->text + editor->cursor_col,
                line->length - editor->cursor_col + 1);
        line->length--;
        editor->cursor_col--;
        editor->modified = true;
        line->modified = true;
        codeeditor_invalidate_line_highlight(editor, editor->cursor_line);
    } else if (editor->cursor_line > 0) {
        // Join with previous line
        vg_code_line_t *current = &editor->lines[editor->cursor_line];
        vg_code_line_t *prev = &editor->lines[editor->cursor_line - 1];
        int old_line_count = editor->line_count;

        size_t new_col = prev->length;

        if (prev->length > (size_t)INT_MAX || current->length > (size_t)INT_MAX - prev->length ||
            !ensure_text_capacity(prev, prev->length + current->length + 1))
            return;

        memcpy(prev->text + prev->length, current->text, current->length + 1);
        prev->length += current->length;

        // Free current line
        free_line(current);

        // Move lines up
        memmove(&editor->lines[editor->cursor_line],
                &editor->lines[editor->cursor_line + 1],
                (editor->line_count - editor->cursor_line - 1) * sizeof(vg_code_line_t));

        editor->line_count--;
        if (old_line_count > editor->line_count) {
            memset(&editor->lines[editor->line_count], 0, sizeof(vg_code_line_t));
        }
        editor->cursor_line--;
        editor->cursor_col = (int)new_col;
        editor->modified = true;
        prev->modified = true;
        codeeditor_invalidate_line_highlight(editor, editor->cursor_line);

        vg_codeeditor_refresh_layout_state(editor);
    }
}

/// @brief Find the two-character delete range for Backspace between an automatic pair.
/// @details When the caret is between an automatic ASCII pair, the range spans
///          both delimiters so a single Backspace removes the pair together.
static bool codeeditor_backspace_pair_range(vg_codeeditor_t *editor,
                                            int line_index,
                                            int col,
                                            int *out_start_col,
                                            int *out_end_col) {
    if (out_start_col)
        *out_start_col = col;
    if (out_end_col)
        *out_end_col = col;
    if (!editor || line_index < 0 || line_index >= editor->line_count || col <= 0)
        return false;
    vg_code_line_t *line = &editor->lines[line_index];
    if (col >= (int)line->length)
        return false;
    int prev_col = codeeditor_prev_byte_boundary(editor, line_index, col);
    if (prev_col < 0 || prev_col >= col)
        return false;
    char opener = line->text[prev_col];
    char closer = line->text[col];
    if (!codeeditor_chars_are_pair(opener, closer))
        return false;
    if (out_start_col)
        *out_start_col = prev_col;
    if (out_end_col)
        *out_end_col = col + 1;
    return true;
}

/// @brief Performs a Backspace deletion across all cursors.
/// @details Deletes selections, removes paired delimiters when the caret is
///          between them, removes the previous UTF-8 codepoint otherwise, or
///          joins with the previous line at column zero.
static void delete_backspace_targets(vg_codeeditor_t *editor) {
    if (!editor)
        return;

    const int max_targets = 1 + editor->extra_cursor_count;
    vg_edit_target_t *targets = calloc((size_t)max_targets, sizeof(vg_edit_target_t));
    if (!targets)
        return;

    int target_count = 0;
    if (editor->has_selection) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->selection.start_line,
                        editor->selection.start_col,
                        editor->selection.end_line,
                        editor->selection.end_col);
    } else if (editor->cursor_col > 0) {
        int delete_start = 0;
        int delete_end = 0;
        if (!codeeditor_backspace_pair_range(editor,
                                             editor->cursor_line,
                                             editor->cursor_col,
                                             &delete_start,
                                             &delete_end)) {
            delete_start =
                codeeditor_prev_byte_boundary(editor, editor->cursor_line, editor->cursor_col);
            delete_end = editor->cursor_col;
        }
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line,
                        delete_start,
                        editor->cursor_line,
                        delete_end);
    } else if (editor->cursor_line > 0) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line - 1,
                        (int)editor->lines[editor->cursor_line - 1].length,
                        editor->cursor_line,
                        0);
    }

    for (int i = 0; i < editor->extra_cursor_count; i++) {
        struct vg_extra_cursor *cursor = &editor->extra_cursors[i];
        if (cursor->has_selection) {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->selection.start_line,
                            cursor->selection.start_col,
                            cursor->selection.end_line,
                            cursor->selection.end_col);
        } else if (cursor->col > 0) {
            int delete_start = 0;
            int delete_end = 0;
            if (!codeeditor_backspace_pair_range(
                    editor, cursor->line, cursor->col, &delete_start, &delete_end)) {
                delete_start = codeeditor_prev_byte_boundary(editor, cursor->line, cursor->col);
                delete_end = cursor->col;
            }
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->line,
                            delete_start,
                            cursor->line,
                            delete_end);
        } else if (cursor->line > 0) {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->line - 1,
                            (int)editor->lines[cursor->line - 1].length,
                            cursor->line,
                            0);
        }
    }

    qsort(targets, (size_t)target_count, sizeof(*targets), edit_target_compare_desc);
    apply_edit_targets(editor, targets, target_count, NULL);
    free(targets);
}

/// @brief VTable handle_event: routes mouse (click/drag/scroll/scrollbar) and keyboard events to
/// the appropriate editor action.
static bool codeeditor_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    switch (event->type) {
        case VG_EVENT_MOUSE_UP:
            if (editor->scrollbar_dragging) {
                editor->scrollbar_dragging = false;
                if (vg_widget_get_input_capture() == widget)
                    vg_widget_release_input_capture();
                widget->needs_paint = true;
                return true;
            }
            if (editor->selection_dragging) {
                editor->selection_dragging = false;
                if (vg_widget_get_input_capture() == widget)
                    vg_widget_release_input_capture();
                widget->needs_paint = true;
                return true;
            }
            break;

        case VG_EVENT_MOUSE_MOVE:
            if (editor->scrollbar_dragging) {
                float thumb_y = 0.0f;
                float thumb_height = 0.0f;
                float max_scroll = 0.0f;
                float thumb_travel = 0.0f;
                if (codeeditor_get_scrollbar_metrics(editor,
                                                     widget,
                                                     NULL,
                                                     &thumb_y,
                                                     &thumb_height,
                                                     &max_scroll,
                                                     &thumb_travel) &&
                    thumb_travel > 0.0f && max_scroll > 0.0f) {
                    float target_thumb_y = event->mouse.y - editor->scrollbar_drag_offset;
                    if (target_thumb_y < 0.0f)
                        target_thumb_y = 0.0f;
                    if (target_thumb_y > thumb_travel)
                        target_thumb_y = thumb_travel;
                    editor->scroll_y = (target_thumb_y / thumb_travel) * max_scroll;
                }
                codeeditor_clamp_scroll(editor, widget);
                widget->needs_paint = true;
                return true;
            }
            if (editor->selection_dragging) {
                int line = 0;
                int col = 0;
                codeeditor_local_point_to_position(
                    editor, widget, event->mouse.x, event->mouse.y, &line, &col);
                editor->selection.start_line = editor->selection_anchor_line;
                editor->selection.start_col = editor->selection_anchor_col;
                editor->selection.end_line = line;
                editor->selection.end_col = col;
                editor->cursor_line = line;
                editor->cursor_col = col;
                editor->has_selection = compare_positions(editor->selection.start_line,
                                                          editor->selection.start_col,
                                                          editor->selection.end_line,
                                                          editor->selection.end_col) != 0;
                editor->cursor_visible = true;
                ensure_cursor_visible(editor);
                widget->needs_paint = true;
                return true;
            }
            break;

        case VG_EVENT_MOUSE_DOWN: {
            float scrollbar_width = CODEEDITOR_SCROLLBAR_WIDTH;

            // Check if click is on scrollbar
            float track_x = 0.0f;
            float thumb_y = 0.0f;
            float thumb_height = 0.0f;
            float max_scroll = 0.0f;
            float thumb_travel = 0.0f;
            if (codeeditor_get_scrollbar_metrics(editor,
                                                 widget,
                                                 &track_x,
                                                 &thumb_y,
                                                 &thumb_height,
                                                 &max_scroll,
                                                 &thumb_travel) &&
                event->mouse.x >= track_x && event->mouse.x < track_x + scrollbar_width &&
                event->mouse.y >= 0.0f && event->mouse.y < widget->height) {
                if (event->mouse.y >= thumb_y && event->mouse.y <= thumb_y + thumb_height) {
                    // Click on thumb — start drag
                    editor->scrollbar_dragging = true;
                    editor->scrollbar_drag_offset = event->mouse.y - thumb_y;
                    editor->scrollbar_drag_start_scroll = editor->scroll_y;
                    vg_widget_set_input_capture(widget);
                } else {
                    // Click on track — jump to position
                    float target_thumb_y = event->mouse.y - thumb_height * 0.5f;
                    if (target_thumb_y < 0.0f)
                        target_thumb_y = 0.0f;
                    if (target_thumb_y > thumb_travel)
                        target_thumb_y = thumb_travel;
                    editor->scroll_y =
                        thumb_travel > 0.0f ? (target_thumb_y / thumb_travel) * max_scroll : 0.0f;
                    codeeditor_clamp_scroll(editor, widget);
                }
                widget->needs_paint = true;
                return true;
            }

            if (editor->gutter_width > 0.0f && event->mouse.x < editor->gutter_width) {
                int line = 0;
                codeeditor_local_point_to_position(
                    editor, widget, event->mouse.x, event->mouse.y, &line, NULL);
                float line_number_gutter_width = codeeditor_line_number_gutter_width(editor);
                float fold_gutter_width = codeeditor_fold_gutter_width(editor);

                if (editor->show_fold_gutter && fold_gutter_width > 0.0f &&
                    event->mouse.x >= line_number_gutter_width) {
                    struct vg_fold_region *region =
                        codeeditor_fold_region_starting_at_mut(editor, line);
                    if (region) {
                        region->folded = !region->folded;
                        vg_codeeditor_refresh_layout_state(editor);
                        return true;
                    }
                }

                int slot = -1;
                for (int i = 0; i < editor->gutter_icon_count; i++) {
                    if (editor->gutter_icons[i].line == line) {
                        slot = editor->gutter_icons[i].type;
                        break;
                    }
                }
                editor->gutter_clicked = true;
                editor->gutter_click_read = false;
                editor->gutter_clicked_line = line;
                editor->gutter_clicked_slot = slot;
                widget->needs_paint = true;
                return true;
            }

            int line = 0;
            int col = 0;
            codeeditor_local_point_to_position(
                editor, widget, event->mouse.x, event->mouse.y, &line, &col);
            uint32_t mods = event->modifiers;
            bool additive_click = (mods & (VG_MOD_CTRL | VG_MOD_SUPER)) != 0;
            bool range_click = (mods & VG_MOD_SHIFT) != 0;
            if (additive_click) {
                codeeditor_add_extra_cursor_at(editor, line, col);
                editor->cursor_visible = true;
                widget->needs_paint = true;
                return true;
            }
            if (range_click) {
                editor->selection.start_line = editor->cursor_line;
                editor->selection.start_col = editor->cursor_col;
                editor->selection.end_line = line;
                editor->selection.end_col = col;
                editor->cursor_line = line;
                editor->cursor_col = col;
                editor->has_selection = compare_positions(editor->selection.start_line,
                                                          editor->selection.start_col,
                                                          editor->selection.end_line,
                                                          editor->selection.end_col) != 0;
                clear_extra_cursor_selections(editor);
                editor->cursor_visible = true;
                widget->needs_paint = true;
                return true;
            }
            editor->cursor_line = line;
            editor->cursor_col = col;
            editor->has_selection = false;
            clear_extra_cursor_selections(editor);
            if (event->mouse.button == VG_MOUSE_LEFT) {
                editor->selection_dragging = true;
                editor->selection_anchor_line = line;
                editor->selection_anchor_col = col;
                vg_widget_set_input_capture(widget);
            }
            editor->cursor_visible = true;
            widget->needs_paint = true;
            return true;
        }

        case VG_EVENT_KEY_DOWN: {
            // Check for modifier key shortcuts first
            uint32_t mods = event->modifiers;
            bool has_ctrl = (mods & VG_MOD_CTRL) != 0 || (mods & VG_MOD_SUPER) != 0; // Ctrl or Cmd

            // Clipboard and editing shortcuts (Ctrl/Cmd + key)
            if (has_ctrl) {
                switch (event->key.key) {
                    case VG_KEY_C: // Copy
                        vg_codeeditor_copy(editor);
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_X: // Cut
                        if (!editor->read_only)
                            vg_codeeditor_cut(editor);
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_V: // Paste
                        if (!editor->read_only)
                            vg_codeeditor_paste(editor);
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_Z: // Undo
                        if (!editor->read_only) {
                            if ((event->modifiers & VG_MOD_SHIFT) != 0)
                                vg_codeeditor_redo(editor);
                            else
                                vg_codeeditor_undo(editor);
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_Y: // Redo
                        if (!editor->read_only) {
                            vg_codeeditor_redo(editor);
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_A: // Select all
                        vg_codeeditor_select_all(editor);
                        widget->needs_paint = true;
                        return true;

                    default:
                        break;
                }
            }

            if (editor->read_only) {
                // Navigation only
                bool handled_key = true;
                switch (event->key.key) {
                    case VG_KEY_UP:
                        codeeditor_move_cursor_vertical(editor, widget, -1);
                        break;
                    case VG_KEY_DOWN:
                        codeeditor_move_cursor_vertical(editor, widget, 1);
                        break;
                    case VG_KEY_LEFT:
                        if (editor->cursor_col > 0)
                            editor->cursor_col = codeeditor_prev_byte_boundary(
                                editor, editor->cursor_line, editor->cursor_col);
                        break;
                    case VG_KEY_RIGHT:
                        if (editor->cursor_col < (int)editor->lines[editor->cursor_line].length)
                            editor->cursor_col = codeeditor_next_byte_boundary(
                                editor, editor->cursor_line, editor->cursor_col);
                        break;
                    case VG_KEY_PAGE_UP: {
                        int visible_lines = (int)(widget->height / editor->line_height);
                        if (visible_lines < 1)
                            visible_lines = 1;
                        codeeditor_move_cursor_vertical(editor, widget, -visible_lines);
                        break;
                    }
                    case VG_KEY_PAGE_DOWN: {
                        int visible_lines = (int)(widget->height / editor->line_height);
                        if (visible_lines < 1)
                            visible_lines = 1;
                        codeeditor_move_cursor_vertical(editor, widget, visible_lines);
                        break;
                    }
                    default:
                        handled_key = false;
                        break;
                }
                if (!handled_key)
                    return false;
                ensure_cursor_visible(editor);
                widget->needs_paint = true;
                return true;
            }

            bool handled_key = true;
            switch (event->key.key) {
                case VG_KEY_UP:
                    codeeditor_move_cursor_vertical(editor, widget, -1);
                    break;
                case VG_KEY_DOWN:
                    codeeditor_move_cursor_vertical(editor, widget, 1);
                    break;
                case VG_KEY_LEFT:
                    if (editor->cursor_col > 0) {
                        editor->cursor_col = codeeditor_prev_byte_boundary(
                            editor, editor->cursor_line, editor->cursor_col);
                    } else if (editor->cursor_line > 0) {
                        editor->cursor_line--;
                        editor->cursor_col = (int)editor->lines[editor->cursor_line].length;
                    }
                    break;
                case VG_KEY_RIGHT:
                    if (editor->cursor_col < (int)editor->lines[editor->cursor_line].length) {
                        editor->cursor_col = codeeditor_next_byte_boundary(
                            editor, editor->cursor_line, editor->cursor_col);
                    } else if (editor->cursor_line < editor->line_count - 1) {
                        editor->cursor_line++;
                        editor->cursor_col = 0;
                    }
                    break;
                case VG_KEY_HOME:
                    if (editor->cursor_line >= 0 && editor->cursor_line < editor->line_count) {
                        vg_code_line_t *line = &editor->lines[editor->cursor_line];
                        int first_text_col =
                            codeeditor_leading_indent_len(line->text, line->length);
                        editor->cursor_col =
                            editor->cursor_col == first_text_col ? 0 : first_text_col;
                    } else {
                        editor->cursor_col = 0;
                    }
                    break;
                case VG_KEY_END:
                    if (editor->cursor_line >= 0 && editor->cursor_line < editor->line_count) {
                        vg_code_line_t *line = &editor->lines[editor->cursor_line];
                        int end_col = (int)line->length;
                        int last_text_col = end_col;
                        while (last_text_col > 0 && (line->text[last_text_col - 1] == ' ' ||
                                                     line->text[last_text_col - 1] == '\t'))
                            last_text_col--;
                        editor->cursor_col =
                            editor->cursor_col == last_text_col ? end_col : last_text_col;
                    }
                    break;
                case VG_KEY_PAGE_UP: {
                    int visible_lines = (int)(widget->height / editor->line_height);
                    if (visible_lines < 1)
                        visible_lines = 1;
                    codeeditor_move_cursor_vertical(editor, widget, -visible_lines);
                    break;
                }
                case VG_KEY_PAGE_DOWN: {
                    int visible_lines = (int)(widget->height / editor->line_height);
                    if (visible_lines < 1)
                        visible_lines = 1;
                    codeeditor_move_cursor_vertical(editor, widget, visible_lines);
                    break;
                }
                case VG_KEY_BACKSPACE:
                    delete_backspace_targets(editor);
                    break;
                case VG_KEY_ENTER:
                    codeeditor_insert_newline_with_indent(editor);
                    break;
                case VG_KEY_TAB:
                    if (editor->use_spaces) {
                        char spaces[32];
                        int space_count = editor->tab_width;
                        if (space_count < 0)
                            space_count = 0;
                        if (space_count >= (int)sizeof(spaces))
                            space_count = (int)sizeof(spaces) - 1;
                        memset(spaces, ' ', (size_t)space_count);
                        spaces[space_count] = '\0';
                        vg_codeeditor_insert_text(editor, spaces);
                    } else {
                        vg_codeeditor_insert_text(editor, "\t");
                    }
                    break;
                default:
                    handled_key = false;
                    break;
            }

            if (!handled_key)
                return false;

            // Ensure cursor stays visible after movement
            ensure_cursor_visible(editor);

            editor->cursor_visible = true;
            editor->has_selection = false;
            clear_extra_cursor_selections(editor);
            widget->needs_paint = true;
            return true;
        }

        case VG_EVENT_KEY_CHAR: {
            if (!codeeditor_key_char_allows_text(event))
                return false;
            uint32_t cp = event->key.codepoint;
            bool printable = codeeditor_codepoint_is_text(cp);
            if (!editor->read_only && printable) {
                if (cp < 0x80) {
                    char text[2] = {(char)cp, '\0'};
                    if (text[0] == '}' && codeeditor_try_format_closing_brace(editor)) {
                        ensure_cursor_visible(editor);
                        widget->needs_paint = true;
                        return true;
                    }
                    if (codeeditor_try_skip_pair_closer(editor, text[0])) {
                        ensure_cursor_visible(editor);
                        widget->needs_paint = true;
                        return true;
                    }
                    if (codeeditor_try_insert_pair(editor, text[0])) {
                        ensure_cursor_visible(editor);
                        widget->needs_paint = true;
                        return true;
                    }
                    vg_codeeditor_insert_text(editor, text);
                } else {
                    char utf8[5] = {0};
                    int n = encode_utf8(cp, utf8);
                    if (n > 0) {
                        utf8[n] = '\0';
                        vg_codeeditor_insert_text(editor, utf8);
                    }
                }
                ensure_cursor_visible(editor);
                widget->needs_paint = true;
            }
            return true;
        }

        case VG_EVENT_MOUSE_WHEEL:
            editor->scroll_y -=
                event->wheel.delta_y * editor->line_height * CODEEDITOR_MOUSE_WHEEL_LINES;
            codeeditor_clamp_scroll(editor, widget);
            widget->needs_paint = true;
            return true;

        default:
            break;
    }

    return false;
}

/// @brief VTable can_focus: returns true when the editor is both enabled and visible.
static bool codeeditor_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

/// @brief VTable on_focus: on focus gain, resets the cursor blink state and triggers a repaint.
static void codeeditor_on_focus(vg_widget_t *widget, bool gained) {
    vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;
    if (gained) {
        editor->cursor_visible = true;
        editor->cursor_blink_time = 0;
        widget->needs_paint = true;
    }
}
