// vg_label.c - Label widget implementation
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

vg_label_t *vg_label_create(vg_widget_t *parent, const char *text)
{
    vg_label_t *label = calloc(1, sizeof(vg_label_t));
    if (!label)
        return NULL;

    // Initialize base widget
    vg_widget_init(&label->base, VG_WIDGET_LABEL, &g_label_vtable);

    // Initialize label-specific fields
    label->text = text ? strdup(text) : strdup("");
    label->font = NULL;
    label->font_size = 13.0f; // Default size
    label->text_color = vg_theme_get_current()->colors.fg_primary;
    label->h_align = VG_ALIGN_H_LEFT;
    label->v_align = VG_ALIGN_V_CENTER;
    label->word_wrap = false;
    label->max_lines = 0;

    // Add to parent
    if (parent)
    {
        vg_widget_add_child(parent, &label->base);
    }

    return label;
}

static void label_destroy(vg_widget_t *widget)
{
    vg_label_t *label = (vg_label_t *)widget;
    if (label->text)
    {
        free((void *)label->text);
        label->text = NULL;
    }
}

/// @brief Measure the height of word-wrapped text capped at max_lines.
/// @return Number of lines produced.
static int label_measure_wrapped(vg_label_t *label,
                                  float wrap_width,
                                  float line_height,
                                  float *out_total_height)
{
    const char *p = label->text;
    float line_w = 0.0f;
    int lines = 1;
    char word_buf[1024];

    while (*p)
    {
        /* Collect next word (up to a space or newline) */
        const char *start = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        size_t word_len = (size_t)(p - start);

        /* Measure the word */
        if (word_len > 0 && word_len < sizeof(word_buf))
        {
            memcpy(word_buf, start, word_len);
            word_buf[word_len] = '\0';
            vg_text_metrics_t wm;
            vg_font_measure_text(label->font, label->font_size, word_buf, &wm);

            /* Measure separator (space) */
            float sep_w = 0.0f;
            if (line_w > 0.0f)
            {
                vg_text_metrics_t sm;
                vg_font_measure_text(label->font, label->font_size, " ", &sm);
                sep_w = sm.width;
            }

            if (line_w > 0.0f && line_w + sep_w + wm.width > wrap_width)
            {
                /* Word wraps to next line */
                lines++;
                if (label->max_lines > 0 && lines > label->max_lines)
                {
                    lines = label->max_lines;
                    break;
                }
                line_w = wm.width;
            }
            else
            {
                line_w += sep_w + wm.width;
            }
        }

        /* Handle newlines and spaces */
        while (*p == ' ')
            p++;
        if (*p == '\n')
        {
            lines++;
            if (label->max_lines > 0 && lines > label->max_lines)
            {
                lines = label->max_lines;
                break;
            }
            line_w = 0.0f;
            p++;
        }
    }

    if (out_total_height)
        *out_total_height = lines * line_height;
    return lines;
}

static void label_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_label_t *label = (vg_label_t *)widget;
    (void)available_height;

    if (!label->text || !label->font)
    {
        // No text or font - use minimum size
        widget->measured_width = widget->constraints.min_width;
        widget->measured_height = widget->constraints.min_height;
        return;
    }

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(label->font, label->font_size, &font_metrics);
    float line_height = (float)font_metrics.line_height;

    if (label->word_wrap && available_width > 0.0f)
    {
        float wrap_w = available_width;
        if (widget->constraints.max_width > 0.0f && widget->constraints.max_width < wrap_w)
            wrap_w = widget->constraints.max_width;

        float total_h = 0.0f;
        label_measure_wrapped(label, wrap_w, line_height, &total_h);
        widget->measured_width = wrap_w;
        widget->measured_height = total_h;
    }
    else
    {
        // Measure text
        vg_text_metrics_t metrics;
        vg_font_measure_text(label->font, label->font_size, label->text, &metrics);

        widget->measured_width = metrics.width;
        widget->measured_height = metrics.height;
    }

    // Apply constraints
    if (widget->measured_width < widget->constraints.min_width)
    {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height)
    {
        widget->measured_height = widget->constraints.min_height;
    }
    if (widget->constraints.max_width > 0 && widget->measured_width > widget->constraints.max_width)
    {
        widget->measured_width = widget->constraints.max_width;
    }
    if (widget->constraints.max_height > 0 &&
        widget->measured_height > widget->constraints.max_height)
    {
        widget->measured_height = widget->constraints.max_height;
    }
}

