//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_codeeditor.c
// Purpose: Runtime bindings for the ViperGUI CodeEditor widget. Implements
//   syntax highlighting (Zia and BASIC keyword/type color tables), gutter icon
//   management, code folding, multiple cursors, edit operations, and completion
//   helpers. MessageBox, FileDialog, and FindBar live in separate files.
//
// Key invariants:
//   - Syntax highlight colors use ARGB 0xAARRGGBB format matching the VS Code
//     dark-theme palette defined at the top of this file.
//   - rt_codeeditor_get_selected_text() returns a freshly allocated C string
//     (from vg_codeeditor_get_selection); the caller must free it.
//
// Ownership/Lifetime:
//   - Selected-text C strings are malloc'd by the vg layer; the caller frees them.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/runtime/graphics/rt_gui_messagebox.c (MessageBox dialogs),
//        src/runtime/graphics/rt_gui_filedialog.c (FileDialog dialogs),
//        src/runtime/graphics/rt_gui_findbar.c (FindBar widget),
//        src/lib/gui/src/widgets/vg_codeeditor.c (underlying widget)
//
//===----------------------------------------------------------------------===//

#include "rt_error.h"
#include "rt_gui_internal.h"
#include "rt_pixels.h"
#include "rt_platform.h"
#include <ctype.h>

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// CodeEditor Enhancements - Syntax Highlighting (Phase 4)
//=============================================================================

// VS Code dark-theme inspired palette (ARGB 0xAARRGGBB)
#define SYN_COLOR_DEFAULT 0xFFD4D4D4u  // light grey
#define SYN_COLOR_KEYWORD 0xFF569CD6u  // blue
#define SYN_COLOR_TYPE 0xFF4EC9B0u     // teal
#define SYN_COLOR_STRING 0xFFCE9178u   // orange
#define SYN_COLOR_COMMENT 0xFF6A9955u  // green
#define SYN_COLOR_NUMBER 0xFFB5CEA8u   // light green
#define SYN_COLOR_FUNCTION 0xFFDCDCAAu // yellow — function/method calls

/// @brief Set `n` adjacent slots in the per-character `colors` array to `color`.
///
/// Used by every tokenizer to paint a span of characters (a keyword,
/// string literal, comment) the same color in one sweep.
static void syn_fill(uint32_t *colors, size_t pos, size_t n, uint32_t color) {
    for (size_t i = 0; i < n; i++)
        colors[pos + i] = color;
}

/// @brief Pick the color for a token kind, honoring per-editor overrides.
///
/// Each editor instance can override the default theme colors via
/// `SetTokenColor`. If no override is set, returns the supplied
/// `fallback` (which is the VS Code dark-theme default).
///
/// Token type indices: 0=default, 1=keyword, 2=type, 3=string,
/// 4=comment, 5=number.
static uint32_t syn_color(vg_codeeditor_t *ce, int token_type, uint32_t fallback) {
    if (ce && token_type >= 0 && token_type < 6 && ce->token_colors[token_type])
        return ce->token_colors[token_type];
    return fallback;
}

/// @brief Safe-cast an opaque handle to a live CodeEditor widget.
/// @return The code editor, or NULL if @p editor is not a live one.
static vg_codeeditor_t *rt_codeeditor_handle_checked(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    return (vg_codeeditor_t *)rt_gui_widget_handle_checked_type(editor, VG_WIDGET_CODEEDITOR);
}

static void rt_codeeditor_invalidate_syntax_cache(vg_codeeditor_t *ce) {
    if (!ce)
        return;
    ce->highlight_generation =
        ce->highlight_generation == UINT64_MAX ? 1 : ce->highlight_generation + 1;
    for (int i = 0; i < ce->line_count; i++) {
        ce->lines[i].highlight_generation = 0;
        ce->lines[i].syntax_state_generation = 0;
    }
    ce->base.needs_paint = true;
}

/// @brief Validate a gutter-marker slot index (0–3) and report it via @p out_type.
/// @return 1 if the slot is in range; 0 otherwise (leaving `*out_type` untouched).
static int rt_codeeditor_gutter_slot_checked(int64_t slot, int *out_type) {
    if (slot < 0 || slot > 3)
        return 0;
    if (out_type)
        *out_type = (int)slot;
    return 1;
}

/// @brief Byte length of a line clamped to INT_MAX; 0 for an out-of-range line index.
static int rt_codeeditor_line_length_i32(const vg_codeeditor_t *ce, int line) {
    if (!ce || line < 0 || line >= ce->line_count)
        return 0;
    return ce->lines[line].length > (size_t)INT_MAX ? INT_MAX : (int)ce->lines[line].length;
}

/// @brief Byte length (1–4) of the UTF-8 sequence beginning at @p p.
/// @details Validates continuation bytes and respects @p remaining so a truncated
///          multibyte sequence at the buffer end cannot over-read. Any malformed
///          lead byte yields 1, guaranteeing forward progress in column-walk loops.
static size_t rt_codeeditor_utf8_span(const char *p, size_t remaining) {
    if (!p || remaining == 0)
        return 0;
    if (*p == '\0')
        return 1;
    const unsigned char *s = (const unsigned char *)p;
    if ((s[0] & 0x80u) == 0)
        return 1;
    if ((s[0] & 0xE0u) == 0xC0u && remaining >= 2 && (s[1] & 0xC0u) == 0x80u)
        return 2;
    if ((s[0] & 0xF0u) == 0xE0u && remaining >= 3 && (s[1] & 0xC0u) == 0x80u &&
        (s[2] & 0xC0u) == 0x80u)
        return 3;
    if ((s[0] & 0xF8u) == 0xF0u && remaining >= 4 && (s[1] & 0xC0u) == 0x80u &&
        (s[2] & 0xC0u) == 0x80u && (s[3] & 0xC0u) == 0x80u)
        return 4;
    return 1;
}

/// @brief Convert a byte offset within a line to a character (codepoint) column.
/// @details The editor stores text as UTF-8 bytes but the scripting API speaks in
///          characters; this walks the line by UTF-8 spans so multibyte glyphs count
///          as one column. @p byte_col is clamped to the line length and never splits
///          a multibyte sequence (it stops at the last whole character that fits).
static int rt_codeeditor_byte_col_to_char_col(const vg_codeeditor_t *ce, int line, int byte_col) {
    if (!ce || line < 0 || line >= ce->line_count || byte_col <= 0)
        return 0;
    const vg_code_line_t *code_line = &ce->lines[line];
    if (byte_col > (int)code_line->length)
        byte_col = (int)code_line->length;
    int chars = 0;
    size_t pos = 0;
    while (pos < (size_t)byte_col) {
        size_t span = rt_codeeditor_utf8_span(code_line->text + pos, code_line->length - pos);
        if (pos + span > (size_t)byte_col)
            break;
        pos += span;
        chars++;
    }
    return chars;
}

/// @brief Convert a character (codepoint) column to a byte offset within a line.
/// @details Inverse of rt_codeeditor_byte_col_to_char_col: advances @p char_col
///          UTF-8 spans (or to end of line), then returns the byte position clamped
///          to INT_MAX.
static int rt_codeeditor_char_col_to_byte_col(const vg_codeeditor_t *ce, int line, int char_col) {
    if (!ce || line < 0 || line >= ce->line_count || char_col <= 0)
        return 0;
    const vg_code_line_t *code_line = &ce->lines[line];
    int chars = 0;
    size_t pos = 0;
    while (pos < code_line->length && chars < char_col) {
        size_t span = rt_codeeditor_utf8_span(code_line->text + pos, code_line->length - pos);
        pos += span;
        chars++;
    }
    return pos > (size_t)INT_MAX ? INT_MAX : (int)pos;
}

/// @brief Lexicographic compare of two (line, col) caret positions.
/// @return -1 if lhs precedes rhs, 1 if it follows, 0 if equal.
static int rt_codeeditor_compare_positions(int lhs_line, int lhs_col, int rhs_line, int rhs_col) {
    if (lhs_line != rhs_line)
        return lhs_line < rhs_line ? -1 : 1;
    if (lhs_col != rhs_col)
        return lhs_col < rhs_col ? -1 : 1;
    return 0;
}

/// @brief Order a selection so `start <= end`, swapping the endpoints if the user
///        dragged backwards. Lets downstream range logic assume a forward span.
static void rt_codeeditor_normalize_selection(vg_selection_t *selection) {
    if (!selection)
        return;
    if (rt_codeeditor_compare_positions(
            selection->start_line, selection->start_col, selection->end_line, selection->end_col) <=
        0)
        return;
    int line = selection->start_line;
    int col = selection->start_col;
    selection->start_line = selection->end_line;
    selection->start_col = selection->end_col;
    selection->end_line = line;
    selection->end_col = col;
}

/// @brief Linear-scan the editor's user-supplied keyword list for an exact match.
///
/// Custom keywords let scripts add domain-specific syntax (e.g., your
/// game's scripting commands) without modifying the built-in tables.
/// Case-sensitive — matches `SetCustomKeywords`'s contract.
static int syn_is_custom_keyword(const char *word, size_t wlen, vg_codeeditor_t *ce) {
    if (!ce || !ce->custom_keywords)
        return 0;
    for (int i = 0; i < ce->custom_keyword_count; i++) {
        if (!ce->custom_keywords[i])
            continue;
        size_t klen = strlen(ce->custom_keywords[i]);
        if (klen == wlen && memcmp(word, ce->custom_keywords[i], wlen) == 0)
            return 1;
    }
    return 0;
}

/// @brief True if `c` may begin an identifier (letter or underscore).
///
/// ASCII-only — Unicode identifiers are not yet supported by the
/// tokenizer (would require a full UTF-8 codepoint classifier).
static int syn_is_id_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/// @brief True if `c` may continue an identifier (id-start chars + digits).
static int syn_is_id_cont(char c) {
    return syn_is_id_start(c) || (c >= '0' && c <= '9');
}

/// @brief Compare `len` chars of `a` and `b` ignoring ASCII case.
///
/// Folds lowercase letters via the bit-flip trick (`a..z` differ from
/// `A..Z` by exactly 0x20). Used for BASIC keyword matching, which is
/// case-insensitive.
static int syn_word_eq_ci(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char ca = (a[i] >= 'a' && a[i] <= 'z') ? a[i] - 32 : a[i];
        char cb = (b[i] >= 'a' && b[i] <= 'z') ? b[i] - 32 : b[i];
        if (ca != cb)
            return 0;
    }
    return 1;
}

/// @brief Match `word[0..wlen]` against a NULL-terminated keyword table (case-sensitive).
///
/// Used for Zia keywords/types — Zia is case-sensitive. Linear scan;
/// keyword tables are short enough that hashing isn't worth it.
static int syn_is_keyword(const char *word, size_t wlen, const char *const *table) {
    for (int i = 0; table[i]; i++) {
        size_t klen = strlen(table[i]);
        if (klen == wlen && memcmp(word, table[i], wlen) == 0)
            return 1;
    }
    return 0;
}

/// @brief Case-insensitive variant of `syn_is_keyword` for BASIC.
static int syn_is_keyword_ci(const char *word, size_t wlen, const char *const *table) {
    for (int i = 0; table[i]; i++) {
        size_t klen = strlen(table[i]);
        if (klen == wlen && syn_word_eq_ci(word, table[i], wlen))
            return 1;
    }
    return 0;
}

// ─── Zia language tokenizer ────────────────────────────────────────────────

// Bridge into fe_zia's authoritative kKeywordTable (52 entries).
// Strong impl in src/frontends/zia/rt_zia_highlight.cpp; weak fallback in
// src/runtime/core/rt_zia_highlight_stub.c returns 0 (no keyword) when fe_zia
// is not linked. The zia binary force-loads fe_zia (see src/CMakeLists.txt)
// so the strong implementation always wins for the IDE.
extern int rt_zia_is_keyword(const char *name, int64_t len);

