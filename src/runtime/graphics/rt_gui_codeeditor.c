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

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// CodeEditor Enhancements - Syntax Highlighting (Phase 4)
//=============================================================================

// VS Code dark-theme inspired palette (ARGB 0xAARRGGBB)
#define SYN_COLOR_DEFAULT 0xFFD4D4D4u // light grey
#define SYN_COLOR_KEYWORD 0xFF569CD6u // blue
#define SYN_COLOR_TYPE 0xFF4EC9B0u    // teal
#define SYN_COLOR_STRING 0xFFCE9178u  // orange
#define SYN_COLOR_COMMENT 0xFF6A9955u // green
#define SYN_COLOR_NUMBER 0xFFB5CEA8u  // light green

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

/// @brief Linear-scan the editor's user-supplied keyword list for an exact match.
///
/// Custom keywords let scripts add domain-specific syntax (e.g., your
/// game's scripting commands) without modifying the built-in tables.
/// Case-sensitive — matches `SetCustomKeywords`'s contract.
static int syn_is_custom_keyword(const char *word, size_t wlen, vg_codeeditor_t *ce) {
    if (!ce || !ce->custom_keywords)
        return 0;
    for (int i = 0; i < ce->custom_keyword_count; i++) {
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

static const char *const zia_keywords[] = {
    "func",  "expose", "hide",   "class", "struct",   "var",   "new",  "if", "else", "while",
    "for",   "in",     "return", "break", "continue", "do",    "and",  "or", "not",  "true",
    "false", "null",   "module", "bind",  "self",     "match", "enum", NULL};

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

/// @brief Tokenize a Zia source line and write per-character color codes.
///
/// Walks the line once, classifying each span:
///   - `// ...` line comment → green
///   - `"..."` string (with `\\` escape handling) → orange
///   - `[0-9]+` number → light green
///   - identifier → keyword/type/custom-keyword/default lookup
///   - everything else → default color
/// Per-line tokenization (no multi-line state) — block comments would
/// need a stateful tokenizer.
static void rt_zia_syntax_cb(
    vg_widget_t *editor, int line_num, const char *text, uint32_t *colors, void *user_data) {
    (void)line_num;
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

    while (i < len) {
        // Line comment
        if (text[i] == '/' && i + 1 < len && text[i + 1] == '/') {
            syn_fill(colors, i, len - i, c_comment);
            return;
        }

        // String literal
        if (text[i] == '"') {
            size_t start = i++;
            while (i < len && text[i] != '"') {
                if (text[i] == '\\')
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
            if (syn_is_keyword(text + start, wlen, zia_keywords))
                color = c_keyword;
            else if (syn_is_keyword(text + start, wlen, zia_types))
                color = c_type;
            else if (syn_is_custom_keyword(text + start, wlen, ce))
                color = c_keyword;
            syn_fill(colors, start, wlen, color);
            continue;
        }

        // Default (operators, punctuation)
        colors[i++] = c_default;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    // Token type indices: 0=default, 1=keyword, 2=type, 3=string, 4=comment, 5=number
    if (token_type >= 0 && token_type < 6) {
        ce->token_colors[token_type] = (uint32_t)color;
        ce->base.needs_paint = true;
    }
}

/// @brief `CodeEditor.SetCustomKeywords(keywords)` — install a comma-separated keyword list.
///
/// The list is parsed into `ce->custom_keywords` (newly allocated copy
/// of each token, leading/trailing whitespace trimmed). Replaces any
/// previous list (no append). Empty input clears the list. Doubling
/// growth from an initial capacity of 8.
void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;

    // Free old custom keywords
    for (int i = 0; i < ce->custom_keyword_count; i++)
        free(ce->custom_keywords[i]);
    free(ce->custom_keywords);
    ce->custom_keywords = NULL;
    ce->custom_keyword_count = 0;

    // Parse comma-separated keywords into array
    char *ckw = rt_string_to_cstr(keywords);
    if (!ckw || !ckw[0]) {
        free(ckw);
        return;
    }

    // Count commas to estimate capacity
    int cap = 8;
    ce->custom_keywords = (char **)malloc((size_t)cap * sizeof(char *));
    if (!ce->custom_keywords) {
        free(ckw);
        return;
    }

    char *saveptr = NULL;
    char *token = rt_strtok_r(ckw, ",", &saveptr);
    while (token) {
        // Trim whitespace
        while (*token == ' ')
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ')
            *end-- = '\0';

        if (*token) {
            if (ce->custom_keyword_count >= cap) {
                cap *= 2;
                char **p = (char **)realloc(ce->custom_keywords, (size_t)cap * sizeof(char *));
                if (!p)
                    break;
                ce->custom_keywords = p;
            }
            ce->custom_keywords[ce->custom_keyword_count++] = strdup(token);
        }
        token = rt_strtok_r(NULL, ",", &saveptr);
    }
    free(ckw);
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.ClearHighlights()` — remove every custom highlight span.
///
/// Highlights are user-defined colored ranges painted on top of the
/// syntax highlighting (e.g., for diagnostics, find-results, etc.).
void rt_codeeditor_clear_highlights(void *editor) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    free(ce->highlight_spans);
    ce->highlight_spans = NULL;
    ce->highlight_span_count = 0;
    ce->highlight_span_cap = 0;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->highlight_span_count >= ce->highlight_span_cap) {
        int new_cap = ce->highlight_span_cap ? ce->highlight_span_cap * 2 : 8;
        void *p = realloc(ce->highlight_spans, (size_t)new_cap * sizeof(*ce->highlight_spans));
        if (!p)
            return;
        ce->highlight_spans = p;
        ce->highlight_span_cap = new_cap;
    }
    struct vg_highlight_span *s = &ce->highlight_spans[ce->highlight_span_count++];
    s->start_line = (int)start_line;
    s->start_col = (int)start_col;
    s->end_line = (int)end_line;
    s->end_col = (int)end_col;
    s->color = (uint32_t)color;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.RefreshHighlights()` — schedule a repaint.
///
/// Useful when callers mutate highlight state through other means
/// and need the editor to redraw on the next frame.
void rt_codeeditor_refresh_highlights(void *editor) {
    if (!editor)
        return;
    ((vg_codeeditor_t *)editor)->base.needs_paint = true;
}

//=============================================================================
// CodeEditor Enhancements - Gutter & Line Numbers (Phase 4)
//=============================================================================

/// @brief `CodeEditor.SetShowLineNumbers(show)` — toggle the line-number gutter.
void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->show_line_numbers = show != 0;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.GetShowLineNumbers` — read the line-number visibility flag.
///
/// Returns 1 (showing) for NULL receiver to match the default.
int64_t rt_codeeditor_get_show_line_numbers(void *editor) {
    if (!editor)
        return 1; // Default to showing
    return ((vg_codeeditor_t *)editor)->show_line_numbers ? 1 : 0;
}

/// @brief `CodeEditor.SetLineNumberWidth(width)` — set gutter width in characters.
///
/// Internally stored as pixels (`width * char_width`) so layout doesn't
/// have to keep recomputing it.
void rt_codeeditor_set_line_number_width(void *editor, int64_t width) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->line_number_width_override = width > 0 ? (float)((int)width) : 0.0f;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    /* slot maps to icon type: 0=breakpoint, 1=warning, 2=error, 3=info */
    int type = (int)(slot & 3);
    /* Update existing icon on same line+type if present */
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].line == (int)line && ce->gutter_icons[i].type == type) {
            vg_icon_destroy(&ce->gutter_icons[i].image);
            ce->gutter_icons[i].image = rt_codeeditor_icon_from_pixels(pixels);
            ce->base.needs_paint = true;
            return;
        }
    }
    if (ce->gutter_icon_count >= ce->gutter_icon_cap) {
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
    icon->line = (int)line;
    icon->type = type;
    icon->color = s_type_colors[type < 0 || type >= 4 ? 0 : type];
    icon->image = rt_codeeditor_icon_from_pixels(pixels);
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.ClearGutterIcon(line, slot)` — remove one icon entry.
///
/// Swap-with-last compaction. No-op if no matching icon exists.
void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int type = (int)(slot & 3);
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].line == (int)line && ce->gutter_icons[i].type == type) {
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int type = (int)(slot & 3);
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
    // Legacy global entry point — forwards to a per-editor setter.
    // The vg layer paint callback doesn't know which editor was clicked,
    // so we broadcast to the most-recently-focused editor via s_current_app.
    // A future improvement would pass the editor pointer through the callback.
    (void)line;
    (void)slot;
}

/// @brief Legacy global clear — currently a no-op (state lives per-editor).
void rt_gui_clear_gutter_click(void) {
    // No-op: per-editor state is cleared after read in the getter functions.
}

/// @brief `CodeEditor.WasGutterClicked` — edge-detect: true once per click.
///
/// Returns the latched click flag and clears it as a side effect, so
/// subsequent reads return 0 until the next click. Pair with
/// `GetGutterClickedLine`/`Slot` for the click coordinates.
int64_t rt_codeeditor_was_gutter_clicked(void *editor) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int64_t result = ce->gutter_clicked ? 1 : 0;
    ce->gutter_clicked = false; // Edge-triggered: clear after read
    return result;
}

/// @brief `CodeEditor.GetGutterClickedLine` — line number of the most recent click.
///
/// Returns -1 for NULL receiver or no click. Stale after `WasGutterClicked`
/// reads/clears the flag.
int64_t rt_codeeditor_get_gutter_clicked_line(void *editor) {
    if (!editor)
        return -1;
    return ((vg_codeeditor_t *)editor)->gutter_clicked_line;
}

/// @brief `CodeEditor.GetGutterClickedSlot` — slot index of the most recent click.
int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor) {
    if (!editor)
        return -1;
    return ((vg_codeeditor_t *)editor)->gutter_clicked_slot;
}

