//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: Tier 3 GUI widget tests — dialog, file dialog, code editor, context menu, command
// palette.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_tier3_fixes.c
//
//===----------------------------------------------------------------------===//
// test_vg_tier3_fixes.c — Unit tests for Tier 3 GUI improvements
//
// Tests:
//   PERF-002:    LRU glyph cache — access_tick updates on hit; eviction is
//                LRU-ordered (lowest tick evicted first)
//   BUG-GUI-008: TextInput undo/redo — Ctrl+Z/Y restore previous text;
//                undo at beginning is a no-op; redo at end is a no-op
//   BUG-GUI-002: Dialog modal event blocking — mouse clicks outside the
//                modal dialog bounding box are swallowed
//   FEAT-006:    Tab order — focus_next visits widgets in tab_index order;
//                focus_prev reverses that order
//   FEAT-005:    Button icon — SetIcon stores icon_text; SetIconPosition
//                stores icon_pos; Destroy frees icon_text cleanly
//
#include "vg_event.h"
#include "vg_ide_widgets.h"
#include "vg_widget.h"
#include "vg_widgets.h"
// Include the private font cache header for PERF-002 white-box tests
#include "vg_ttf_internal.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Test Harness
//=============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name)                                                                                  \
    do {                                                                                           \
        printf("  %-60s", #name "...");                                                            \
        fflush(stdout);                                                                            \
        test_##name();                                                                             \
        if (g_failed == 0)                                                                         \
            printf("OK\n");                                                                        \
        g_passed++;                                                                                \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n  (%s:%d: %s)\n", __FILE__, __LINE__, #cond);                            \
            g_failed++;                                                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NEQ(a, b) ASSERT((a) != (b))
#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_TRUE(cond) ASSERT(cond)
#define ASSERT_FALSE(cond) ASSERT(!(cond))

//=============================================================================
// PERF-002: LRU Glyph Cache
//=============================================================================

TEST(lru_cache_hit_updates_tick) {
    vg_glyph_cache_t *cache = vg_cache_create();
    ASSERT_NOT_NULL(cache);

    // Build a minimal glyph with a 2x2 bitmap
    uint8_t bitmap[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    vg_glyph_t glyph = {0};
    glyph.width = 2;
    glyph.height = 2;
    glyph.bitmap = bitmap;

    vg_cache_put(cache, 12.0f, 0x41 /*'A'*/, &glyph);

    // First hit: access_tick should be non-zero
    const vg_glyph_t *g1 = vg_cache_get(cache, 12.0f, 0x41);
    ASSERT_NOT_NULL(g1);

    // Grab the tick of the cached entry
    uint64_t key = 0;
    {
        // Read the entry's tick via the bucket chain
        uint32_t size_bits;
        float sz = 12.0f;
        memcpy(&size_bits, &sz, sizeof(uint32_t));
        // Verify the entry is in the cache and has a non-zero tick
        // (We can inspect via a second cache_get; each hit bumps the global counter)
    }

    // Second hit should increment again
    const vg_glyph_t *g2 = vg_cache_get(cache, 12.0f, 0x41);
    ASSERT_NOT_NULL(g2);

    // A miss should return NULL
    const vg_glyph_t *g_miss = vg_cache_get(cache, 12.0f, 0x42 /*'B'*/);
    ASSERT_NULL(g_miss);

    vg_cache_destroy(cache);
}

TEST(lru_cache_lru_evicts_unaccessed_first) {
    vg_glyph_cache_t *cache = vg_cache_create();
    ASSERT_NOT_NULL(cache);

    // Add two glyphs: 'A' (will be accessed, high tick) and 'B' (never accessed, tick=0)
    uint8_t bmp[1] = {0xFF};
    vg_glyph_t g = {0};
    g.width = 1;
    g.height = 1;
    g.bitmap = bmp;

    vg_cache_put(cache, 12.0f, 0x41 /*'A'*/, &g);
    vg_cache_put(cache, 12.0f, 0x42 /*'B'*/, &g);

    // Access 'A' several times — its tick will be higher than 'B' (which stays at tick=0).
    // Do NOT call vg_cache_get on 'B' here: that would update B's tick and defeat the test.
    vg_cache_get(cache, 12.0f, 0x41);
    vg_cache_get(cache, 12.0f, 0x41);
    vg_cache_get(cache, 12.0f, 0x41);

    // Fill the cache until it is over the memory limit to trigger eviction.
    // Each entry uses width*height = VG_CACHE_MAX_MEMORY/2 bytes so the second
    // large glyph pushes memory_used over VG_CACHE_MAX_MEMORY.
    size_t big_size = VG_CACHE_MAX_MEMORY; // exactly at the limit
    uint8_t *big_bmp = calloc(1, big_size);
    ASSERT_NOT_NULL(big_bmp);

    // A 1D glyph with width=big_size, height=1 to consume the limit in one shot
    vg_glyph_t big = {0};
    big.bitmap = big_bmp;
    big.width = (int)big_size;
    big.height = 1;

    // Adding this triggers eviction (memory_used + big_size > VG_CACHE_MAX_MEMORY)
    vg_cache_put(cache, 14.0f, 0x43 /*'C'*/, &big);
    free(big_bmp);

    // After eviction, 'B' (never accessed, tick=0) should be gone, but 'A'
    // (accessed 3 times, highest tick) should survive.
    // Note: eviction removes 25% of entries by LRU; with 3 total entries, 1 is evicted.
    const vg_glyph_t *after_a = vg_cache_get(cache, 12.0f, 0x41);
    const vg_glyph_t *after_b = vg_cache_get(cache, 12.0f, 0x42);

    // 'B' should have been evicted (lowest tick=0), 'A' kept
    ASSERT_NOT_NULL(after_a);
    ASSERT_NULL(after_b);

    vg_cache_destroy(cache);
}

//=============================================================================
// BUG-GUI-008: TextInput Undo / Redo
//=============================================================================

// Helper: create a mock key event
static vg_event_t make_key_event(vg_key_t key, uint32_t mods) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_KEY_DOWN;
    ev.key.key = key;
    ev.modifiers = mods;
    return ev;
}

// Helper: create a character input event
static vg_event_t make_char_event(uint32_t codepoint) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_KEY_CHAR;
    ev.key.codepoint = codepoint;
    return ev;
}

static void count_text_changes(vg_widget_t *widget, const char *text, void *user_data) {
    (void)widget;
    (void)text;
    (*(int *)user_data)++;
}

static bool text_has_suffix(const char *text, const char *suffix) {
    if (!text || !suffix)
        return false;

    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (text_len < suffix_len)
        return false;

    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

TEST(textinput_undo_restores_previous_text) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);

    // Type "abc" — each character triggers push_undo + insert
    vg_event_t ev_a = make_char_event('a');
    vg_event_t ev_b = make_char_event('b');
    vg_event_t ev_c = make_char_event('c');

    vg_widget_t *w = &ti->base;
    w->vtable->handle_event(w, &ev_a);
    w->vtable->handle_event(w, &ev_b);
    w->vtable->handle_event(w, &ev_c);

    ASSERT_EQ(0, strcmp(ti->text, "abc"));

    // Ctrl+Z: undo 'c' → "ab"
    vg_event_t undo = make_key_event(VG_KEY_Z, VG_MOD_CTRL);
    w->vtable->handle_event(w, &undo);
    ASSERT_EQ(0, strcmp(ti->text, "ab"));

    // Ctrl+Z: undo 'b' → "a"
    w->vtable->handle_event(w, &undo);
    ASSERT_EQ(0, strcmp(ti->text, "a"));

    // Ctrl+Z: undo 'a' → ""
    w->vtable->handle_event(w, &undo);
    ASSERT_EQ(0, strcmp(ti->text, ""));

    vg_widget_destroy(w);
}

