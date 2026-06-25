//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: Regression tests for CodeEditor performance counters and large-file
//          no-wrap fast paths.
//
//===----------------------------------------------------------------------===//

#include "vg_event.h"
#include "vg_ide_widgets.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define PERF_TYPING_5K_FRAME_BUDGET_MS 16.0
#define PERF_TYPING_20K_FRAME_BUDGET_MS 33.0
#define PERF_PAINT_50K_BUDGET_MS 16.0
#define PERF_SCROLL_50K_BUDGET_MS 16.0
#define PERF_SELECTION_DRAG_50K_BUDGET_MS 16.0
#define PERF_MINIMAP_50K_BUDGET_MS 16.0

static double now_ms(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return ((double)value.QuadPart * 1000.0) / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static void assert_under_ms(const char *name, double elapsed_ms, double budget_ms) {
    if (elapsed_ms > budget_ms) {
        fprintf(stderr, "%s took %.3f ms, budget %.3f ms\n", name, elapsed_ms, budget_ms);
        assert(elapsed_ms <= budget_ms);
    }
}

static void put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put_i16(uint8_t *p, int16_t value) {
    put_u16(p, (uint16_t)value);
}

static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put_table(
    uint8_t *dir, int index, const char tag[4], uint32_t offset, uint32_t length) {
    uint8_t *entry = dir + 12 + index * 16;
    memcpy(entry, tag, 4);
    put_u32(entry + 4, 0);
    put_u32(entry + 8, offset);
    put_u32(entry + 12, length);
}

static void append_table(uint8_t *font,
                         uint8_t *dir,
                         int *index,
                         uint32_t *offset,
                         const char tag[4],
                         const uint8_t *data,
                         uint32_t length) {
    memcpy(font + *offset, data, length);
    put_table(dir, (*index)++, tag, *offset, length);
    *offset += length;
}

static size_t build_minimal_test_font(uint8_t *font) {
    memset(font, 0, 512);
    put_u32(font + 0, 0x00010000);
    put_u16(font + 4, 7);

    int index = 0;
    uint32_t offset = 12 + 7 * 16;

    uint8_t head[54] = {0};
    put_u16(head + 18, 1000);
    put_i16(head + 36, 0);
    put_i16(head + 38, 0);
    put_i16(head + 40, 1000);
    put_i16(head + 42, 1000);
    put_i16(head + 50, 1);
    append_table(font, font, &index, &offset, "head", head, sizeof(head));

    uint8_t hhea[36] = {0};
    put_i16(hhea + 4, 800);
    put_i16(hhea + 6, -200);
    put_i16(hhea + 8, 200);
    put_u16(hhea + 34, 2);
    append_table(font, font, &index, &offset, "hhea", hhea, sizeof(hhea));

    uint8_t maxp[6] = {0};
    put_u16(maxp + 4, 2);
    append_table(font, font, &index, &offset, "maxp", maxp, sizeof(maxp));

    uint8_t cmap[64] = {0};
    put_u16(cmap + 0, 0);
    put_u16(cmap + 2, 1);
    put_u16(cmap + 4, 3);
    put_u16(cmap + 6, 1);
    put_u32(cmap + 8, 12);
    uint8_t *sub = cmap + 12;
    put_u16(sub + 0, 4);
    put_u16(sub + 2, 32);
    put_u16(sub + 4, 0);
    put_u16(sub + 6, 4);
    put_u16(sub + 8, 4);
    put_u16(sub + 10, 1);
    put_u16(sub + 12, 0);
    put_u16(sub + 14, 0x0041);
    put_u16(sub + 16, 0xFFFF);
    put_u16(sub + 18, 0);
    put_u16(sub + 20, 0x0041);
    put_u16(sub + 22, 0xFFFF);
    put_i16(sub + 24, -64);
    put_i16(sub + 26, 1);
    put_u16(sub + 28, 0);
    put_u16(sub + 30, 0);
    append_table(font, font, &index, &offset, "cmap", cmap, 44);

    uint8_t glyf[10] = {0};
    append_table(font, font, &index, &offset, "glyf", glyf, sizeof(glyf));

    uint8_t loca[12] = {0};
    put_u32(loca + 0, 0);
    put_u32(loca + 4, 0);
    put_u32(loca + 8, 0);
    append_table(font, font, &index, &offset, "loca", loca, sizeof(loca));

    uint8_t hmtx[8] = {0};
    put_u16(hmtx + 0, 1000);
    put_i16(hmtx + 2, 0);
    put_u16(hmtx + 4, 1000);
    put_i16(hmtx + 6, 0);
    append_table(font, font, &index, &offset, "hmtx", hmtx, sizeof(hmtx));

    return offset;
}

static char *make_lines(int count) {
    const char *line = "let value = 42;";
    size_t line_len = strlen(line);
    size_t total = (line_len + 1) * (size_t)count;
    char *text = (char *)malloc(total + 1);
    assert(text != NULL);

    char *p = text;
    for (int i = 0; i < count; i++) {
        memcpy(p, line, line_len);
        p += line_len;
        if (i + 1 < count)
            *p++ = '\n';
    }
    *p = '\0';
    return text;
}

static void send_key_down(vg_codeeditor_t *editor, vg_key_t key) {
    vg_event_t event = vg_event_key(VG_EVENT_KEY_DOWN, key, 0, VG_MOD_NONE);
    bool handled = editor->base.vtable->handle_event(&editor->base, &event);
    assert(handled);
}

static void send_key_char(vg_codeeditor_t *editor, char ch) {
    vg_event_t event = vg_event_key(VG_EVENT_KEY_CHAR, VG_KEY_UNKNOWN, (uint32_t)ch, VG_MOD_NONE);
    bool handled = editor->base.vtable->handle_event(&editor->base, &event);
    assert(handled);
}

static void send_mouse(vg_codeeditor_t *editor, vg_event_type_t type, float x, float y) {
    vg_event_t event = vg_event_mouse(type, x, y, VG_MOUSE_LEFT, VG_MOD_NONE);
    bool handled = editor->base.vtable->handle_event(&editor->base, &event);
    assert(handled);
}

static void send_wheel(vg_codeeditor_t *editor, float delta_y) {
    vg_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = VG_EVENT_MOUSE_WHEEL;
    event.wheel.delta_y = delta_y;
    bool handled = editor->base.vtable->handle_event(&editor->base, &event);
    assert(handled);
}

static void test_nowrap_large_file_navigation_avoids_linear_scans(void) {
    enum { line_count = 50000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = false;
    vg_codeeditor_set_text(editor, text);
    assert(vg_codeeditor_get_line_count(editor) == line_count);

    vg_codeeditor_reset_perf_stats(editor);
    vg_codeeditor_set_cursor(editor, line_count - 10, 4);
    vg_codeeditor_scroll_to_line(editor, line_count - 5);
    assert(vg_codeeditor_get_scroll_top_line(editor) >= line_count - 50);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.total_height_linear_scans == 0);
    assert(stats.total_visual_row_linear_scans == 0);
    assert(stats.visual_row_linear_scans == 0);
    assert(stats.locate_visual_row_linear_scans == 0);

    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_full_text_copy_counter(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    vg_codeeditor_set_text(editor, "one\ntwo\nthree");

    vg_codeeditor_reset_perf_stats(editor);
    char *text = vg_codeeditor_get_text(editor);
    assert(text != NULL);
    assert(strcmp(text, "one\ntwo\nthree") == 0);
    free(text);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.full_text_copies == 1);
    assert(stats.full_text_copy_bytes == strlen("one\ntwo\nthree"));

    vg_widget_destroy(&editor->base);
}

static void test_set_text_clears_stale_fold_regions(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    vg_codeeditor_set_text(editor, "one\ntwo\nthree");

    editor->fold_regions = (struct vg_fold_region *)calloc(1, sizeof(*editor->fold_regions));
    assert(editor->fold_regions != NULL);
    editor->fold_region_cap = 1;
    editor->fold_region_count = 1;
    editor->fold_regions[0].start_line = 0;
    editor->fold_regions[0].end_line = 2;
    editor->fold_regions[0].folded = true;
    vg_codeeditor_refresh_layout_state(editor);
    assert(editor->has_folded_lines);

    vg_codeeditor_set_text(editor, "replacement");
    assert(editor->fold_regions == NULL);
    assert(editor->fold_region_count == 0);
    assert(editor->fold_region_cap == 0);
    assert(!editor->has_folded_lines);

    vg_widget_destroy(&editor->base);
}

static void test_folded_large_file_navigation_uses_layout_cache(void) {
    enum { line_count = 50000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = false;
    vg_codeeditor_set_text(editor, text);

    editor->fold_regions = (struct vg_fold_region *)calloc(1, sizeof(*editor->fold_regions));
    assert(editor->fold_regions != NULL);
    editor->fold_region_cap = 1;
    editor->fold_region_count = 1;
    editor->fold_regions[0].start_line = 10;
    editor->fold_regions[0].end_line = line_count - 100;
    editor->fold_regions[0].folded = true;
    vg_codeeditor_refresh_layout_state(editor);
    assert(editor->layout_cache_valid);

    vg_codeeditor_reset_perf_stats(editor);
    vg_codeeditor_scroll_to_line(editor, line_count - 20);
    assert(vg_codeeditor_get_scroll_top_line(editor) >= line_count - 200);
    vg_codeeditor_set_cursor(editor, line_count - 10, 4);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.total_height_linear_scans == 0);
    assert(stats.total_visual_row_linear_scans == 0);
    assert(stats.visual_row_linear_scans == 0);
    assert(stats.locate_visual_row_linear_scans == 0);

    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_wrapped_large_file_navigation_uses_layout_cache(void) {
    enum { line_count = 20000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    editor->base.width = 320.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = true;
    vg_codeeditor_set_text(editor, text);
    vg_codeeditor_refresh_layout_state(editor);
    assert(editor->layout_cache_valid);

    vg_codeeditor_reset_perf_stats(editor);
    vg_codeeditor_set_cursor(editor, line_count - 5, 4);
    vg_codeeditor_scroll_to_line(editor, line_count - 4);
    assert(vg_codeeditor_get_scroll_top_line(editor) >= line_count - 100);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.total_height_linear_scans == 0);
    assert(stats.total_visual_row_linear_scans == 0);
    assert(stats.visual_row_linear_scans == 0);
    assert(stats.locate_visual_row_linear_scans == 0);

    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_highlight_span_paint_uses_line_index(void) {
    enum { line_count = 50000, span_count = 10000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    uint8_t font_blob[512];
    size_t font_size = build_minimal_test_font(font_blob);
    vg_font_t *font = vg_font_load(font_blob, font_size);
    assert(font != NULL);

    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->show_line_numbers = false;
    editor->gutter_width = 0.0f;
    editor->word_wrap = false;
    vg_codeeditor_set_font(editor, font, 16.0f);
    vg_codeeditor_set_text(editor, text);

    editor->highlight_spans =
        (struct vg_highlight_span *)calloc(span_count, sizeof(*editor->highlight_spans));
    assert(editor->highlight_spans != NULL);
    editor->highlight_span_cap = span_count;
    editor->highlight_span_count = span_count;
    for (int i = 0; i < span_count; i++) {
        editor->highlight_spans[i].start_line = i;
        editor->highlight_spans[i].start_col = 0;
        editor->highlight_spans[i].end_line = i;
        editor->highlight_spans[i].end_col = 3;
        editor->highlight_spans[i].color = 0x00FF00u;
    }
    editor->highlight_spans_sorted = false;

    vg_codeeditor_scroll_to_line(editor, line_count - 100);
    vg_codeeditor_reset_perf_stats(editor);
    editor->base.vtable->paint(&editor->base, NULL);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.highlight_span_checks == 0);
    assert(editor->highlight_line_index_valid);

    vg_font_destroy(font);
    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_typing_burst_does_not_copy_full_buffer(void) {
    enum { line_count = 20000, burst_count = 200 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = false;
    vg_codeeditor_set_text(editor, text);
    vg_codeeditor_set_cursor(editor, line_count / 2, 4);

    vg_codeeditor_reset_perf_stats(editor);
    for (int i = 0; i < burst_count; i++) {
        send_key_char(editor, 'x');
    }

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.full_text_copies == 0);
    assert(stats.full_text_copy_bytes == 0);
    assert(stats.total_height_linear_scans == 0);
    assert(stats.total_visual_row_linear_scans == 0);
    assert(stats.visual_row_linear_scans == 0);
    assert(stats.locate_visual_row_linear_scans == 0);

    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_large_selection_does_not_copy_full_buffer(void) {
    enum { line_count = 20000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = false;
    vg_codeeditor_set_text(editor, text);

    vg_codeeditor_reset_perf_stats(editor);
    vg_codeeditor_set_selection(editor, 10, 0, line_count - 10, 3);
    vg_codeeditor_set_cursor(editor, line_count - 10, 3);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.full_text_copies == 0);
    assert(stats.full_text_copy_bytes == 0);
    assert(stats.total_height_linear_scans == 0);
    assert(stats.total_visual_row_linear_scans == 0);
    assert(stats.visual_row_linear_scans == 0);
    assert(stats.locate_visual_row_linear_scans == 0);

    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_typing_and_paint_wall_clock_budgets(void) {
    const int line_counts[] = {5000, 20000};
    const double budgets[] = {PERF_TYPING_5K_FRAME_BUDGET_MS, PERF_TYPING_20K_FRAME_BUDGET_MS};

    uint8_t font_blob[512];
    size_t font_size = build_minimal_test_font(font_blob);
    vg_font_t *font = vg_font_load(font_blob, font_size);
    assert(font != NULL);

    for (int case_idx = 0; case_idx < 2; case_idx++) {
        int line_count = line_counts[case_idx];
        char *text = make_lines(line_count);
        vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
        assert(editor != NULL);

        editor->base.width = 900.0f;
        editor->base.height = 600.0f;
        editor->word_wrap = false;
        editor->show_line_numbers = false;
        editor->gutter_width = 0.0f;
        vg_codeeditor_set_font(editor, font, 16.0f);
        vg_codeeditor_set_text(editor, text);
        vg_codeeditor_set_cursor(editor, line_count / 2, 4);
        editor->base.vtable->paint(&editor->base, NULL);

        vg_codeeditor_reset_perf_stats(editor);
        double max_ms = 0.0;
        for (int i = 0; i < 80; i++) {
            double start = now_ms();
            send_key_char(editor, 'x');
            editor->base.vtable->paint(&editor->base, NULL);
            double elapsed = now_ms() - start;
            if (elapsed > max_ms)
                max_ms = elapsed;
        }

        assert_under_ms(line_count == 5000 ? "5k typing+paint frame" : "20k typing+paint frame",
                        max_ms,
                        budgets[case_idx]);

        vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
        assert(stats.full_text_copies == 0);
        assert(stats.full_text_copy_bytes == 0);
        assert(stats.total_height_linear_scans == 0);
        assert(stats.total_visual_row_linear_scans == 0);
        assert(stats.visual_row_linear_scans == 0);
        assert(stats.locate_visual_row_linear_scans == 0);

        vg_widget_destroy(&editor->base);
        free(text);
    }

    vg_font_destroy(font);
}

static void test_paint_and_scroll_wall_clock_budgets(void) {
    enum { line_count = 50000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    uint8_t font_blob[512];
    size_t font_size = build_minimal_test_font(font_blob);
    vg_font_t *font = vg_font_load(font_blob, font_size);
    assert(font != NULL);

    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = false;
    editor->show_line_numbers = false;
    editor->gutter_width = 0.0f;
    vg_codeeditor_set_font(editor, font, 16.0f);
    vg_codeeditor_set_text(editor, text);
    editor->base.vtable->paint(&editor->base, NULL);

    vg_codeeditor_reset_perf_stats(editor);
    double max_paint_ms = 0.0;
    for (int i = 0; i < 40; i++) {
        int line = (i * 1237) % line_count;
        double start = now_ms();
        vg_codeeditor_scroll_to_line(editor, line);
        editor->base.vtable->paint(&editor->base, NULL);
        double elapsed = now_ms() - start;
        if (elapsed > max_paint_ms)
            max_paint_ms = elapsed;
    }
    assert_under_ms("50k scroll+paint frame", max_paint_ms, PERF_PAINT_50K_BUDGET_MS);

    double max_wheel_ms = 0.0;
    for (int i = 0; i < 80; i++) {
        double start = now_ms();
        send_wheel(editor, -1.0f);
        editor->base.vtable->paint(&editor->base, NULL);
        double elapsed = now_ms() - start;
        if (elapsed > max_wheel_ms)
            max_wheel_ms = elapsed;
    }
    assert_under_ms("50k wheel-scroll frame", max_wheel_ms, PERF_SCROLL_50K_BUDGET_MS);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.full_text_copies == 0);
    assert(stats.full_text_copy_bytes == 0);
    assert(stats.total_height_linear_scans == 0);
    assert(stats.total_visual_row_linear_scans == 0);
    assert(stats.visual_row_linear_scans == 0);
    assert(stats.locate_visual_row_linear_scans == 0);

    vg_font_destroy(font);
    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_pointer_selection_drag_wall_clock_budget(void) {
    enum { line_count = 50000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    uint8_t font_blob[512];
    size_t font_size = build_minimal_test_font(font_blob);
    vg_font_t *font = vg_font_load(font_blob, font_size);
    assert(font != NULL);

    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = false;
    editor->show_line_numbers = false;
    editor->gutter_width = 0.0f;
    vg_codeeditor_set_font(editor, font, 16.0f);
    vg_codeeditor_set_text(editor, text);
    editor->base.vtable->paint(&editor->base, NULL);

    vg_codeeditor_reset_perf_stats(editor);
    send_mouse(editor, VG_EVENT_MOUSE_DOWN, 20.0f, 12.0f);
    assert(editor->selection_dragging);

    double max_ms = 0.0;
    for (int i = 0; i < 120; i++) {
        float y = 12.0f + (float)((i % 28) + 1) * editor->line_height;
        double start = now_ms();
        send_mouse(editor, VG_EVENT_MOUSE_MOVE, 220.0f, y);
        editor->base.vtable->paint(&editor->base, NULL);
        double elapsed = now_ms() - start;
        if (elapsed > max_ms)
            max_ms = elapsed;
    }
    send_mouse(editor, VG_EVENT_MOUSE_UP, 220.0f, 12.0f + 20.0f * editor->line_height);

    assert_under_ms("50k pointer selection-drag frame", max_ms, PERF_SELECTION_DRAG_50K_BUDGET_MS);
    assert(!editor->selection_dragging);
    assert(editor->has_selection);
    assert(editor->cursor_line > 0);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.full_text_copies == 0);
    assert(stats.full_text_copy_bytes == 0);
    assert(stats.total_height_linear_scans == 0);
    assert(stats.total_visual_row_linear_scans == 0);
    assert(stats.visual_row_linear_scans == 0);
    assert(stats.locate_visual_row_linear_scans == 0);

    char *selection = vg_codeeditor_get_selection(editor);
    assert(selection != NULL);
    assert(strlen(selection) > 0);
    free(selection);

    vg_font_destroy(font);
    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_minimap_wall_clock_budget(void) {
    enum { line_count = 50000 };

    char *text = make_lines(line_count);
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->word_wrap = false;
    vg_codeeditor_set_text(editor, text);
    vg_codeeditor_scroll_to_line(editor, line_count / 2);

    vg_minimap_t *minimap = vg_minimap_create(editor);
    assert(minimap != NULL);
    minimap->base.width = 120.0f;
    minimap->base.height = 600.0f;

    double start = now_ms();
    minimap->base.vtable->paint(&minimap->base, NULL);
    double elapsed = now_ms() - start;
    assert_under_ms("50k minimap paint", elapsed, PERF_MINIMAP_50K_BUDGET_MS);

    vg_codeeditor_perf_stats_t stats = vg_codeeditor_get_perf_stats(editor);
    assert(stats.full_text_copies == 0);
    assert(stats.full_text_copy_bytes == 0);

    vg_minimap_destroy(minimap);
    vg_widget_destroy(&editor->base);
    free(text);
}

static void test_auto_indent_newline_is_single_undo_step(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    vg_codeeditor_set_text(editor, "func main() {");
    vg_codeeditor_set_cursor(editor, 0, (int)strlen("func main() {"));

    send_key_down(editor, VG_KEY_ENTER);
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "func main() {\n    ") == 0);
    free(text);
    assert(editor->cursor_line == 1);
    assert(editor->cursor_col == 4);

    vg_codeeditor_undo(editor);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "func main() {") == 0);
    free(text);

    vg_widget_destroy(&editor->base);
}

/// @brief Enter between an auto-inserted brace pair splits the pair vertically.
/// @details This covers the IDE-visible case where typing `{` creates `{}` and
///          pressing Enter should leave the cursor on an indented blank line with
///          the closing brace below it.
static void test_enter_between_braces_splits_pair(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    vg_codeeditor_set_text(editor, "");

    send_key_char(editor, '{');
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "{}") == 0);
    free(text);
    assert(editor->cursor_line == 0);
    assert(editor->cursor_col == 1);

    send_key_down(editor, VG_KEY_ENTER);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "{\n    \n}") == 0);
    free(text);
    assert(editor->cursor_line == 1);
    assert(editor->cursor_col == 4);

    vg_codeeditor_undo(editor);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "{}") == 0);
    free(text);
    assert(editor->cursor_line == 0);
    assert(editor->cursor_col == 1);

    vg_widget_destroy(&editor->base);
}