// Type names highlighted as types (teal). The real lexer doesn't separate
// these from identifiers, but VS Code-style coloring distinguishes the
// common runtime-class names visually. Keep this list curated; kept short
// because the user's biggest pain was missing keywords, not missing types.
static const char *const zia_types[] = {"Integer",
                                        "Boolean",
                                        "String",
                                        "Number",
                                        "Byte",
                                        "List",
                                        "Seq",
                                        "Map",
                                        "Set",
                                        "Stack",
                                        "Queue",
                                        NULL};

static int rt_zia_comment_depth_after_text(const char *line, int depth) {
    if (!line)
        return depth;

    size_t len = strlen(line);
    for (size_t i = 0; i < len;) {
        if (depth > 0) {
            if (i + 1 < len && line[i] == '/' && line[i + 1] == '*') {
                depth++;
                i += 2;
            } else if (i + 1 < len && line[i] == '*' && line[i + 1] == '/') {
                depth--;
                i += 2;
            } else {
                i++;
            }
            continue;
        }

        if (i + 1 < len && line[i] == '/' && line[i + 1] == '/')
            break;
        if (i + 1 < len && line[i] == '/' && line[i + 1] == '*') {
            depth = 1;
            i += 2;
            continue;
        }
        if (line[i] == '"') {
            i++;
            while (i < len) {
                if (line[i] == '\\' && i + 1 < len) {
                    i += 2;
                    continue;
                }
                if (line[i++] == '"')
                    break;
            }
            continue;
        }
        i++;
    }

    return depth;
}

/// @brief Compute the open Zia block-comment nesting depth at the start of
///        @p line_num.
/// @details Caches per-line lexical state so painting a viewport near the end
///          of a large file does not rescan all preceding lines for every
///          visible row.
static int rt_zia_comment_depth_before_line(vg_codeeditor_t *ce, int line_num) {
    if (!ce || line_num <= 0 || !ce->lines)
        return 0;

    int limit = line_num < ce->line_count ? line_num : ce->line_count;
    uint64_t generation = ce->highlight_generation;
    int start = 0;
    int depth = 0;

    for (int i = limit - 1; i >= 0; i--) {
        if (ce->lines[i].syntax_state_generation == generation) {
            start = i + 1;
            depth = ce->lines[i].syntax_state_out;
            break;
        }
    }

    for (int line_index = start; line_index < limit; line_index++) {
        vg_code_line_t *line = &ce->lines[line_index];
        line->syntax_state_in = depth;
        depth = rt_zia_comment_depth_after_text(line->text ? line->text : "", depth);
        line->syntax_state_out = depth;
        line->syntax_state_generation = generation;
        ce->perf_stats.syntax_state_line_scans++;
    }

    return depth;
}

/// @brief Tokenize a Zia source line and write per-character color codes.
///
/// Walks the line once, classifying each span:
///   - `// ...` line comment → green
///   - `/* ... */` block comment → green. Multi-line depth is derived from
///     prior buffer lines so highlighting is independent of render order.
///   - `"..."` string (with `\\` escape handling) → orange
///   - `[0-9]+` number → light green
///   - identifier → keyword (via real Zia keyword table) / type / custom
///     keyword / default lookup
///   - everything else → default color
static void rt_zia_syntax_cb(
    vg_widget_t *editor, int line_num, const char *text, uint32_t *colors, void *user_data) {
    vg_codeeditor_t *ce = (vg_codeeditor_t *)user_data;
    (void)editor;

    uint32_t c_default = syn_color(ce, 0, SYN_COLOR_DEFAULT);
    uint32_t c_keyword = syn_color(ce, 1, SYN_COLOR_KEYWORD);
    uint32_t c_type = syn_color(ce, 2, SYN_COLOR_TYPE);
    uint32_t c_string = syn_color(ce, 3, SYN_COLOR_STRING);
    uint32_t c_comment = syn_color(ce, 4, SYN_COLOR_COMMENT);
    uint32_t c_number = syn_color(ce, 5, SYN_COLOR_NUMBER);

    size_t len = strlen(text);
    size_t i = 0;
    int block_depth = rt_zia_comment_depth_before_line(ce, line_num);
    if (ce && line_num >= 0 && line_num < ce->line_count) {
        ce->lines[line_num].syntax_state_in = block_depth;
    }

    // If we entered this line inside a block comment, paint until we find */
    // (with proper nesting).
    if (block_depth > 0) {
        size_t comment_start = 0;
        while (i < len && block_depth > 0) {
            if (i + 1 < len && text[i] == '/' && text[i + 1] == '*') {
                block_depth++;
                i += 2;
            } else if (i + 1 < len && text[i] == '*' && text[i + 1] == '/') {
                block_depth--;
                i += 2;
            } else {
                i++;
            }
        }
        syn_fill(colors, comment_start, i - comment_start, c_comment);
        if (block_depth > 0) {
            // Block comment continues past this line — done with line.
            if (ce) {
                ce->zia_block_comment_depth = block_depth;
                if (line_num >= 0 && line_num < ce->line_count) {
                    ce->lines[line_num].syntax_state_out = block_depth;
                    ce->lines[line_num].syntax_state_generation = ce->highlight_generation;
                }
            }
            return;
        }
    }

    while (i < len) {
        // Line comment
        if (text[i] == '/' && i + 1 < len && text[i + 1] == '/') {
            syn_fill(colors, i, len - i, c_comment);
            if (ce && line_num >= 0 && line_num < ce->line_count) {
                ce->lines[line_num].syntax_state_out = block_depth;
                ce->lines[line_num].syntax_state_generation = ce->highlight_generation;
            }
            return;
        }

        // Block comment (may continue past end of line)
        if (text[i] == '/' && i + 1 < len && text[i + 1] == '*') {
            size_t start = i;
            block_depth = 1;
            i += 2;
            while (i < len && block_depth > 0) {
                if (i + 1 < len && text[i] == '/' && text[i + 1] == '*') {
                    block_depth++;
                    i += 2;
                } else if (i + 1 < len && text[i] == '*' && text[i + 1] == '/') {
                    block_depth--;
                    i += 2;
                } else {
                    i++;
                }
            }
            syn_fill(colors, start, i - start, c_comment);
            continue;
        }

        // String literal
        if (text[i] == '"') {
            size_t start = i++;
            while (i < len && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < len)
                    i++; // skip escaped character
                i++;
            }
            if (i < len)
                i++; // closing quote
            syn_fill(colors, start, i - start, c_string);
            continue;
        }

        // Number literal
        if (text[i] >= '0' && text[i] <= '9') {
            size_t start = i;
            while (i < len && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'))
                i++;
            syn_fill(colors, start, i - start, c_number);
            continue;
        }

        // Identifier or keyword
        if (syn_is_id_start(text[i])) {
            size_t start = i;
            while (i < len && syn_is_id_cont(text[i]))
                i++;
            size_t wlen = i - start;
            uint32_t color = c_default;
            if (rt_zia_is_keyword(text + start, (int64_t)wlen)) {
                color = c_keyword;
            } else if (syn_is_keyword(text + start, wlen, zia_types)) {
                color = c_type;
            } else if (syn_is_custom_keyword(text + start, wlen, ce)) {
                color = c_keyword;
            } else {
                // Peek past whitespace for an opening paren — identifies
                // function and method calls regardless of capitalization
                // (`packPixel(`, `Canvas3D.New(`, `Math.Sin(` all match).
                size_t peek = i;
                while (peek < len && (text[peek] == ' ' || text[peek] == '\t'))
                    peek++;
                if (peek < len && text[peek] == '(') {
                    color = SYN_COLOR_FUNCTION;
                } else if (wlen > 0 && text[start] >= 'A' && text[start] <= 'Z') {
                    // Identifiers starting with an uppercase letter are
                    // type / class / module names (Foo, MyClass, Math, Viper).
                    // Matches VS Code / IntelliJ convention; gives the
                    // highlighter a much richer feel than a hand-curated
                    // 11-name type list.
                    color = c_type;
                }
            }
            syn_fill(colors, start, wlen, color);
            continue;
        }

        // Default (operators, punctuation)
        colors[i++] = c_default;
    }
    if (ce) {
        ce->zia_block_comment_depth = block_depth;
        if (line_num >= 0 && line_num < ce->line_count) {
            ce->lines[line_num].syntax_state_out = block_depth;
            ce->lines[line_num].syntax_state_generation = ce->highlight_generation;
        }
    }
}

// ─── Viper BASIC language tokenizer ───────────────────────────────────────

static const char *const basic_keywords[] = {
    "DIM",    "LET",   "IF",    "THEN", "ELSE", "ENDIF", "FOR",      "NEXT",
    "TO",     "STEP",  "WHILE", "WEND", "DO",   "LOOP",  "UNTIL",    "GOSUB",
    "RETURN", "PRINT", "INPUT", "GOTO", "SUB",  "END",   "FUNCTION", "CALL",
    "TRUE",   "FALSE", "AND",   "OR",   "NOT",  "MOD",   NULL};

/// @brief Tokenize a Viper BASIC source line.
///
/// Differences from Zia:
///   - Comments use `'` (apostrophe) or the `REM` keyword.
///   - Keyword matching is case-insensitive (`PRINT`, `print`, `Print`
///     all highlight).
///   - No type table — BASIC's type system isn't surfaced as keywords.
static void rt_basic_syntax_cb(
    vg_widget_t *editor, int line_num, const char *text, uint32_t *colors, void *user_data) {
    (void)line_num;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)user_data;
    (void)editor;

    uint32_t c_default = syn_color(ce, 0, SYN_COLOR_DEFAULT);
    uint32_t c_keyword = syn_color(ce, 1, SYN_COLOR_KEYWORD);
    uint32_t c_string = syn_color(ce, 3, SYN_COLOR_STRING);
    uint32_t c_comment = syn_color(ce, 4, SYN_COLOR_COMMENT);
    uint32_t c_number = syn_color(ce, 5, SYN_COLOR_NUMBER);

    size_t len = strlen(text);
    size_t i = 0;

    while (i < len) {
        // Single-quote comment
        if (text[i] == '\'') {
            syn_fill(colors, i, len - i, c_comment);
            return;
        }

        // String literal
        if (text[i] == '"') {
            size_t start = i++;
            while (i < len && text[i] != '"')
                i++;
            if (i < len)
                i++;
            syn_fill(colors, start, i - start, c_string);
            continue;
        }

        // Number literal
        if (text[i] >= '0' && text[i] <= '9') {
            size_t start = i;
            while (i < len && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'))
                i++;
            syn_fill(colors, start, i - start, c_number);
            continue;
        }

        // Identifier or keyword (case-insensitive for BASIC)
        if (syn_is_id_start(text[i])) {
            size_t start = i;
            while (i < len && syn_is_id_cont(text[i]))
                i++;
            size_t wlen = i - start;

            // REM comment: rest of line is a comment
            if (wlen == 3 && syn_word_eq_ci(text + start, "REM", 3)) {
                syn_fill(colors, start, len - start, c_comment);
                return;
            }

            uint32_t color = c_default;
            if (syn_is_keyword_ci(text + start, wlen, basic_keywords))
                color = c_keyword;
            else if (syn_is_custom_keyword(text + start, wlen, ce))
                color = c_keyword;
            syn_fill(colors, start, wlen, color);
            continue;
        }

        // Default
        colors[i++] = c_default;
    }
}

// ─── Public: set language ─────────────────────────────────────────────────

/// @brief `CodeEditor.SetLanguage(language)` — install a syntax-highlight callback.
///
/// Recognized values: `"zia"`, `"basic"`. Anything else (including
/// empty string) installs the no-op highlighter (plain text).
/// The editor pointer itself is the `user_data` for the callback so
/// the tokenizer can read the per-editor color overrides + custom
/// keyword list.
void rt_codeeditor_set_language(void *editor, rt_string language) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    char *clang = rt_string_to_cstr(language);
    if (!clang)
        return;

    // Pass the editor itself as user_data so the syntax callback can read
    // per-editor token_colors[] and custom_keywords[].
    if (strcmp(clang, "zia") == 0)
        vg_codeeditor_set_syntax(ce, rt_zia_syntax_cb, ce);
    else if (strcmp(clang, "basic") == 0)
        vg_codeeditor_set_syntax(ce, rt_basic_syntax_cb, ce);
    else
        vg_codeeditor_set_syntax(ce, NULL, NULL); // plain text

    free(clang);
}

