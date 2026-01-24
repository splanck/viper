// vg_outputpane.c - Output Pane widget implementation
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void outputpane_destroy(vg_widget_t *widget);
static void outputpane_measure(vg_widget_t *widget, float available_width, float available_height);
static void outputpane_paint(vg_widget_t *widget, void *canvas);
static bool outputpane_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// OutputPane VTable
//=============================================================================

static vg_widget_vtable_t g_outputpane_vtable = {.destroy = outputpane_destroy,
                                                 .measure = outputpane_measure,
                                                 .arrange = NULL,
                                                 .paint = outputpane_paint,
                                                 .handle_event = outputpane_handle_event,
                                                 .can_focus = NULL,
                                                 .on_focus = NULL};

//=============================================================================
// ANSI Color Tables
//=============================================================================

static const uint32_t g_ansi_colors[] = {
    0xFF000000, // Black
    0xFFCC0000, // Red
    0xFF00CC00, // Green
    0xFFCCCC00, // Yellow
    0xFF0000CC, // Blue
    0xFFCC00CC, // Magenta
    0xFF00CCCC, // Cyan
    0xFFCCCCCC, // White
};

static const uint32_t g_ansi_bright_colors[] = {
    0xFF666666, // Bright Black
    0xFFFF6666, // Bright Red
    0xFF66FF66, // Bright Green
    0xFFFFFF66, // Bright Yellow
    0xFF6666FF, // Bright Blue
    0xFFFF66FF, // Bright Magenta
    0xFF66FFFF, // Bright Cyan
    0xFFFFFFFF, // Bright White
};

static uint32_t ansi_code_to_color(int code)
{
    if (code >= 30 && code <= 37)
    {
        return g_ansi_colors[code - 30];
    }
    else if (code >= 90 && code <= 97)
    {
        return g_ansi_bright_colors[code - 90];
    }
    return 0xFFCCCCCC; // Default
}

//=============================================================================
// Output Line Management
//=============================================================================

static void free_output_line(vg_output_line_t *line)
{
    if (!line)
        return;

    for (size_t i = 0; i < line->segment_count; i++)
    {
        free(line->segments[i].text);
    }
    free(line->segments);
}

static vg_styled_segment_t *add_segment(vg_output_line_t *line)
{
    if (line->segment_count >= line->segment_capacity)
    {
        size_t new_cap = line->segment_capacity * 2;
        if (new_cap < 4)
            new_cap = 4;
        vg_styled_segment_t *new_segs =
            realloc(line->segments, new_cap * sizeof(vg_styled_segment_t));
        if (!new_segs)
            return NULL;
        line->segments = new_segs;
        line->segment_capacity = new_cap;
    }

    vg_styled_segment_t *seg = &line->segments[line->segment_count++];
    memset(seg, 0, sizeof(vg_styled_segment_t));
    return seg;
}

static vg_output_line_t *add_line(vg_outputpane_t *pane)
{
    // Check if we need to wrap around (ring buffer)
    if (pane->line_count >= pane->max_lines)
    {
        // Free oldest line
        free_output_line(&pane->lines[0]);
        // Shift all lines down
        memmove(
            &pane->lines[0], &pane->lines[1], (pane->line_count - 1) * sizeof(vg_output_line_t));
        pane->line_count--;
    }

    // Expand capacity if needed
    if (pane->line_count >= pane->line_capacity)
    {
        size_t new_cap = pane->line_capacity * 2;
        if (new_cap < 64)
            new_cap = 64;
        if (new_cap > pane->max_lines)
            new_cap = pane->max_lines;
        vg_output_line_t *new_lines = realloc(pane->lines, new_cap * sizeof(vg_output_line_t));
        if (!new_lines)
            return NULL;
        pane->lines = new_lines;
        pane->line_capacity = new_cap;
    }

    vg_output_line_t *line = &pane->lines[pane->line_count++];
    memset(line, 0, sizeof(vg_output_line_t));
    return line;
}

//=============================================================================
// ANSI Parser
//=============================================================================

