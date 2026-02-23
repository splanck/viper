//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_codeeditor.c
// Purpose: Runtime bindings for the ViperGUI CodeEditor widget, plus
//   MessageBox and FileDialog overlays, and a FindBar widget. Implements
//   syntax highlighting (Zia and BASIC keyword/type color tables), gutter icon
//   management, breakpoint and diagnostic annotations, selected-text retrieval,
//   and scroll/cursor control. MessageBox and FileDialog wrap vg_dialog_t with
//   GC-safe state structs that store the user's selection after dismiss.
//
// Key invariants:
//   - Syntax highlight colors use ARGB 0xAARRGGBB format matching the VS Code
//     dark-theme palette defined at the top of this file.
//   - rt_codeeditor_set_syntax_highlight() overwrites the full per-character
//     color array; callers must provide a buffer sized to the text length.
//   - MessageBox and FileDialog objects are allocated via rt_obj_new_i64 (GC)
//     and hold a pointer to the underlying vg_dialog_t; the dialog must be
//     destroyed before the wrapper is GC'd.
//   - rt_codeeditor_get_selected_text() returns a freshly allocated C string
//     (from vg_codeeditor_get_selection); the caller must free it.
//   - FindBar integration uses the vg_findbar_t widget parented to the editor.
//
// Ownership/Lifetime:
//   - Wrapper structs (rt_messagebox_data_t, rt_filedialog_data_t) are GC heap
//     objects; the embedded vg_dialog_t pointer is manually freed on destroy.
//   - Selected-text C strings are malloc'd by the vg layer; the caller frees them.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/src/widgets/vg_codeeditor.c (underlying widget),
//        src/lib/gui/src/widgets/vg_dialog.c (dialog widget)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"

//=============================================================================
// CodeEditor Enhancements - Syntax Highlighting (Phase 4)
//=============================================================================

// VS Code dark-theme inspired palette (ARGB 0xAARRGGBB)
#define SYN_COLOR_DEFAULT  0xFFD4D4D4u   // light grey
#define SYN_COLOR_KEYWORD  0xFF569CD6u   // blue
#define SYN_COLOR_TYPE     0xFF4EC9B0u   // teal
#define SYN_COLOR_STRING   0xFFCE9178u   // orange
#define SYN_COLOR_COMMENT  0xFF6A9955u   // green
#define SYN_COLOR_NUMBER   0xFFB5CEA8u   // light green

// Fill `n` colors with `color` starting at `colors[pos]`
static void syn_fill(uint32_t *colors, size_t pos, size_t n, uint32_t color)
{
    for (size_t i = 0; i < n; i++)
        colors[pos + i] = color;
}

// Check if character is an identifier start character
static int syn_is_id_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Check if character is an identifier continuation character
static int syn_is_id_cont(char c)
{
    return syn_is_id_start(c) || (c >= '0' && c <= '9');
}

// Case-insensitive string equality check for a fixed-length word
static int syn_word_eq_ci(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        char ca = (a[i] >= 'a' && a[i] <= 'z') ? a[i] - 32 : a[i];
        char cb = (b[i] >= 'a' && b[i] <= 'z') ? b[i] - 32 : b[i];
        if (ca != cb)
            return 0;
    }
    return 1;
}

// Match `word` (length wlen) against a NULL-terminated keyword table (case-sensitive)
static int syn_is_keyword(const char *word, size_t wlen, const char *const *table)
{
    for (int i = 0; table[i]; i++)
    {
        size_t klen = strlen(table[i]);
        if (klen == wlen && memcmp(word, table[i], wlen) == 0)
            return 1;
    }
    return 0;
}

// Match `word` (length wlen) against a NULL-terminated keyword table (case-insensitive)
static int syn_is_keyword_ci(const char *word, size_t wlen, const char *const *table)
{
    for (int i = 0; table[i]; i++)
    {
        size_t klen = strlen(table[i]);
        if (klen == wlen && syn_word_eq_ci(word, table[i], wlen))
            return 1;
    }
    return 0;
}

// ─── Zia language tokenizer ────────────────────────────────────────────────

static const char *const zia_keywords[] = {
    "func", "expose", "hide", "entity", "value", "var", "new",
    "if", "else", "while", "for", "in", "return", "break", "continue", "do",
    "and", "or", "not", "true", "false", "null", "module", "bind", "self",
    NULL
};

static const char *const zia_types[] = {
    "Integer", "Boolean", "String", "Number", "Byte",
    "List", "Seq", "Map", "Set", "Stack", "Queue",
    NULL
};

