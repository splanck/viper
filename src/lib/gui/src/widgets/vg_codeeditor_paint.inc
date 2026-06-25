//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file vg_codeeditor_paint.inc
/// @brief CodeEditor painting, gutter, selection, cursor, syntax highlight,
/// diagnostics, folds, inlay hints, and overlay rendering.
///
/// @details This include fragment is consumed only by vg_codeeditor.c and
/// separates the visual rendering path from editing, history, lifecycle, input,
/// and API-facing code.
///
// File: src/lib/gui/src/widgets/vg_codeeditor_paint.inc
// Purpose: Gutter icon rendering, fold markers, highlights, inlay hints, text painting, cursor
// drawing, and scrollbar painting.
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

static void codeeditor_draw_gutter_icon_image(vgfx_window_t canvas,
                                              const struct vg_gutter_icon *icon,
                                              int32_t dst_x,
                                              int32_t dst_y,
                                              int32_t dst_w,
                                              int32_t dst_h) {
    if (!icon || icon->image.type != VG_ICON_IMAGE || !icon->image.data.image.pixels ||
        dst_w <= 0 || dst_h <= 0)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas, &fb))
        return;

    const uint8_t *src_pixels = icon->image.data.image.pixels;
    int src_w = (int)icon->image.data.image.width;
    int src_h = (int)icon->image.data.image.height;
    int clip_x = 0;
    int clip_y = 0;
    int clip_w = fb.width;
    int clip_h = fb.height;
    if (src_w <= 0 || src_h <= 0)
        return;

    (void)vgfx_get_clip(canvas, &clip_x, &clip_y, &clip_w, &clip_h);

    for (int row = 0; row < dst_h; row++) {
        int src_y = row * src_h / dst_h;
        if (src_y >= src_h)
            src_y = src_h - 1;
        int fb_y = dst_y + row;
        if (fb_y < 0 || fb_y >= fb.height || fb_y < clip_y || fb_y >= clip_y + clip_h)
            continue;

        for (int col = 0; col < dst_w; col++) {
            int src_x = col * src_w / dst_w;
            if (src_x >= src_w)
                src_x = src_w - 1;
            int fb_x = dst_x + col;
            if (fb_x < 0 || fb_x >= fb.width || fb_x < clip_x || fb_x >= clip_x + clip_w)
                continue;

            const uint8_t *src = &src_pixels[(src_y * src_w + src_x) * 4];
            uint8_t alpha = src[3];
            if (alpha == 0)
                continue;

            int fb_idx = fb_y * fb.stride + fb_x * 4;
            if (alpha == 255) {
                fb.pixels[fb_idx + 0] = src[0];
                fb.pixels[fb_idx + 1] = src[1];
                fb.pixels[fb_idx + 2] = src[2];
                fb.pixels[fb_idx + 3] = 0xFF;
                continue;
            }

            uint8_t dst_r = fb.pixels[fb_idx + 0];
            uint8_t dst_g = fb.pixels[fb_idx + 1];
            uint8_t dst_b = fb.pixels[fb_idx + 2];
            uint8_t inv_alpha = (uint8_t)(255 - alpha);
            fb.pixels[fb_idx + 0] = (uint8_t)((src[0] * alpha + dst_r * inv_alpha) / 255);
            fb.pixels[fb_idx + 1] = (uint8_t)((src[1] * alpha + dst_g * inv_alpha) / 255);
            fb.pixels[fb_idx + 2] = (uint8_t)((src[2] * alpha + dst_b * inv_alpha) / 255);
            fb.pixels[fb_idx + 3] = 0xFF;
        }
    }
}

/// @brief Draws a '+' or '-' fold marker in the fold gutter column for the region starting at @p
/// line.
static void codeeditor_draw_fold_marker(vg_codeeditor_t *editor,
                                        void *canvas,
                                        float fold_gutter_x,
                                        float fold_gutter_width,
                                        float line_y,
                                        const vg_font_metrics_t *font_metrics,
                                        int line) {
    const struct vg_fold_region *region = codeeditor_fold_region_starting_at(editor, line);
    if (!editor || !region || fold_gutter_width <= 0.0f || !font_metrics)
        return;

    char marker[2] = {region->folded ? '+' : '-', '\0'};
    vg_text_metrics_t marker_metrics = {0};
    vg_font_measure_text(editor->font, editor->font_size, marker, &marker_metrics);
    float marker_x = fold_gutter_x + (fold_gutter_width - marker_metrics.width) * 0.5f;
    float marker_y = line_y + font_metrics->ascent;
    vg_font_draw_text(canvas,
                      editor->font,
                      editor->font_size,
                      marker_x,
                      marker_y,
                      marker,
                      editor->line_number_color);
}