/// @brief `CodeEditor.SetShowFoldGutter(show)` — toggle the fold-region gutter.
///
/// The fold gutter sits next to the line-number gutter and shows
/// triangle indicators next to foldable regions.
void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (start_line < 0)
        start_line = 0;
    if (end_line >= ce->line_count)
        end_line = ce->line_count - 1;
    if (end_line <= start_line)
        return;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)start_line) {
            ce->fold_regions[i].end_line = (int)end_line;
            ce->fold_regions[i].folded = false;
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
    if (ce->fold_region_count >= ce->fold_region_cap) {
        int new_cap = ce->fold_region_cap ? ce->fold_region_cap * 2 : 8;
        void *p = realloc(ce->fold_regions, (size_t)new_cap * sizeof(*ce->fold_regions));
        if (!p)
            return;
        ce->fold_regions = p;
        ce->fold_region_cap = new_cap;
    }
    struct vg_fold_region *r = &ce->fold_regions[ce->fold_region_count++];
    r->start_line = (int)start_line;
    r->end_line = (int)end_line;
    r->folded = false;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.RemoveFoldRegion(startLine)` — drop a fold region.
///
/// Identified by the start line. Swap-with-last compaction. No-op if
/// no region starts at the given line.
void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)start_line) {
            ce->fold_regions[i] = ce->fold_regions[--ce->fold_region_count];
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.ClearFoldRegions` — drop every registered fold region.
void rt_codeeditor_clear_fold_regions(void *editor) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    free(ce->fold_regions);
    ce->fold_regions = NULL;
    ce->fold_region_count = 0;
    ce->fold_region_cap = 0;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.Fold(line)` — collapse the region starting at `line`.
void rt_codeeditor_fold(void *editor, int64_t line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)line) {
            ce->fold_regions[i].folded = true;
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.Unfold(line)` — expand the region starting at `line`.
void rt_codeeditor_unfold(void *editor, int64_t line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)line) {
            ce->fold_regions[i].folded = false;
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.ToggleFold(line)` — flip the folded state of one region.
void rt_codeeditor_toggle_fold(void *editor, int64_t line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)line) {
            ce->fold_regions[i].folded = !ce->fold_regions[i].folded;
            vg_codeeditor_refresh_layout_state(ce);
            return;
        }
    }
}

/// @brief `CodeEditor.IsFolded(line)` — true iff the region starting at `line` is collapsed.
int64_t rt_codeeditor_is_folded(void *editor, int64_t line) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)line)
            return ce->fold_regions[i].folded ? 1 : 0;
    }
    return 0;
}

/// @brief `CodeEditor.FoldAll` — collapse every fold region.
void rt_codeeditor_fold_all(void *editor) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
        ce->fold_regions[i].folded = true;
    vg_codeeditor_refresh_layout_state(ce);
}

/// @brief `CodeEditor.UnfoldAll` — expand every fold region.
void rt_codeeditor_unfold_all(void *editor) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
            if (cur_indent >= (int)ce->lines[i].length)
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
                    if (indent >= (int)ce->lines[j].length) {
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
    if (*col > (int)ce->lines[*line].length)
        *col = (int)ce->lines[*line].length;
}

/// @brief `CodeEditor.GetCursorCount` — number of active cursors (always >= 1).
///
/// Always 1 + extras; the primary cursor is always present. Returns 1
/// for NULL receiver to match the "at least one cursor" invariant.
int64_t rt_codeeditor_get_cursor_count(void *editor) {
    if (!editor)
        return 1;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return 1 + ce->extra_cursor_count;
}

/// @brief `CodeEditor.AddCursor(line, col)` — add a secondary cursor.
///
/// Index 0 is reserved for the primary cursor; new cursors get
/// indices 1, 2, … Position is clamped to a valid buffer position.
/// Geometric growth (cap doubles, starting at 4).
void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->extra_cursor_count >= ce->extra_cursor_cap) {
        int new_cap = ce->extra_cursor_cap ? ce->extra_cursor_cap * 2 : 4;
        void *p = realloc(ce->extra_cursors, (size_t)new_cap * sizeof(*ce->extra_cursors));
        if (!p)
            return;
        ce->extra_cursors = p;
        ce->extra_cursor_cap = new_cap;
    }
    struct vg_extra_cursor *c = &ce->extra_cursors[ce->extra_cursor_count++];
    c->line = (int)line;
    c->col = (int)col;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (index == 0)
        return ce->cursor_line;
    int extra_idx = (int)index - 1;
    if (extra_idx >= 0 && extra_idx < ce->extra_cursor_count)
        return ce->extra_cursors[extra_idx].line;
    return 0;
}

/// @brief `CodeEditor.GetCursorColAt(index)` — column of the i-th cursor.
int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (index == 0)
        return ce->cursor_col;
    int extra_idx = (int)index - 1;
    if (extra_idx >= 0 && extra_idx < ce->extra_cursor_count)
        return ce->extra_cursors[extra_idx].col;
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

/// @brief `CodeEditor.SetCursorPositionAt(index, line, col)` — move one cursor.
///
/// Index 0 routes to the underlying widget's `set_cursor` (which also
/// scrolls the viewport). Index 1+ updates the extras directly.
/// Position is clamped; selection is cleared.
void rt_codeeditor_set_cursor_position_at(void *editor, int64_t index, int64_t line, int64_t col) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (index == 0) {
        vg_codeeditor_set_cursor(ce, (int)line, (int)col);
        return;
    }
    int extra_idx = (int)index - 1;
    if (extra_idx < 0 || extra_idx >= ce->extra_cursor_count)
        return;
    ce->extra_cursors[extra_idx].line = (int)line;
    ce->extra_cursors[extra_idx].col = (int)col;
    rt_codeeditor_clamp_position(
        ce, &ce->extra_cursors[extra_idx].line, &ce->extra_cursors[extra_idx].col);
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int s_line = (int)start_line;
    int s_col = (int)start_col;
    int e_line = (int)end_line;
    int e_col = (int)end_col;
    rt_codeeditor_clamp_position(ce, &s_line, &s_col);
    rt_codeeditor_clamp_position(ce, &e_line, &e_col);

    if (index == 0) {
        vg_codeeditor_set_selection(ce, s_line, s_col, e_line, e_col);
        return;
    }

    int extra_idx = (int)index - 1;
    if (extra_idx < 0 || extra_idx >= ce->extra_cursor_count)
        return;

    ce->extra_cursors[extra_idx].selection.start_line = s_line;
    ce->extra_cursors[extra_idx].selection.start_col = s_col;
    ce->extra_cursors[extra_idx].selection.end_line = e_line;
    ce->extra_cursors[extra_idx].selection.end_col = e_col;
    ce->extra_cursors[extra_idx].line = e_line;
    ce->extra_cursors[extra_idx].col = e_col;
    ce->extra_cursors[extra_idx].has_selection = true;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.CursorHasSelection(index)` — whether the i-th cursor has a selection.
///
/// Index 0 reads the editor's main `has_selection` flag; extras keep
/// their own per-cursor flag set by `SetCursorSelection`.
int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (index == 0)
        return ce->has_selection ? 1 : 0;
    int extra_idx = (int)index - 1;
    if (extra_idx < 0 || extra_idx >= ce->extra_cursor_count)
        return 0;
    return ce->extra_cursors[extra_idx].has_selection ? 1 : 0;
}

