//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_label.c
// Purpose: Label widget implementation — single/multi-line text display with
//          word-wrap, horizontal/vertical alignment, and optional font override.
// Key invariants:
//   - label->text is always a valid heap-allocated string (never NULL).
//   - The word-wrap line cache is invalidated whenever text or font changes.
// Ownership/Lifetime:
//   - vg_label_create copies the text string; label_destroy frees it.
//   - The line cache is heap-allocated per-layout and freed on text/font change.
// Links: lib/gui/include/vg_widgets.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_font.h
//
//===----------------------------------------------------------------------===//
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void label_destroy(vg_widget_t *widget);
static void label_measure(vg_widget_t *widget, float available_width, float available_height);
static void label_paint(vg_widget_t *widget, void *canvas);

//=============================================================================
// Label VTable
//=============================================================================

static vg_widget_vtable_t g_label_vtable = {.destroy = label_destroy,
                                            .measure = label_measure,
                                            .arrange = NULL, // Labels don't have children to layout
                                            .paint = label_paint,
                                            .handle_event =
                                                NULL, // Labels don't handle events by default
                                            .can_focus = NULL,
                                            .on_focus = NULL};

//=============================================================================
// Label Implementation
//=============================================================================

/// @brief Create a label widget with the given text.
///
/// @param parent Widget to attach to as a child (may be NULL).
/// @param text   Display text; copied internally. An empty string is used if NULL.
/// @return Newly allocated vg_label_t, or NULL on allocation failure.
vg_label_t *vg_label_create(vg_widget_t *parent, const char *text) {
    vg_label_t *label = calloc(1, sizeof(vg_label_t));
    if (!label)
        return NULL;

    // Initialize base widget
    vg_widget_init(&label->base, VG_WIDGET_LABEL, &g_label_vtable);

    // Initialize label-specific fields
    label->text = text ? strdup(text) : strdup("");
    if (!label->text) {
        vg_widget_destroy(&label->base);
        return NULL;
    }
    vg_theme_t *theme = vg_theme_get_current();
    label->font = theme->typography.font_regular;
    label->font_size = theme->typography.size_normal;
    label->text_color = theme->colors.fg_primary;
    label->h_align = VG_ALIGN_H_LEFT;
    label->v_align = VG_ALIGN_V_CENTER;
    label->word_wrap = false;
    label->max_lines = 0;
    label->wrap_line_bufs = NULL;
    label->wrap_line_count = 0;
    label->wrap_cached_w = -1.0f;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &label->base);
    }

    return label;
}

/// @brief Free the word-wrap line cache.
static void label_free_wrap_cache(vg_label_t *label) {
    if (label->wrap_line_bufs) {
        for (int i = 0; i < label->wrap_line_count; i++)
            free(label->wrap_line_bufs[i]);
        free(label->wrap_line_bufs);
        label->wrap_line_bufs = NULL;
    }
    label->wrap_line_count = 0;
    label->wrap_cached_w = -1.0f;
}

/// @brief VTable destroy: frees the label text string and the word-wrap line cache.
static void label_destroy(vg_widget_t *widget) {
    vg_label_t *label = (vg_label_t *)widget;
    if (label->text) {
        free((void *)label->text);
        label->text = NULL;
    }
    label_free_wrap_cache(label);
}