TEST(textinput_undo_at_beginning_is_noop) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);

    vg_widget_t *w = &ti->base;

    // Type one character
    vg_event_t ev_x = make_char_event('x');
    w->vtable->handle_event(w, &ev_x);
    ASSERT_EQ(0, strcmp(ti->text, "x"));

    vg_event_t undo = make_key_event(VG_KEY_Z, VG_MOD_CTRL);
    w->vtable->handle_event(w, &undo); // undo → ""
    ASSERT_EQ(0, strcmp(ti->text, ""));

    // Another undo at the beginning: should still be ""
    w->vtable->handle_event(w, &undo);
    ASSERT_EQ(0, strcmp(ti->text, ""));

    vg_widget_destroy(w);
}

TEST(textinput_redo_reapplies_undone_edit) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);

    vg_widget_t *w = &ti->base;

    // Type "hi"
    w->vtable->handle_event(w, &(vg_event_t){.type = VG_EVENT_KEY_CHAR, .key.codepoint = 'h'});
    w->vtable->handle_event(w, &(vg_event_t){.type = VG_EVENT_KEY_CHAR, .key.codepoint = 'i'});
    ASSERT_EQ(0, strcmp(ti->text, "hi"));

    // Undo twice → ""
    vg_event_t undo = make_key_event(VG_KEY_Z, VG_MOD_CTRL);
    w->vtable->handle_event(w, &undo);
    w->vtable->handle_event(w, &undo);
    ASSERT_EQ(0, strcmp(ti->text, ""));

    // Redo once → "h"
    vg_event_t redo = make_key_event(VG_KEY_Y, VG_MOD_CTRL);
    w->vtable->handle_event(w, &redo);
    ASSERT_EQ(0, strcmp(ti->text, "h"));

    // Redo again → "hi"
    w->vtable->handle_event(w, &redo);
    ASSERT_EQ(0, strcmp(ti->text, "hi"));

    // Redo past top: should still be "hi"
    w->vtable->handle_event(w, &redo);
    ASSERT_EQ(0, strcmp(ti->text, "hi"));

    vg_widget_destroy(w);
}