static void rt_zia_syntax_cb(vg_widget_t *editor,
                              int line_num,
                              const char *text,
                              uint32_t *colors,
                              void *user_data)
{
    (void)editor;
    (void)line_num;
    (void)user_data;

    size_t len = strlen(text);
    size_t i   = 0;

    while (i < len)
    {
        // Line comment
        if (text[i] == '/' && i + 1 < len && text[i + 1] == '/')
        {
            syn_fill(colors, i, len - i, SYN_COLOR_COMMENT);
            return;
        }

        // String literal
        if (text[i] == '"')
        {
            size_t start = i++;
            while (i < len && text[i] != '"')
            {
                if (text[i] == '\\')
                    i++; // skip escaped character
                i++;
            }
            if (i < len)
                i++; // closing quote
            syn_fill(colors, start, i - start, SYN_COLOR_STRING);
            continue;
        }

        // Number literal
        if (text[i] >= '0' && text[i] <= '9')
        {
            size_t start = i;
            while (i < len && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'))
                i++;
            syn_fill(colors, start, i - start, SYN_COLOR_NUMBER);
            continue;
        }

        // Identifier or keyword
        if (syn_is_id_start(text[i]))
        {
            size_t start = i;
            while (i < len && syn_is_id_cont(text[i]))
                i++;
            size_t wlen = i - start;
            uint32_t color = SYN_COLOR_DEFAULT;
            if (syn_is_keyword(text + start, wlen, zia_keywords))
                color = SYN_COLOR_KEYWORD;
            else if (syn_is_keyword(text + start, wlen, zia_types))
                color = SYN_COLOR_TYPE;
            syn_fill(colors, start, wlen, color);
            continue;
        }

        // Default (operators, punctuation)
        colors[i++] = SYN_COLOR_DEFAULT;
    }
}

// ─── Viper BASIC language tokenizer ───────────────────────────────────────

static const char *const basic_keywords[] = {
    "DIM", "LET", "IF", "THEN", "ELSE", "ENDIF", "FOR", "NEXT", "TO", "STEP",
    "WHILE", "WEND", "DO", "LOOP", "UNTIL", "GOSUB", "RETURN", "PRINT",
    "INPUT", "GOTO", "SUB", "END", "FUNCTION", "CALL",
    "TRUE", "FALSE", "AND", "OR", "NOT", "MOD",
    NULL
};

static void rt_basic_syntax_cb(vg_widget_t *editor,
                                int line_num,
                                const char *text,
                                uint32_t *colors,
                                void *user_data)
{
    (void)editor;
    (void)line_num;
    (void)user_data;

    size_t len = strlen(text);
    size_t i   = 0;

    // Skip leading whitespace to detect REM comments
    size_t first_word_start = 0;
    while (first_word_start < len && text[first_word_start] == ' ')
        first_word_start++;

    while (i < len)
    {
        // Single-quote comment
        if (text[i] == '\'')
        {
            syn_fill(colors, i, len - i, SYN_COLOR_COMMENT);
            return;
        }

        // String literal
        if (text[i] == '"')
        {
            size_t start = i++;
            while (i < len && text[i] != '"')
                i++;
            if (i < len)
                i++;
            syn_fill(colors, start, i - start, SYN_COLOR_STRING);
            continue;
        }

        // Number literal
        if (text[i] >= '0' && text[i] <= '9')
        {
            size_t start = i;
            while (i < len && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'))
                i++;
            syn_fill(colors, start, i - start, SYN_COLOR_NUMBER);
            continue;
        }

        // Identifier or keyword (case-insensitive for BASIC)
        if (syn_is_id_start(text[i]))
        {
            size_t start = i;
            while (i < len && syn_is_id_cont(text[i]))
                i++;
            size_t wlen = i - start;

            // REM comment: rest of line is a comment
            if (wlen == 3 && syn_word_eq_ci(text + start, "REM", 3))
            {
                syn_fill(colors, start, len - start, SYN_COLOR_COMMENT);
                return;
            }

            uint32_t color = SYN_COLOR_DEFAULT;
            if (syn_is_keyword_ci(text + start, wlen, basic_keywords))
                color = SYN_COLOR_KEYWORD;
            syn_fill(colors, start, wlen, color);
            continue;
        }

        // Default
        colors[i++] = SYN_COLOR_DEFAULT;
    }
}

// ─── Public: set language ─────────────────────────────────────────────────

void rt_codeeditor_set_language(void *editor, rt_string language)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    char *clang = rt_string_to_cstr(language);
    if (!clang)
        return;

    if (strcmp(clang, "zia") == 0)
        vg_codeeditor_set_syntax(ce, rt_zia_syntax_cb, NULL);
    else if (strcmp(clang, "basic") == 0)
        vg_codeeditor_set_syntax(ce, rt_basic_syntax_cb, NULL);
    else
        vg_codeeditor_set_syntax(ce, NULL, NULL); // plain text

    free(clang);
}

void rt_codeeditor_set_token_color(void *editor, int64_t token_type, int64_t color)
{
    /* No token_colors array yet — no-op for now */
    (void)editor;
    (void)token_type;
    (void)color;
}

void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords)
{
    /* No custom_keywords field yet — no-op for now */
    (void)editor;
    char *ckw = rt_string_to_cstr(keywords);
    free(ckw);
}