/// @brief `CodeEditor.SetTokenColor(tokenType, color)` — override one theme color.
///
/// `tokenType`: 0=default, 1=keyword, 2=type, 3=string, 4=comment,
/// 5=number. `color`: ARGB 0xAARRGGBB. Out-of-range types are ignored.
/// Triggers a repaint.
void rt_codeeditor_set_token_color(void *editor, int64_t token_type, int64_t color) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    // Token type indices: 0=default, 1=keyword, 2=type, 3=string, 4=comment, 5=number
    if (token_type >= 0 && token_type < 6) {
        ce->token_colors[token_type] = (uint32_t)color;
        rt_codeeditor_invalidate_syntax_cache(ce);
    }
}

/// @brief `CodeEditor.SetCustomKeywords(keywords)` — install a comma-separated keyword list.
///
/// The list is parsed into `ce->custom_keywords` (newly allocated copy
/// of each token, leading/trailing whitespace trimmed). Replaces any
/// previous list (no append). Empty input clears the list. Doubling
/// growth from an initial capacity of 8.
void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;

    // Parse comma-separated keywords into array
    char *ckw = rt_string_to_cstr(keywords);
    if (!ckw || !ckw[0]) {
        free(ckw);
        for (int i = 0; i < ce->custom_keyword_count; i++)
            free(ce->custom_keywords[i]);
        free(ce->custom_keywords);
        ce->custom_keywords = NULL;
        ce->custom_keyword_count = 0;
        rt_codeeditor_invalidate_syntax_cache(ce);
        return;
    }

    // Count commas to estimate capacity
    int cap = 8;
    char **new_keywords = (char **)malloc((size_t)cap * sizeof(char *));
    if (!new_keywords) {
        free(ckw);
        return;
    }
    int new_count = 0;
    int ok = 1;

    char *saveptr = NULL;
    char *token = rt_strtok_r(ckw, ",", &saveptr);
    while (token) {
        // Trim whitespace
        while (isspace((unsigned char)*token))
            token++;
        char *end = token + strlen(token);
        while (end > token && isspace((unsigned char)end[-1]))
            *--end = '\0';

        if (*token) {
            if (new_count >= cap) {
                if (cap > INT_MAX / 2 || (size_t)cap * 2 > SIZE_MAX / sizeof(*new_keywords)) {
                    ok = 0;
                    break;
                }
                cap *= 2;
                char **p = (char **)realloc(new_keywords, (size_t)cap * sizeof(char *));
                if (!p) {
                    ok = 0;
                    break;
                }
                new_keywords = p;
            }
            char *copy = strdup(token);
            if (!copy) {
                ok = 0;
                break;
            }
            new_keywords[new_count++] = copy;
        }
        token = rt_strtok_r(NULL, ",", &saveptr);
    }
    free(ckw);

    if (!ok) {
        for (int i = 0; i < new_count; i++)
            free(new_keywords[i]);
        free(new_keywords);
        return;
    }

    for (int i = 0; i < ce->custom_keyword_count; i++)
        free(ce->custom_keywords[i]);
    free(ce->custom_keywords);
    ce->custom_keywords = new_keywords;
    ce->custom_keyword_count = new_count;
    rt_codeeditor_invalidate_syntax_cache(ce);
}

/// @brief `CodeEditor.ClearHighlights()` — remove every custom highlight span.
///
/// Highlights are user-defined colored ranges painted on top of the
/// syntax highlighting (e.g., for diagnostics, find-results, etc.).
void rt_codeeditor_clear_highlights(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    free(ce->highlight_spans);
    ce->highlight_spans = NULL;
    ce->highlight_span_count = 0;
    ce->highlight_span_cap = 0;
    ce->highlight_spans_sorted = true;
    free(ce->highlight_line_offsets);
    free(ce->highlight_line_span_indices);
    ce->highlight_line_offsets = NULL;
    ce->highlight_line_span_indices = NULL;
    ce->highlight_line_offsets_cap = 0;
    ce->highlight_line_span_indices_cap = 0;
    ce->highlight_line_index_line_count = 0;
    ce->highlight_line_index_span_count = 0;
    ce->highlight_line_index_valid = false;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.AddHighlight(line0, col0, line1, col1, color)` — add a colored range.
///
/// Spans are inclusive on start, exclusive on end. Geometric growth
/// for the spans array (cap doubles, starting at 8). Silently no-ops
/// on OOM (better than trapping the editor).
void rt_codeeditor_add_highlight(void *editor,
                                 int64_t start_line,
                                 int64_t start_col,
                                 int64_t end_line,
                                 int64_t end_col,
                                 int64_t color) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int32_t sl = rt_gui_clamp_i64_to_i32(start_line, 0, INT32_MAX);
    int32_t sc = rt_gui_clamp_i64_to_i32(start_col, 0, INT32_MAX);
    int32_t el = rt_gui_clamp_i64_to_i32(end_line, 0, INT32_MAX);
    int32_t ec = rt_gui_clamp_i64_to_i32(end_col, 0, INT32_MAX);
    if (el < sl || (el == sl && ec <= sc))
        return;
    if (ce->highlight_span_count >= ce->highlight_span_cap) {
        if (ce->highlight_span_cap > INT_MAX / 2)
            return;
        int new_cap = ce->highlight_span_cap ? ce->highlight_span_cap * 2 : 8;
        void *p = realloc(ce->highlight_spans, (size_t)new_cap * sizeof(*ce->highlight_spans));
        if (!p)
            return;
        ce->highlight_spans = p;
        ce->highlight_span_cap = new_cap;
    }
    struct vg_highlight_span *s = &ce->highlight_spans[ce->highlight_span_count++];
    s->start_line = sl;
    s->start_col = sc;
    s->end_line = el;
    s->end_col = ec;
    s->color = (uint32_t)color;
    ce->highlight_spans_sorted = false;
    ce->highlight_line_index_valid = false;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.RefreshHighlights()` — schedule a repaint.
///
/// Useful when callers mutate highlight state through other means
/// and need the editor to redraw on the next frame.
void rt_codeeditor_refresh_highlights(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.AddInlayHint(line, col, text, color)` — add ghost annotation text.
void rt_codeeditor_add_inlay_hint(
    void *editor, int64_t line, int64_t col, rt_string text, int64_t color) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return;
    int32_t line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    int32_t col_i = rt_gui_clamp_i64_to_i32(col, 0, INT32_MAX);
    vg_codeeditor_add_inlay_hint(ce, line_i, col_i, ctext, (uint32_t)color);
    free(ctext);
}

/// @brief `CodeEditor.ClearInlayHints()` — remove every inlay hint.
void rt_codeeditor_clear_inlay_hints(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    vg_codeeditor_clear_inlay_hints(ce);
}

/// @brief `CodeEditor.GetInlayHintCount()` — return active inlay hints.
int64_t rt_codeeditor_get_inlay_hint_count(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return vg_codeeditor_get_inlay_hint_count(ce);
}

//=============================================================================
// CodeEditor Enhancements - Gutter & Line Numbers (Phase 4)
//=============================================================================

/// @brief `CodeEditor.SetShowLineNumbers(show)` — toggle the line-number gutter.
void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    ce->show_line_numbers = show != 0;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.GetShowLineNumbers` — read the line-number visibility flag.
///
/// Returns 1 (showing) for NULL receiver to match the default.
int64_t rt_codeeditor_get_show_line_numbers(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 1; // Default to showing
    return ce->show_line_numbers ? 1 : 0;
}

/// @brief `CodeEditor.SetLineNumberWidth(width)` — set gutter width in characters.
///
/// Internally stored as pixels (`width * char_width`) so layout doesn't
/// have to keep recomputing it.
void rt_codeeditor_set_line_number_width(void *editor, int64_t width) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    ce->line_number_width_override =
        width > 0 ? (float)rt_gui_clamp_i64_to_i32(width, 0, INT32_MAX) : 0.0f;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief Convert an `rt_pixels` ARGB buffer into a `vg_icon_t`.
///
/// Repacks the source buffer (top-byte-alpha ARGB stored as uint32)
/// into the RGBA byte order vg expects. Validates dimensions to
/// guard against integer overflow (W*H*4 > SIZE_MAX). Traps on
/// allocation failure.
static vg_icon_t rt_codeeditor_icon_from_pixels(void *pixels) {
    vg_icon_t icon = {0};
    if (!pixels)
        return icon;

    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    const uint32_t *raw = rt_pixels_raw_buffer(pixels);
    if (width <= 0 || height <= 0 || !raw)
        return icon;
    if ((uintmax_t)width > (uintmax_t)SIZE_MAX || (uintmax_t)height > (uintmax_t)SIZE_MAX)
        rt_trap_raise_kind(
            RT_TRAP_KIND_OVERFLOW, Err_Overflow, -1, "CodeEditor.SetGutterIcon: icon too large");

    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count / (size_t)width != (size_t)height)
        rt_trap_raise_kind(
            RT_TRAP_KIND_OVERFLOW, Err_Overflow, -1, "CodeEditor.SetGutterIcon: icon too large");
    if (width > UINT32_MAX || height > UINT32_MAX || pixel_count > SIZE_MAX / 4)
        rt_trap_raise_kind(
            RT_TRAP_KIND_OVERFLOW, Err_Overflow, -1, "CodeEditor.SetGutterIcon: icon too large");

    uint8_t *rgba = (uint8_t *)malloc(pixel_count * 4);
    if (!rgba)
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "CodeEditor.SetGutterIcon: allocation failed");

    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t px = raw[i];
        rgba[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF);
        rgba[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);
        rgba[i * 4 + 3] = (uint8_t)(px & 0xFF);
    }

    icon = vg_icon_from_pixels(rgba, (uint32_t)width, (uint32_t)height);
    free(rgba);
    if (icon.type == VG_ICON_NONE)
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "CodeEditor.SetGutterIcon: allocation failed");
    return icon;
}