static void label_paint(vg_widget_t *widget, void *canvas)
{
    vg_label_t *label = (vg_label_t *)widget;

    if (!label->text || !label->text[0])
    {
        return; // Nothing to draw
    }

    if (!label->font)
    {
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

    if (label->word_wrap && widget->width > 0.0f)
    {
        /* Word-wrap paint: re-run the same greedy algorithm used in measure,
         * emitting each line as it fills up.  Vertical position starts at the
         * widget top + ascent regardless of v_align (wrapping multi-line text
         * is only meaningful in top-aligned mode). */
        float text_y = widget->y + font_metrics.ascent;
        float wrap_w = widget->width;
        float line_w = 0.0f;
        int lines = 0;
        char line_buf[4096];
        size_t line_pos = 0;
        char word_buf[1024];

        vg_text_metrics_t space_m;
        vg_font_measure_text(label->font, label->font_size, " ", &space_m);
        float space_w = space_m.width;

        const char *p = label->text;

        while (*p)
        {
            const char *word_start = p;
            while (*p && *p != ' ' && *p != '\n')
                p++;
            size_t word_len = (size_t)(p - word_start);

            if (word_len > 0 && word_len < sizeof(word_buf))
            {
                memcpy(word_buf, word_start, word_len);
                word_buf[word_len] = '\0';
                vg_text_metrics_t wm;
                vg_font_measure_text(label->font, label->font_size, word_buf, &wm);

                float needed = (line_w > 0.0f) ? (space_w + wm.width) : wm.width;

                bool do_wrap = (line_w > 0.0f && line_w + needed > wrap_w);
                bool max_hit = (label->max_lines > 0 && lines + 1 >= label->max_lines);

                if (do_wrap || (*p == '\n' && line_pos > 0))
                {
                    /* Flush current line */
                    line_buf[line_pos] = '\0';
                    float tx = widget->x;
                    switch (label->h_align)
                    {
                        case VG_ALIGN_H_CENTER:
                        {
                            vg_text_metrics_t lm;
                            vg_font_measure_text(label->font, label->font_size, line_buf, &lm);
                            tx += (widget->width - lm.width) / 2.0f;
                            break;
                        }
                        case VG_ALIGN_H_RIGHT:
                        {
                            vg_text_metrics_t lm;
                            vg_font_measure_text(label->font, label->font_size, line_buf, &lm);
                            tx += widget->width - lm.width;
                            break;
                        }
                        default:
                            break;
                    }
                    vg_font_draw_text(canvas, label->font, label->font_size, tx, text_y, line_buf, color);
                    lines++;
                    text_y += line_height;
                    line_pos = 0;
                    line_w = 0.0f;

                    if (max_hit)
                        goto done_wrap;
                }

                if (!do_wrap && line_w > 0.0f && line_pos + 1 < sizeof(line_buf))
                {
                    line_buf[line_pos++] = ' ';
                    line_w += space_w;
                }

                if (line_pos + word_len < sizeof(line_buf))
                {
                    memcpy(line_buf + line_pos, word_buf, word_len);
                    line_pos += word_len;
                    line_w += wm.width;
                }
            }

            while (*p == ' ')
                p++;
            if (*p == '\n')
            {
                /* Flush line at explicit newline */
                line_buf[line_pos] = '\0';
                if (line_pos > 0)
                {
                    float tx = widget->x;
                    switch (label->h_align)
                    {
                        case VG_ALIGN_H_CENTER:
                        {
                            vg_text_metrics_t lm;
                            vg_font_measure_text(label->font, label->font_size, line_buf, &lm);
                            tx += (widget->width - lm.width) / 2.0f;
                            break;
                        }
                        case VG_ALIGN_H_RIGHT:
                        {
                            vg_text_metrics_t lm;
                            vg_font_measure_text(label->font, label->font_size, line_buf, &lm);
                            tx += widget->width - lm.width;
                            break;
                        }
                        default:
                            break;
                    }
                    vg_font_draw_text(canvas, label->font, label->font_size, tx, text_y, line_buf, color);
                }
                lines++;
                text_y += line_height;
                line_pos = 0;
                line_w = 0.0f;
                p++;

                if (label->max_lines > 0 && lines >= label->max_lines)
                    goto done_wrap;
            }
        }

        /* Draw last (possibly partial) line */
        if (line_pos > 0 && (label->max_lines == 0 || lines < label->max_lines))
        {
            line_buf[line_pos] = '\0';
            float tx = widget->x;
            switch (label->h_align)
            {
                case VG_ALIGN_H_CENTER:
                {
                    vg_text_metrics_t lm;
                    vg_font_measure_text(label->font, label->font_size, line_buf, &lm);
                    tx += (widget->width - lm.width) / 2.0f;
                    break;
                }
                case VG_ALIGN_H_RIGHT:
                {
                    vg_text_metrics_t lm;
                    vg_font_measure_text(label->font, label->font_size, line_buf, &lm);
                    tx += widget->width - lm.width;
                    break;
                }
                default:
                    break;
            }
            vg_font_draw_text(canvas, label->font, label->font_size, tx, text_y, line_buf, color);
        }
done_wrap:;
    }
    else
    {
        // Calculate text position based on alignment
        float text_x = widget->x;
        float text_y = widget->y;

        vg_text_metrics_t metrics;
        vg_font_measure_text(label->font, label->font_size, label->text, &metrics);

        // Horizontal alignment
        switch (label->h_align)
        {
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
        switch (label->v_align)
        {
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

        vg_font_draw_text(canvas, label->font, label->font_size, text_x, text_y, label->text, color);
    }
}

//=============================================================================
// Label API
//=============================================================================

void vg_label_set_text(vg_label_t *label, const char *text)
{
    if (!label)
        return;

    if (label->text)
    {
        free((void *)label->text);
    }
    label->text = text ? strdup(text) : strdup("");
    label->base.needs_layout = true;
    label->base.needs_paint = true;
}

const char *vg_label_get_text(vg_label_t *label)
{
    return label ? label->text : NULL;
}

void vg_label_set_font(vg_label_t *label, vg_font_t *font, float size)
{
    if (!label)
        return;

    label->font = font;
    label->font_size = size > 0 ? size : 13.0f;
    label->base.needs_layout = true;
    label->base.needs_paint = true;
}

void vg_label_set_color(vg_label_t *label, uint32_t color)
{
    if (!label)
        return;

    label->text_color = color;
    label->base.needs_paint = true;
}

void vg_label_set_alignment(vg_label_t *label, vg_h_align_t h_align, vg_v_align_t v_align)
{
    if (!label)
        return;

    label->h_align = h_align;
    label->v_align = v_align;
    label->base.needs_paint = true;
}