static void process_ansi_escape(vg_outputpane_t *pane)
{
    // Parse escape sequence: ESC[<params>m
    // Common codes:
    // 0 = reset, 1 = bold, 30-37 = fg color, 40-47 = bg color
    // 90-97 = bright fg, 100-107 = bright bg

    char *buf = pane->escape_buf;
    if (buf[0] != '[')
    {
        pane->escape_len = 0;
        pane->in_escape = false;
        return;
    }

    // Parse parameters
    int params[16];
    int param_count = 0;
    char *p = buf + 1;

    while (*p && param_count < 16)
    {
        if (*p >= '0' && *p <= '9')
        {
            params[param_count] = (int)strtol(p, &p, 10);
            param_count++;
            if (*p == ';')
                p++;
        }
        else if (*p == 'm')
        {
            break;
        }
        else
        {
            p++;
        }
    }

    // Apply parameters
    for (int i = 0; i < param_count; i++)
    {
        int code = params[i];
        if (code == 0)
        {
            // Reset
            pane->current_fg = pane->default_fg;
            pane->current_bg = 0;
            pane->ansi_bold = false;
        }
        else if (code == 1)
        {
            pane->ansi_bold = true;
        }
        else if (code >= 30 && code <= 37)
        {
            pane->current_fg = ansi_code_to_color(code);
        }
        else if (code >= 40 && code <= 47)
        {
            pane->current_bg = ansi_code_to_color(code - 10);
        }
        else if (code >= 90 && code <= 97)
        {
            pane->current_fg = ansi_code_to_color(code);
        }
        else if (code >= 100 && code <= 107)
        {
            pane->current_bg = ansi_code_to_color(code - 10);
        }
    }

    pane->escape_len = 0;
    pane->in_escape = false;
}

//=============================================================================
// OutputPane Implementation
//=============================================================================

vg_outputpane_t *vg_outputpane_create(void)
{
    vg_outputpane_t *pane = calloc(1, sizeof(vg_outputpane_t));
    if (!pane)
        return NULL;

    vg_widget_init(&pane->base, VG_WIDGET_CUSTOM, &g_outputpane_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    pane->max_lines = 10000;
    pane->auto_scroll = true;
    pane->line_height = 16;
    pane->font_size = theme->typography.size_normal;

    pane->bg_color = 0xFF1E1E1E;
    pane->default_fg = 0xFFCCCCCC;
    pane->current_fg = pane->default_fg;

    return pane;
}

static void outputpane_destroy(vg_widget_t *widget)
{
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;

    for (size_t i = 0; i < pane->line_count; i++)
    {
        free_output_line(&pane->lines[i]);
    }
    free(pane->lines);
}

void vg_outputpane_destroy(vg_outputpane_t *pane)
{
    if (!pane)
        return;
    vg_widget_destroy(&pane->base);
}

static void outputpane_measure(vg_widget_t *widget, float available_width, float available_height)
{
    (void)available_width;
    (void)available_height;

    // OutputPane typically fills available space
    widget->measured_width = available_width;
    widget->measured_height = available_height;
}

static void outputpane_paint(vg_widget_t *widget, void *canvas)
{
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;

    // Draw background (placeholder)
    (void)pane->bg_color;

    if (!pane->font)
        return;

    // Calculate visible lines
    int first_visible = (int)(pane->scroll_y / pane->line_height);
    int visible_count = (int)(widget->height / pane->line_height) + 1;

    float y = widget->y - fmodf(pane->scroll_y, pane->line_height);

    for (int i = 0; i < visible_count && first_visible + i < (int)pane->line_count; i++)
    {
        int line_idx = first_visible + i;
        if (line_idx < 0)
            continue;

        vg_output_line_t *line = &pane->lines[line_idx];
        float x = widget->x + 4; // Left padding

        for (size_t s = 0; s < line->segment_count; s++)
        {
            vg_styled_segment_t *seg = &line->segments[s];
            if (!seg->text)
                continue;

            // Draw segment background if any
            if (seg->bg_color != 0)
            {
                // Background drawing would go here
            }

            // Draw text
            vg_font_draw_text(canvas, pane->font, pane->font_size, x, y, seg->text, seg->fg_color);

            // Advance X position
            vg_text_metrics_t metrics;
            vg_font_measure_text(pane->font, pane->font_size, seg->text, &metrics);
            x += metrics.width;
        }

        y += pane->line_height;
    }

    // Draw selection if any
    if (pane->has_selection)
    {
        // Selection drawing would go here
    }
}

static bool outputpane_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;

    if (event->type == VG_EVENT_MOUSE_WHEEL)
    {
        float delta = event->wheel.delta_y * 30; // 30 pixels per scroll unit
        pane->scroll_y -= delta;

        // Clamp scroll
        float max_scroll = pane->line_count * pane->line_height - widget->height;
        if (max_scroll < 0)
            max_scroll = 0;
        if (pane->scroll_y < 0)
            pane->scroll_y = 0;
        if (pane->scroll_y > max_scroll)
            pane->scroll_y = max_scroll;

        // Lock auto-scroll if user scrolled up
        pane->scroll_locked = pane->scroll_y < max_scroll - pane->line_height;

        pane->base.needs_paint = true;
        return true;
    }

    return false;
}