static void test_pair_autoclose_skip_and_undo(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    uint8_t font_blob[512];
    size_t font_size = build_minimal_test_font(font_blob);
    vg_font_t *font = vg_font_load(font_blob, font_size);
    assert(font != NULL);
    editor->base.width = 900.0f;
    editor->base.height = 600.0f;
    editor->base.state |= VG_STATE_FOCUSED;
    vg_codeeditor_set_font(editor, font, 16.0f);
    vg_codeeditor_set_text(editor, "");

    send_key_char(editor, '(');
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "()") == 0);
    free(text);
    assert(editor->cursor_line == 0);
    assert(editor->cursor_col == 1);
    editor->base.vtable->paint(&editor->base, NULL);
    assert(editor->pair_match_active);
    assert(editor->pair_anchor_line == 0);
    assert(editor->pair_anchor_col == 1);
    assert(editor->pair_peer_line == 0);
    assert(editor->pair_peer_col == 0);

    uint64_t revision_before_skip = editor->revision;
    send_key_char(editor, ')');
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "()") == 0);
    free(text);
    assert(editor->cursor_col == 2);
    assert(editor->revision == revision_before_skip);
    editor->base.vtable->paint(&editor->base, NULL);
    assert(editor->pair_match_active);
    assert(editor->pair_anchor_col == 1);
    assert(editor->pair_peer_col == 0);

    vg_codeeditor_undo(editor);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "") == 0);
    free(text);
    assert(editor->cursor_line == 0);
    assert(editor->cursor_col == 0);
    editor->base.vtable->paint(&editor->base, NULL);
    assert(!editor->pair_match_active);

    vg_codeeditor_set_text(editor, "{\n    call()\n}");
    vg_codeeditor_set_cursor(editor, 2, 0);
    editor->base.vtable->paint(&editor->base, NULL);
    assert(editor->pair_match_active);
    assert(editor->pair_anchor_line == 2);
    assert(editor->pair_anchor_col == 0);
    assert(editor->pair_peer_line == 0);
    assert(editor->pair_peer_col == 0);

    vg_font_destroy(font);
    vg_widget_destroy(&editor->base);
}