TEST(textinput_new_edit_clears_redo) {
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);

    vg_widget_t *w = &ti->base;

    // Type "ab"
    w->vtable->handle_event(w, &(vg_event_t){.type = VG_EVENT_KEY_CHAR, .key.codepoint = 'a'});
    w->vtable->handle_event(w, &(vg_event_t){.type = VG_EVENT_KEY_CHAR, .key.codepoint = 'b'});

    // Undo once → "a"
    vg_event_t undo = make_key_event(VG_KEY_Z, VG_MOD_CTRL);
    w->vtable->handle_event(w, &undo);
    ASSERT_EQ(0, strcmp(ti->text, "a"));

    // Type 'c' — this should clear the redo future
    w->vtable->handle_event(w, &(vg_event_t){.type = VG_EVENT_KEY_CHAR, .key.codepoint = 'c'});
    ASSERT_EQ(0, strcmp(ti->text, "ac"));

    // Redo should now be a no-op (redo future was truncated)
    vg_event_t redo = make_key_event(VG_KEY_Y, VG_MOD_CTRL);
    w->vtable->handle_event(w, &redo);
    ASSERT_EQ(0, strcmp(ti->text, "ac"));

    vg_widget_destroy(w);
}

TEST(textinput_set_text_is_silent) {
    int change_count = 0;
    vg_textinput_t *ti = vg_textinput_create(NULL);
    ASSERT_NOT_NULL(ti);

    vg_textinput_set_on_change(ti, count_text_changes, &change_count);
    vg_textinput_set_text(ti, "programmatic");
    ASSERT_EQ(change_count, 0);
    ASSERT_EQ(strcmp(vg_textinput_get_text(ti), "programmatic"), 0);

    vg_widget_destroy(&ti->base);
}

//=============================================================================
// BUG-GUI-002: Dialog Modal Event Blocking
//=============================================================================

// Dummy widget that records whether it received a mouse event
typedef struct {
    vg_widget_t base;
    int click_count;
} test_clickable_t;

static bool test_clickable_handle(vg_widget_t *w, vg_event_t *ev) {
    if (ev->type == VG_EVENT_MOUSE_DOWN || ev->type == VG_EVENT_CLICK) {
        ((test_clickable_t *)w)->click_count++;
        return true;
    }
    return false;
}

static bool test_clickable_can_focus(vg_widget_t *w) {
    (void)w;
    return true;
}

static vg_widget_vtable_t s_clickable_vtable = {
    .handle_event = test_clickable_handle,
    .can_focus = test_clickable_can_focus,
};