void rt_codeeditor_clear_highlights(void *editor)
{
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
                                 int64_t color)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->highlight_span_count >= ce->highlight_span_cap)
    {
        int new_cap = ce->highlight_span_cap ? ce->highlight_span_cap * 2 : 8;
        void *p = realloc(ce->highlight_spans,
                          (size_t)new_cap * sizeof(*ce->highlight_spans));
        if (!p)
            return;
        ce->highlight_spans = p;
        ce->highlight_span_cap = new_cap;
    }
    struct vg_highlight_span *s = &ce->highlight_spans[ce->highlight_span_count++];
    s->start_line = (int)start_line;
    s->start_col  = (int)start_col;
    s->end_line   = (int)end_line;
    s->end_col    = (int)end_col;
    s->color      = (uint32_t)color;
    ce->base.needs_paint = true;
}

void rt_codeeditor_refresh_highlights(void *editor)
{
    if (!editor)
        return;
    ((vg_codeeditor_t *)editor)->base.needs_paint = true;
}

//=============================================================================
// CodeEditor Enhancements - Gutter & Line Numbers (Phase 4)
//=============================================================================

void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->show_line_numbers = show != 0;
}

int64_t rt_codeeditor_get_show_line_numbers(void *editor)
{
    if (!editor)
        return 1; // Default to showing
    return ((vg_codeeditor_t *)editor)->show_line_numbers ? 1 : 0;
}

void rt_codeeditor_set_line_number_width(void *editor, int64_t width)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    ce->gutter_width = (int)width * 8; // Approximate char width
}

void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    /* slot maps to icon type: 0=breakpoint, 1=warning, 2=error, 3=info */
    int type = (int)(slot & 3);
    /* Update existing icon on same line+type if present */
    for (int i = 0; i < ce->gutter_icon_count; i++)
    {
        if (ce->gutter_icons[i].line == (int)line && ce->gutter_icons[i].type == type)
        {
            ce->base.needs_paint = true;
            return; /* already registered */
        }
    }
    if (ce->gutter_icon_count >= ce->gutter_icon_cap)
    {
        int new_cap = ce->gutter_icon_cap ? ce->gutter_icon_cap * 2 : 8;
        void *p = realloc(ce->gutter_icons, (size_t)new_cap * sizeof(*ce->gutter_icons));
        if (!p)
            return;
        ce->gutter_icons = p;
        ce->gutter_icon_cap = new_cap;
    }
    /* Default color per type */
    static const uint32_t s_type_colors[] = {0x00E81123, 0x00FFB900, 0x00E81123, 0x000078D4};
    struct vg_gutter_icon *icon = &ce->gutter_icons[ce->gutter_icon_count++];
    icon->line  = (int)line;
    icon->type  = type;
    icon->color = s_type_colors[type];
    (void)pixels; /* pixel icons not yet blitted; use colored disc */
    ce->base.needs_paint = true;
}

void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int type = (int)(slot & 3);
    for (int i = 0; i < ce->gutter_icon_count; i++)
    {
        if (ce->gutter_icons[i].line == (int)line && ce->gutter_icons[i].type == type)
        {
            /* Swap-remove */
            ce->gutter_icons[i] = ce->gutter_icons[--ce->gutter_icon_count];
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    int type = (int)(slot & 3);
    int w = 0;
    for (int i = 0; i < ce->gutter_icon_count; i++)
    {
        if (ce->gutter_icons[i].type != type)
            ce->gutter_icons[w++] = ce->gutter_icons[i];
    }
    ce->gutter_icon_count = w;
    ce->base.needs_paint = true;
}

// Gutter click tracking
static int g_gutter_clicked = 0;
static int64_t g_gutter_clicked_line = -1;
static int64_t g_gutter_clicked_slot = -1;

void rt_gui_set_gutter_click(int64_t line, int64_t slot)
{
    g_gutter_clicked = 1;
    g_gutter_clicked_line = line;
    g_gutter_clicked_slot = slot;
}

void rt_gui_clear_gutter_click(void)
{
    g_gutter_clicked = 0;
    g_gutter_clicked_line = -1;
    g_gutter_clicked_slot = -1;
}

int64_t rt_codeeditor_was_gutter_clicked(void *editor)
{
    if (!editor)
        return 0;
    return g_gutter_clicked ? 1 : 0;
}

int64_t rt_codeeditor_get_gutter_clicked_line(void *editor)
{
    if (!editor)
        return -1;
    return g_gutter_clicked_line;
}

int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor)
{
    if (!editor)
        return -1;
    return g_gutter_clicked_slot;
}

void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show)
{
    if (!editor)
        return;
    // Would enable/disable fold gutter column
    // Stub for now
    (void)show;
}

//=============================================================================
// CodeEditor Enhancements - Code Folding (Phase 4)
//=============================================================================