/// @brief Draws a "..." continuation marker after the last visible character of a folded region.
static void codeeditor_draw_fold_ellipsis(vg_codeeditor_t *editor,
                                          void *canvas,
                                          float content_x,
                                          float baseline_y,
                                          int line,
                                          int start_col) {
    const struct vg_fold_region *region = codeeditor_fold_region_starting_at(editor, line);
    if (!editor || !region || !region->folded)
        return;

    float ellipsis_x = content_x + (float)start_col * editor->char_width + 4.0f;
    vg_font_draw_text(canvas,
                      editor->font,
                      editor->font_size,
                      ellipsis_x,
                      baseline_y,
                      "...",
                      editor->line_number_color);
}

/// @brief Draws an editor highlight span as an underline so diagnostics do not obscure the text.
static void codeeditor_draw_highlight_underline(
    void *canvas, float x, float y, float width, float line_height, uint32_t color) {
    if (!canvas || width <= 0.0f || line_height <= 0.0f)
        return;

    float underline_height = line_height >= 8.0f ? 2.0f : 1.0f;
    float underline_y = y + line_height - underline_height - 1.0f;
    if (underline_y < y)
        underline_y = y;

    int32_t draw_width = (int32_t)(width + 0.5f);
    int32_t draw_height = (int32_t)(underline_height + 0.5f);
    if (draw_width < 1)
        draw_width = 1;
    if (draw_height < 1)
        draw_height = 1;

    vgfx_fill_rect((vgfx_window_t)canvas,
                   (int32_t)x,
                   (int32_t)underline_y,
                   draw_width,
                   draw_height,
                   (vgfx_color_t)color);
}

static void codeeditor_paint_inlay_hints_for_segment(vg_codeeditor_t *editor,
                                                     void *canvas,
                                                     int line,
                                                     int segment_start,
                                                     int segment_len,
                                                     float content_x,
                                                     float row_y,
                                                     float scroll_x,
                                                     float baseline_y,
                                                     bool wrapped) {
    if (!editor || !canvas || !editor->font || editor->inlay_hint_count <= 0)
        return;

    int line_len = 0;
    if (line >= 0 && line < editor->line_count)
        line_len = (int)editor->lines[line].length;
    int segment_end = segment_start + segment_len;
    float hint_size = editor->font_size * 0.9f;
    if (hint_size <= 0.0f)
        hint_size = editor->font_size;

    int first_hint = codeeditor_first_inlay_hint_for_line(editor, line);
    if (first_hint < 0)
        return;

    for (int i = first_hint; i < editor->inlay_hint_count; i++) {
        const struct vg_inlay_hint *hint = &editor->inlay_hints[i];
        if (hint->line != line)
            break;
        if (!hint->text || hint->text[0] == '\0')
            continue;

        int col = hint->col;
        if (col < 0)
            col = 0;
        if (col > line_len)
            col = line_len;

        if (wrapped) {
            bool at_segment_end = col == line_len && line_len <= segment_end;
            if ((col < segment_start || col > segment_end) && !at_segment_end)
                continue;
        }

        int visual_col = wrapped ? col - segment_start : col;
        if (visual_col < 0)
            visual_col = 0;
        float x = content_x + (float)visual_col * editor->char_width - scroll_x + 4.0f;
        vg_text_metrics_t metrics = {0};
        vg_font_measure_text(editor->font, hint_size, hint->text, &metrics);
        if (metrics.width > 0.0f) {
            uint32_t bg = (editor->current_line_bg & 0x00FFFFFFu) | 0x66000000u;
            vgfx_fill_rect((vgfx_window_t)canvas,
                           (int32_t)(x - 2.0f),
                           (int32_t)(row_y + 2.0f),
                           (int32_t)(metrics.width + 4.0f),
                           (int32_t)(editor->line_height - 4.0f),
                           bg);
        }
        vg_font_draw_text(canvas, editor->font, hint_size, x, baseline_y, hint->text, hint->color);
    }
}