void vg_outputpane_append(vg_outputpane_t *pane, const char *text)
{
    if (!pane || !text)
        return;

    // Get or create current line
    vg_output_line_t *line = NULL;
    if (pane->line_count > 0)
    {
        line = &pane->lines[pane->line_count - 1];
    }
    else
    {
        line = add_line(pane);
        if (!line)
            return;
    }

    const char *p = text;
    const char *segment_start = p;

    while (*p)
    {
        if (*p == '\033')
        {
            // Flush pending text
            if (p > segment_start)
            {
                vg_styled_segment_t *seg = add_segment(line);
                if (seg)
                {
                    size_t len = (size_t)(p - segment_start);
                    seg->text = malloc(len + 1);
                    if (seg->text)
                    {
                        memcpy(seg->text, segment_start, len);
                        seg->text[len] = '\0';
                    }
                    seg->fg_color = pane->current_fg;
                    seg->bg_color = pane->current_bg;
                    seg->bold = pane->ansi_bold;
                }
            }

            // Start escape sequence
            pane->in_escape = true;
            pane->escape_len = 0;
            p++;
            segment_start = p;
        }
        else if (pane->in_escape)
        {
            // Accumulate escape sequence
            if (pane->escape_len < (int)sizeof(pane->escape_buf) - 1)
            {
                pane->escape_buf[pane->escape_len++] = *p;
                pane->escape_buf[pane->escape_len] = '\0';
            }

            if (*p == 'm' || *p == 'H' || *p == 'J' || *p == 'K')
            {
                // End of escape sequence
                process_ansi_escape(pane);
                segment_start = p + 1;
            }
            p++;
        }
        else if (*p == '\n')
        {
            // Flush pending text
            if (p > segment_start)
            {
                vg_styled_segment_t *seg = add_segment(line);
                if (seg)
                {
                    size_t len = (size_t)(p - segment_start);
                    seg->text = malloc(len + 1);
                    if (seg->text)
                    {
                        memcpy(seg->text, segment_start, len);
                        seg->text[len] = '\0';
                    }
                    seg->fg_color = pane->current_fg;
                    seg->bg_color = pane->current_bg;
                    seg->bold = pane->ansi_bold;
                }
            }

            // Start new line
            line = add_line(pane);
            if (!line)
                return;

            p++;
            segment_start = p;
        }
        else
        {
            p++;
        }
    }

    // Flush remaining text
    if (p > segment_start && !pane->in_escape)
    {
        vg_styled_segment_t *seg = add_segment(line);
        if (seg)
        {
            size_t len = (size_t)(p - segment_start);
            seg->text = malloc(len + 1);
            if (seg->text)
            {
                memcpy(seg->text, segment_start, len);
                seg->text[len] = '\0';
            }
            seg->fg_color = pane->current_fg;
            seg->bg_color = pane->current_bg;
            seg->bold = pane->ansi_bold;
        }
    }

    // Auto-scroll
    if (pane->auto_scroll && !pane->scroll_locked)
    {
        vg_outputpane_scroll_to_bottom(pane);
    }

    pane->base.needs_paint = true;
}

