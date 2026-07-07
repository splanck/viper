//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_codeeditor_journal.c
// Purpose: Verify the incremental-sync edit-delta journal (plan 08): captured
//          forward deltas replayed onto the pre-edit text reproduce the editor's
//          current text byte-for-byte, and cold mutations report "overflow".
// Key invariants:
//   - Replaying journal deltas in order over the baseline == vg_codeeditor_get_text.
//   - Hot-path single-cursor edits journal; undo/redo/SetText/multi-cursor overflow.
// Links: src/lib/gui/src/widgets/vg_codeeditor_core.inc (journal helpers)
//
//===----------------------------------------------------------------------===//
#include "vg_ide_widgets.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Byte offset of 0-based (line, col) in a '\n'-joined buffer, matching
///        codeeditor_materialize_lines (no trailing newline).
static size_t off_of(const char *s, int line, int col) {
    size_t o = 0;
    int l = 0;
    while (l < line && s[o]) {
        if (s[o] == '\n')
            l++;
        o++;
    }
    for (int c = 0; c < col && s[o] && s[o] != '\n'; c++)
        o++;
    return o;
}

/// @brief Return a newly allocated string with [start_off, end_off) replaced by ins.
static char *splice(const char *s, size_t start_off, size_t end_off, const char *ins) {
    size_t slen = strlen(s);
    if (end_off > slen)
        end_off = slen;
    if (start_off > end_off)
        start_off = end_off;
    size_t ilen = strlen(ins);
    size_t out_len = start_off + ilen + (slen - end_off);
    char *out = malloc(out_len + 1);
    assert(out);
    memcpy(out, s, start_off);
    memcpy(out + start_off, ins, ilen);
    memcpy(out + start_off + ilen, s + end_off, slen - end_off);
    out[out_len] = '\0';
    return out;
}

/// @brief Replay the editor's retained journal deltas over @p baseline.
static char *replay_journal(vg_codeeditor_t *e, const char *baseline) {
    char *work = strdup(baseline);
    for (int i = 0; i < e->journal_count; i++) {
        int idx = (e->journal_head + i) % VG_EDIT_JOURNAL_CAP;
        const vg_edit_delta_t *d = &e->journal[idx];
        size_t so = off_of(work, d->start_line, d->start_col);
        size_t eo = off_of(work, d->end_line, d->end_col);
        char *next = splice(work, so, eo, d->text ? d->text : "");
        free(work);
        work = next;
    }
    return work;
}

/// @brief Set a baseline and clear the SetText overflow so the journal is empty.
static void reset_baseline(vg_codeeditor_t *e, const char *text) {
    vg_codeeditor_set_text(e, text);
    char *ov = vg_codeeditor_take_deltas_json(e, 0);
    assert(ov && strcmp(ov, "overflow") == 0);
    free(ov);
    assert(e->journal_count == 0);
    assert(!e->journal_overflowed);
}

/// @brief A varied insert/delete/replace/multiline sequence replays byte-exactly.
static void test_replay_matches(void) {
    vg_codeeditor_t *e = vg_codeeditor_create(NULL);
    reset_baseline(e, "abc\ndef\nghi\n");
    char *baseline = vg_codeeditor_get_text(e);

    vg_codeeditor_set_cursor(e, 0, 1);
    vg_codeeditor_insert_text(e, "XY"); // insert mid-line

    vg_codeeditor_set_cursor(e, 1, 3);
    vg_codeeditor_insert_text(e, "\nNEW"); // multi-line insert

    vg_codeeditor_set_selection(e, 0, 0, 0, 2);
    vg_codeeditor_delete_selection(e); // delete a range

    vg_codeeditor_set_selection(e, 2, 0, 2, 1);
    vg_codeeditor_insert_text(e, "QQ"); // replace (selection + insert)

    char *replayed = replay_journal(e, baseline);
    char *actual = vg_codeeditor_get_text(e);
    if (strcmp(replayed, actual) != 0) {
        fprintf(stderr, "replay mismatch:\n  replayed=[%s]\n  actual  =[%s]\n", replayed, actual);
        assert(0);
    }

    free(baseline);
    free(replayed);
    free(actual);
    vg_widget_destroy(&e->base);
    printf("  test_replay_matches: ok\n");
}

/// @brief A long single-character typing burst replays byte-exactly.
static void test_typing_burst_replays(void) {
    vg_codeeditor_t *e = vg_codeeditor_create(NULL);
    reset_baseline(e, "start\n");
    char *baseline = vg_codeeditor_get_text(e);

    vg_codeeditor_set_cursor(e, 0, 5);
    const char *word = "helloworld";
    for (const char *p = word; *p; p++) {
        char ch[2] = {*p, 0};
        vg_codeeditor_insert_text(e, ch);
    }

    char *replayed = replay_journal(e, baseline);
    char *actual = vg_codeeditor_get_text(e);
    assert(strcmp(replayed, actual) == 0);

    free(baseline);
    free(replayed);
    free(actual);
    vg_widget_destroy(&e->base);
    printf("  test_typing_burst_replays: ok\n");
}

/// @brief take_deltas_json returns a JSON array for hot edits; undo overflows.
static void test_json_and_overflow(void) {
    vg_codeeditor_t *e = vg_codeeditor_create(NULL);
    reset_baseline(e, "line\n");
    // The caller pulls deltas relative to the revision it last synced, not 0.
    uint64_t base_rev = vg_codeeditor_get_revision(e);

    vg_codeeditor_set_cursor(e, 0, 4);
    vg_codeeditor_insert_text(e, "!"); // one hot delta

    char *json = vg_codeeditor_take_deltas_json(e, base_rev);
    assert(json);
    assert(json[0] == '[');                      // JSON array, not "overflow"
    assert(strstr(json, "\"t\":\"!\"") != NULL); // carries the inserted text
    free(json);
    assert(e->journal_count == 0); // pruned after take

    // A stale since_revision (behind the oldest retained delta) must overflow.
    vg_codeeditor_insert_text(e, "?");
    char *stale = vg_codeeditor_take_deltas_json(e, 0);
    assert(stale && strcmp(stale, "overflow") == 0);
    free(stale);

    // A cold mutation (undo) must overflow regardless of since_revision.
    uint64_t rev2 = vg_codeeditor_get_revision(e);
    vg_codeeditor_insert_text(e, "#");
    vg_codeeditor_undo(e);
    char *ov = vg_codeeditor_take_deltas_json(e, rev2);
    assert(ov && strcmp(ov, "overflow") == 0);
    free(ov);

    vg_widget_destroy(&e->base);
    printf("  test_json_and_overflow: ok\n");
}

int main(void) {
    printf("test_vg_codeeditor_journal:\n");
    test_replay_matches();
    test_typing_burst_replays();
    test_json_and_overflow();
    printf("all journal tests passed\n");
    return 0;
}