/// @brief Draw faint vertical indent guides for one (non-wrapped) line.
///
/// One guide per indentation level — columns 0, tab_width, 2*tab_width, … up to
/// the line's leading-whitespace depth (tabs snap to the next tab stop). The
/// guide color falls back to a muted dark grey when unset. Wrapped continuation
/// rows are not handled (the non-wrapped paint path is the only caller).
static void codeeditor_paint_indent_guides(
    vg_codeeditor_t *editor, void *canvas, int line, float content_x, float line_y) {
    if (!editor || line < 0 || line >= editor->line_count)
        return;
    const vg_code_line_t *ln = &editor->lines[line];
    const char *text = ln->text;
    if (!text)
        return;
    int tab_w = editor->tab_width > 0 ? editor->tab_width : 4;

    int cols = 0;
    for (int k = 0; k < (int)ln->length; k++) {
        char c = text[k];
        if (c == ' ')
            cols++;
        else if (c == '\t')
            cols += tab_w - (cols % tab_w);
        else
            break;
    }
    int levels = cols / tab_w;
    if (levels <= 0)
        return;

    uint32_t color = editor->indent_guide_color ? editor->indent_guide_color : 0xFF3C3C3Cu;
    for (int lvl = 0; lvl < levels; lvl++) {
        float gx = content_x + (float)(lvl * tab_w) * editor->char_width - editor->scroll_x;
        if (gx < content_x - 0.5f)
            continue; // scrolled left of the content area
        vgfx_fill_rect((vgfx_window_t)canvas,
                       (int32_t)gx,
                       (int32_t)line_y,
                       1,
                       (int32_t)editor->line_height,
                       color);
    }
}