void vg_outputpane_append_line(vg_outputpane_t *pane, const char *text)
{
    if (!pane)
        return;

    // Always create new line
    vg_output_line_t *line = add_line(pane);
    if (!line)
        return;

    if (text && *text)
    {
        // Use append to handle ANSI codes
        size_t len = strlen(text);
        char *with_newline = malloc(len + 2);
        if (with_newline)
        {
            memcpy(with_newline, text, len);
            with_newline[len] = '\n';
            with_newline[len + 1] = '\0';
            vg_outputpane_append(pane, with_newline);
            free(with_newline);
        }
    }
}

void vg_outputpane_append_styled(
    vg_outputpane_t *pane, const char *text, uint32_t fg, uint32_t bg, bool bold)
{
    if (!pane || !text)
        return;

    // Get or create current line
    vg_output_line_t *line = NULL;
    if (pane->line_count > 0)
    {
        line = &pane->lines[pane->line_count - 1];
    }
    else
    {
        line = add_line(pane);
        if (!line)
            return;
    }

    vg_styled_segment_t *seg = add_segment(line);
    if (seg)
    {
        seg->text = strdup(text);
        seg->fg_color = fg;
        seg->bg_color = bg;
        seg->bold = bold;
    }

    // Auto-scroll
    if (pane->auto_scroll && !pane->scroll_locked)
    {
        vg_outputpane_scroll_to_bottom(pane);
    }

    pane->base.needs_paint = true;
}

void vg_outputpane_clear(vg_outputpane_t *pane)
{
    if (!pane)
        return;

    for (size_t i = 0; i < pane->line_count; i++)
    {
        free_output_line(&pane->lines[i]);
    }
    pane->line_count = 0;

    // Reset ANSI state
    pane->current_fg = pane->default_fg;
    pane->current_bg = 0;
    pane->ansi_bold = false;
    pane->in_escape = false;
    pane->escape_len = 0;

    // Reset scroll
    pane->scroll_y = 0;
    pane->scroll_locked = false;

    pane->base.needs_paint = true;
}

void vg_outputpane_scroll_to_bottom(vg_outputpane_t *pane)
{
    if (!pane)
        return;

    float content_height = pane->line_count * pane->line_height;
    float max_scroll = content_height - pane->base.height;
    if (max_scroll < 0)
        max_scroll = 0;

    pane->scroll_y = max_scroll;
    pane->scroll_locked = false;
    pane->base.needs_paint = true;
}

void vg_outputpane_scroll_to_top(vg_outputpane_t *pane)
{
    if (!pane)
        return;

    pane->scroll_y = 0;
    pane->scroll_locked = true;
    pane->base.needs_paint = true;
}

void vg_outputpane_set_auto_scroll(vg_outputpane_t *pane, bool auto_scroll)
{
    if (!pane)
        return;
    pane->auto_scroll = auto_scroll;
}

char *vg_outputpane_get_selection(vg_outputpane_t *pane)
{
    if (!pane || !pane->has_selection)
        return NULL;

    // TODO: Implement selection text extraction
    return NULL;
}

void vg_outputpane_select_all(vg_outputpane_t *pane)
{
    if (!pane || pane->line_count == 0)
        return;

    pane->has_selection = true;
    pane->sel_start_line = 0;
    pane->sel_start_col = 0;
    pane->sel_end_line = (uint32_t)(pane->line_count - 1);
    pane->sel_end_col = UINT32_MAX; // End of line

    pane->base.needs_paint = true;
}

void vg_outputpane_set_max_lines(vg_outputpane_t *pane, size_t max)
{
    if (!pane)
        return;
    pane->max_lines = max;
}

void vg_outputpane_set_font(vg_outputpane_t *pane, vg_font_t *font, float size)
{
    if (!pane)
        return;

    pane->font = font;
    pane->font_size = size;
    pane->base.needs_paint = true;
}
