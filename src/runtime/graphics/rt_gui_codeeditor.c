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

#include "rt_gui_internal.h"
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

// Fill `n` colors with `color` starting at `colors[pos]`
static void syn_fill(uint32_t *colors, size_t pos, size_t n, uint32_t color) {
    for (size_t i = 0; i < n; i++)
        colors[pos + i] = color;
}

// Resolve a token color: use per-editor override if set, else theme default.
// Token type indices: 0=default, 1=keyword, 2=type, 3=string, 4=comment, 5=number
static uint32_t syn_color(vg_codeeditor_t *ce, int token_type, uint32_t fallback) {
    if (ce && token_type >= 0 && token_type < 6 && ce->token_colors[token_type])
        return ce->token_colors[token_type];
    return fallback;
}

// Check if word matches any custom keywords (case-sensitive)
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

// Check if character is an identifier start character
static int syn_is_id_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Check if character is an identifier continuation character
static int syn_is_id_cont(char c) {
    return syn_is_id_start(c) || (c >= '0' && c <= '9');
}

// Case-insensitive string equality check for a fixed-length word
static int syn_word_eq_ci(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char ca = (a[i] >= 'a' && a[i] <= 'z') ? a[i] - 32 : a[i];
        char cb = (b[i] >= 'a' && b[i] <= 'z') ? b[i] - 32 : b[i];
        if (ca != cb)
            return 0;
    }
    return 1;
}

// Match `word` (length wlen) against a NULL-terminated keyword table (case-sensitive)
static int syn_is_keyword(const char *word, size_t wlen, const char *const *table) {
    for (int i = 0; table[i]; i++) {
        size_t klen = strlen(table[i]);
        if (klen == wlen && memcmp(word, table[i], wlen) == 0)
            return 1;
    }
    return 0;
}

// Match `word` (length wlen) against a NULL-terminated keyword table (case-insensitive)
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
    "func",  "expose", "hide",   "class",  "struct",   "var",   "new",  "if", "else", "while",
    "for",   "in",     "return", "break",  "continue", "do",    "and",  "or", "not",  "true",
    "false", "null",   "module", "bind",   "self",     "match", "enum", NULL};

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

void rt_codeeditor_refresh_highlights(void *editor) {
    if (!editor)
        return;
    ((vg_codeeditor_t *)editor)->base.needs_paint = true;
}

//=============================================================================
// CodeEditor Enhancements - Gutter & Line Numbers (Phase 4)
//=============================================================================

void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->show_line_numbers = show != 0;
}

int64_t rt_codeeditor_get_show_line_numbers(void *editor) {
    if (!editor)
        return 1; // Default to showing
    return ((vg_codeeditor_t *)editor)->show_line_numbers ? 1 : 0;
}

void rt_codeeditor_set_line_number_width(void *editor, int64_t width) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->gutter_width = (float)((int)width) * ce->char_width;
}

void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    /* slot maps to icon type: 0=breakpoint, 1=warning, 2=error, 3=info */
    int type = (int)(slot & 3);
    /* Update existing icon on same line+type if present */
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].line == (int)line && ce->gutter_icons[i].type == type) {
            ce->base.needs_paint = true;
            return; /* already registered */
        }
    }
    if (ce->gutter_icon_count >= ce->gutter_icon_cap) {
        int new_cap = ce->gutter_icon_cap ? ce->gutter_icon_cap * 2 : 8;
        void *p = realloc(ce->gutter_icons, (size_t)new_cap * sizeof(*ce->gutter_icons));
        if (!p)
            return;
        ce->gutter_icons = p;
        ce->gutter_icon_cap = new_cap;
    }
    /* Default color per type */
    static const uint32_t s_type_colors[] = {0xFFE81123, 0xFFFFB900, 0xFFE81123, 0xFF0078D4};
    struct vg_gutter_icon *icon = &ce->gutter_icons[ce->gutter_icon_count++];
    icon->line = (int)line;
    icon->type = type;
    icon->color = s_type_colors[type < 0 || type >= 4 ? 0 : type];
    (void)pixels; /* pixel icons not yet blitted; use colored disc */
    ce->base.needs_paint = true;
}

void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int type = (int)(slot & 3);
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].line == (int)line && ce->gutter_icons[i].type == type) {
            /* Swap-remove */
            ce->gutter_icons[i] = ce->gutter_icons[--ce->gutter_icon_count];
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int type = (int)(slot & 3);
    int w = 0;
    for (int i = 0; i < ce->gutter_icon_count; i++) {
        if (ce->gutter_icons[i].type != type)
            ce->gutter_icons[w++] = ce->gutter_icons[i];
    }
    ce->gutter_icon_count = w;
    ce->base.needs_paint = true;
}

// Gutter click tracking — per-editor state (not global statics) so
// multiple CodeEditor instances each track their own gutter clicks.

void rt_gui_set_gutter_click(int64_t line, int64_t slot) {
    // Legacy global entry point — forwards to a per-editor setter.
    // The vg layer paint callback doesn't know which editor was clicked,
    // so we broadcast to the most-recently-focused editor via s_current_app.
    // A future improvement would pass the editor pointer through the callback.
    (void)line;
    (void)slot;
}