/// @brief VTable paint: renders background, gutter, current-line highlight, selection,
/// syntax-coloured text, cursor, and scrollbar.
static void codeeditor_paint(vg_widget_t *widget, void *canvas) {
    vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;

    if (!editor->font)
        return;

    float content_x = widget->x + editor->gutter_width;
    float content_width = codeeditor_content_draw_width(editor, widget);
    float total_content_height = codeeditor_total_content_height_for_width(editor, content_width);
    float visible_height = widget->height;
    float scrollbar_width = CODEEDITOR_SCROLLBAR_WIDTH;
    float line_number_gutter_width = codeeditor_line_number_gutter_width(editor);
    float fold_gutter_width = codeeditor_fold_gutter_width(editor);
    codeeditor_clamp_scroll(editor, widget);

    int first_visual_row = (int)(editor->scroll_y / editor->line_height);
    codeeditor_locate_visual_row(
        editor, content_width, first_visual_row, &editor->visible_first_line, NULL);
    editor->visible_line_count = (int)(widget->height / editor->line_height) + 2;

    // Draw background
    vgfx_fill_rect((vgfx_window_t)canvas,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   editor->bg_color);

    // Draw gutter background
    if (editor->gutter_width > 0.0f) {
        vgfx_fill_rect((vgfx_window_t)canvas,
                       (int32_t)widget->x,
                       (int32_t)widget->y,
                       (int32_t)editor->gutter_width,
                       (int32_t)widget->height,
                       editor->gutter_bg);
    }

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(editor->font, editor->font_size, &font_metrics);
    codeeditor_sort_highlight_spans(editor);
    codeeditor_sort_inlay_hints(editor);
    int32_t clip_x = (int32_t)content_x;
    int32_t clip_y = (int32_t)widget->y;
    int32_t clip_w = (int32_t)content_width;
    int32_t clip_h = (int32_t)widget->height;

    if (clip_w > 0 && clip_h > 0) {
        vgfx_set_clip((vgfx_window_t)canvas, clip_x, clip_y, clip_w, clip_h);
    }

    if (!editor->word_wrap) {
        float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
        float line_y = widget->y - row_offset;
        for (int i = editor->visible_first_line;
             i >= 0 && i < editor->line_count && line_y < widget->y + widget->height;
             i = codeeditor_next_visible_line(editor, i)) {
            if (codeeditor_line_is_hidden(editor, i))
                continue;

            if (i == editor->cursor_line && (widget->state & VG_STATE_FOCUSED)) {
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)content_x,
                               (int32_t)line_y,
                               (int32_t)content_width,
                               (int32_t)editor->line_height,
                               editor->current_line_bg);
                // Bright accent bar down the left edge of the active line.
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)content_x,
                               (int32_t)line_y,
                               3,
                               (int32_t)editor->line_height,
                               vg_theme_get_current()->colors.accent_primary);
            }

            if (editor->show_indent_guides)
                codeeditor_paint_indent_guides(editor, canvas, i, content_x, line_y);

            codeeditor_paint_highlights_for_line(editor,
                                                 canvas,
                                                 i,
                                                 0,
                                                 (int)editor->lines[i].length,
                                                 content_x,
                                                 line_y,
                                                 editor->scroll_x,
                                                 false);
            codeeditor_paint_pair_highlight_for_segment(editor,
                                                        canvas,
                                                        i,
                                                        0,
                                                        (int)editor->lines[i].length,
                                                        content_x,
                                                        line_y,
                                                        editor->scroll_x,
                                                        false);

            if (editor->has_selection && (widget->state & VG_STATE_FOCUSED)) {
                int sel_start_line = editor->selection.start_line;
                int sel_start_col = editor->selection.start_col;
                int sel_end_line = editor->selection.end_line;
                int sel_end_col = editor->selection.end_col;
                normalize_selection_range(
                    &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                if (i >= sel_start_line && i <= sel_end_line) {
                    int col_start = (i == sel_start_line) ? sel_start_col : 0;
                    int col_end = (i == sel_end_line) ? sel_end_col : (int)editor->lines[i].length;
                    float sel_x = content_x + col_start * editor->char_width - editor->scroll_x;
                    float sel_width = (col_end - col_start) * editor->char_width;
                    vgfx_fill_rect((vgfx_window_t)canvas,
                                   (int32_t)sel_x,
                                   (int32_t)line_y,
                                   (int32_t)sel_width,
                                   (int32_t)editor->line_height,
                                   editor->selection_color);
                }
            }

            if (widget->state & VG_STATE_FOCUSED) {
                for (int c = 0; c < editor->extra_cursor_count; c++) {
                    if (!editor->extra_cursors[c].has_selection)
                        continue;
                    int sel_start_line = editor->extra_cursors[c].selection.start_line;
                    int sel_start_col = editor->extra_cursors[c].selection.start_col;
                    int sel_end_line = editor->extra_cursors[c].selection.end_line;
                    int sel_end_col = editor->extra_cursors[c].selection.end_col;
                    normalize_selection_range(
                        &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                    if (i < sel_start_line || i > sel_end_line)
                        continue;
                    int col_start = (i == sel_start_line) ? sel_start_col : 0;
                    int col_end = (i == sel_end_line) ? sel_end_col : (int)editor->lines[i].length;
                    float sel_x = content_x + col_start * editor->char_width - editor->scroll_x;
                    float sel_width = (col_end - col_start) * editor->char_width;
                    vgfx_fill_rect((vgfx_window_t)canvas,
                                   (int32_t)sel_x,
                                   (int32_t)line_y,
                                   (int32_t)sel_width,
                                   (int32_t)editor->line_height,
                                   editor->selection_color);
                }
            }

            if (editor->lines[i].text && editor->lines[i].length > 0) {
                float text_y = line_y + font_metrics.ascent;
                highlight_line(editor, i);
                float text_x = content_x - editor->scroll_x;
                if (editor->lines[i].colors) {
                    codeeditor_draw_colored_slice(canvas,
                                                  editor->font,
                                                  editor->font_size,
                                                  text_x,
                                                  text_y,
                                                  editor->lines[i].text,
                                                  editor->lines[i].colors,
                                                  editor->lines[i].colors_capacity,
                                                  0,
                                                  editor->lines[i].length,
                                                  editor->char_width,
                                                  editor->text_color);
                } else {
                    vg_font_draw_text(canvas,
                                      editor->font,
                                      editor->font_size,
                                      text_x,
                                      text_y,
                                      editor->lines[i].text,
                                      editor->text_color);
                }
                codeeditor_draw_fold_ellipsis(editor,
                                              canvas,
                                              content_x - editor->scroll_x,
                                              text_y,
                                              i,
                                              (int)editor->lines[i].length);
            }
            codeeditor_paint_inlay_hints_for_segment(editor,
                                                     canvas,
                                                     i,
                                                     0,
                                                     (int)editor->lines[i].length,
                                                     content_x,
                                                     line_y,
                                                     editor->scroll_x,
                                                     line_y + font_metrics.ascent,
                                                     false);

            line_y += editor->line_height;
        }

        if ((widget->state & VG_STATE_FOCUSED) && editor->cursor_visible && !editor->read_only) {
            int visible_cursor_line = codeeditor_visual_row_for_position(
                editor, content_width, editor->cursor_line, editor->cursor_col);
            float cursor_y =
                widget->y + (float)visible_cursor_line * editor->line_height - editor->scroll_y;
            if (cursor_y + editor->line_height > widget->y &&
                cursor_y < widget->y + widget->height) {
                float cursor_x =
                    content_x + editor->cursor_col * editor->char_width - editor->scroll_x;
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)cursor_x,
                               (int32_t)cursor_y,
                               2,
                               (int32_t)editor->line_height,
                               editor->cursor_color);
            }

            for (int c = 0; c < editor->extra_cursor_count; c++) {
                int visible_extra_line =
                    codeeditor_visual_row_for_position(editor,
                                                       content_width,
                                                       editor->extra_cursors[c].line,
                                                       editor->extra_cursors[c].col);
                float cursor_x = content_x + editor->extra_cursors[c].col * editor->char_width -
                                 editor->scroll_x;
                float extra_cursor_y =
                    widget->y + (float)visible_extra_line * editor->line_height - editor->scroll_y;
                if (extra_cursor_y + editor->line_height <= widget->y ||
                    extra_cursor_y >= widget->y + widget->height)
                    continue;
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)cursor_x,
                               (int32_t)extra_cursor_y,
                               2,
                               (int32_t)editor->line_height,
                               editor->cursor_color);
            }
        }
    } else {
        int chars_per_row = codeeditor_chars_per_row(editor, content_width);
        int wrapped_first_visual_row = (int)(editor->scroll_y / editor->line_height);
        float row_offset = editor->scroll_y - (float)wrapped_first_visual_row * editor->line_height;
        int line = 0;
        int row_in_line = 0;
        codeeditor_locate_visual_row(
            editor, content_width, wrapped_first_visual_row, &line, &row_in_line);

        float row_y = widget->y - row_offset;
        while (line < editor->line_count && row_y < widget->y + widget->height) {
            if (line >= 0 && line < editor->line_count) {
                size_t line_len = editor->lines[line].length;
                int row_count = codeeditor_cached_row_count_for_line(editor, content_width, line);
                if (row_count == 0) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                    continue;
                }
                int seg_start = chars_per_row > 0 ? row_in_line * chars_per_row : 0;
                size_t remaining = line_len > (size_t)seg_start ? line_len - (size_t)seg_start : 0;
                int seg_len = chars_per_row > 0 ? (int)((remaining < (size_t)chars_per_row)
                                                            ? remaining
                                                            : (size_t)chars_per_row)
                                                : (int)line_len;

                if (line == editor->cursor_line && (widget->state & VG_STATE_FOCUSED)) {
                    vgfx_fill_rect((vgfx_window_t)canvas,
                                   (int32_t)content_x,
                                   (int32_t)row_y,
                                   (int32_t)content_width,
                                   (int32_t)editor->line_height,
                                   editor->current_line_bg);
                }

                codeeditor_paint_highlights_for_line(
                    editor, canvas, line, seg_start, seg_len, content_x, row_y, 0.0f, true);
                codeeditor_paint_pair_highlight_for_segment(
                    editor, canvas, line, seg_start, seg_len, content_x, row_y, 0.0f, true);

                if (editor->has_selection && (widget->state & VG_STATE_FOCUSED)) {
                    int sel_start_line = editor->selection.start_line;
                    int sel_start_col = editor->selection.start_col;
                    int sel_end_line = editor->selection.end_line;
                    int sel_end_col = editor->selection.end_col;
                    normalize_selection_range(
                        &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                    if (line >= sel_start_line && line <= sel_end_line) {
                        int sel_start = (line == sel_start_line) ? sel_start_col : 0;
                        int sel_end = (line == sel_end_line) ? sel_end_col : (int)line_len;
                        int overlap_start = sel_start > seg_start ? sel_start : seg_start;
                        int overlap_end =
                            sel_end < seg_start + seg_len ? sel_end : seg_start + seg_len;
                        if (overlap_end > overlap_start) {
                            float sel_x =
                                content_x + (float)(overlap_start - seg_start) * editor->char_width;
                            float sel_w = (float)(overlap_end - overlap_start) * editor->char_width;
                            vgfx_fill_rect((vgfx_window_t)canvas,
                                           (int32_t)sel_x,
                                           (int32_t)row_y,
                                           (int32_t)sel_w,
                                           (int32_t)editor->line_height,
                                           editor->selection_color);
                        }
                    }
                }

                if (widget->state & VG_STATE_FOCUSED) {
                    for (int c = 0; c < editor->extra_cursor_count; c++) {
                        if (!editor->extra_cursors[c].has_selection)
                            continue;
                        int sel_start_line = editor->extra_cursors[c].selection.start_line;
                        int sel_start_col = editor->extra_cursors[c].selection.start_col;
                        int sel_end_line = editor->extra_cursors[c].selection.end_line;
                        int sel_end_col = editor->extra_cursors[c].selection.end_col;
                        normalize_selection_range(
                            &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                        if (line < sel_start_line || line > sel_end_line)
                            continue;
                        int sel_start = (line == sel_start_line) ? sel_start_col : 0;
                        int sel_end = (line == sel_end_line) ? sel_end_col : (int)line_len;
                        int overlap_start = sel_start > seg_start ? sel_start : seg_start;
                        int overlap_end =
                            sel_end < seg_start + seg_len ? sel_end : seg_start + seg_len;
                        if (overlap_end > overlap_start) {
                            float sel_x =
                                content_x + (float)(overlap_start - seg_start) * editor->char_width;
                            float sel_w = (float)(overlap_end - overlap_start) * editor->char_width;
                            vgfx_fill_rect((vgfx_window_t)canvas,
                                           (int32_t)sel_x,
                                           (int32_t)row_y,
                                           (int32_t)sel_w,
                                           (int32_t)editor->line_height,
                                           editor->selection_color);
                        }
                    }
                }

                highlight_line(editor, line);
                if (editor->lines[line].text && line_len > 0 && seg_len > 0) {
                    float text_y = row_y + font_metrics.ascent;
                    if (editor->lines[line].colors) {
                        codeeditor_draw_colored_slice(canvas,
                                                      editor->font,
                                                      editor->font_size,
                                                      content_x,
                                                      text_y,
                                                      editor->lines[line].text,
                                                      editor->lines[line].colors,
                                                      editor->lines[line].colors_capacity,
                                                      (size_t)seg_start,
                                                      (size_t)seg_len,
                                                      editor->char_width,
                                                      editor->text_color);
                    } else {
                        char stack_buf[256];
                        char *seg_buf = stack_buf;
                        size_t copy_len = (size_t)seg_len;
                        if (copy_len >= sizeof(stack_buf)) {
                            seg_buf = (char *)malloc(copy_len + 1);
                            if (!seg_buf)
                                seg_buf = stack_buf;
                        }
                        if (seg_buf) {
                            if (seg_buf == stack_buf && copy_len >= sizeof(stack_buf)) {
                                copy_len = sizeof(stack_buf) - 1;
                            }
                            memcpy(seg_buf, editor->lines[line].text + seg_start, copy_len);
                            seg_buf[copy_len] = '\0';
                            vg_font_draw_text(canvas,
                                              editor->font,
                                              editor->font_size,
                                              content_x,
                                              text_y,
                                              seg_buf,
                                              editor->text_color);
                            if (seg_buf != stack_buf)
                                free(seg_buf);
                        }
                    }
                }
                if (row_in_line == 0) {
                    codeeditor_draw_fold_ellipsis(
                        editor, canvas, content_x, row_y + font_metrics.ascent, line, seg_len);
                }
                codeeditor_paint_inlay_hints_for_segment(editor,
                                                         canvas,
                                                         line,
                                                         seg_start,
                                                         seg_len,
                                                         content_x,
                                                         row_y,
                                                         0.0f,
                                                         row_y + font_metrics.ascent,
                                                         true);

                row_in_line++;
                if (row_in_line >= row_count) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                }
            } else {
                line = codeeditor_next_visible_line(editor, line);
                row_in_line = 0;
            }
            row_y += editor->line_height;
        }

        if ((widget->state & VG_STATE_FOCUSED) && editor->cursor_visible && !editor->read_only) {
            int cursor_visual_row = codeeditor_visual_row_for_position(
                editor, content_width, editor->cursor_line, editor->cursor_col);
            float cursor_y =
                widget->y + (float)cursor_visual_row * editor->line_height - editor->scroll_y;
            int col_in_row = editor->cursor_col;
            codeeditor_visual_offset_for_position(
                editor, content_width, editor->cursor_line, editor->cursor_col, NULL, &col_in_row);
            float cursor_x = content_x + (float)col_in_row * editor->char_width;
            vgfx_fill_rect((vgfx_window_t)canvas,
                           (int32_t)cursor_x,
                           (int32_t)cursor_y,
                           2,
                           (int32_t)editor->line_height,
                           editor->cursor_color);

            for (int c = 0; c < editor->extra_cursor_count; c++) {
                int extra_visual_row =
                    codeeditor_visual_row_for_position(editor,
                                                       content_width,
                                                       editor->extra_cursors[c].line,
                                                       editor->extra_cursors[c].col);
                float extra_y =
                    widget->y + (float)extra_visual_row * editor->line_height - editor->scroll_y;
                int extra_col_in_row = editor->extra_cursors[c].col;
                codeeditor_visual_offset_for_position(editor,
                                                      content_width,
                                                      editor->extra_cursors[c].line,
                                                      editor->extra_cursors[c].col,
                                                      NULL,
                                                      &extra_col_in_row);
                float extra_x = content_x + (float)extra_col_in_row * editor->char_width;
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)extra_x,
                               (int32_t)extra_y,
                               2,
                               (int32_t)editor->line_height,
                               editor->cursor_color);
            }
        }
    }

    if (clip_w > 0 && clip_h > 0) {
        vgfx_clear_clip((vgfx_window_t)canvas);
    }

    if (editor->gutter_width > 0.0f) {
        float fold_gutter_x = widget->x + line_number_gutter_width;
        if (!editor->word_wrap) {
            float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
            float line_y = widget->y - row_offset;
            for (int i = editor->visible_first_line;
                 i >= 0 && i < editor->line_count && line_y < widget->y + widget->height;
                 i = codeeditor_next_visible_line(editor, i)) {
                if (codeeditor_line_is_hidden(editor, i))
                    continue;

                if (editor->show_line_numbers) {
                    char line_num[16];
                    snprintf(line_num, sizeof(line_num), "%d", i + 1);
                    vg_text_metrics_t num_metrics = {0};
                    vg_font_measure_text(editor->font, editor->font_size, line_num, &num_metrics);
                    float num_x = widget->x + line_number_gutter_width - num_metrics.width - 8.0f;
                    float num_y = line_y + font_metrics.ascent;
                    vg_font_draw_text(canvas,
                                      editor->font,
                                      editor->font_size,
                                      num_x,
                                      num_y,
                                      line_num,
                                      editor->line_number_color);
                }

                for (int g = 0; g < editor->gutter_icon_count; g++) {
                    struct vg_gutter_icon *icon = &editor->gutter_icons[g];
                    if (icon->line != i)
                        continue;
                    int32_t icon_box = (int32_t)editor->line_height - 4;
                    if (icon_box < 8)
                        icon_box = 8;
                    int32_t icon_x = (int32_t)widget->x + 2;
                    int32_t icon_y =
                        (int32_t)line_y + ((int32_t)editor->line_height - icon_box) / 2;
                    if (icon->image.type == VG_ICON_IMAGE && icon->image.data.image.pixels) {
                        codeeditor_draw_gutter_icon_image(
                            (vgfx_window_t)canvas, icon, icon_x, icon_y, icon_box, icon_box);
                    } else {
                        int32_t icon_r = icon_box / 2;
                        int32_t icon_cx = icon_x + icon_r;
                        int32_t icon_cy = icon_y + icon_r;
                        vgfx_fill_circle((vgfx_window_t)canvas,
                                         icon_cx,
                                         icon_cy,
                                         icon_r,
                                         (vgfx_color_t)icon->color);
                    }
                    break;
                }

                if (editor->show_fold_gutter) {
                    codeeditor_draw_fold_marker(
                        editor, canvas, fold_gutter_x, fold_gutter_width, line_y, &font_metrics, i);
                }

                line_y += editor->line_height;
            }
        } else {
            float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
            int line = 0;
            int row_in_line = 0;
            codeeditor_locate_visual_row(
                editor, content_width, first_visual_row, &line, &row_in_line);
            float row_y = widget->y - row_offset;
            while (line < editor->line_count && row_y < widget->y + widget->height) {
                int row_count = codeeditor_cached_row_count_for_line(editor, content_width, line);
                if (row_count == 0) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                    continue;
                }
                if (row_in_line == 0) {
                    if (editor->show_line_numbers) {
                        char line_num[16];
                        snprintf(line_num, sizeof(line_num), "%d", line + 1);
                        vg_text_metrics_t num_metrics = {0};
                        vg_font_measure_text(
                            editor->font, editor->font_size, line_num, &num_metrics);
                        float num_x =
                            widget->x + line_number_gutter_width - num_metrics.width - 8.0f;
                        float num_y = row_y + font_metrics.ascent;
                        vg_font_draw_text(canvas,
                                          editor->font,
                                          editor->font_size,
                                          num_x,
                                          num_y,
                                          line_num,
                                          editor->line_number_color);
                    }

                    for (int g = 0; g < editor->gutter_icon_count; g++) {
                        struct vg_gutter_icon *icon = &editor->gutter_icons[g];
                        if (icon->line != line)
                            continue;
                        int32_t icon_box = (int32_t)editor->line_height - 4;
                        if (icon_box < 8)
                            icon_box = 8;
                        int32_t icon_x = (int32_t)widget->x + 2;
                        int32_t icon_y =
                            (int32_t)row_y + ((int32_t)editor->line_height - icon_box) / 2;
                        if (icon->image.type == VG_ICON_IMAGE && icon->image.data.image.pixels) {
                            codeeditor_draw_gutter_icon_image(
                                (vgfx_window_t)canvas, icon, icon_x, icon_y, icon_box, icon_box);
                        } else {
                            int32_t icon_r = icon_box / 2;
                            int32_t icon_cx = icon_x + icon_r;
                            int32_t icon_cy = icon_y + icon_r;
                            vgfx_fill_circle((vgfx_window_t)canvas,
                                             icon_cx,
                                             icon_cy,
                                             icon_r,
                                             (vgfx_color_t)icon->color);
                        }
                        break;
                    }

                    if (editor->show_fold_gutter) {
                        codeeditor_draw_fold_marker(editor,
                                                    canvas,
                                                    fold_gutter_x,
                                                    fold_gutter_width,
                                                    row_y,
                                                    &font_metrics,
                                                    line);
                    }
                }

                row_in_line++;
                if (row_in_line >= row_count) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                }
                row_y += editor->line_height;
            }
        }
    }

    if (total_content_height > visible_height) {
        // Scrollbar track background
        float track_x_local = 0.0f;
        float thumb_y_local = 0.0f;
        float thumb_height = 0.0f;
        if (!codeeditor_get_scrollbar_metrics(
                editor, widget, &track_x_local, &thumb_y_local, &thumb_height, NULL, NULL))
            return;
        float track_x = widget->x + track_x_local;
        uint32_t track_color = 0xFF3C3C3C; // Dark gray
        vgfx_fill_rect((vgfx_window_t)canvas,
                       (int32_t)track_x,
                       (int32_t)widget->y,
                       (int32_t)scrollbar_width,
                       (int32_t)visible_height,
                       track_color);
        float thumb_y = widget->y + thumb_y_local;

        // Scrollbar thumb
        uint32_t thumb_color = 0xFF6C6C6C; // Medium gray
        vgfx_fill_rect((vgfx_window_t)canvas,
                       (int32_t)(track_x + 2),
                       (int32_t)thumb_y,
                       (int32_t)(scrollbar_width - 4),
                       (int32_t)thumb_height,
                       thumb_color);
    }
}