void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->fold_region_count >= ce->fold_region_cap)
    {
        int new_cap = ce->fold_region_cap ? ce->fold_region_cap * 2 : 8;
        void *p = realloc(ce->fold_regions, (size_t)new_cap * sizeof(*ce->fold_regions));
        if (!p)
            return;
        ce->fold_regions = p;
        ce->fold_region_cap = new_cap;
    }
    struct vg_fold_region *r = &ce->fold_regions[ce->fold_region_count++];
    r->start_line = (int)start_line;
    r->end_line   = (int)end_line;
    r->folded     = false;
    ce->base.needs_paint = true;
}

void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
    {
        if (ce->fold_regions[i].start_line == (int)start_line)
        {
            ce->fold_regions[i] = ce->fold_regions[--ce->fold_region_count];
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_clear_fold_regions(void *editor)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    free(ce->fold_regions);
    ce->fold_regions = NULL;
    ce->fold_region_count = 0;
    ce->fold_region_cap = 0;
    ce->base.needs_paint = true;
}

void rt_codeeditor_fold(void *editor, int64_t line)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
    {
        if (ce->fold_regions[i].start_line == (int)line)
        {
            ce->fold_regions[i].folded = true;
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_unfold(void *editor, int64_t line)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
    {
        if (ce->fold_regions[i].start_line == (int)line)
        {
            ce->fold_regions[i].folded = false;
            ce->base.needs_paint = true;
            return;
        }
    }
}

void rt_codeeditor_toggle_fold(void *editor, int64_t line)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
    {
        if (ce->fold_regions[i].start_line == (int)line)
        {
            ce->fold_regions[i].folded = !ce->fold_regions[i].folded;
            ce->base.needs_paint = true;
            return;
        }
    }
}

int64_t rt_codeeditor_is_folded(void *editor, int64_t line)
{
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
    {
        if (ce->fold_regions[i].start_line == (int)line)
            return ce->fold_regions[i].folded ? 1 : 0;
    }
    return 0;
}

void rt_codeeditor_fold_all(void *editor)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
        ce->fold_regions[i].folded = true;
    ce->base.needs_paint = true;
}

void rt_codeeditor_unfold_all(void *editor)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    for (int i = 0; i < ce->fold_region_count; i++)
        ce->fold_regions[i].folded = false;
    ce->base.needs_paint = true;
}

void rt_codeeditor_set_auto_fold_detection(void *editor, int64_t enable)
{
    /* Auto fold detection requires language-specific parsing; no-op for now */
    (void)editor;
    (void)enable;
}

//=============================================================================
// CodeEditor Enhancements - Multiple Cursors (Phase 4)
//=============================================================================

int64_t rt_codeeditor_get_cursor_count(void *editor)
{
    if (!editor)
        return 1;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return 1 + ce->extra_cursor_count;
}

void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->extra_cursor_count >= ce->extra_cursor_cap)
    {
        int new_cap = ce->extra_cursor_cap ? ce->extra_cursor_cap * 2 : 4;
        void *p = realloc(ce->extra_cursors, (size_t)new_cap * sizeof(*ce->extra_cursors));
        if (!p)
            return;
        ce->extra_cursors = p;
        ce->extra_cursor_cap = new_cap;
    }
    struct vg_extra_cursor *c = &ce->extra_cursors[ce->extra_cursor_count++];
    c->line = (int)line;
    c->col  = (int)col;
    ce->base.needs_paint = true;
}

void rt_codeeditor_remove_cursor(void *editor, int64_t index)
{
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

void rt_codeeditor_clear_extra_cursors(void *editor)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    free(ce->extra_cursors);
    ce->extra_cursors = NULL;
    ce->extra_cursor_count = 0;
    ce->extra_cursor_cap = 0;
    ce->base.needs_paint = true;
}

int64_t rt_codeeditor_get_cursor_line_at(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    return ((vg_codeeditor_t *)editor)->cursor_line;
}

int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    return ((vg_codeeditor_t *)editor)->cursor_col;
}

int64_t rt_codeeditor_get_cursor_line(void *editor)
{
    return rt_codeeditor_get_cursor_line_at(editor, 0);
}

int64_t rt_codeeditor_get_cursor_col(void *editor)
{
    return rt_codeeditor_get_cursor_col_at(editor, 0);
}

void rt_codeeditor_set_cursor_position_at(void *editor, int64_t index, int64_t line, int64_t col)
{
    if (!editor)
        return;
    if (index != 0)
        return; // Only primary cursor supported
    vg_codeeditor_set_cursor((vg_codeeditor_t *)editor, (int)line, (int)col);
}

void rt_codeeditor_set_cursor_selection(void *editor,
                                        int64_t index,
                                        int64_t start_line,
                                        int64_t start_col,
                                        int64_t end_line,
                                        int64_t end_col)
{
    if (!editor)
        return;
    if (index != 0)
        return; // Only primary cursor supported
    // Would set selection for cursor
    // Stub for now
    (void)start_line;
    (void)start_col;
    (void)end_line;
    (void)end_col;
}