/// @brief Build the word-wrap line cache and measure total height.
///
/// Runs the greedy algorithm once, storing each line as a heap-allocated
/// string in label->wrap_line_bufs.  Subsequent calls with the same
/// wrap_width return immediately using the cached results.
///
/// @return Number of lines produced.
static int label_measure_wrapped(vg_label_t *label,
                                 float wrap_width,
                                 float line_height,
                                 float *out_total_height) {
    /* Cache hit: same width as last measure — reuse stored lines. */
    if (label->wrap_line_bufs && label->wrap_cached_w == wrap_width) {
        if (out_total_height)
            *out_total_height = label->wrap_line_count * line_height;
        return label->wrap_line_count;
    }

    /* Cache miss: invalidate old cache and rebuild. */
    label_free_wrap_cache(label);

    size_t text_len = strlen(label->text);
    char *word_buf = malloc(text_len + 1);
    char *line_buf = malloc(text_len + 1);
    if (!word_buf || !line_buf) {
        free(word_buf);
        free(line_buf);
        if (out_total_height)
            *out_total_height = line_height;
        return 1;
    }

    /* Measure space width once */
    vg_text_metrics_t sm;
    vg_font_measure_text(label->font, label->font_size, " ", &sm);
    float space_w = sm.width;

    /* Dynamic cache array */
    int cache_cap = 8;
    char **cache = malloc((size_t)cache_cap * sizeof(char *));
    int line_count = 0;
    if (!cache) {
        free(word_buf);
        free(line_buf);
        if (out_total_height)
            *out_total_height = line_height;
        return 1;
    }

    const char *p = label->text;
    size_t line_pos = 0;
    float line_w = 0.0f;

    /* Helper lambda (inline): flush current line_buf to cache */
#define FLUSH_LINE()                                                                               \
    do {                                                                                           \
        if (line_count == cache_cap) {                                                             \
            cache_cap *= 2;                                                                        \
            char **tmp = realloc(cache, (size_t)cache_cap * sizeof(char *));                       \
            if (!tmp)                                                                              \
                goto wrap_oom;                                                                     \
            cache = tmp;                                                                           \
        }                                                                                          \
        line_buf[line_pos] = '\0';                                                                 \
        cache[line_count] = strdup(line_buf);                                                      \
        if (!cache[line_count])                                                                    \
            goto wrap_oom;                                                                         \
        line_count++;                                                                              \
        line_pos = 0;                                                                              \
        line_w = 0.0f;                                                                             \
    } while (0)

    while (*p) {
        /* Collect next word */
        const char *start = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        size_t word_len = (size_t)(p - start);

        if (word_len > 0) {
            memcpy(word_buf, start, word_len);
            word_buf[word_len] = '\0';
            vg_text_metrics_t wm;
            vg_font_measure_text(label->font, label->font_size, word_buf, &wm);

            float sep_w = (line_w > 0.0f) ? space_w : 0.0f;

            if (line_w > 0.0f && line_w + sep_w + wm.width > wrap_width) {
                /* Wrap: flush line and start fresh */
                FLUSH_LINE();
                if (label->max_lines > 0 && line_count >= label->max_lines)
                    goto wrap_done;
            } else if (line_w > 0.0f) {
                line_buf[line_pos++] = ' ';
                line_w += sep_w;
            }
            memcpy(line_buf + line_pos, word_buf, word_len);
            line_pos += word_len;
            line_w += wm.width;
        }

        /* Skip spaces */
        while (*p == ' ')
            p++;
        if (*p == '\n') {
            FLUSH_LINE();
            if (label->max_lines > 0 && line_count >= label->max_lines)
                goto wrap_done;
            p++;
        }
    }
    /* Flush last line if non-empty */
    if (line_pos > 0 || line_count == 0)
        FLUSH_LINE();

wrap_done:
#undef FLUSH_LINE
    free(word_buf);
    free(line_buf);

    label->wrap_line_bufs = cache;
    label->wrap_line_count = line_count;
    label->wrap_cached_w = wrap_width;

    if (out_total_height)
        *out_total_height = line_count * line_height;
    return line_count;

wrap_oom:
    free(word_buf);
    free(line_buf);
    for (int k = 0; k < line_count; k++)
        free(cache[k]);
    free(cache);
    if (out_total_height)
        *out_total_height = line_height;
    return 1;
}

/// @brief VTable measure: measures text using the word-wrap cache (when enabled) or a single-line
/// font measurement, then applies constraints.
static void label_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_label_t *label = (vg_label_t *)widget;
    (void)available_height;

    if (!label->text || !label->font) {
        // No text or font - use minimum size
        widget->measured_width = widget->constraints.min_width;
        widget->measured_height = widget->constraints.min_height;
        return;
    }

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(label->font, label->font_size, &font_metrics);
    float line_height = (float)font_metrics.line_height;

    if (label->word_wrap && available_width > 0.0f) {
        float wrap_w = available_width;
        if (widget->constraints.max_width > 0.0f && widget->constraints.max_width < wrap_w)
            wrap_w = widget->constraints.max_width;

        float total_h = 0.0f;
        label_measure_wrapped(label, wrap_w, line_height, &total_h);
        widget->measured_width = wrap_w;
        widget->measured_height = total_h;
    } else {
        // Measure text
        vg_text_metrics_t metrics;
        vg_font_measure_text(label->font, label->font_size, label->text, &metrics);

        widget->measured_width = metrics.width;
        widget->measured_height = metrics.height;
    }

    vg_widget_apply_constraints(widget);
}