/// @brief `CodeEditor.SetGutterIcon(line, pixels, slot)` — paint an icon in the gutter.
///
/// `slot` selects an icon "channel" so multiple icons can stack on
/// the same line: 0=breakpoint, 1=warning, 2=error, 3=info. Setting
/// the same line+slot replaces the existing icon. Geometric growth
/// for the icons array. Default per-slot tint colors are red/orange/
/// red/blue.
void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int type = 0;
    if (!rt_codeeditor_gutter_slot_checked(slot, &type))
        return;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    if (!pixels) {
        rt_codeeditor_clear_gutter_icon(editor, line_i, slot);
        return;
    }
    /* Update existing icon on same line+type if present */
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].line == line_i && ce->gutter_icons[i].type == type) {
            vg_icon_destroy(&ce->gutter_icons[i].image);
            ce->gutter_icons[i].image = rt_codeeditor_icon_from_pixels(pixels);
            ce->base.needs_paint = true;
            return;
        }
    }
    if (ce->gutter_icon_count >= ce->gutter_icon_cap) {
        if (ce->gutter_icon_cap > INT_MAX / 2)
            return;
        int new_cap = ce->gutter_icon_cap ? ce->gutter_icon_cap * 2 : 8;
        void *p = realloc(ce->gutter_icons, (size_t)new_cap * sizeof(*ce->gutter_icons));
        if (!p)
            rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                               Err_RuntimeError,
                               -1,
                               "CodeEditor.SetGutterIcon: allocation failed");
        ce->gutter_icons = p;
        ce->gutter_icon_cap = new_cap;
    }
    /* Default color per type */
    static const uint32_t s_type_colors[] = {0xFFE81123, 0xFFFFB900, 0xFFE81123, 0xFF0078D4};
    struct vg_gutter_icon *icon = &ce->gutter_icons[ce->gutter_icon_count++];
    icon->line = line_i;
    icon->type = type;
    icon->color = s_type_colors[type];
    icon->image = rt_codeeditor_icon_from_pixels(pixels);
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.ClearGutterIcon(line, slot)` — remove one icon entry.
///
/// Swap-with-last compaction. No-op if no matching icon exists.
void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int type = 0;
    if (!rt_codeeditor_gutter_slot_checked(slot, &type))
        return;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].line == line_i && ce->gutter_icons[i].type == type) {
            int last = --ce->gutter_icon_count;
            vg_icon_destroy(&ce->gutter_icons[i].image);
            if (i != last) {
                ce->gutter_icons[i] = ce->gutter_icons[last];
            }
            memset(&ce->gutter_icons[last], 0, sizeof(ce->gutter_icons[last]));
            ce->base.needs_paint = true;
            return;
        }
    }
}

/// @brief `CodeEditor.ClearAllGutterIcons(slot)` — remove every icon of a given type.
///
/// In-place compaction by writing kept entries to `[0..w)` and clearing
/// the trailing slots. Useful for "clear all breakpoints" type ops.
void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int type = 0;
    if (!rt_codeeditor_gutter_slot_checked(slot, &type))
        return;
    int w = 0;
    int original_count = ce->gutter_icon_count;
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].type != type) {
            ce->gutter_icons[w++] = ce->gutter_icons[i];
            continue;
        }
        vg_icon_destroy(&ce->gutter_icons[i].image);
    }
    ce->gutter_icon_count = w;
    for (int i = w; i < original_count; i++)
        memset(&ce->gutter_icons[i], 0, sizeof(ce->gutter_icons[i]));
    ce->base.needs_paint = true;
}

// Gutter click tracking — per-editor state (not global statics) so
// multiple CodeEditor instances each track their own gutter clicks.

/// @brief Legacy global setter — currently a no-op, kept for ABI stability.
///
/// The vg layer paint callback doesn't pass the editor pointer
/// through, so we can't route clicks to the right editor here. A
/// future refactor would pass the editor through; until then, gutter
/// state is per-editor and updated directly inside the widget code.
void rt_gui_set_gutter_click(int64_t line, int64_t slot) {
    RT_ASSERT_MAIN_THREAD();
    // Legacy global entry point — forwards to a per-editor setter.
    // The vg layer paint callback doesn't know which editor was clicked,
    // so we broadcast to the most-recently-focused editor via s_current_app.
    // A future improvement would pass the editor pointer through the callback.
    (void)line;
    (void)slot;
}

/// @brief Legacy global clear — currently a no-op (state lives per-editor).
void rt_gui_clear_gutter_click(void) {
    RT_ASSERT_MAIN_THREAD();
    // No-op: per-editor state is cleared after read in the getter functions.
}

/// @brief `CodeEditor.WasGutterClicked` — edge-detect: true once per click.
///
/// Returns the latched click flag and clears it as a side effect, so
/// subsequent reads return 0 until the next click. Pair with
/// `GetGutterClickedLine`/`Slot` for the click coordinates.
int64_t rt_codeeditor_was_gutter_clicked(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    int64_t result = ce->gutter_clicked ? 1 : 0;
    if (result) {
        ce->gutter_clicked = false; // Edge-triggered: clear after read
        ce->gutter_clicked_line = -1;
        ce->gutter_clicked_slot = -1;
    }
    return result;
}

/// @brief `CodeEditor.GetGutterClickedLine` — line number of the most recent click.
///
/// Returns -1 for NULL receiver or no unconsumed click. Read before
/// `WasGutterClicked`, which consumes the click payload.
int64_t rt_codeeditor_get_gutter_clicked_line(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce || !ce->gutter_clicked || ce->gutter_clicked_line < 0)
        return -1;
    return ce->gutter_clicked_line;
}

/// @brief `CodeEditor.GetGutterClickedSlot` — slot index of the most recent click.
int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce || !ce->gutter_clicked || ce->gutter_clicked_slot < 0)
        return -1;
    return ce->gutter_clicked_slot;
}

/// @brief `CodeEditor.SetShowFoldGutter(show)` — toggle the fold-region gutter.
///
/// The fold gutter sits next to the line-number gutter and shows
/// triangle indicators next to foldable regions.
void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    ce->show_fold_gutter = show != 0;
    vg_codeeditor_refresh_layout_state(ce);
}

//=============================================================================
// CodeEditor Enhancements - Code Folding (Phase 4)
//=============================================================================

/// @brief `CodeEditor.AddFoldRegion(startLine, endLine)` — register a foldable region.
///
/// Regions can be folded/unfolded individually via `Fold(line)`/`Unfold(line)`
/// or in bulk via `FoldAll`/`UnfoldAll`. Initial state is unfolded.
/// No overlap detection — caller is responsible for sane region layout.
void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    if (start_line < 0)
        start_line = 0;
    if (end_line >= ce->line_count)
        end_line = ce->line_count - 1;
    if (end_line <= start_line)
        return;
    int start_i = rt_gui_clamp_i64_to_i32(start_line, 0, INT32_MAX);
    int end_i = rt_gui_clamp_i64_to_i32(end_line, 0, INT32_MAX);
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == start_i) {
            bool was_folded = ce->fold_regions[i].folded;
            ce->fold_regions[i].end_line = end_i;
            ce->fold_regions[i].folded = false;
            if (was_folded)
                vg_codeeditor_refresh_layout_state(ce);
            else
                ce->base.needs_paint = true;
            return;
        }
    }
    if (ce->fold_region_count >= ce->fold_region_cap) {
        if (ce->fold_region_cap > INT_MAX / 2)
            return;
        int new_cap = ce->fold_region_cap ? ce->fold_region_cap * 2 : 8;
        void *p = realloc(ce->fold_regions, (size_t)new_cap * sizeof(*ce->fold_regions));
        if (!p)
            return;
        ce->fold_regions = p;
        ce->fold_region_cap = new_cap;
    }
    struct vg_fold_region *r = &ce->fold_regions[ce->fold_region_count++];
    r->start_line = start_i;
    r->end_line = end_i;
    r->folded = false;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.RemoveFoldRegion(startLine)` — drop a fold region.
///
/// Identified by the start line. Swap-with-last compaction. No-op if
/// no region starts at the given line.
void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int start_i = rt_gui_clamp_i64_to_i32(start_line, 0, INT32_MAX);
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == start_i) {
            ce->fold_regions[i] = ce->fold_regions[--ce->fold_region_count];
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.ClearFoldRegions` — drop every registered fold region.
void rt_codeeditor_clear_fold_regions(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    free(ce->fold_regions);
    ce->fold_regions = NULL;
    ce->fold_region_count = 0;
    ce->fold_region_cap = 0;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.Fold(line)` — collapse the region starting at `line`.
void rt_codeeditor_fold(void *editor, int64_t line) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == line_i) {
            ce->fold_regions[i].folded = true;
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.Unfold(line)` — expand the region starting at `line`.
void rt_codeeditor_unfold(void *editor, int64_t line) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == line_i) {
            ce->fold_regions[i].folded = false;
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.ToggleFold(line)` — flip the folded state of one region.
void rt_codeeditor_toggle_fold(void *editor, int64_t line) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == line_i) {
            ce->fold_regions[i].folded = !ce->fold_regions[i].folded;
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.IsFolded(line)` — true iff the region starting at `line` is collapsed.
int64_t rt_codeeditor_is_folded(void *editor, int64_t line) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == line_i)
            return ce->fold_regions[i].folded ? 1 : 0;
    }
    return 0;
}

/// @brief `CodeEditor.FoldAll` — collapse every fold region.
void rt_codeeditor_fold_all(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    for (int i = 0; i < ce->fold_region_count; i++)
        ce->fold_regions[i].folded = true;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.UnfoldAll` — expand every fold region.
void rt_codeeditor_unfold_all(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    for (int i = 0; i < ce->fold_region_count; i++)
        ce->fold_regions[i].folded = false;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.SetAutoFoldDetection(enable)` — derive fold regions from indentation.
///
/// When enabled, immediately walks the buffer looking for places where
/// the next line is more indented than the current line and registers
/// a fold region from the start of the indented block to the line where
/// indentation drops back. Blank lines extend the fold (don't break it).
/// Replaces any manually-added regions. No effect if the buffer is empty.
void rt_codeeditor_set_auto_fold_detection(void *editor, int64_t enable) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    ce->auto_fold_detection = enable != 0;

    // When enabling, immediately detect fold regions from indentation.
    if (ce->auto_fold_detection && ce->line_count > 0) {
        // Clear existing fold regions
        free(ce->fold_regions);
        ce->fold_regions = NULL;
        ce->fold_region_count = 0;
        ce->fold_region_cap = 0;

        // Indent-based fold detection: a fold starts when the next line's
        // indentation increases, and ends when it returns to the start level.
        for (int i = 0; i < ce->line_count - 1; i++) {
            // Count leading spaces/tabs for this line and next
            const char *cur = ce->lines[i].text;
            const char *nxt = ce->lines[i + 1].text;
            int cur_indent = 0, nxt_indent = 0;
            while (cur[cur_indent] == ' ' || cur[cur_indent] == '\t')
                cur_indent++;
            while (nxt[nxt_indent] == ' ' || nxt[nxt_indent] == '\t')
                nxt_indent++;

            // Skip blank lines
            if ((size_t)cur_indent >= ce->lines[i].length)
                continue;

            // Fold region starts when indentation increases
            if (nxt_indent > cur_indent) {
                int start_line = i;
                int base_indent = cur_indent;

                // Find end: where indentation returns to base level or below
                int end_line = i + 1;
                for (int j = i + 2; j < ce->line_count; j++) {
                    const char *line = ce->lines[j].text;
                    int indent = 0;
                    while (line[indent] == ' ' || line[indent] == '\t')
                        indent++;
                    if ((size_t)indent >= ce->lines[j].length) {
                        end_line = j; // blank line extends the fold
                        continue;
                    }
                    if (indent <= base_indent)
                        break;
                    end_line = j;
                }

                if (end_line > start_line) {
                    // Add fold region via realloc
                    if (ce->fold_region_count >= ce->fold_region_cap) {
                        if (ce->fold_region_cap > INT_MAX / 2)
                            break;
                        int new_cap = ce->fold_region_cap ? ce->fold_region_cap * 2 : 16;
                        void *p =
                            realloc(ce->fold_regions, (size_t)new_cap * sizeof(*ce->fold_regions));
                        if (!p)
                            break;
                        ce->fold_regions = p;
                        ce->fold_region_cap = new_cap;
                    }
                    struct vg_fold_region *r = &ce->fold_regions[ce->fold_region_count++];
                    r->start_line = start_line;
                    r->end_line = end_line;
                    r->folded = false;

                    // Skip past this fold region
                    i = end_line - 1;
                }
            }
        }
    }
    vg_codeeditor_refresh_layout_state(ce);
}

//=============================================================================
// CodeEditor Enhancements - Multiple Cursors (Phase 4)
//=============================================================================

/// @brief Clamp a `(line, col)` pair to a valid in-buffer position.
///
/// Negative coordinates clamp to 0; out-of-bounds line clamps to the
/// last line; out-of-bounds column clamps to the line's length. Used
/// before storing user-supplied cursor positions to avoid OOB reads.
static void rt_codeeditor_clamp_position(vg_codeeditor_t *ce, int *line, int *col) {
    if (!ce || !line || !col || ce->line_count <= 0)
        return;

    if (*line < 0)
        *line = 0;
    if (*line >= ce->line_count)
        *line = ce->line_count - 1;
    if (*col < 0)
        *col = 0;
    int line_len = rt_codeeditor_line_length_i32(ce, *line);
    if (*col > line_len)
        *col = line_len;
}

/// @brief `CodeEditor.GetCursorCount` — number of active cursors (always >= 1).
///
/// Always 1 + extras; the primary cursor is always present. Returns 1
/// for NULL receiver to match the "at least one cursor" invariant.
int64_t rt_codeeditor_get_cursor_count(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 1;
    return 1 + ce->extra_cursor_count;
}

/// @brief `CodeEditor.AddCursor(line, col)` — add a secondary cursor.
///
/// Index 0 is reserved for the primary cursor; new cursors get
/// indices 1, 2, … Position is clamped to a valid buffer position.
/// Geometric growth (cap doubles, starting at 4).
void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    if (ce->extra_cursor_count >= ce->extra_cursor_cap) {
        if (ce->extra_cursor_cap > INT_MAX / 2)
            return;
        int new_cap = ce->extra_cursor_cap ? ce->extra_cursor_cap * 2 : 4;
        void *p = realloc(ce->extra_cursors, (size_t)new_cap * sizeof(*ce->extra_cursors));
        if (!p)
            return;
        ce->extra_cursors = p;
        ce->extra_cursor_cap = new_cap;
    }
    struct vg_extra_cursor *c = &ce->extra_cursors[ce->extra_cursor_count++];
    c->line = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    c->col = rt_gui_clamp_i64_to_i32(col, 0, INT32_MAX);
    rt_codeeditor_clamp_position(ce, &c->line, &c->col);
    memset(&c->selection, 0, sizeof(c->selection));
    c->has_selection = false;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.RemoveCursor(index)` — drop a secondary cursor.
///
/// Index 0 (primary) cannot be removed (that cursor is intrinsic to
/// the editor). Indices 1+ refer to entries in the `extra_cursors`
/// array. Shifts remaining cursors down to keep the array dense.
void rt_codeeditor_remove_cursor(void *editor, int64_t index) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    if (index <= 0 || index > INT32_MAX)
        return;
    int idx = (int)index - 1; /* index 0 = primary cursor (not in extra array) */
    if (idx < 0 || idx >= ce->extra_cursor_count)
        return;
    /* Shift remaining cursors down */
    for (int i = idx; i < ce->extra_cursor_count - 1; i++)
        ce->extra_cursors[i] = ce->extra_cursors[i + 1];
    ce->extra_cursor_count--;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.ClearExtraCursors` — remove every secondary cursor.
///
/// Primary cursor stays. Useful for "Escape" key handling in
/// multi-cursor editing modes.
void rt_codeeditor_clear_extra_cursors(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    free(ce->extra_cursors);
    ce->extra_cursors = NULL;
    ce->extra_cursor_count = 0;
    ce->extra_cursor_cap = 0;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.GetCursorLineAt(index)` — line of the i-th cursor.
///
/// Index 0 is the primary cursor; 1+ are extras. Out-of-range
/// returns 0 (defensive default).
int64_t rt_codeeditor_get_cursor_line_at(void *editor, int64_t index) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    if (index == 0)
        return ce->cursor_line;
    if (index < 0 || index > INT32_MAX)
        return 0;
    int extra_idx = (int)index - 1;
    if (extra_idx >= 0 && extra_idx < ce->extra_cursor_count)
        return ce->extra_cursors[extra_idx].line;
    return 0;
}

/// @brief `CodeEditor.GetCursorColAt(index)` — column of the i-th cursor.
int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    if (index == 0)
        return rt_codeeditor_byte_col_to_char_col(ce, ce->cursor_line, ce->cursor_col);
    if (index < 0 || index > INT32_MAX)
        return 0;
    int extra_idx = (int)index - 1;
    if (extra_idx >= 0 && extra_idx < ce->extra_cursor_count)
        return rt_codeeditor_byte_col_to_char_col(
            ce, ce->extra_cursors[extra_idx].line, ce->extra_cursors[extra_idx].col);
    return 0;
}