int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index)
{
    if (!editor)
        return 0;
    if (index != 0)
        return 0; // Only primary cursor supported
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    return ce->has_selection ? 1 : 0;
}

void rt_codeeditor_undo(void *editor)
{
    if (editor)
        vg_codeeditor_undo((vg_codeeditor_t *)editor);
}

void rt_codeeditor_redo(void *editor)
{
    if (editor)
        vg_codeeditor_redo((vg_codeeditor_t *)editor);
}

int64_t rt_codeeditor_copy(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_copy((vg_codeeditor_t *)editor) ? 1 : 0;
}

int64_t rt_codeeditor_cut(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_cut((vg_codeeditor_t *)editor) ? 1 : 0;
}

int64_t rt_codeeditor_paste(void *editor)
{
    if (!editor)
        return 0;
    return vg_codeeditor_paste((vg_codeeditor_t *)editor) ? 1 : 0;
}

void rt_codeeditor_select_all(void *editor)
{
    if (editor)
        vg_codeeditor_select_all((vg_codeeditor_t *)editor);
}

//=============================================================================
// Phase 5: MessageBox Dialog
//=============================================================================

int64_t rt_messagebox_info(rt_string title, rt_string message)
{
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

int64_t rt_messagebox_warning(rt_string title, rt_string message)
{
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

int64_t rt_messagebox_error(rt_string title, rt_string message)
{
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

int64_t rt_messagebox_question(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg   = rt_string_to_cstr(message);
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
    while (dlg->is_open && s_current_app && !s_current_app->should_close)
    {
        rt_gui_app_poll(s_current_app);
        rt_gui_app_render(s_current_app);
    }

    vg_dialog_result_t result = vg_dialog_get_result(dlg);
    rt_gui_set_active_dialog(NULL);
    vg_widget_destroy(&dlg->base);
    return (result == VG_DIALOG_RESULT_YES) ? 1 : 0;
}

int64_t rt_messagebox_confirm(rt_string title, rt_string message)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cmsg   = rt_string_to_cstr(message);
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
    while (dlg->is_open && s_current_app && !s_current_app->should_close)
    {
        rt_gui_app_poll(s_current_app);
        rt_gui_app_render(s_current_app);
    }

    vg_dialog_result_t result = vg_dialog_get_result(dlg);
    rt_gui_set_active_dialog(NULL);
    vg_widget_destroy(&dlg->base);
    return (result == VG_DIALOG_RESULT_OK) ? 1 : 0;
}

// Prompt commit callback data
typedef struct
{
    vg_dialog_t *dialog;
} rt_prompt_commit_data_t;

static void prompt_on_commit(vg_widget_t *w, const char *text, void *user_data)
{
    (void)w;
    (void)text;
    rt_prompt_commit_data_t *d = (rt_prompt_commit_data_t *)user_data;
    if (d && d->dialog)
        vg_dialog_close(d->dialog, VG_DIALOG_RESULT_OK);
}

rt_string rt_messagebox_prompt(rt_string title, rt_string message)
{
    if (!s_current_app)
        return rt_string_from_bytes("", 0);

    char *ctitle = rt_string_to_cstr(title);
    char *cmsg   = rt_string_to_cstr(message);

    vg_dialog_t *dlg = vg_dialog_create(ctitle);
    if (ctitle)
        free(ctitle);
    if (!dlg)
    {
        if (cmsg)
            free(cmsg);
        return rt_string_from_bytes("", 0);
    }

    // Show the prompt message above the text input
    if (cmsg)
    {
        vg_dialog_set_message(dlg, cmsg);
        free(cmsg);
    }

    // Apply app font to dialog
    if (s_current_app->default_font)
        vg_dialog_set_font(dlg, s_current_app->default_font, s_current_app->default_font_size);

    // Create the text input (no parent — set as dialog content, not widget-tree child)
    vg_textinput_t *input = vg_textinput_create(NULL);
    if (!input)
    {
        vg_widget_destroy((vg_widget_t *)dlg);
        return rt_string_from_bytes("", 0);
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
    while (vg_dialog_is_open(dlg))
    {
        rt_gui_app_poll(s_current_app);
        rt_gui_app_render(s_current_app);
    }

    // Collect result before destroying
    rt_string result = rt_string_from_bytes("", 0);
    if (vg_dialog_get_result(dlg) == VG_DIALOG_RESULT_OK)
    {
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
typedef struct
{
    vg_dialog_t *dialog;
    int64_t result;
    int64_t default_button;
} rt_messagebox_data_t;

void *rt_messagebox_new(rt_string title, rt_string message, int64_t type)
{
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
    switch (type)
    {
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
    data->dialog = dlg;
    data->result = -1;
    data->default_button = 0;

    return data;
}

void rt_messagebox_add_button(void *box, rt_string text, int64_t id)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    // In a full implementation, we'd track custom buttons
    // For now, stub - the dialog system uses presets
    (void)data;
    (void)text;
    (void)id;
}

void rt_messagebox_set_default_button(void *box, int64_t id)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    data->default_button = id;
}

int64_t rt_messagebox_show(void *box)
{
    if (!box)
        return -1;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    vg_dialog_show(data->dialog);
    // Would need modal loop to get actual result
    return data->default_button;
}

void rt_messagebox_destroy(void *box)
{
    if (!box)
        return;
    rt_messagebox_data_t *data = (rt_messagebox_data_t *)box;
    if (data->dialog)
    {
        vg_widget_destroy((vg_widget_t *)data->dialog);
    }
}

//=============================================================================
// Phase 5: FileDialog
//=============================================================================

rt_string rt_filedialog_open(rt_string title, rt_string filter, rt_string default_path)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cfilter = rt_string_to_cstr(filter);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_open_file(ctitle, cpath, "Files", cfilter);
#else
    // vg_filedialog_open_file expects: title, path, filter_name, filter_pattern
    char *result = vg_filedialog_open_file(ctitle, cpath, "Files", cfilter);
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);
    char *cfilter = rt_string_to_cstr(filter);

    vg_filedialog_t *dlg = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    if (!dlg)
    {
        if (ctitle)
            free(ctitle);
        if (cpath)
            free(cpath);
        if (cfilter)
            free(cfilter);
        return rt_string_from_bytes("", 0);
    }

    vg_filedialog_set_title(dlg, ctitle);
    vg_filedialog_set_initial_path(dlg, cpath);
    vg_filedialog_set_multi_select(dlg, true);
    if (cfilter && cfilter[0])
    {
        vg_filedialog_add_filter(dlg, "Files", cfilter);
    }

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    vg_filedialog_show(dlg);

    size_t count = 0;
    char **paths = vg_filedialog_get_selected_paths(dlg, &count);

    rt_string result = rt_string_from_bytes("", 0);
    if (paths && count > 0)
    {
        // Join paths with semicolon
        size_t total_len = 0;
        for (size_t i = 0; i < count; i++)
        {
            total_len += strlen(paths[i]) + 1;
        }
        char *joined = (char *)malloc(total_len);
        if (joined)
        {
            size_t off = 0;
            for (size_t i = 0; i < count; i++)
            {
                if (i > 0)
                    joined[off++] = ';';
                size_t len = strlen(paths[i]);
                memcpy(joined + off, paths[i], len);
                off += len;
            }
            joined[off] = '\0';
            result = rt_string_from_bytes(joined, off);
            free(joined);
        }
        for (size_t i = 0; i < count; i++)
        {
            free(paths[i]);
        }
        free(paths);
    }

    vg_filedialog_destroy(dlg);
    return result;
}

rt_string rt_filedialog_save(rt_string title,
                             rt_string filter,
                             rt_string default_name,
                             rt_string default_path)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cfilter = rt_string_to_cstr(filter);
    char *cname = rt_string_to_cstr(default_name);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_save_file(ctitle, cpath, cname, "Files", cfilter);
#else
    // vg_filedialog_save_file expects: title, path, default_name, filter_name, filter_pattern
    char *result = vg_filedialog_save_file(ctitle, cpath, cname, "Files", cfilter);
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);
    if (cname)
        free(cname);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path)
{
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_select_folder(ctitle, cpath);
#else
    char *result = vg_filedialog_select_folder(ctitle, cpath);
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);

    if (result)
    {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_string_from_bytes("", 0);
}