/// @brief VTable paint: renders the label text aligned within the widget bounds, handling word-wrap
/// by drawing each cached line individually.
static void label_paint(vg_widget_t *widget, void *canvas) {
    vg_label_t *label = (vg_label_t *)widget;

    if (!label->text || !label->text[0]) {
        return; // Nothing to draw
    }

    if (!label->font) {
        return; // No font set
    }

    // Get font metrics for baseline
    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(label->font, label->font_size, &font_metrics);
    float line_height = (float)font_metrics.line_height;

    // Draw text
    uint32_t color = (widget->state & VG_STATE_DISABLED)
                         ? vg_theme_get_current()->colors.fg_disabled
                         : label->text_color;

    if (label->word_wrap && widget->width > 0.0f) {
        /* Word-wrap paint: use the line cache populated during measure.
         * If the cache is stale (different width or not yet built), rebuild it
         * now before rendering.  This eliminates redundant greedy-algorithm
         * execution every frame when the label size is stable. */
        float wrap_w = widget->width;
        label_measure_wrapped(label, wrap_w, line_height, NULL);

        float text_y = widget->y + font_metrics.ascent;
        for (int ln = 0; ln < label->wrap_line_count; ln++) {
            const char *line_text = label->wrap_line_bufs[ln];
            float tx = widget->x;
            if (label->h_align == VG_ALIGN_H_CENTER || label->h_align == VG_ALIGN_H_RIGHT) {
                vg_text_metrics_t lm;
                vg_font_measure_text(label->font, label->font_size, line_text, &lm);
                if (label->h_align == VG_ALIGN_H_CENTER)
                    tx += (widget->width - lm.width) / 2.0f;
                else
                    tx += widget->width - lm.width;
            }
            vg_font_draw_text(canvas, label->font, label->font_size, tx, text_y, line_text, color);
            text_y += line_height;
        }
    } else {
        // Calculate text position based on alignment
        float text_x = widget->x;
        float text_y = widget->y;

        vg_text_metrics_t metrics;
        vg_font_measure_text(label->font, label->font_size, label->text, &metrics);

        // Horizontal alignment
        switch (label->h_align) {
            case VG_ALIGN_H_CENTER:
                text_x += (widget->width - metrics.width) / 2.0f;
                break;
            case VG_ALIGN_H_RIGHT:
                text_x += widget->width - metrics.width;
                break;
            default:
                // Left alignment - use x
                break;
        }

        // Vertical alignment
        switch (label->v_align) {
            case VG_ALIGN_V_CENTER:
                text_y += (widget->height - metrics.height) / 2.0f + font_metrics.ascent;
                break;
            case VG_ALIGN_V_BOTTOM:
                text_y += widget->height - font_metrics.descent;
                break;
            case VG_ALIGN_V_BASELINE:
                text_y += font_metrics.ascent;
                break;
            default:
                // Top alignment
                text_y += font_metrics.ascent;
                break;
        }

        vg_font_draw_text(
            canvas, label->font, label->font_size, text_x, text_y, label->text, color);
    }
}

//=============================================================================
// Label API
//=============================================================================

/// @brief Replace the label's display text and invalidate the wrap cache.
///
/// @param label The label to update.
/// @param text  New text (copied); NULL is treated as an empty string.
void vg_label_set_text(vg_label_t *label, const char *text) {
    if (!label)
        return;

    const char *new_text = text ? text : "";
    if (label->text && strcmp(label->text, new_text) == 0)
        return;

    char *copy = strdup(new_text);
    if (!copy)
        return;

    free((void *)label->text);
    label->text = copy;
    label_free_wrap_cache(label); /* text changed — cache stale */
    label->base.needs_layout = true;
    label->base.needs_paint = true;
}

/// @brief Enable or disable word wrapping. When enabled, the label wraps its
///        text to the width it is laid out at and reports a multi-line height.
void vg_label_set_word_wrap(vg_label_t *label, bool enabled) {
    if (!label || label->word_wrap == enabled)
        return;
    label->word_wrap = enabled;
    label_free_wrap_cache(label); /* wrap mode changed — cache stale */
    label->base.needs_layout = true;
    label->base.needs_paint = true;
}

/// @brief Return the label's current display text.
///
/// @param label The label to query.
/// @return Internal string pointer, or NULL if label is NULL.
const char *vg_label_get_text(vg_label_t *label) {
    return label ? label->text : NULL;
}

/// @brief Override the label's font and size; invalidates the wrap cache.
///
/// @param label The label to configure.
/// @param font  Font to use; NULL keeps the existing font.
/// @param size  Font size in pixels; <= 0 defaults to 13.0.
void vg_label_set_font(vg_label_t *label, vg_font_t *font, float size) {
    if (!label)
        return;

    float font_size = size > 0 ? size : 13.0f;
    if (label->font == font && label->font_size == font_size)
        return;
    label->font = font;
    label->font_size = font_size;
    label_free_wrap_cache(label); /* font metrics changed — cache stale */
    label->base.needs_layout = true;
    label->base.needs_paint = true;
}

/// @brief Set the label's text colour.
///
/// @param label The label to configure.
/// @param color Text colour in 0xRRGGBB format.
void vg_label_set_color(vg_label_t *label, uint32_t color) {
    if (!label)
        return;
    if (label->text_color == color)
        return;

    label->text_color = color;
    label->base.needs_paint = true;
}

/// @brief Set the label's text alignment within its bounding box.
///
/// @param label   The label to configure.
/// @param h_align Horizontal alignment (VG_ALIGN_LEFT, CENTER, or RIGHT).
/// @param v_align Vertical alignment (VG_ALIGN_TOP, MIDDLE, or BOTTOM).
void vg_label_set_alignment(vg_label_t *label, vg_h_align_t h_align, vg_v_align_t v_align) {
    if (!label)
        return;
    if (label->h_align == h_align && label->v_align == v_align)
        return;

    label->h_align = h_align;
    label->v_align = v_align;
    label->base.needs_paint = true;
}