/// @brief Quote auto-pairing places and skips the cursor like bracket pairs.
static void test_quote_autoclose_places_cursor_between_quotes(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    vg_codeeditor_set_text(editor, "");

    send_key_char(editor, '"');
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "\"\"") == 0);
    free(text);
    assert(editor->cursor_line == 0);
    assert(editor->cursor_col == 1);

    uint64_t revision_before_skip = editor->revision;
    send_key_char(editor, '"');
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "\"\"") == 0);
    free(text);
    assert(editor->cursor_col == 2);
    assert(editor->revision == revision_before_skip);

    vg_codeeditor_undo(editor);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "") == 0);
    free(text);

    vg_widget_destroy(&editor->base);
}

/// @brief Backspace between an empty pair removes both generated characters.
static void test_backspace_between_pair_deletes_both_sides(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    vg_codeeditor_set_text(editor, "");

    send_key_char(editor, '(');
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "()") == 0);
    free(text);
    assert(editor->cursor_col == 1);

    send_key_down(editor, VG_KEY_BACKSPACE);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "") == 0);
    free(text);
    assert(editor->cursor_line == 0);
    assert(editor->cursor_col == 0);

    vg_widget_destroy(&editor->base);
}

/// @brief Auto-pair insertion wraps the current selection.
static void test_pair_insertion_wraps_selection(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    vg_codeeditor_set_text(editor, "value");
    vg_codeeditor_set_selection(editor, 0, 0, 0, 5);

    send_key_char(editor, '"');
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "\"value\"") == 0);
    free(text);

    vg_widget_destroy(&editor->base);
}