// Custom FileDialog structure
typedef struct
{
    vg_filedialog_t *dialog;
    char **selected_paths;
    size_t selected_count;
    int64_t result;
} rt_filedialog_data_t;

void *rt_filedialog_new(int64_t type)
{
    vg_filedialog_mode_t mode;
    switch (type)
    {
        case RT_FILEDIALOG_OPEN:
            mode = VG_FILEDIALOG_OPEN;
            break;
        case RT_FILEDIALOG_SAVE:
            mode = VG_FILEDIALOG_SAVE;
            break;
        case RT_FILEDIALOG_FOLDER:
            mode = VG_FILEDIALOG_SELECT_FOLDER;
            break;
        default:
            mode = VG_FILEDIALOG_OPEN;
            break;
    }

    vg_filedialog_t *dlg = vg_filedialog_create(mode);
    if (!dlg)
        return NULL;

    rt_filedialog_data_t *data =
        (rt_filedialog_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_filedialog_data_t));
    data->dialog = dlg;
    data->selected_paths = NULL;
    data->selected_count = 0;
    data->result = 0;

    return data;
}

void rt_filedialog_set_title(void *dialog, rt_string title)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *ctitle = rt_string_to_cstr(title);
    vg_filedialog_set_title(data->dialog, ctitle);
    if (ctitle)
        free(ctitle);
}