/// @brief `CodeEditor.CursorLine` — convenience for the primary cursor's line.
int64_t rt_codeeditor_get_cursor_line(void *editor) {
    return rt_codeeditor_get_cursor_line_at(editor, 0);
}

/// @brief `CodeEditor.CursorCol` — convenience for the primary cursor's column.
int64_t rt_codeeditor_get_cursor_col(void *editor) {
    return rt_codeeditor_get_cursor_col_at(editor, 0);
}

/// @brief `CodeEditor.ScrollTopLine` — source line nearest the viewport top.
int64_t rt_codeeditor_get_scroll_top_line(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return vg_codeeditor_get_scroll_top_line(ce);
}

/// @brief Set `CodeEditor.ScrollTopLine`.
void rt_codeeditor_set_scroll_top_line(void *editor, int64_t line) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    vg_codeeditor_set_scroll_top_line(ce, line_i);
}

/// @brief `CodeEditor.SetCursorPositionAt(index, line, col)` — move one cursor.
///
/// Index 0 routes to the underlying widget's `set_cursor` (which also
/// scrolls the viewport). Index 1+ updates the extras directly.
/// Position is clamped; selection is cleared.
void rt_codeeditor_set_cursor_position_at(void *editor, int64_t index, int64_t line, int64_t col) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int line_i = rt_gui_clamp_i64_to_i32(line, 0, INT32_MAX);
    int col_i = rt_gui_clamp_i64_to_i32(col, 0, INT32_MAX);
    if (index == 0) {
        vg_codeeditor_set_cursor(ce, line_i, col_i);
        return;
    }
    if (index < 0 || index > INT32_MAX)
        return;
    int extra_idx = (int)index - 1;
    if (extra_idx < 0 || extra_idx >= ce->extra_cursor_count)
        return;
    rt_codeeditor_clamp_position(ce, &line_i, &col_i);
    col_i = rt_codeeditor_char_col_to_byte_col(ce, line_i, col_i);
    ce->extra_cursors[extra_idx].line = line_i;
    ce->extra_cursors[extra_idx].col = col_i;
    ce->extra_cursors[extra_idx].has_selection = false;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.SetCursorSelection(index, sLine, sCol, eLine, eCol)` — set a selection.
///
/// Both start and end positions are clamped. The cursor for that
/// index moves to the end of the selection (matching standard
/// editor behavior where shift+click extends from the existing
/// cursor to the click position).
void rt_codeeditor_set_cursor_selection(void *editor,
                                        int64_t index,
                                        int64_t start_line,
                                        int64_t start_col,
                                        int64_t end_line,
                                        int64_t end_col) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    int s_line = rt_gui_clamp_i64_to_i32(start_line, 0, INT32_MAX);
    int s_col = rt_gui_clamp_i64_to_i32(start_col, 0, INT32_MAX);
    int e_line = rt_gui_clamp_i64_to_i32(end_line, 0, INT32_MAX);
    int e_col = rt_gui_clamp_i64_to_i32(end_col, 0, INT32_MAX);
    rt_codeeditor_clamp_position(ce, &s_line, &s_col);
    rt_codeeditor_clamp_position(ce, &e_line, &e_col);

    if (index == 0) {
        vg_codeeditor_set_selection(ce, s_line, s_col, e_line, e_col);
        return;
    }

    if (index < 0 || index > INT32_MAX)
        return;
    int extra_idx = (int)index - 1;
    if (extra_idx < 0 || extra_idx >= ce->extra_cursor_count)
        return;

    s_col = rt_codeeditor_char_col_to_byte_col(ce, s_line, s_col);
    e_col = rt_codeeditor_char_col_to_byte_col(ce, e_line, e_col);
    ce->extra_cursors[extra_idx].selection.start_line = s_line;
    ce->extra_cursors[extra_idx].selection.start_col = s_col;
    ce->extra_cursors[extra_idx].selection.end_line = e_line;
    ce->extra_cursors[extra_idx].selection.end_col = e_col;
    ce->extra_cursors[extra_idx].line = e_line;
    ce->extra_cursors[extra_idx].col = e_col;
    ce->extra_cursors[extra_idx].has_selection = s_line != e_line || s_col != e_col;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.CursorHasSelection(index)` — whether the i-th cursor has a selection.
///
/// Index 0 reads the editor's main `has_selection` flag; extras keep
/// their own per-cursor flag set by `SetCursorSelection`.
int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    if (index == 0)
        return ce->has_selection ? 1 : 0;
    if (index < 0 || index > INT32_MAX)
        return 0;
    int extra_idx = (int)index - 1;
    if (extra_idx < 0 || extra_idx >= ce->extra_cursor_count)
        return 0;
    return ce->extra_cursors[extra_idx].has_selection ? 1 : 0;
}

/// @brief Fetch the selection range for cursor @p index (0 = primary caret,
///        ≥1 = extra multi-cursor) into @p out.
/// @return true if that cursor has an active selection; false otherwise.
static bool rt_codeeditor_get_selection_at(void *editor, int64_t index, vg_selection_t *out) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce || !out)
        return false;

    if (index == 0) {
        if (!ce->has_selection)
            return false;
        *out = ce->selection;
    } else {
        if (index < 0 || index > INT32_MAX)
            return false;
        int extra_idx = (int)index - 1;
        if (extra_idx < 0 || extra_idx >= ce->extra_cursor_count ||
            !ce->extra_cursors[extra_idx].has_selection)
            return false;
        *out = ce->extra_cursors[extra_idx].selection;
    }

    rt_codeeditor_normalize_selection(out);
    return true;
}

/// @brief `CodeEditor.GetSelectionStartLineAt(index)` — normalized selection start line.
int64_t rt_codeeditor_get_selection_start_line_at(void *editor, int64_t index) {
    vg_selection_t selection;
    if (!rt_codeeditor_get_selection_at(editor, index, &selection))
        return 0;
    return selection.start_line;
}

/// @brief `CodeEditor.GetSelectionStartColAt(index)` — normalized selection start column.
int64_t rt_codeeditor_get_selection_start_col_at(void *editor, int64_t index) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    vg_selection_t selection;
    if (!rt_codeeditor_get_selection_at(editor, index, &selection))
        return 0;
    return rt_codeeditor_byte_col_to_char_col(ce, selection.start_line, selection.start_col);
}

/// @brief `CodeEditor.GetSelectionEndLineAt(index)` — normalized selection end line.
int64_t rt_codeeditor_get_selection_end_line_at(void *editor, int64_t index) {
    vg_selection_t selection;
    if (!rt_codeeditor_get_selection_at(editor, index, &selection))
        return 0;
    return selection.end_line;
}

/// @brief `CodeEditor.GetSelectionEndColAt(index)` — normalized selection end column.
int64_t rt_codeeditor_get_selection_end_col_at(void *editor, int64_t index) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    vg_selection_t selection;
    if (!rt_codeeditor_get_selection_at(editor, index, &selection))
        return 0;
    return rt_codeeditor_byte_col_to_char_col(ce, selection.end_line, selection.end_col);
}

// Edit-history and clipboard ops — thin wrappers around the underlying
// `vg_codeeditor_*` widget API. NULL receiver is a no-op (or zero return).

/// @brief `CodeEditor.Undo` — pop one entry from the undo stack.
void rt_codeeditor_undo(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (ce)
        vg_codeeditor_undo(ce);
}

/// @brief `CodeEditor.Redo` — re-apply one undone entry.
void rt_codeeditor_redo(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (ce)
        vg_codeeditor_redo(ce);
}

/// @brief `CodeEditor.CanUndo` — true when the undo stack has an available entry.
int64_t rt_codeeditor_can_undo(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return (ce->history && ce->history->current_index > 0) ? 1 : 0;
}

/// @brief `CodeEditor.CanRedo` — true when the redo stack has an available entry.
int64_t rt_codeeditor_can_redo(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return (ce->history && ce->history->current_index < ce->history->count) ? 1 : 0;
}

/// @brief `CodeEditor.Copy` — copy selection to the system clipboard. Returns 1 on success.
int64_t rt_codeeditor_copy(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return vg_codeeditor_copy(ce) ? 1 : 0;
}

/// @brief `CodeEditor.Cut` — copy selection then delete. Returns 1 on success.
int64_t rt_codeeditor_cut(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return vg_codeeditor_cut(ce) ? 1 : 0;
}

/// @brief `CodeEditor.Paste` — insert clipboard text at cursor. Returns 1 on success.
int64_t rt_codeeditor_paste(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return vg_codeeditor_paste(ce) ? 1 : 0;
}