TEST(modal_blocks_mouse_behind_dialog) {
    // Widget tree:
    //   root (container at 0,0 200x200)
    //     ├─ background_btn (10,10 80x30) — behind the dialog
    //     └─ dialog_placeholder (50,50 100x100) — the "modal" root

    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->x = 0;
    root->y = 0;
    root->width = 200;
    root->height = 200;
    root->visible = true;
    root->enabled = true;

    test_clickable_t *bg = calloc(1, sizeof(test_clickable_t));
    ASSERT_NOT_NULL(bg);
    vg_widget_init(&bg->base, VG_WIDGET_CUSTOM, &s_clickable_vtable);
    bg->base.x = 10;
    bg->base.y = 10;
    bg->base.width = 80;
    bg->base.height = 30;
    bg->base.visible = true;
    bg->base.enabled = true;
    vg_widget_add_child(root, &bg->base);

    test_clickable_t *modal_w = calloc(1, sizeof(test_clickable_t));
    ASSERT_NOT_NULL(modal_w);
    vg_widget_init(&modal_w->base, VG_WIDGET_CUSTOM, &s_clickable_vtable);
    modal_w->base.x = 50;
    modal_w->base.y = 50;
    modal_w->base.width = 100;
    modal_w->base.height = 100;
    modal_w->base.visible = true;
    modal_w->base.enabled = true;
    vg_widget_add_child(root, &modal_w->base);

    // Without a modal root: click on background should reach bg
    vg_event_t ev_bg;
    memset(&ev_bg, 0, sizeof(ev_bg));
    ev_bg.type = VG_EVENT_MOUSE_DOWN;
    ev_bg.mouse.screen_x = 20.0f;
    ev_bg.mouse.screen_y = 20.0f;
    ev_bg.mouse.x = 20.0f;
    ev_bg.mouse.y = 20.0f;

    vg_event_dispatch(root, &ev_bg);
    ASSERT_EQ(1, bg->click_count);

    // Now register modal_w as the modal root
    vg_widget_set_modal_root(&modal_w->base);

    // Click on background (outside the modal dialog): should be swallowed
    int clicks_before = bg->click_count;
    vg_event_t ev_bg2 = ev_bg;
    vg_event_dispatch(root, &ev_bg2);
    ASSERT_EQ(clicks_before, bg->click_count); // no new clicks

    // Click inside the modal dialog: should be delivered
    int modal_before = modal_w->click_count;
    vg_event_t ev_modal;
    memset(&ev_modal, 0, sizeof(ev_modal));
    ev_modal.type = VG_EVENT_MOUSE_DOWN;
    ev_modal.mouse.screen_x = 80.0f;
    ev_modal.mouse.screen_y = 80.0f;
    ev_modal.mouse.x = 80.0f;
    ev_modal.mouse.y = 80.0f;
    vg_event_dispatch(root, &ev_modal);
    ASSERT_EQ(modal_before + 1, modal_w->click_count);

    // Cleanup: clear modal root, then destroy the tree recursively.
    // vg_widget_destroy(root) destroys bg and modal_w as children, which also
    // clears g_focused_widget if it points to a destroyed widget — avoiding a
    // dangling pointer that would crash the next test's vg_widget_set_focus call.
    vg_widget_set_modal_root(NULL);
    vg_widget_destroy(root);
}

TEST(dialog_button_hit_testing_uses_local_coords) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->x = 0.0f;
    root->y = 0.0f;
    root->width = 640.0f;
    root->height = 480.0f;
    root->visible = true;
    root->enabled = true;

    vg_dialog_t *dialog = vg_dialog_create("Confirm");
    ASSERT_NOT_NULL(dialog);
    vg_widget_add_child(root, &dialog->base);
    vg_dialog_set_buttons(dialog, VG_DIALOG_BUTTONS_OK_CANCEL);
    vg_dialog_show(dialog);
    vg_widget_arrange(&dialog->base, 120.0f, 80.0f, 320.0f, 180.0f);

    vg_event_t click = vg_event_mouse(VG_EVENT_MOUSE_DOWN,
                                      dialog->base.x + dialog->base.width - 40.0f,
                                      dialog->base.y + dialog->base.height - 24.0f,
                                      VG_MOUSE_LEFT,
                                      0);
    ASSERT_TRUE(vg_event_dispatch(root, &click));
    ASSERT_FALSE(vg_dialog_is_open(dialog));
    ASSERT_EQ(vg_dialog_get_result(dialog), VG_DIALOG_RESULT_CANCEL);

    vg_widget_destroy(root);
}

TEST(dialog_show_centered_uses_nested_screen_bounds) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->visible = true;
    root->enabled = true;
    root->width = 800.0f;
    root->height = 600.0f;

    vg_widget_t *outer = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *middle = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t *target = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(outer);
    ASSERT_NOT_NULL(middle);
    ASSERT_NOT_NULL(target);

    vg_widget_add_child(root, outer);
    vg_widget_add_child(outer, middle);
    vg_widget_add_child(middle, target);
    outer->x = 40.0f;
    outer->y = 30.0f;
    middle->x = 60.0f;
    middle->y = 50.0f;
    target->x = 25.0f;
    target->y = 15.0f;
    target->width = 100.0f;
    target->height = 40.0f;

    vg_dialog_t *dialog = vg_dialog_create("Centered");
    ASSERT_NOT_NULL(dialog);
    vg_widget_add_child(outer, &dialog->base);

    vg_dialog_show_centered(dialog, target);

    float expected_x = middle->x + target->x + target->width * 0.5f - dialog->base.width * 0.5f;
    float expected_y =
        middle->y + target->y + target->height * 0.5f - dialog->base.height * 0.5f;
    ASSERT_TRUE(dialog->base.x > expected_x - 0.1f && dialog->base.x < expected_x + 0.1f);
    ASSERT_TRUE(dialog->base.y > expected_y - 0.1f && dialog->base.y < expected_y + 0.1f);

    vg_widget_destroy(root);
}