void rt_filedialog_set_path(void *dialog, rt_string path)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cpath = rt_string_to_cstr(path);
    vg_filedialog_set_initial_path(data->dialog, cpath);
    if (cpath)
        free(cpath);
}

void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_clear_filters(data->dialog);
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_set_default_name(void *dialog, rt_string name)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    vg_filedialog_set_filename(data->dialog, cname);
    if (cname)
        free(cname);
}

void rt_filedialog_set_multiple(void *dialog, int64_t multiple)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_set_multi_select(data->dialog, multiple != 0);
}

int64_t rt_filedialog_show(void *dialog)
{
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_show(data->dialog);

    // Get selected paths
    if (data->selected_paths)
    {
        for (size_t i = 0; i < data->selected_count; i++)
        {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
    }
    data->selected_paths = vg_filedialog_get_selected_paths(data->dialog, &data->selected_count);
    data->result = (data->selected_count > 0) ? 1 : 0;

    return data->result;
}

rt_string rt_filedialog_get_path(void *dialog)
{
    if (!dialog)
        return rt_string_from_bytes("", 0);
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && data->selected_count > 0)
    {
        return rt_string_from_bytes(data->selected_paths[0], strlen(data->selected_paths[0]));
    }
    return rt_string_from_bytes("", 0);
}

int64_t rt_filedialog_get_path_count(void *dialog)
{
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    return (int64_t)data->selected_count;
}

rt_string rt_filedialog_get_path_at(void *dialog, int64_t index)
{
    if (!dialog)
        return rt_string_from_bytes("", 0);
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && index >= 0 && (size_t)index < data->selected_count)
    {
        return rt_string_from_bytes(data->selected_paths[index],
                                    strlen(data->selected_paths[index]));
    }
    return rt_string_from_bytes("", 0);
}

void rt_filedialog_destroy(void *dialog)
{
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths)
    {
        for (size_t i = 0; i < data->selected_count; i++)
        {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
    }
    if (data->dialog)
    {
        vg_filedialog_destroy(data->dialog);
    }
}

//=============================================================================
// Phase 6: FindBar (Search & Replace)
//=============================================================================

// FindBar state tracking
typedef struct
{
    vg_findreplacebar_t *bar;
    void *bound_editor;
    char *find_text;
    char *replace_text;
    int64_t case_sensitive;
    int64_t whole_word;
    int64_t regex;
    int64_t replace_mode;
} rt_findbar_data_t;

void *rt_findbar_new(void *parent)
{
    vg_findreplacebar_t *bar = vg_findreplacebar_create();
    if (!bar)
        return NULL;

    rt_findbar_data_t *data =
        (rt_findbar_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_findbar_data_t));
    data->bar = bar;
    data->bound_editor = NULL;
    data->find_text = NULL;
    data->replace_text = NULL;
    data->case_sensitive = 0;
    data->whole_word = 0;
    data->regex = 0;
    data->replace_mode = 0;

    (void)parent; // Parent not used in current implementation
    return data;
}

void rt_findbar_destroy(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->bar)
    {
        vg_findreplacebar_destroy(data->bar);
    }
    if (data->find_text)
        free(data->find_text);
    if (data->replace_text)
        free(data->replace_text);
}

void rt_findbar_bind_editor(void *bar, void *editor)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = editor;
    vg_findreplacebar_set_target(data->bar, (vg_codeeditor_t *)editor);
}

void rt_findbar_unbind_editor(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->bound_editor = NULL;
    vg_findreplacebar_set_target(data->bar, NULL);
}

void rt_findbar_set_replace_mode(void *bar, int64_t replace)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->replace_mode = replace;
    vg_findreplacebar_set_show_replace(data->bar, replace != 0);
}

int64_t rt_findbar_is_replace_mode(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->replace_mode;
}

void rt_findbar_set_find_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text)
        free(data->find_text);
    data->find_text = rt_string_to_cstr(text);
    vg_findreplacebar_set_find_text(data->bar, data->find_text);
}

rt_string rt_findbar_get_find_text(void *bar)
{
    if (!bar)
        return rt_string_from_bytes("", 0);
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->find_text)
    {
        return rt_string_from_bytes(data->find_text, strlen(data->find_text));
    }
    return rt_string_from_bytes("", 0);
}

void rt_findbar_set_replace_text(void *bar, rt_string text)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text)
        free(data->replace_text);
    data->replace_text = rt_string_to_cstr(text);
    // vg_findreplacebar doesn't have a set_replace_text - would need to track locally
}

rt_string rt_findbar_get_replace_text(void *bar)
{
    if (!bar)
        return rt_string_from_bytes("", 0);
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    if (data->replace_text)
    {
        return rt_string_from_bytes(data->replace_text, strlen(data->replace_text));
    }
    return rt_string_from_bytes("", 0);
}