// Edit-history and clipboard ops — thin wrappers around the underlying
// `vg_codeeditor_*` widget API. NULL receiver is a no-op (or zero return).

/// @brief `CodeEditor.Undo` — pop one entry from the undo stack.
void rt_codeeditor_undo(void *editor) {
    if (editor)
        vg_codeeditor_undo((vg_codeeditor_t *)editor);
}

/// @brief `CodeEditor.Redo` — re-apply one undone entry.
void rt_codeeditor_redo(void *editor) {
    if (editor)
        vg_codeeditor_redo((vg_codeeditor_t *)editor);
}

/// @brief `CodeEditor.CanUndo` — true when the undo stack has an available entry.
int64_t rt_codeeditor_can_undo(void *editor) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return (ce->history && ce->history->current_index > 0) ? 1 : 0;
}

/// @brief `CodeEditor.CanRedo` — true when the redo stack has an available entry.
int64_t rt_codeeditor_can_redo(void *editor) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return (ce->history && ce->history->current_index < ce->history->count) ? 1 : 0;
}

/// @brief `CodeEditor.Copy` — copy selection to the system clipboard. Returns 1 on success.
int64_t rt_codeeditor_copy(void *editor) {
    if (!editor)
        return 0;
    return vg_codeeditor_copy((vg_codeeditor_t *)editor) ? 1 : 0;
}