/// @brief `CodeEditor.SelectAll` — selection from buffer start to end.
void rt_codeeditor_select_all(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (ce)
        vg_codeeditor_select_all(ce);
}

/// @brief `CodeEditor.SetTabSize` — set tab width in spaces.
void rt_codeeditor_set_tab_size(void *editor, int64_t size) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    if (size < 1)
        size = 1;
    if (size > 16)
        size = 16;
    ce->tab_width = (int)size;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.GetTabSize` — return tab width in spaces.
int64_t rt_codeeditor_get_tab_size(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return ce->tab_width;
}

/// @brief `CodeEditor.SetInsertSpaces` — choose soft tabs vs hard tabs.
void rt_codeeditor_set_insert_spaces(void *editor, int64_t enabled) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    ce->use_spaces = enabled != 0;
}

/// @brief `CodeEditor.GetInsertSpaces` — return soft-tab setting.
int64_t rt_codeeditor_get_insert_spaces(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return ce->use_spaces ? 1 : 0;
}

/// @brief `CodeEditor.SetWordWrap` — toggle display-only word wrapping.
void rt_codeeditor_set_word_wrap(void *editor, int64_t enabled) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    ce->word_wrap = enabled != 0;
    if (ce->word_wrap)
        ce->scroll_x = 0.0f;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.GetWordWrap` — return display-only word wrapping state.
int64_t rt_codeeditor_get_word_wrap(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    return ce->word_wrap ? 1 : 0;
}

//=============================================================================
// CodeEditor Completion Helpers
//=============================================================================

#define RT_CODEEDITOR_SCROLLBAR_WIDTH 12.0f

/// @brief Test whether a line is hidden inside a folded region.
/// @details Walks the fold-region list and returns 1 if `line` falls strictly
///          inside any active fold (`start_line < line <= end_line`). The
///          fold's start line itself stays visible — it carries the fold-icon
///          glyph and shows the collapsed-summary text — so the check is
///          asymmetric on purpose.
/// @return 1 if the line is currently hidden by an active fold, 0 otherwise.
static int rt_codeeditor_line_is_hidden(const vg_codeeditor_t *ce, int line) {
    if (!ce)
        return 0;
    for (int i = 0; i < ce->fold_region_count; i++) {
        const struct vg_fold_region *region = &ce->fold_regions[i];
        if (region->folded && line > region->start_line && line <= region->end_line)
            return 1;
    }
    return 0;
}

/// @brief Resolve a possibly-hidden line to the visible line that anchors it.
/// @details When code-folding hides a line, cursor / scroll / hit-test
///          operations need a visible "stand-in" — the start line of the
///          enclosing fold, since that's the line currently on screen.
///          If `line` isn't hidden, it's returned unchanged after a clamp
///          to `[0, line_count - 1]`. If it's hidden, the function picks
///          the *outermost* containing fold's start line (smallest
///          `start_line` of any fold whose range covers `line`), so nested
///          folds collapse correctly to the topmost visible anchor.
/// @return Clamped visible line index suitable for cursor / scroll math.
static int rt_codeeditor_visible_anchor_line(const vg_codeeditor_t *ce, int line) {
    if (!ce || ce->line_count <= 0)
        return 0;
    if (line < 0)
        line = 0;
    if (line >= ce->line_count)
        line = ce->line_count - 1;
    if (!rt_codeeditor_line_is_hidden(ce, line))
        return line;

    int best_start = line;
    int found = 0;
    for (int i = 0; i < ce->fold_region_count; i++) {
        const struct vg_fold_region *region = &ce->fold_regions[i];
        if (region->folded && line > region->start_line && line <= region->end_line) {
            if (!found || region->start_line < best_start) {
                best_start = region->start_line;
                found = 1;
            }
        }
    }
    return found ? best_start : line;
}

/// @brief Compute how many monospace characters fit in one wrapped row.
/// @details Returns 0 when word-wrap is disabled (caller should treat that as
///          "no wrap budget — emit the full line as one row"). When word-wrap
///          is on, returns at least 1 even for absurdly narrow viewports so
///          the caller's division never trips on a zero divisor.
/// @return Characters per row (>= 1 with word-wrap on; 0 with word-wrap off).
static int rt_codeeditor_chars_per_row(const vg_codeeditor_t *ce, float content_width) {
    if (!ce || !ce->word_wrap || ce->char_width <= 0.0f)
        return 0;
    if (content_width <= 0.0f)
        return 1;
    double chars_f = (double)content_width / (double)ce->char_width;
    int chars = chars_f > (double)INT_MAX ? INT_MAX : (int)chars_f;
    return chars > 0 ? chars : 1;
}

/// @brief Compute how many visual rows a single source line occupies under word-wrap.
/// @details Ceiling-divides line length by `chars_per_row`. Returns 1 (a
///          single-row fallback) when word-wrap is off, when the line is
///          empty, or when the chars-per-row computation produces 0 (so
///          callers always get a positive row count for any in-range line).
/// @return Row count; always >= 1 for valid lines.
static int rt_codeeditor_wrapped_rows_for_line(const vg_codeeditor_t *ce,
                                               int line,
                                               float content_width) {
    if (!ce || line < 0 || line >= ce->line_count)
        return 1;
    int chars_per_row = rt_codeeditor_chars_per_row(ce, content_width);
    if (chars_per_row <= 0)
        return 1;
    size_t len = ce->lines[line].length;
    if (len == 0)
        return 1;
    size_t rows = (len + (size_t)chars_per_row - 1) / (size_t)chars_per_row;
    return rows > (size_t)INT_MAX ? INT_MAX : (int)rows;
}

/// @brief Combine fold-hiding and word-wrap to get the on-screen row count for a line.
/// @details Three cases:
///          - Line is hidden by a fold → returns 0 (consumes no vertical space).
///          - Word-wrap is off → returns 1 (one source line = one screen row).
///          - Word-wrap is on → defers to `rt_codeeditor_wrapped_rows_for_line`.
///          This is the canonical helper for all visual-row arithmetic in
///          the editor; cursor positioning, scrollbar math, and hit-testing
///          all sum these counts to convert between source-line space and
///          screen-row space.
/// @return Visual row count for the line (0 if hidden, >= 1 otherwise).
static int rt_codeeditor_visual_rows_for_line(const vg_codeeditor_t *ce,
                                              int line,
                                              float content_width) {
    if (!ce || line < 0 || line >= ce->line_count || rt_codeeditor_line_is_hidden(ce, line))
        return 0;
    if (!ce->word_wrap)
        return 1;
    return rt_codeeditor_wrapped_rows_for_line(ce, line, content_width);
}

/// @brief Compute the editor's text-content width, accounting for the vertical scrollbar.
/// @details This is a fixed-point convergence loop because word-wrap and the
///          vertical-scrollbar's presence are mutually dependent: narrower
///          content (because the scrollbar took space) produces more wrapped
///          rows, which can push content past the viewport height and *make*
///          the scrollbar appear, which narrows the content again. Without
///          word-wrap the answer is just `base.width - gutter_width` and the
///          loop is skipped.
///
///          The convergence is bounded at 3 passes — empirically the answer
///          stabilises within 2 (the only oscillation possible is "no
///          scrollbar / yes scrollbar"; once that flips, the next iteration
///          sees the same width and the loop breaks via equality check).
///          The pass cap defends against pathological cases where the cap
///          line height makes the test oscillate; bounded iteration is
///          better than risking an infinite loop in the paint path.
/// @return Pixel width available for text after gutter and (if needed) scrollbar.
static float rt_codeeditor_content_draw_width(const vg_codeeditor_t *ce) {
    if (!ce)
        return 0.0f;

    float base_width = ce->base.width - ce->gutter_width;
    if (base_width < 0.0f)
        base_width = 0.0f;
    if (!ce->word_wrap)
        return base_width;

    float content_width = base_width;
    for (int pass = 0; pass < 3; pass++) {
        int64_t total_rows = 0;
        for (int i = 0; i < ce->line_count; i++) {
            int rows = rt_codeeditor_visual_rows_for_line(ce, i, content_width);
            total_rows = total_rows > INT64_MAX - rows ? INT64_MAX : total_rows + rows;
        }
        float total_height = (float)total_rows * ce->line_height;
        float next_width =
            base_width - ((total_height > ce->base.height) ? RT_CODEEDITOR_SCROLLBAR_WIDTH : 0.0f);
        if (next_width < 0.0f)
            next_width = 0.0f;
        if (next_width == content_width)
            break;
        content_width = next_width;
    }
    return content_width;
}

/// @brief Within a single source line, map column → (wrapped row, column-in-row).
/// @details For a line of length L wrapped at C chars-per-row, column `col`
///          normally lives at `(col / C, col % C)`. The one subtlety is the
///          end-of-line cursor: when `col == L` *and* L is a non-zero exact
///          multiple of C, the simple division produces `row = L/C` (one
///          past the last row), which would paint the cursor on a phantom
///          row below the line. The special-case branch maps that situation
///          back to the actual last row's trailing position.
///          When word-wrap is off (chars_per_row == 0), `row_index` stays 0
///          and `col_in_row == col`. Out parameters may be NULL.
static void rt_codeeditor_visual_offset_for_position(const vg_codeeditor_t *ce,
                                                     float content_width,
                                                     int line,
                                                     int col,
                                                     int *out_row_index,
                                                     int *out_col_in_row) {
    if (out_row_index)
        *out_row_index = 0;
    if (out_col_in_row)
        *out_col_in_row = 0;
    if (!ce || line < 0 || line >= ce->line_count)
        return;
    line = rt_codeeditor_visible_anchor_line(ce, line);

    int chars_per_row = rt_codeeditor_chars_per_row(ce, content_width);
    int row_index = 0;
    int col_in_row = col;
    if (chars_per_row > 0) {
        size_t len = ce->lines[line].length;
        row_index = col / chars_per_row;
        if (col > 0 && (size_t)col == len && len > 0 && (len % (size_t)chars_per_row) == 0) {
            row_index = (int)((len - 1) / (size_t)chars_per_row);
        }
        col_in_row = col - row_index * chars_per_row;
        if (col_in_row < 0)
            col_in_row = 0;
    }

    if (out_row_index)
        *out_row_index = row_index;
    if (out_col_in_row)
        *out_col_in_row = col_in_row;
}

/// @brief Convert a (line, col) position into an absolute visual row index.
/// @details Sums the visual row counts of every preceding line (skipping
///          folded-out lines, which contribute 0) and adds the wrapped-row
///          offset within the target line. The result is the row coordinate
///          to use against scroll position and viewport height — i.e., what
///          you'd subtract `scroll_y / line_height` from to get the screen
///          row.
///          The leading clamp + `visible_anchor_line` resolution ensures
///          callers passing an out-of-range or fold-hidden line still get
///          a meaningful row number rather than triggering OOB reads on
///          the per-line iteration.
/// @return Absolute visual row index (always >= 0).
static int rt_codeeditor_visual_row_for_position(const vg_codeeditor_t *ce,
                                                 float content_width,
                                                 int line,
                                                 int col) {
    if (!ce || ce->line_count <= 0)
        return 0;
    if (line < 0)
        line = 0;
    if (line >= ce->line_count)
        line = ce->line_count - 1;
    line = rt_codeeditor_visible_anchor_line(ce, line);

    int64_t visual_row = 0;
    for (int i = 0; i < line; i++) {
        int rows = rt_codeeditor_visual_rows_for_line(ce, i, content_width);
        visual_row = visual_row > INT64_MAX - rows ? INT64_MAX : visual_row + rows;
    }

    int row_index = 0;
    rt_codeeditor_visual_offset_for_position(ce, content_width, line, col, &row_index, NULL);
    visual_row = visual_row > INT64_MAX - row_index ? INT64_MAX : visual_row + row_index;
    return visual_row > INT_MAX ? INT_MAX : (int)visual_row;
}