void rt_gui_clear_gutter_click(void) {
    // No-op: per-editor state is cleared after read in the getter functions.
}

int64_t rt_codeeditor_was_gutter_clicked(void *editor) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int64_t result = ce->gutter_clicked ? 1 : 0;
    ce->gutter_clicked = false; // Edge-triggered: clear after read
    return result;
}

int64_t rt_codeeditor_get_gutter_clicked_line(void *editor) {
    if (!editor)
        return -1;
    return ((vg_codeeditor_t *)editor)->gutter_clicked_line;
}

int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor) {
    if (!editor)
        return -1;
    return ((vg_codeeditor_t *)editor)->gutter_clicked_slot;
}

void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->show_fold_gutter = show != 0;
    ce->base.needs_paint = true;
}

//=============================================================================
// CodeEditor Enhancements - Code Folding (Phase 4)
//=============================================================================

void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
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
    ce->base.needs_paint = true;
}

void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)start_line) {
            ce->fold_regions[i] = ce->fold_regions[--ce->fold_region_count];
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_clear_fold_regions(void *editor) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    free(ce->fold_regions);
    ce->fold_regions = NULL;
    ce->fold_region_count = 0;
    ce->fold_region_cap = 0;
    ce->base.needs_paint = true;
}

void rt_codeeditor_fold(void *editor, int64_t line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)line) {
            ce->fold_regions[i].folded = true;
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_unfold(void *editor, int64_t line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)line) {
            ce->fold_regions[i].folded = false;
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_toggle_fold(void *editor, int64_t line) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++) {
        if (ce->fold_regions[i].start_line == (int)line) {
            ce->fold_regions[i].folded = !ce->fold_regions[i].folded;
            ce->base.needs_paint = true;
            return;
        }
    }
}

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

void rt_codeeditor_fold_all(void *editor) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
        ce->fold_regions[i].folded = true;
    ce->base.needs_paint = true;
}

void rt_codeeditor_unfold_all(void *editor) {
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
        ce->fold_regions[i].folded = false;
    ce->base.needs_paint = true;
}

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
        ce->base.needs_paint = true;
    }
}

//=============================================================================
// CodeEditor Enhancements - Multiple Cursors (Phase 4)
//=============================================================================

int64_t rt_codeeditor_get_cursor_count(void *editor) {
    if (!editor)
        return 1;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return 1 + ce->extra_cursor_count;
}

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
    ce->base.needs_paint = true;
}

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

int64_t rt_codeeditor_get_cursor_line(void *editor) {
    return rt_codeeditor_get_cursor_line_at(editor, 0);
}

int64_t rt_codeeditor_get_cursor_col(void *editor) {
    return rt_codeeditor_get_cursor_col_at(editor, 0);
}

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
    ce->base.needs_paint = true;
}

void rt_codeeditor_set_cursor_selection(void *editor,
                                        int64_t index,
                                        int64_t start_line,
                                        int64_t start_col,
                                        int64_t end_line,
                                        int64_t end_col) {
    if (!editor)
        return;
    if (index != 0)
        return; // Only primary cursor supported for selection
    vg_codeeditor_set_selection(
        (vg_codeeditor_t *)editor, (int)start_line, (int)start_col, (int)end_line, (int)end_col);
}

int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index) {
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return ce->has_selection ? 1 : 0;
}

void rt_codeeditor_undo(void *editor) {
    if (editor)
        vg_codeeditor_undo((vg_codeeditor_t *)editor);
}

void rt_codeeditor_redo(void *editor) {
    if (editor)
        vg_codeeditor_redo((vg_codeeditor_t *)editor);
}

int64_t rt_codeeditor_copy(void *editor) {
    if (!editor)
        return 0;
    return vg_codeeditor_copy((vg_codeeditor_t *)editor) ? 1 : 0;
}

int64_t rt_codeeditor_cut(void *editor) {
    if (!editor)
        return 0;
    return vg_codeeditor_cut((vg_codeeditor_t *)editor) ? 1 : 0;
}

int64_t rt_codeeditor_paste(void *editor) {
    if (!editor)
        return 0;
    return vg_codeeditor_paste((vg_codeeditor_t *)editor) ? 1 : 0;
}

void rt_codeeditor_select_all(void *editor) {
    if (editor)
        vg_codeeditor_select_all((vg_codeeditor_t *)editor);
}
//=============================================================================
// CodeEditor Completion Helpers
//=============================================================================

/// @brief Get the screen-absolute X pixel coordinate of the primary cursor.
/// @details Combines the widget's screen-space origin, gutter width, and
///          cursor column × character width.
int64_t rt_codeeditor_get_cursor_pixel_x(void *editor) {
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    float ax = 0, ay = 0;
    vg_widget_get_screen_bounds(&ce->base, &ax, &ay, NULL, NULL);
    float px = ax + ce->gutter_width + (float)(ce->cursor_col) * ce->char_width;
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
    float py = ay + (float)(ce->cursor_line - ce->visible_first_line) * ce->line_height;
    return (int64_t)py;
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

int64_t rt_codeeditor_get_cursor_pixel_x(void *editor) {
    (void)editor;
    return 0;
}

int64_t rt_codeeditor_get_cursor_pixel_y(void *editor) {
    (void)editor;
    return 0;
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