/// @brief `CodeEditor.Cut` — copy selection then delete. Returns 1 on success.
int64_t rt_codeeditor_cut(void *editor) {
    if (!editor)
        return 0;
    return vg_codeeditor_cut((vg_codeeditor_t *)editor) ? 1 : 0;
}

/// @brief `CodeEditor.Paste` — insert clipboard text at cursor. Returns 1 on success.
int64_t rt_codeeditor_paste(void *editor) {
    if (!editor)
        return 0;
    return vg_codeeditor_paste((vg_codeeditor_t *)editor) ? 1 : 0;
}

/// @brief `CodeEditor.SelectAll` — selection from buffer start to end.
void rt_codeeditor_select_all(void *editor) {
    if (editor)
        vg_codeeditor_select_all((vg_codeeditor_t *)editor);
}

/// @brief `CodeEditor.SetTabSize` — set tab width in spaces.
void rt_codeeditor_set_tab_size(void *editor, int64_t size) {
    if (!editor)
        return;
    if (size < 1)
        size = 1;
    if (size > 16)
        size = 16;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->tab_width = (int)size;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.GetTabSize` — return tab width in spaces.
int64_t rt_codeeditor_get_tab_size(void *editor) {
    if (!editor)
        return 0;
    return ((vg_codeeditor_t *)editor)->tab_width;
}

/// @brief `CodeEditor.SetWordWrap` — toggle display-only word wrapping.
void rt_codeeditor_set_word_wrap(void *editor, int64_t enabled) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->word_wrap = enabled != 0;
    if (ce->word_wrap)
        ce->scroll_x = 0.0f;
    ce->base.needs_layout = true;
    ce->base.needs_paint = true;
}

/// @brief `CodeEditor.GetWordWrap` — return display-only word wrapping state.
int64_t rt_codeeditor_get_word_wrap(void *editor) {
    if (!editor)
        return 0;
    return ((vg_codeeditor_t *)editor)->word_wrap ? 1 : 0;
}

//=============================================================================
// CodeEditor Completion Helpers
//=============================================================================

#define RT_CODEEDITOR_SCROLLBAR_WIDTH 12.0f

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

static int rt_codeeditor_chars_per_row(const vg_codeeditor_t *ce, float content_width) {
    if (!ce || !ce->word_wrap || ce->char_width <= 0.0f || content_width <= 0.0f)
        return 0;
    int chars = (int)(content_width / ce->char_width);
    return chars > 0 ? chars : 1;
}

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
    return (int)((len + (size_t)chars_per_row - 1) / (size_t)chars_per_row);
}

static int rt_codeeditor_visual_rows_for_line(const vg_codeeditor_t *ce,
                                              int line,
                                              float content_width) {
    if (!ce || line < 0 || line >= ce->line_count || rt_codeeditor_line_is_hidden(ce, line))
        return 0;
    if (!ce->word_wrap)
        return 1;
    return rt_codeeditor_wrapped_rows_for_line(ce, line, content_width);
}

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
        int total_rows = 0;
        for (int i = 0; i < ce->line_count; i++) {
            total_rows += rt_codeeditor_visual_rows_for_line(ce, i, content_width);
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

    int visual_row = 0;
    for (int i = 0; i < line; i++) {
        visual_row += rt_codeeditor_visual_rows_for_line(ce, i, content_width);
    }

    int row_index = 0;
    rt_codeeditor_visual_offset_for_position(ce, content_width, line, col, &row_index, NULL);
    return visual_row + row_index;
}

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

    int accumulated = 0;
    for (int line = 0; line < ce->line_count; line++) {
        int row_count = rt_codeeditor_visual_rows_for_line(ce, line, content_width);
        if (row_count == 0)
            continue;
        if (visual_row < accumulated + row_count) {
            if (out_line)
                *out_line = line;
            if (out_row_in_line)
                *out_row_in_line = visual_row - accumulated;
            return;
        }
        accumulated += row_count;
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
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return -1;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (!editor)
        return -1;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    if (col > (int)ce->lines[line].length)
        col = (int)ce->lines[line].length;
    return col;
}

/// @brief Insert text at the primary cursor position.
void rt_codeeditor_insert_at_cursor(void *editor, rt_string text) {
    if (!editor || !text)
        return;
    char *cstr = rt_string_to_cstr(text);
    if (!cstr)
        return;
    vg_codeeditor_insert_text((vg_codeeditor_t *)editor, cstr);
    free(cstr);
}

/// @brief Return the identifier word under the primary cursor.
/// @details Scans left and right from cursor_col over [A-Za-z0-9_] characters.
rt_string rt_codeeditor_get_word_at_cursor(void *editor) {
    if (!editor)
        return rt_str_empty();
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->cursor_line < 0 || ce->cursor_line >= ce->line_count)
        return rt_str_empty();
    const char *text = ce->lines[ce->cursor_line].text;
    int len = (int)ce->lines[ce->cursor_line].length;
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
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->cursor_line < 0 || ce->cursor_line >= ce->line_count)
        return;
    const char *text = ce->lines[ce->cursor_line].text;
    int len = (int)ce->lines[ce->cursor_line].length;
    int col = ce->cursor_col < len ? ce->cursor_col : len;

    /* find word boundaries */
    int start = col;
    while (start > 0 && (isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_'))
        --start;
    int end = col;
    while (end < len && (isalnum((unsigned char)text[end]) || text[end] == '_'))
        ++end;

    /* select the word, then insert the replacement (replaces selection) */
    vg_codeeditor_set_selection(
        (vg_codeeditor_t *)editor, ce->cursor_line, start, ce->cursor_line, end);
    char *cstr = rt_string_to_cstr(new_text);
    if (cstr) {
        vg_codeeditor_insert_text((vg_codeeditor_t *)editor, cstr);
        free(cstr);
    }
}

/// @brief Return the text of a single line (0-based index).
rt_string rt_codeeditor_get_line(void *editor, int64_t line_index) {
    if (!editor)
        return rt_str_empty();
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (line_index < 0 || line_index >= (int64_t)ce->line_count)
        return rt_str_empty();
    vg_code_line_t *line = &ce->lines[(int)line_index];
    return rt_string_from_bytes(line->text, line->length);
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

void rt_codeeditor_set_token_color(void *editor, int64_t token_type, int64_t color) {
    (void)editor;
    (void)token_type;
    (void)color;
}

void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords) {
    (void)editor;
    (void)keywords;
}

void rt_codeeditor_clear_highlights(void *editor) {
    (void)editor;
}

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

void rt_codeeditor_refresh_highlights(void *editor) {
    (void)editor;
}

void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show) {
    (void)editor;
    (void)show;
}

int64_t rt_codeeditor_get_show_line_numbers(void *editor) {
    (void)editor;
    return 0;
}

void rt_codeeditor_set_line_number_width(void *editor, int64_t width) {
    (void)editor;
    (void)width;
}

void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot) {
    (void)editor;
    (void)line;
    (void)pixels;
    (void)slot;
}

void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot) {
    (void)editor;
    (void)line;
    (void)slot;
}

void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot) {
    (void)editor;
    (void)slot;
}

void rt_gui_set_gutter_click(int64_t line, int64_t slot) {
    (void)line;
    (void)slot;
}

void rt_gui_clear_gutter_click(void) {}

int64_t rt_codeeditor_was_gutter_clicked(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_get_gutter_clicked_line(void *editor) {
    (void)editor;
    return -1;
}

int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor) {
    (void)editor;
    return -1;
}

void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show) {
    (void)editor;
    (void)show;
}

void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line) {
    (void)editor;
    (void)start_line;
    (void)end_line;
}

void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line) {
    (void)editor;
    (void)start_line;
}

void rt_codeeditor_clear_fold_regions(void *editor) {
    (void)editor;
}

void rt_codeeditor_fold(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

void rt_codeeditor_unfold(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

void rt_codeeditor_toggle_fold(void *editor, int64_t line) {
    (void)editor;
    (void)line;
}

int64_t rt_codeeditor_is_folded(void *editor, int64_t line) {
    (void)editor;
    (void)line;
    return 0;
}

void rt_codeeditor_fold_all(void *editor) {
    (void)editor;
}

void rt_codeeditor_unfold_all(void *editor) {
    (void)editor;
}

void rt_codeeditor_set_auto_fold_detection(void *editor, int64_t enable) {
    (void)editor;
    (void)enable;
}

int64_t rt_codeeditor_get_cursor_count(void *editor) {
    (void)editor;
    return 0;
}

void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col) {
    (void)editor;
    (void)line;
    (void)col;
}

void rt_codeeditor_remove_cursor(void *editor, int64_t index) {
    (void)editor;
    (void)index;
}

void rt_codeeditor_clear_extra_cursors(void *editor) {
    (void)editor;
}

int64_t rt_codeeditor_get_cursor_line_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

int64_t rt_codeeditor_get_cursor_line(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_get_cursor_col(void *editor) {
    (void)editor;
    return 0;
}

void rt_codeeditor_set_cursor_position_at(void *editor, int64_t index, int64_t line, int64_t col) {
    (void)editor;
    (void)index;
    (void)line;
    (void)col;
}

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

int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index) {
    (void)editor;
    (void)index;
    return 0;
}

void rt_codeeditor_undo(void *editor) {
    (void)editor;
}

void rt_codeeditor_redo(void *editor) {
    (void)editor;
}

int64_t rt_codeeditor_can_undo(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_can_redo(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_copy(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_cut(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_paste(void *editor) {
    (void)editor;
    return 0;
}

void rt_codeeditor_select_all(void *editor) {
    (void)editor;
}

void rt_codeeditor_set_tab_size(void *editor, int64_t size) {
    (void)editor;
    (void)size;
}

int64_t rt_codeeditor_get_tab_size(void *editor) {
    (void)editor;
    return 0;
}

void rt_codeeditor_set_word_wrap(void *editor, int64_t enabled) {
    (void)editor;
    (void)enabled;
}

int64_t rt_codeeditor_get_word_wrap(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_get_cursor_pixel_x(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_get_cursor_pixel_y(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_get_line_at_pixel(void *editor, int64_t y) {
    (void)editor;
    (void)y;
    return -1;
}

int64_t rt_codeeditor_get_col_at_pixel(void *editor, int64_t x, int64_t y) {
    (void)editor;
    (void)x;
    (void)y;
    return -1;
}

void rt_codeeditor_insert_at_cursor(void *editor, rt_string text) {
    (void)editor;
    (void)text;
}

rt_string rt_codeeditor_get_word_at_cursor(void *editor) {
    (void)editor;
    return rt_str_empty();
}

void rt_codeeditor_replace_word_at_cursor(void *editor, rt_string new_text) {
    (void)editor;
    (void)new_text;
}

rt_string rt_codeeditor_get_line(void *editor, int64_t line_index) {
    (void)editor;
    (void)line_index;
    return rt_str_empty();
}

#endif /* VIPER_ENABLE_GRAPHICS */