/// @brief Map a 0-based visual row index back to a logical `(line, row_in_line)` pair.
/// @details Inverse of `rt_codeeditor_total_visual_row_for_position`. Walks the line
///          array accumulating visual row counts (accounting for word-wrap) until the
///          target visual row is consumed, then uses `rt_codeeditor_visual_offset_for_position`
///          to find the exact sub-line offset within the found logical line.
static void rt_codeeditor_locate_visual_row(const vg_codeeditor_t *ce,
                                            float content_width,
                                            int visual_row,
                                            int *out_line,
                                            int *out_row_in_line) {
    if (out_line)
        *out_line = 0;
    if (out_row_in_line)
        *out_row_in_line = 0;
    if (!ce || ce->line_count <= 0)
        return;

    if (visual_row < 0)
        visual_row = 0;

    int64_t accumulated = 0;
    for (int line = 0; line < ce->line_count; line++) {
        int row_count = rt_codeeditor_visual_rows_for_line(ce, line, content_width);
        if (row_count == 0)
            continue;
        int64_t next = accumulated > INT64_MAX - row_count ? INT64_MAX : accumulated + row_count;
        if ((int64_t)visual_row < next) {
            if (out_line)
                *out_line = line;
            if (out_row_in_line)
                *out_row_in_line = (int)((int64_t)visual_row - accumulated);
            return;
        }
        accumulated = next;
    }

    if (out_line)
        *out_line = rt_codeeditor_visible_anchor_line(ce, ce->line_count - 1);
    if (out_row_in_line) {
        int last_rows = rt_codeeditor_visual_rows_for_line(
            ce, rt_codeeditor_visible_anchor_line(ce, ce->line_count - 1), content_width);
        *out_row_in_line = last_rows > 0 ? last_rows - 1 : 0;
    }
}

/// @brief Get the screen-absolute X pixel coordinate of the primary cursor.
/// @details Combines the widget's screen-space origin, gutter width, and
///          cursor column × character width.
int64_t rt_codeeditor_get_cursor_pixel_x(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    float ax = 0, ay = 0;
    vg_widget_get_screen_bounds(&ce->base, &ax, &ay, NULL, NULL);
    (void)ay;
    float px = ax + ce->gutter_width;
    if (ce->word_wrap) {
        float content_width = rt_codeeditor_content_draw_width(ce);
        int col_in_row = ce->cursor_col;
        rt_codeeditor_visual_offset_for_position(
            ce, content_width, ce->cursor_line, ce->cursor_col, NULL, &col_in_row);
        px += (float)col_in_row * ce->char_width;
    } else {
        px += (float)(ce->cursor_col) * ce->char_width - ce->scroll_x;
    }
    return (int64_t)px;
}

/// @brief Get the screen-absolute Y pixel coordinate of the primary cursor.
/// @details Combines the widget's screen-space origin with the cursor's
///          visible line offset scaled by line height.
int64_t rt_codeeditor_get_cursor_pixel_y(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    float ax = 0, ay = 0;
    vg_widget_get_screen_bounds(&ce->base, &ax, &ay, NULL, NULL);
    (void)ax;
    float py = ay;
    float content_width = rt_codeeditor_content_draw_width(ce);
    int visual_row =
        rt_codeeditor_visual_row_for_position(ce, content_width, ce->cursor_line, ce->cursor_col);
    py += (float)visual_row * ce->line_height - ce->scroll_y;
    return (int64_t)py;
}

/// @brief Return the 0-based editor line at a screen-absolute Y coordinate.
int64_t rt_codeeditor_get_line_at_pixel(void *editor, int64_t y) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return -1;
    if (ce->line_count <= 0 || ce->line_height <= 0.0f)
        return -1;

    float ax = 0, ay = 0;
    vg_widget_get_screen_bounds(&ce->base, &ax, &ay, NULL, NULL);
    (void)ax;

    float local_y = (float)y - ay + ce->scroll_y;
    float content_width = rt_codeeditor_content_draw_width(ce);
    int visual_row = ce->line_height > 0.0f ? (int)(local_y / ce->line_height) : 0;
    int line = 0;
    rt_codeeditor_locate_visual_row(ce, content_width, visual_row, &line, NULL);
    if (line < 0)
        line = 0;
    if (line >= ce->line_count)
        line = ce->line_count - 1;
    return line;
}

/// @brief Return the 0-based editor column at a screen-absolute X/Y coordinate.
int64_t rt_codeeditor_get_col_at_pixel(void *editor, int64_t x, int64_t y) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return -1;
    if (ce->line_count <= 0 || ce->char_width <= 0.0f)
        return -1;

    int64_t line64 = rt_codeeditor_get_line_at_pixel(editor, y);
    if (line64 < 0)
        return -1;
    int line = (int)line64;

    float ax = 0, ay = 0;
    vg_widget_get_screen_bounds(&ce->base, &ax, &ay, NULL, NULL);
    (void)ay;

    float local_x = (float)x - ax - ce->gutter_width;
    int col = 0;
    if (ce->word_wrap) {
        float content_width = rt_codeeditor_content_draw_width(ce);
        int chars_per_row = rt_codeeditor_chars_per_row(ce, content_width);
        int visual_row =
            ce->line_height > 0.0f ? (int)(((float)y - ay + ce->scroll_y) / ce->line_height) : 0;
        int row_in_line = 0;
        rt_codeeditor_locate_visual_row(ce, content_width, visual_row, NULL, &row_in_line);
        int col_in_row = ce->char_width > 0.0f ? (int)(local_x / ce->char_width + 0.5f) : 0;
        if (col_in_row < 0)
            col_in_row = 0;
        col = row_in_line * chars_per_row + col_in_row;
    } else {
        local_x += ce->scroll_x;
        col = (int)(local_x / ce->char_width + 0.5f);
    }
    if (col < 0)
        col = 0;
    int line_len = rt_codeeditor_line_length_i32(ce, line);
    if (col > line_len)
        col = line_len;
    return col;
}

/// @brief Insert text at the primary cursor position.
void rt_codeeditor_insert_at_cursor(void *editor, rt_string text) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce || !text)
        return;
    char *cstr = rt_string_to_gui_cstr(text);
    if (!cstr)
        return;
    vg_codeeditor_insert_text(ce, cstr);
    free(cstr);
}

/// @brief Return the identifier word under the primary cursor.
/// @details Scans left and right from cursor_col over [A-Za-z0-9_] characters.
rt_string rt_codeeditor_get_word_at_cursor(void *editor) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return rt_str_empty();
    if (ce->cursor_line < 0 || ce->cursor_line >= ce->line_count)
        return rt_str_empty();
    const char *text = ce->lines[ce->cursor_line].text;
    int len = rt_codeeditor_line_length_i32(ce, ce->cursor_line);
    int col = ce->cursor_col < len ? ce->cursor_col : len;

    /* scan left to find word start */
    int start = col;
    while (start > 0 && (isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_'))
        --start;

    /* scan right to find word end */
    int end = col;
    while (end < len && (isalnum((unsigned char)text[end]) || text[end] == '_'))
        ++end;

    return rt_string_from_bytes(text + start, (size_t)(end - start));
}

/// @brief Replace the identifier word under the primary cursor with new_text.
/// @details Selects the same word range that get_word_at_cursor() would return,
///          then inserts the replacement via vg_codeeditor_insert_text.
void rt_codeeditor_replace_word_at_cursor(void *editor, rt_string new_text) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return;
    if (ce->cursor_line < 0 || ce->cursor_line >= ce->line_count)
        return;
    const char *text = ce->lines[ce->cursor_line].text;
    int len = rt_codeeditor_line_length_i32(ce, ce->cursor_line);
    int col = ce->cursor_col < len ? ce->cursor_col : len;

    /* find word boundaries */
    int start = col;
    while (start > 0 && (isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_'))
        --start;
    int end = col;
    while (end < len && (isalnum((unsigned char)text[end]) || text[end] == '_'))
        ++end;

    char *cstr = rt_string_to_gui_cstr(new_text);
    if (cstr) {
        /* select the word, then insert the replacement (replaces selection) */
        int start_col = rt_codeeditor_byte_col_to_char_col(ce, ce->cursor_line, start);
        int end_col = rt_codeeditor_byte_col_to_char_col(ce, ce->cursor_line, end);
        vg_codeeditor_set_selection(ce, ce->cursor_line, start_col, ce->cursor_line, end_col);
        vg_codeeditor_insert_text(ce, cstr);
        free(cstr);
    }
}

/// @brief Return the text of a single line (0-based index).
rt_string rt_codeeditor_get_line(void *editor, int64_t line_index) {
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return rt_str_empty();
    if (line_index < 0 || line_index >= (int64_t)ce->line_count)
        return rt_str_empty();
    vg_code_line_t *line = &ce->lines[(int)line_index];
    return rt_string_from_bytes(line->text, line->length);
}

static int64_t rt_codeeditor_perf_i64(uint64_t value) {
    return value > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)value;
}

/// @brief Clear low-level editor performance counters.
void rt_codeeditor_reset_perf_stats(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (ce)
        vg_codeeditor_reset_perf_stats(ce);
}

/// @brief Return full-buffer materialization count.
int64_t rt_codeeditor_get_full_text_copy_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(ce);
    return rt_codeeditor_perf_i64(stats.full_text_copies);
}

/// @brief Return aggregate line visits from layout/scroll visual-row scans.
int64_t rt_codeeditor_get_layout_linear_scan_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(ce);
    uint64_t total = stats.total_height_linear_scans;
    if (UINT64_MAX - total < stats.total_visual_row_linear_scans)
        total = UINT64_MAX;
    else
        total += stats.total_visual_row_linear_scans;
    if (UINT64_MAX - total < stats.visual_row_linear_scans)
        total = UINT64_MAX;
    else
        total += stats.visual_row_linear_scans;
    if (UINT64_MAX - total < stats.locate_visual_row_linear_scans)
        total = UINT64_MAX;
    else
        total += stats.locate_visual_row_linear_scans;
    return rt_codeeditor_perf_i64(total);
}

/// @brief Return syntax-highlighter invocation count.
int64_t rt_codeeditor_get_syntax_highlight_call_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(ce);
    return rt_codeeditor_perf_i64(stats.line_highlight_calls);
}

/// @brief Return cached syntax-state line scan count.
int64_t rt_codeeditor_get_syntax_state_line_scan_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(ce);
    return rt_codeeditor_perf_i64(stats.syntax_state_line_scans);
}

/// @brief Return highlight span checks performed during paint.
int64_t rt_codeeditor_get_highlight_span_check_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(ce);
    return rt_codeeditor_perf_i64(stats.highlight_span_checks);
}

/// @brief Return bytes copied by full-buffer materializations.
int64_t rt_codeeditor_get_full_text_copy_byte_count(void *editor) {
    RT_ASSERT_MAIN_THREAD();
    vg_codeeditor_t *ce = rt_codeeditor_handle_checked(editor);
    if (!ce)
        return 0;
    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(ce);
    return rt_codeeditor_perf_i64(stats.full_text_copy_bytes);
}

#else /* !VIPER_ENABLE_GRAPHICS */

//=============================================================================
// CodeEditor Stubs (graphics disabled)
//=============================================================================
//
// Every public CodeEditor API has a no-op stub below so headless / server
// builds (without VIPER_ENABLE_GRAPHICS) link cleanly. Each stub:
//   - swallows its arguments via `(void)` casts to silence unused warnings
//   - returns a "neutral" value (0/-1/empty string) for getter signatures
//
// Callers that try to actually use a CodeEditor in a headless build will
// see no errors but also no output — matching the silent-stub pattern
// used elsewhere in the runtime.
//=============================================================================

/// @brief Stub: `CodeEditor.SetLanguage` is a no-op without graphics.
void rt_codeeditor_set_language(void *editor, rt_string language) {
    (void)editor;
    (void)language;
}

/// @brief Stub: `CodeEditor.SetTokenColor` is a no-op without graphics.
void rt_codeeditor_set_token_color(void *editor, int64_t token_type, int64_t color) {
    (void)editor;
    (void)token_type;
    (void)color;
}