// Helper to update find options
static void rt_findbar_update_options(rt_findbar_data_t *data)
{
    vg_search_options_t opts = {.case_sensitive = data->case_sensitive != 0,
                                .whole_word = data->whole_word != 0,
                                .use_regex = data->regex != 0,
                                .in_selection = false,
                                .wrap_around = true};
    vg_findreplacebar_set_options(data->bar, &opts);
}

void rt_findbar_set_case_sensitive(void *bar, int64_t sensitive)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->case_sensitive = sensitive;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_case_sensitive(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->case_sensitive;
}

void rt_findbar_set_whole_word(void *bar, int64_t whole)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->whole_word = whole;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_whole_word(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->whole_word;
}

void rt_findbar_set_regex(void *bar, int64_t regex)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    data->regex = regex;
    rt_findbar_update_options(data);
}

int64_t rt_findbar_is_regex(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return data->regex;
}

int64_t rt_findbar_find_next(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_next(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

int64_t rt_findbar_find_previous(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_find_prev(data->bar);
    return vg_findreplacebar_get_match_count(data->bar) > 0 ? 1 : 0;
}

int64_t rt_findbar_replace(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_replace_current(data->bar);
    return 1; // Assume success
}

int64_t rt_findbar_replace_all(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    size_t count_before = vg_findreplacebar_get_match_count(data->bar);
    vg_findreplacebar_replace_all(data->bar);
    return (int64_t)count_before;
}

int64_t rt_findbar_get_match_count(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_match_count(data->bar);
}

int64_t rt_findbar_get_current_match(void *bar)
{
    if (!bar)
        return 0;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    return (int64_t)vg_findreplacebar_get_current_match(data->bar);
}

void rt_findbar_set_visible(void *bar, int64_t visible)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    // The widget would need visibility control - stub for now
    (void)data;
    (void)visible;
}

int64_t rt_findbar_is_visible(void *bar)
{
    if (!bar)
        return 0;
    // Stub - would need widget visibility query
    (void)bar;
    return 0;
}

void rt_findbar_focus(void *bar)
{
    if (!bar)
        return;
    rt_findbar_data_t *data = (rt_findbar_data_t *)bar;
    vg_findreplacebar_focus(data->bar);
}

//=============================================================================
// CodeEditor Completion Helpers
//=============================================================================

/// @brief Get the screen-absolute X pixel coordinate of the primary cursor.
/// @details Combines the widget's screen-space origin, gutter width, and
///          cursor column × character width.
int64_t rt_codeeditor_get_cursor_pixel_x(void *editor)
{
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
int64_t rt_codeeditor_get_cursor_pixel_y(void *editor)
{
    if (!editor)
        return 0;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    float ax = 0, ay = 0;
    vg_widget_get_screen_bounds(&ce->base, &ax, &ay, NULL, NULL);
    float py = ay + (float)(ce->cursor_line - ce->visible_first_line) * ce->line_height;
    return (int64_t)py;
}

/// @brief Insert text at the primary cursor position.
void rt_codeeditor_insert_at_cursor(void *editor, rt_string text)
{
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
rt_string rt_codeeditor_get_word_at_cursor(void *editor)
{
    if (!editor)
        return rt_str_empty();
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->cursor_line < 0 || ce->cursor_line >= ce->line_count)
        return rt_str_empty();
    const char *text = ce->lines[ce->cursor_line].text;
    int         len  = (int)ce->lines[ce->cursor_line].length;
    int         col  = ce->cursor_col < len ? ce->cursor_col : len;

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
void rt_codeeditor_replace_word_at_cursor(void *editor, rt_string new_text)
{
    if (!editor)
        return;
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (ce->cursor_line < 0 || ce->cursor_line >= ce->line_count)
        return;
    const char *text = ce->lines[ce->cursor_line].text;
    int         len  = (int)ce->lines[ce->cursor_line].length;
    int         col  = ce->cursor_col < len ? ce->cursor_col : len;

    /* find word boundaries */
    int start = col;
    while (start > 0 && (isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_'))
        --start;
    int end = col;
    while (end < len && (isalnum((unsigned char)text[end]) || text[end] == '_'))
        ++end;

    /* select the word, then insert the replacement (replaces selection) */
    vg_codeeditor_set_selection((vg_codeeditor_t *)editor,
                                ce->cursor_line, start,
                                ce->cursor_line, end);
    char *cstr = rt_string_to_cstr(new_text);
    if (cstr)
    {
        vg_codeeditor_insert_text((vg_codeeditor_t *)editor, cstr);
        free(cstr);
    }
}

/// @brief Return the text of a single line (0-based index).
rt_string rt_codeeditor_get_line(void *editor, int64_t line_index)
{
    if (!editor)
        return rt_str_empty();
    vg_codeeditor_t *ce = (vg_codeeditor_t *)editor;
    if (line_index < 0 || line_index >= (int64_t)ce->line_count)
        return rt_str_empty();
    vg_code_line_t *line = &ce->lines[(int)line_index];
    return rt_string_from_bytes(line->text, line->length);
}
