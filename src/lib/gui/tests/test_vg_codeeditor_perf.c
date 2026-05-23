//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: Regression tests for CodeEditor performance counters and large-file
//          no-wrap fast paths.
//
//===----------------------------------------------------------------------===//

#include "vg_ide_widgets.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void) {
    test_nowrap_large_file_navigation_avoids_linear_scans();
    test_full_text_copy_counter();
    printf("test_vg_codeeditor_perf: PASSED\n");
    return 0;
}