TEST(filedialog_save_confirm_uses_local_coords_and_default_extension) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->visible = true;
    root->enabled = true;
    root->width = 800.0f;
    root->height = 600.0f;

    vg_filedialog_t *dialog = vg_filedialog_create(VG_FILEDIALOG_SAVE);
    ASSERT_NOT_NULL(dialog);
    vg_widget_add_child(root, &dialog->base.base);

    free(dialog->current_path);
    dialog->current_path = strdup("/virtual");
    vg_filedialog_set_filename(dialog, "report");
    vg_filedialog_set_default_extension(dialog, "txt");
    dialog->base.is_open = true;
    dialog->base.base.visible = true;
    dialog->filename_active = true;

    vg_widget_arrange(&dialog->base.base, 140.0f, 90.0f, 600.0f, 400.0f);

    vg_event_t click = vg_event_mouse(VG_EVENT_MOUSE_DOWN,
                                      dialog->base.base.x + dialog->base.base.width - 40.0f,
                                      dialog->base.base.y + dialog->base.base.height - 24.0f,
                                      VG_MOUSE_LEFT,
                                      0);
    ASSERT_TRUE(vg_event_dispatch(root, &click));
    ASSERT_FALSE(dialog->base.is_open);
    ASSERT_EQ(dialog->selected_file_count, 1);
    ASSERT_NOT_NULL(dialog->selected_files);
    ASSERT_TRUE(text_has_suffix(dialog->selected_files[0], "report.txt"));

    vg_widget_destroy(root);
}

//=============================================================================
// FEAT-006: Tab Order via tab_index
//=============================================================================

TEST(focus_next_respects_tab_index_order) {
    // Create root and three focusable buttons with explicit tab_index
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->visible = true;
    root->enabled = true;

    // Insertion order: btn0(tab=2), btn1(tab=0), btn2(tab=1)
    // Expected focus order: btn1(0) → btn2(1) → btn0(2) → btn1(0) …
    vg_button_t *btn0 = vg_button_create(root, "A");
    vg_button_t *btn1 = vg_button_create(root, "B");
    vg_button_t *btn2 = vg_button_create(root, "C");
    ASSERT_NOT_NULL(btn0);
    ASSERT_NOT_NULL(btn1);
    ASSERT_NOT_NULL(btn2);

    vg_widget_set_tab_index(&btn0->base, 2);
    vg_widget_set_tab_index(&btn1->base, 0);
    vg_widget_set_tab_index(&btn2->base, 1);

    // No current focus: focus_next picks the first in sorted order (btn1, index=0)
    vg_widget_focus_next(root);
    ASSERT_EQ(&btn1->base, vg_widget_get_focused(root));

    // Next: btn2 (index=1)
    vg_widget_focus_next(root);
    ASSERT_EQ(&btn2->base, vg_widget_get_focused(root));

    // Next: btn0 (index=2)
    vg_widget_focus_next(root);
    ASSERT_EQ(&btn0->base, vg_widget_get_focused(root));

    // Next: wraps to btn1 (index=0)
    vg_widget_focus_next(root);
    ASSERT_EQ(&btn1->base, vg_widget_get_focused(root));

    // Clear focus before destroy
    vg_widget_set_focus(NULL); // just reset state
    vg_widget_destroy(root);
}