/// @brief Stub: `CodeEditor.SetCustomKeywords` is a no-op without graphics.
void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords) {
    (void)editor;
    (void)keywords;
}

/// @brief Stub: `CodeEditor.ClearHighlights` is a no-op without graphics.
void rt_codeeditor_clear_highlights(void *editor) {
    (void)editor;
}

/// @brief Stub: `CodeEditor.AddHighlight` is a no-op without graphics.
void rt_codeeditor_add_highlight(void *editor,
                                 int64_t start_line,
                                 int64_t start_col,
                                 int64_t end_line,
                                 int64_t end_col,
                                 int64_t color) {
    (void)editor;
    (void)start_line;
    (void)start_col;
    (void)end_line;
    (void)end_col;
    (void)color;
}

/// @brief Stub: `CodeEditor.RefreshHighlights` is a no-op without graphics.
void rt_codeeditor_refresh_highlights(void *editor) {
    (void)editor;
}

/// @brief Stub: `CodeEditor.AddInlayHint` is a no-op without graphics.
void rt_codeeditor_add_inlay_hint(
    void *editor, int64_t line, int64_t col, rt_string text, int64_t color) {
    (void)editor;
    (void)line;
    (void)col;
    (void)text;
    (void)color;
}

/// @brief Stub: `CodeEditor.ClearInlayHints` is a no-op without graphics.
void rt_codeeditor_clear_inlay_hints(void *editor) {
    (void)editor;
}

/// @brief Stub: returns 0 (no inlay hint state without graphics).
int64_t rt_codeeditor_get_inlay_hint_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: `CodeEditor.SetShowLineNumbers` is a no-op without graphics.
void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show) {
    (void)editor;
    (void)show;
}

/// @brief Stub: returns the default visible line-number state in headless builds.
int64_t rt_codeeditor_get_show_line_numbers(void *editor) {
    (void)editor;
    return 1;
}

/// @brief Stub: `CodeEditor.SetLineNumberWidth` is a no-op without graphics.
void rt_codeeditor_set_line_number_width(void *editor, int64_t width) {
    (void)editor;
    (void)width;
}

/// @brief Stub: `CodeEditor.SetGutterIcon` is a no-op without graphics.
void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot) {
    (void)editor;
    (void)line;
    (void)pixels;
    (void)slot;
}

/// @brief Stub: `CodeEditor.ClearGutterIcon` is a no-op without graphics.
void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot) {
    (void)editor;
    (void)line;
    (void)slot;
}

/// @brief Stub: `CodeEditor.ClearAllGutterIcons` is a no-op without graphics.
void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot) {
    (void)editor;
    (void)slot;
}

/// @brief Stub: internal gutter-click injection is a no-op without graphics.
void rt_gui_set_gutter_click(int64_t line, int64_t slot) {
    (void)line;
    (void)slot;
}

/// @brief Stub: internal gutter-click clear is a no-op without graphics.
void rt_gui_clear_gutter_click(void) {}

/// @brief Stub: returns 0 (no gutter can be clicked in headless builds).
int64_t rt_codeeditor_was_gutter_clicked(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns -1 (no gutter click available in headless builds).
int64_t rt_codeeditor_get_gutter_clicked_line(void *editor) {
    (void)editor;
    return -1;
}

/// @brief Stub: returns -1 (no gutter click available in headless builds).
int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor) {
    (void)editor;
    return -1;
}

/// @brief Stub: `CodeEditor.SetShowFoldGutter` is a no-op without graphics.
void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show) {
    (void)editor;
    (void)show;
}

/// @brief Stub: `CodeEditor.AddFoldRegion` is a no-op without graphics.
void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line) {
    (void)editor;
    (void)start_line;
    (void)end_line;
}

/// @brief Stub: `CodeEditor.RemoveFoldRegion` is a no-op without graphics.
void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line) {
    (void)editor;
    (void)start_line;
}

/// @brief Stub: `CodeEditor.ClearFoldRegions` is a no-op without graphics.
void rt_codeeditor_clear_fold_regions(void *editor) {
    (void)editor;
}

/// @brief Stub: `CodeEditor.Fold` is a no-op without graphics.
void rt_codeeditor_fold(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

/// @brief Stub: `CodeEditor.Unfold` is a no-op without graphics.
void rt_codeeditor_unfold(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

/// @brief Stub: `CodeEditor.ToggleFold` is a no-op without graphics.
void rt_codeeditor_toggle_fold(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

/// @brief Stub: returns 0 (no fold state exists in headless builds).
int64_t rt_codeeditor_is_folded(void *editor, int64_t line) {
    (void)editor;
    (void)line;
    return 0;
}

/// @brief Stub: `CodeEditor.FoldAll` is a no-op without graphics.
void rt_codeeditor_fold_all(void *editor) {
    (void)editor;
}

/// @brief Stub: `CodeEditor.UnfoldAll` is a no-op without graphics.
void rt_codeeditor_unfold_all(void *editor) {
    (void)editor;
}

/// @brief Stub: `CodeEditor.SetAutoFoldDetection` is a no-op without graphics.
void rt_codeeditor_set_auto_fold_detection(void *editor, int64_t enable) {
    (void)editor;
    (void)enable;
}

/// @brief Stub: returns the primary cursor count in headless builds.
int64_t rt_codeeditor_get_cursor_count(void *editor) {
    (void)editor;
    return 1;
}

/// @brief Stub: `CodeEditor.AddCursor` is a no-op without graphics.
void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col) {
    (void)editor;
    (void)line;
    (void)col;
}

/// @brief Stub: `CodeEditor.RemoveCursor` is a no-op without graphics.
void rt_codeeditor_remove_cursor(void *editor, int64_t index) {
    (void)editor;
    (void)index;
}

/// @brief Stub: `CodeEditor.ClearExtraCursors` is a no-op without graphics.
void rt_codeeditor_clear_extra_cursors(void *editor) {
    (void)editor;
}

/// @brief Stub: returns 0 (no cursor state in headless builds).
int64_t rt_codeeditor_get_cursor_line_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

/// @brief Stub: returns 0 (no cursor state in headless builds).
int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

/// @brief Stub: returns 0 (no cursor state in headless builds).
int64_t rt_codeeditor_get_cursor_line(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (no cursor state in headless builds).
int64_t rt_codeeditor_get_cursor_col(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (no scroll state in headless builds).
int64_t rt_codeeditor_get_scroll_top_line(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: `CodeEditor.ScrollTopLine` setter is a no-op without graphics.
void rt_codeeditor_set_scroll_top_line(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

/// @brief Stub: `CodeEditor.SetCursorPositionAt` is a no-op without graphics.
void rt_codeeditor_set_cursor_position_at(void *editor, int64_t index, int64_t line, int64_t col) {
    (void)editor;
    (void)index;
    (void)line;
    (void)col;
}

/// @brief Stub: `CodeEditor.SetCursorSelection` is a no-op without graphics.
void rt_codeeditor_set_cursor_selection(void *editor,
                                        int64_t index,
                                        int64_t start_line,
                                        int64_t start_col,
                                        int64_t end_line,
                                        int64_t end_col) {
    (void)editor;
    (void)index;
    (void)start_line;
    (void)start_col;
    (void)end_line;
    (void)end_col;
}

/// @brief Stub: returns 0 (no selection exists in headless builds).
int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

/// @brief Stub: returns 0 (no selection exists in headless builds).
int64_t rt_codeeditor_get_selection_start_line_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

/// @brief Stub: returns 0 (no selection exists in headless builds).
int64_t rt_codeeditor_get_selection_start_col_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

/// @brief Stub: returns 0 (no selection exists in headless builds).
int64_t rt_codeeditor_get_selection_end_line_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

/// @brief Stub: returns 0 (no selection exists in headless builds).
int64_t rt_codeeditor_get_selection_end_col_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

/// @brief Stub: `CodeEditor.Undo` is a no-op without graphics.
void rt_codeeditor_undo(void *editor) {
    (void)editor;
}

/// @brief Stub: `CodeEditor.Redo` is a no-op without graphics.
void rt_codeeditor_redo(void *editor) {
    (void)editor;
}

/// @brief Stub: returns 0 (no undo history in headless builds).
int64_t rt_codeeditor_can_undo(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (no redo history in headless builds).
int64_t rt_codeeditor_can_redo(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (clipboard unavailable without graphics).
int64_t rt_codeeditor_copy(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (clipboard unavailable without graphics).
int64_t rt_codeeditor_cut(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (clipboard unavailable without graphics).
int64_t rt_codeeditor_paste(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: `CodeEditor.SelectAll` is a no-op without graphics.
void rt_codeeditor_select_all(void *editor) {
    (void)editor;
}

/// @brief Stub: `CodeEditor.SetTabSize` is a no-op without graphics.
void rt_codeeditor_set_tab_size(void *editor, int64_t size) {
    (void)editor;
    (void)size;
}

/// @brief Stub: returns 0 (no tab size state in headless builds).
int64_t rt_codeeditor_get_tab_size(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: `CodeEditor.SetInsertSpaces` is a no-op without graphics.
void rt_codeeditor_set_insert_spaces(void *editor, int64_t enabled) {
    (void)editor;
    (void)enabled;
}

/// @brief Stub: returns 1 so headless preference probes match the editor default.
int64_t rt_codeeditor_get_insert_spaces(void *editor) {
    (void)editor;
    return 1;
}

/// @brief Stub: `CodeEditor.SetWordWrap` is a no-op without graphics.
void rt_codeeditor_set_word_wrap(void *editor, int64_t enabled) {
    (void)editor;
    (void)enabled;
}

/// @brief Stub: returns 0 (no word-wrap state in headless builds).
int64_t rt_codeeditor_get_word_wrap(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (no pixel cursor position without graphics).
int64_t rt_codeeditor_get_cursor_pixel_x(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 (no pixel cursor position without graphics).
int64_t rt_codeeditor_get_cursor_pixel_y(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns -1 (no hit-testing without graphics).
int64_t rt_codeeditor_get_line_at_pixel(void *editor, int64_t y) {
    (void)editor;
    (void)y;
    return -1;
}

/// @brief Stub: returns -1 (no hit-testing without graphics).
int64_t rt_codeeditor_get_col_at_pixel(void *editor, int64_t x, int64_t y) {
    (void)editor;
    (void)x;
    (void)y;
    return -1;
}

/// @brief Stub: `CodeEditor.InsertAtCursor` is a no-op without graphics.
void rt_codeeditor_insert_at_cursor(void *editor, rt_string text) {
    (void)editor;
    (void)text;
}

/// @brief Stub: returns empty string (no word-at-cursor without graphics).
rt_string rt_codeeditor_get_word_at_cursor(void *editor) {
    (void)editor;
    return rt_str_empty();
}

/// @brief Stub: `CodeEditor.ReplaceWordAtCursor` is a no-op without graphics.
void rt_codeeditor_replace_word_at_cursor(void *editor, rt_string new_text) {
    (void)editor;
    (void)new_text;
}

/// @brief Stub: returns empty string (no line content without graphics).
rt_string rt_codeeditor_get_line(void *editor, int64_t line_index) {
    (void)editor;
    (void)line_index;
    return rt_str_empty();
}

/// @brief Stub: `CodeEditor.ResetPerfStats` is a no-op without graphics.
void rt_codeeditor_reset_perf_stats(void *editor) {
    (void)editor;
}

/// @brief Stub: returns 0 without graphics.
int64_t rt_codeeditor_get_full_text_copy_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 without graphics.
int64_t rt_codeeditor_get_layout_linear_scan_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 without graphics.
int64_t rt_codeeditor_get_syntax_highlight_call_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 without graphics.
int64_t rt_codeeditor_get_syntax_state_line_scan_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 without graphics.
int64_t rt_codeeditor_get_highlight_span_check_count(void *editor) {
    (void)editor;
    return 0;
}

/// @brief Stub: returns 0 without graphics.
int64_t rt_codeeditor_get_full_text_copy_byte_count(void *editor) {
    (void)editor;
    return 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