/// @brief Delimiter handling ignores comments and strings.
static void test_pair_matching_and_insertion_ignore_non_code_text(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);
    editor->base.width = 900.0f;
    editor->base.height = 240.0f;
    editor->base.state |= VG_STATE_FOCUSED;

    vg_codeeditor_set_text(editor, "// ");
    vg_codeeditor_set_cursor(editor, 0, 3);
    send_key_char(editor, '{');
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "// {") == 0);
    free(text);

    vg_codeeditor_set_text(editor, "// {\n}");
    vg_codeeditor_set_cursor(editor, 1, 0);
    editor->base.vtable->paint(&editor->base, NULL);
    assert(!editor->pair_match_active);

    vg_widget_destroy(&editor->base);
}

static void test_closing_brace_formats_indent_on_type(void) {
    vg_codeeditor_t *editor = vg_codeeditor_create(NULL);
    assert(editor != NULL);

    vg_codeeditor_set_text(editor, "{\n        ");
    vg_codeeditor_set_cursor(editor, 1, 8);
    send_key_char(editor, '}');
    char *text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "{\n}") == 0);
    free(text);
    assert(editor->cursor_line == 1);
    assert(editor->cursor_col == 1);

    vg_codeeditor_undo(editor);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "{\n        ") == 0);
    free(text);
    assert(editor->cursor_line == 1);
    assert(editor->cursor_col == 8);

    vg_codeeditor_set_text(editor, "{\n    }");
    vg_codeeditor_set_cursor(editor, 1, 4);
    send_key_char(editor, '}');
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "{\n}") == 0);
    free(text);
    assert(editor->cursor_line == 1);
    assert(editor->cursor_col == 1);

    vg_codeeditor_undo(editor);
    text = vg_codeeditor_get_text(editor);
    assert(strcmp(text, "{\n    }") == 0);
    free(text);
    assert(editor->cursor_line == 1);
    assert(editor->cursor_col == 4);

    vg_widget_destroy(&editor->base);
}

int main(void) {
    test_nowrap_large_file_navigation_avoids_linear_scans();
    test_full_text_copy_counter();
    test_set_text_clears_stale_fold_regions();
    test_folded_large_file_navigation_uses_layout_cache();
    test_wrapped_large_file_navigation_uses_layout_cache();
    test_highlight_span_paint_uses_line_index();
    test_typing_burst_does_not_copy_full_buffer();
    test_large_selection_does_not_copy_full_buffer();
    test_typing_and_paint_wall_clock_budgets();
    test_paint_and_scroll_wall_clock_budgets();
    test_pointer_selection_drag_wall_clock_budget();
    test_minimap_wall_clock_budget();
    test_auto_indent_newline_is_single_undo_step();
    test_enter_between_braces_splits_pair();
    test_pair_autoclose_skip_and_undo();
    test_quote_autoclose_places_cursor_between_quotes();
    test_backspace_between_pair_deletes_both_sides();
    test_pair_insertion_wraps_selection();
    test_pair_matching_and_insertion_ignore_non_code_text();
    test_closing_brace_formats_indent_on_type();
    printf("test_vg_codeeditor_perf: PASSED\n");
    return 0;
}