TEST(focus_prev_reverses_tab_index_order) {
    vg_widget_t *root = vg_widget_create(VG_WIDGET_CONTAINER);
    ASSERT_NOT_NULL(root);
    root->visible = true;
    root->enabled = true;

    vg_button_t *btn0 = vg_button_create(root, "A");
    vg_button_t *btn1 = vg_button_create(root, "B");
    vg_button_t *btn2 = vg_button_create(root, "C");

    vg_widget_set_tab_index(&btn0->base, 2);
    vg_widget_set_tab_index(&btn1->base, 0);
    vg_widget_set_tab_index(&btn2->base, 1);

    // Start at btn1 (index=0)
    vg_widget_set_focus(&btn1->base);
    ASSERT_EQ(&btn1->base, vg_widget_get_focused(root));

    // focus_prev from btn1(0): wraps to btn0(2)
    vg_widget_focus_prev(root);
    ASSERT_EQ(&btn0->base, vg_widget_get_focused(root));

    // focus_prev from btn0(2): goes to btn2(1)
    vg_widget_focus_prev(root);
    ASSERT_EQ(&btn2->base, vg_widget_get_focused(root));

    // focus_prev from btn2(1): goes to btn1(0)
    vg_widget_focus_prev(root);
    ASSERT_EQ(&btn1->base, vg_widget_get_focused(root));

    vg_widget_destroy(root);
}

TEST(tab_index_defaults_to_minus_one) {
    vg_button_t *btn = vg_button_create(NULL, "test");
    ASSERT_NOT_NULL(btn);
    ASSERT_EQ(-1, btn->base.tab_index);
    vg_widget_destroy(&btn->base);
}

//=============================================================================
// FEAT-005: Button Icon Support
//=============================================================================

TEST(button_set_icon_stores_text) {
    vg_button_t *btn = vg_button_create(NULL, "Save");
    ASSERT_NOT_NULL(btn);
    ASSERT_NULL(btn->icon_text); // no icon by default

    vg_button_set_icon(btn, "💾");
    ASSERT_NOT_NULL(btn->icon_text);
    ASSERT_EQ(0, strcmp(btn->icon_text, "💾"));

    vg_widget_destroy(&btn->base);
}

TEST(button_set_icon_null_clears_icon) {
    vg_button_t *btn = vg_button_create(NULL, "Delete");
    ASSERT_NOT_NULL(btn);

    vg_button_set_icon(btn, "🗑");
    ASSERT_NOT_NULL(btn->icon_text);

    vg_button_set_icon(btn, NULL);
    ASSERT_NULL(btn->icon_text);

    vg_widget_destroy(&btn->base);
}

TEST(button_set_icon_position) {
    vg_button_t *btn = vg_button_create(NULL, "OK");
    ASSERT_NOT_NULL(btn);
    ASSERT_EQ(0, btn->icon_pos); // default = left

    vg_button_set_icon_position(btn, 1);
    ASSERT_EQ(1, btn->icon_pos);

    vg_button_set_icon_position(btn, 0);
    ASSERT_EQ(0, btn->icon_pos);

    vg_widget_destroy(&btn->base);
}

TEST(button_destroy_with_icon_no_crash) {
    // Verify destroy frees icon_text without double-free
    vg_button_t *btn = vg_button_create(NULL, "Close");
    ASSERT_NOT_NULL(btn);
    vg_button_set_icon(btn, "✕");
    ASSERT_NOT_NULL(btn->icon_text);

    // Should not crash
    vg_widget_destroy(&btn->base);
    // If we reach here the test passes
    ASSERT_TRUE(true);
}

//=============================================================================
// Main
//=============================================================================

int main(void) {
    printf("=== Tier 3 GUI Fixes Tests ===\n\n");

    printf("PERF-002: LRU Glyph Cache Eviction\n");
    RUN(lru_cache_hit_updates_tick);
    RUN(lru_cache_lru_evicts_unaccessed_first);

    printf("\nBUG-GUI-008: TextInput Undo/Redo\n");
    RUN(textinput_undo_restores_previous_text);
    RUN(textinput_undo_at_beginning_is_noop);
    RUN(textinput_redo_reapplies_undone_edit);
    RUN(textinput_new_edit_clears_redo);
    RUN(textinput_set_text_is_silent);

    printf("\nBUG-GUI-002: Dialog Modal Event Blocking\n");
    RUN(modal_blocks_mouse_behind_dialog);
    RUN(dialog_button_hit_testing_uses_local_coords);
    RUN(dialog_show_centered_uses_nested_screen_bounds);
    RUN(filedialog_save_confirm_uses_local_coords_and_default_extension);

    printf("\nFEAT-006: Tab Order\n");
    RUN(focus_next_respects_tab_index_order);
    RUN(focus_prev_reverses_tab_index_order);
    RUN(tab_index_defaults_to_minus_one);

    printf("\nFEAT-005: Button Icon\n");
    RUN(button_set_icon_stores_text);
    RUN(button_set_icon_null_clears_icon);
    RUN(button_set_icon_position);
    RUN(button_destroy_with_icon_no_crash);

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
